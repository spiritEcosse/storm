module;

#include <meta>

export module storm_orm_transaction;

import std;

export namespace storm::orm::utilities {

    // ============================================================================
    // Transaction Guard - RAII-style transaction management
    // ============================================================================
    //
    // Usage:
    //   auto txn = TransactionGuard<ConnType>::begin(conn_);
    //   if (!txn) return std::unexpected(txn.error());
    //
    //   auto result = do_work();
    //   if (!result) return result;  // Destructor will ROLLBACK
    //
    //   return txn->commit();  // Explicit COMMIT, returns expected<void, Error>
    //
    // Destructor automatically ROLLBACKs if commit() was not called.
    // ============================================================================

    template <typename ConnType> class TransactionGuard {
        using Error = typename ConnType::Error;

        std::shared_ptr<ConnType> conn_;
        bool                      committed_ = false;

        explicit TransactionGuard(std::shared_ptr<ConnType> conn) noexcept : conn_(std::move(conn)) {}

      public:
        // Factory method - begins transaction, returns expected
        [[nodiscard]] static auto begin(std::shared_ptr<ConnType> conn) noexcept
                -> std::expected<TransactionGuard, Error> {
            if (auto result = conn->execute("BEGIN TRANSACTION"); !result) {
                return std::unexpected(result.error());
            }
            return TransactionGuard(std::move(conn));
        }

        // Move-only (no copying)
        TransactionGuard(const TransactionGuard&)                    = delete;
        auto operator=(const TransactionGuard&) -> TransactionGuard& = delete;

        TransactionGuard(TransactionGuard&& other) noexcept
            : conn_(std::move(other.conn_)), committed_(other.committed_) {
            // Neutralize the source so a moved-from guard can never attempt a DB op,
            // independent of std::move(shared_ptr) having nulled other.conn_.
            other.committed_ = true;
        }

        auto operator=(TransactionGuard&& other) noexcept -> TransactionGuard& {
            if (this != &other) {
                rollback_if_needed();
                conn_      = std::move(other.conn_);
                committed_ = other.committed_;
                // Neutralize the source so a moved-from guard can never rollback.
                other.committed_ = true;
            }
            return *this;
        }

        // Destructor - rollback if not committed
        ~TransactionGuard() {
            rollback_if_needed();
        }

        // Explicit commit - must be called for successful transaction
        [[nodiscard]] auto commit() noexcept -> std::expected<void, Error> {
            if (!conn_ || committed_) {
                return {}; // Already committed or moved-from
            }

            if (auto result = conn_->execute("COMMIT"); !result) {
                // Best-effort ROLLBACK. execute() is not noexcept (it builds a
                // std::string / inserts into the statement cache and can throw
                // std::bad_alloc under memory pressure); swallow any throw so it
                // never escapes this noexcept function and calls std::terminate.
                try {
                    (void)conn_->execute("ROLLBACK");
                } catch (...) { // NOSONAR(cpp:S2486) NOLINT(bugprone-empty-catch)
                }
                // Transaction is already rolled back; mark committed so the
                // destructor does not issue a second, redundant ROLLBACK.
                committed_ = true;
                return std::unexpected(result.error());
            }

            committed_ = true;
            return {};
        }

      private:
        auto rollback_if_needed() noexcept -> void {
            if (conn_ && !committed_) {
                // Best-effort ROLLBACK during unwinding. execute() is not noexcept
                // (it builds a std::string / inserts into the statement cache and
                // can throw std::bad_alloc under memory pressure); swallow any
                // throw so it never escapes this noexcept function — a throw here
                // would call std::terminate (the throwing-destructor trap).
                try {
                    (void)conn_->execute("ROLLBACK");
                } catch (...) { // NOSONAR(cpp:S2486) NOLINT(bugprone-empty-catch)
                }
            }
        }
    };

} // namespace storm::orm::utilities
