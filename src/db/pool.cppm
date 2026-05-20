module;

// LINT-EXCLUDE-FILE: complexity, length
// `duplicate` removed in #277 Phase 3 (count_entries(pred) helper shared by available() / in_use() counters).

#include <condition_variable>
#include <mutex>

export module storm_db_pool;
import storm_db_concept;
import <expected>;
import <string_view>;
import <string>;
import <memory>;
import <vector>;
import <chrono>;

export namespace storm::db {

    struct PoolConfig {
        int  min_connections      = 1;       // Pre-created on init
        int  max_connections      = 10;      // Hard upper bound
        int  checkout_timeout_ms  = 5000;    // 0 = fail immediately
        int  max_lifetime_ms      = 1800000; // 30 min (0 = never recycle)
        int  idle_timeout_ms      = 600000;  // 10 min (0 = never expire idle)
        bool validate_on_checkout = true;    // is_open() check before handing out
    };

    // Forward declaration
    template <CachedDatabaseConnection ConnType> class ConnectionPool;

    namespace detail {

        using Clock     = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        template <CachedDatabaseConnection ConnType> struct PoolEntry {
            std::unique_ptr<ConnType> conn;
            bool                      in_use       = false;
            TimePoint                 created_at   = Clock::now();
            TimePoint                 last_used_at = Clock::now();
        };

        template <CachedDatabaseConnection ConnType> class PoolCore {
          public:
            using Error = typename ConnType::Error;

            PoolCore(std::string conninfo, PoolConfig config) : conninfo_(std::move(conninfo)), config_(config) {
                entries_.reserve(static_cast<size_t>(config_.max_connections));
            }

            [[nodiscard]] auto init() -> std::expected<void, Error> {
                for (int i = 0; i < config_.min_connections; ++i) {
                    auto result = create_entry();
                    if (!result) {
                        return std::unexpected(result.error());
                    }
                }
                return {};
            }

            auto checkout() -> std::expected<ConnType*, Error> {
                std::unique_lock lock(mutex_);

                if (shutdown_) {
                    return std::unexpected(Error{-1, "Pool is shut down"});
                }

                // Evict expired idle connections before searching
                evict_expired();

                // Try to find an idle connection
                auto* entry = find_idle();
                if (entry != nullptr) {
                    return entry;
                }

                // Try to grow the pool
                if (static_cast<int>(entries_.size()) < config_.max_connections) {
                    // Release lock during potentially slow open()
                    lock.unlock();
                    auto result = ConnType::open(conninfo_);
                    lock.lock();

                    if (shutdown_) {
                        return std::unexpected(Error{-1, "Pool is shut down"});
                    }

                    if (!result) {
                        return std::unexpected(result.error());
                    }

                    // Re-check: another thread may have grown the pool while we were unlocked
                    if (static_cast<int>(entries_.size()) >= config_.max_connections) {
                        // Pool reached max — discard new connection, try to find an idle one
                        auto* idle = find_idle();
                        if (idle != nullptr) {
                            return idle;
                        }
                        // Fall through to wait/fail logic
                    } else {
                        auto now = Clock::now();
                        entries_.emplace_back(std::make_unique<ConnType>(std::move(result.value())), true, now, now);
                        return entries_.back().conn.get();
                    }
                }

                // Pool exhausted — wait or fail
                if (config_.checkout_timeout_ms == 0) {
                    return std::unexpected(Error{-1, "Pool exhausted (timeout=0)"});
                }

                auto deadline = Clock::now() + std::chrono::milliseconds(config_.checkout_timeout_ms);

                while (true) {
                    if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                        return std::unexpected(Error{-1, "Pool checkout timed out"});
                    }

                    if (shutdown_) {
                        return std::unexpected(Error{-1, "Pool is shut down"});
                    }

                    evict_expired();

                    auto* found = find_idle();
                    if (found != nullptr) {
                        return found;
                    }
                }
            }

            auto checkin(ConnType* conn) -> void {
                std::lock_guard lock(mutex_);
                for (auto& entry : entries_) {
                    if (entry.conn.get() == conn) {
                        entry.in_use       = false;
                        entry.last_used_at = Clock::now();
                        cv_.notify_one();
                        return;
                    }
                }
            }

            [[nodiscard]] auto size() const -> int {
                std::lock_guard lock(mutex_);
                return static_cast<int>(entries_.size());
            }

            // Count entries matching a predicate. The available() / in_use()
            // counters used to repeat the lock + loop + count body verbatim;
            // they now differ only in which entries pass the predicate.
            //
            // NOLINTBEGIN(readability-use-anyofallof) — explicit loop to avoid
            // `import <algorithm>`; clang-p2996 clang-scan-deps SIGSEGVs on atomic_ref.h
            template <typename Pred> [[nodiscard]] auto count_entries(Pred pred) const -> int {
                std::lock_guard lock(mutex_);
                int             count = 0;
                for (const auto& entry : entries_) {
                    if (pred(entry)) {
                        ++count;
                    }
                }
                return count;
            }
            // NOLINTEND(readability-use-anyofallof)

            [[nodiscard]] auto available() const -> int {
                return count_entries([](const auto& entry) { return !entry.in_use; });
            }

            [[nodiscard]] auto in_use() const -> int {
                return count_entries([](const auto& entry) { return entry.in_use; });
            }

            auto shutdown() -> void {
                std::unique_lock lock(mutex_);
                shutdown_ = true;
                cv_.notify_all();

                // Wait for all connections to be returned.
                // NOLINTBEGIN(readability-use-anyofallof) — keep explicit loop to avoid
                // `import <algorithm>`; clang-p2996 clang-scan-deps SIGSEGVs on atomic_ref.h
                // via <algorithm> in ninja-release (issue #262).
                cv_.wait(lock, [this] {
                    for (const auto& entry : entries_) {
                        if (entry.in_use) {
                            return false;
                        }
                    }
                    return true;
                });
                // NOLINTEND(readability-use-anyofallof)

                entries_.clear();
            }

          private:
            // Remove expired idle connections (called under lock)
            auto evict_expired() -> void {
                if (config_.max_lifetime_ms <= 0 && config_.idle_timeout_ms <= 0) {
                    return;
                }

                auto now = Clock::now();
                auto it  = entries_.begin();
                while (it != entries_.end()) {
                    if (it->in_use) {
                        ++it;
                        continue;
                    }

                    bool expired = false;
                    if (config_.max_lifetime_ms > 0) {
                        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->created_at).count();
                        expired  = age > config_.max_lifetime_ms;
                    }
                    if (!expired && config_.idle_timeout_ms > 0) {
                        auto idle =
                                std::chrono::duration_cast<std::chrono::milliseconds>(now - it->last_used_at).count();
                        expired = idle > config_.idle_timeout_ms;
                    }

                    if (expired) {
                        it = entries_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            // Find an idle connection, validating if configured.
            // Stale connections (is_open() == false) are erased rather than replaced
            // under the lock — the caller grows the pool outside the lock if needed.
            auto find_idle() -> ConnType* {
                auto it = entries_.begin();
                while (it != entries_.end()) {
                    if (it->in_use) {
                        ++it;
                        continue;
                    }
                    if (config_.validate_on_checkout && !it->conn->is_open()) {
                        it = entries_.erase(it);
                        continue;
                    }
                    it->in_use = true;
                    return it->conn.get();
                }
                return nullptr;
            }

            auto create_entry() -> std::expected<void, Error> {
                auto result = ConnType::open(conninfo_);
                if (!result) {
                    return std::unexpected(result.error());
                }
                auto now = Clock::now();
                entries_.emplace_back(std::make_unique<ConnType>(std::move(result.value())), false, now, now);
                return {};
            }

            std::string                      conninfo_;
            PoolConfig                       config_;
            std::vector<PoolEntry<ConnType>> entries_;
            mutable std::mutex               mutex_;
            std::condition_variable          cv_;
            bool                             shutdown_ = false;
        };

    } // namespace detail

    template <CachedDatabaseConnection ConnType> class ConnectionPool {
      public:
        using Error = typename ConnType::Error;

        [[nodiscard]] static auto create(std::string_view conninfo, PoolConfig config = {})
                -> std::expected<ConnectionPool, Error> {
            if (config.min_connections < 0 || config.max_connections <= 0 ||
                config.min_connections > config.max_connections || config.checkout_timeout_ms < 0 ||
                config.max_lifetime_ms < 0 || config.idle_timeout_ms < 0) {
                return std::unexpected(Error{-1, "Invalid pool config: min >= 0, max > 0, min <= max, timeouts >= 0"});
            }

            auto core        = std::make_shared<detail::PoolCore<ConnType>>(std::string(conninfo), config);
            auto init_result = core->init();
            if (!init_result) {
                return std::unexpected(init_result.error());
            }

            return ConnectionPool{std::move(core)};
        }

        [[nodiscard]] auto checkout() -> std::expected<std::shared_ptr<ConnType>, Error> {
            auto result = core_->checkout();
            if (!result) {
                return std::unexpected(result.error());
            }

            ConnType* raw = result.value();
            // Capture shared_ptr to PoolCore — keeps pool alive until all connections returned.
            // This prevents use-after-free: PoolCore owns connection memory via unique_ptr,
            // so it must outlive all checked-out shared_ptr<ConnType> handles.
            std::shared_ptr<detail::PoolCore<ConnType>> core_ref = core_;

            return std::shared_ptr<ConnType>(raw, [core_ref](ConnType* p) { core_ref->checkin(p); });
        }

        [[nodiscard]] auto size() const -> int {
            return core_->size();
        }

        [[nodiscard]] auto available() const -> int {
            return core_->available();
        }

        [[nodiscard]] auto in_use() const -> int {
            return core_->in_use();
        }

        auto shutdown() -> void {
            core_->shutdown();
        }

      private:
        explicit ConnectionPool(std::shared_ptr<detail::PoolCore<ConnType>> core) : core_(std::move(core)) {}

        std::shared_ptr<detail::PoolCore<ConnType>> core_;
    };

} // namespace storm::db
