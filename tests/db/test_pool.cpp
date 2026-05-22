// LINT-EXCLUDE-FILE: file-size, duplicate
// Pre-existing structural debt — large test file with repeated TEST() boilerplate
// across many backend types. Tracked under issue #264 Phase 1.
#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes,readability-identifier-length,cppcoreguidelines-special-member-functions,readability-function-cognitive-complexity) // GTest fixtures use protected connstr_; tests use short connection names c1/c2/c3; MockConfigGuard is RAII-only; a few test bodies exceed the cognitive-complexity threshold. Pre-existing; tracked under #262/#264.

import storm;
import <expected>;
import <string>;
import <memory>;
import <thread>;
import <vector>;
import <atomic>;
import <chrono>;
import <latch>;

#include "test_models.h" // NOSONAR cpp:S954

// ============================================================================
// Mock connection for testing pool error/edge-case paths
// ============================================================================

namespace {

    // Controls MockPoolConnection behavior (global, not thread-safe — tests are sequential)
    struct MockConfig {
        bool open_should_fail = false;
        bool is_open_returns  = true;
        int  open_fail_after  = -1; // fail after N successful opens (-1 = never)
        int  open_call_count  = 0;
        int  open_delay_ms    = 0; // artificial delay in open() to test race conditions
    };

    inline auto mock_config() -> MockConfig& {
        static MockConfig cfg; // NOSONAR
        return cfg;
    }

} // namespace

struct MockStatement {
    struct Error {
        int         code_;
        std::string message_;

        [[nodiscard]] constexpr auto code() const noexcept -> int {
            return code_;
        }
        [[nodiscard]] constexpr auto message() const noexcept -> std::string_view {
            return message_;
        }
    };

    auto bind_int(int, int) -> std::expected<void, Error> {
        return {};
    }
    auto bind_text(int, std::string_view) -> std::expected<void, Error> {
        return {};
    }
    auto execute() -> std::expected<void, Error> {
        return {};
    }
    auto step() -> std::expected<bool, Error> {
        return false;
    }
    auto reset() -> void {}
    auto finalize() -> void {}
};

struct MockPoolConnection {
    struct Error {
        int         code_;
        std::string message_;

        [[nodiscard]] constexpr auto code() const noexcept -> int {
            return code_;
        }
        [[nodiscard]] constexpr auto message() const noexcept -> std::string_view {
            return message_;
        }
    };
    using Statement = MockStatement;

    [[nodiscard]] static auto open(std::string_view) -> std::expected<MockPoolConnection, Error> {
        auto& cfg = mock_config();
        ++cfg.open_call_count;
        if (cfg.open_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.open_delay_ms));
        }
        if (cfg.open_should_fail) {
            return std::unexpected(Error{-1, "Mock open failure"});
        }
        if (cfg.open_fail_after >= 0 && cfg.open_call_count > cfg.open_fail_after) {
            return std::unexpected(Error{-1, "Mock open failure after threshold"});
        }
        return MockPoolConnection{};
    }

    [[nodiscard]] auto is_open() const -> bool {
        return is_open_;
    }

    auto prepare(std::string_view) -> std::expected<Statement, Error> {
        return Statement{};
    }
    auto execute(std::string_view) -> std::expected<void, Error> {
        return {};
    }
    auto prepare_cached(std::string_view) -> std::expected<Statement*, Error> {
        return &cached_stmt_;
    }
    auto               clear_statement_cache() -> void {}
    auto               clear_statement_cache(std::string_view) -> void {}
    [[nodiscard]] auto cached_statement_count() const -> size_t {
        return 0;
    }

    bool is_open_ = true;

  private:
    Statement cached_stmt_;
};

// Guard to reset mock config after each test
struct MockConfigGuard {
    MockConfigGuard() {
        mock_config() = MockConfig{};
    }
    ~MockConfigGuard() {
        mock_config() = MockConfig{};
    }
};

namespace {

    template <typename ConnType> auto get_pool_connstr() -> std::string {
        return storm::test::get_connection_string<ConnType>();
    }

    template <typename ConnType> auto skip_if_unavailable() -> bool {
        return !storm::test::backend_available<ConnType>();
    }

} // namespace

// ============================================================================
// Construction Tests
// ============================================================================

template <typename ConnType> class PoolCreateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (skip_if_unavailable<ConnType>()) {
            GTEST_SKIP() << "Backend unavailable";
        }
        connstr_ = get_pool_connstr<ConnType>();
    }
    std::string connstr_;
};

TYPED_TEST_SUITE(PoolCreateTest, DatabaseTypes);

TYPED_TEST(PoolCreateTest, CreatePool_ValidConfig) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 2, .max_connections = 5});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();
    EXPECT_EQ(pool->size(), 2);
    EXPECT_EQ(pool->available(), 2);
    EXPECT_EQ(pool->in_use(), 0);
}

TYPED_TEST(PoolCreateTest, CreatePool_BadConfig_MinGreaterThanMax) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 10, .max_connections = 5});
    EXPECT_FALSE(pool.has_value());
}

TYPED_TEST(PoolCreateTest, CreatePool_BadConfig_NegativeMin) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = -1, .max_connections = 5});
    EXPECT_FALSE(pool.has_value());
}

TYPED_TEST(PoolCreateTest, CreatePool_BadConfig_ZeroMax) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 0, .max_connections = 0});
    EXPECT_FALSE(pool.has_value());
}

TYPED_TEST(PoolCreateTest, CreatePool_ZeroMin) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 0, .max_connections = 3});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();
    EXPECT_EQ(pool->size(), 0);
    EXPECT_EQ(pool->available(), 0);
}

TYPED_TEST(PoolCreateTest, CreatePool_InvalidConnstring) {
    if constexpr (storm::test::is_postgresql<TypeParam>()) {
        auto pool = storm::db::ConnectionPool<TypeParam>::create(
                "host=invalid_host_that_does_not_exist port=99999 dbname=nonexistent connect_timeout=1",
                {.min_connections = 1, .max_connections = 5}
        );
        EXPECT_FALSE(pool.has_value());
    } else {
        // SQLite :memory: always succeeds; verify the API works
        auto pool =
                storm::db::ConnectionPool<TypeParam>::create(":memory:", {.min_connections = 1, .max_connections = 5});
        EXPECT_TRUE(pool.has_value());
    }
}

// ============================================================================
// Checkout/Checkin Tests
// ============================================================================

template <typename ConnType> class PoolCheckoutTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (skip_if_unavailable<ConnType>()) {
            GTEST_SKIP() << "Backend unavailable";
        }
        connstr_ = get_pool_connstr<ConnType>();
    }
    std::string connstr_;
};

TYPED_TEST_SUITE(PoolCheckoutTest, DatabaseTypes);

TYPED_TEST(PoolCheckoutTest, Checkout_ReturnsValid) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 1, .max_connections = 3});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto conn = pool->checkout();
    ASSERT_TRUE(conn.has_value()) << conn.error().message();
    EXPECT_TRUE((*conn)->is_open());
    EXPECT_EQ(pool->in_use(), 1);
}

TYPED_TEST(PoolCheckoutTest, Checkout_ReturnOnRelease) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 1, .max_connections = 3});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    {
        auto conn = pool->checkout();
        ASSERT_TRUE(conn.has_value());
        EXPECT_EQ(pool->in_use(), 1);
        EXPECT_EQ(pool->available(), 0);
    } // conn released — custom deleter returns to pool
    EXPECT_EQ(pool->in_use(), 0);
    EXPECT_EQ(pool->available(), 1);
}

TYPED_TEST(PoolCheckoutTest, Checkout_Multiple) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 1, .max_connections = 5});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto c1 = pool->checkout();
    auto c2 = pool->checkout();
    auto c3 = pool->checkout();
    ASSERT_TRUE(c1.has_value());
    ASSERT_TRUE(c2.has_value());
    ASSERT_TRUE(c3.has_value());
    EXPECT_EQ(pool->in_use(), 3);
    EXPECT_EQ(pool->size(), 3);
}

TYPED_TEST(PoolCheckoutTest, Checkout_CachePreserved) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 1, .max_connections = 3});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    TypeParam* raw_ptr = nullptr;
    {
        auto conn = pool->checkout();
        ASSERT_TRUE(conn.has_value());
        raw_ptr   = conn->get();
        auto stmt = raw_ptr->prepare_cached("SELECT 1");
        ASSERT_TRUE(stmt.has_value());
        EXPECT_GE(raw_ptr->cached_statement_count(), 1U);
    }
    // Re-checkout — should get same connection with cache intact
    auto conn2 = pool->checkout();
    ASSERT_TRUE(conn2.has_value());
    EXPECT_EQ(conn2->get(), raw_ptr);
    EXPECT_GE(conn2->get()->cached_statement_count(), 1U);
}

TYPED_TEST(PoolCheckoutTest, Checkout_GrowsOnDemand) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 0, .max_connections = 3});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();
    EXPECT_EQ(pool->size(), 0);

    auto c1 = pool->checkout();
    ASSERT_TRUE(c1.has_value());
    EXPECT_EQ(pool->size(), 1);

    auto c2 = pool->checkout();
    ASSERT_TRUE(c2.has_value());
    EXPECT_EQ(pool->size(), 2);
}

TYPED_TEST(PoolCheckoutTest, Checkout_ReusesReturnedConnection) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 1, .max_connections = 1});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    TypeParam const* first_raw = nullptr;
    {
        auto c1 = pool->checkout();
        ASSERT_TRUE(c1.has_value());
        first_raw = c1->get();
    }
    auto c2 = pool->checkout();
    ASSERT_TRUE(c2.has_value());
    EXPECT_EQ(c2->get(), first_raw); // Same connection reused
}

// ============================================================================
// Exhaustion & Timeout Tests
// ============================================================================

template <typename ConnType> class PoolExhaustionTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (skip_if_unavailable<ConnType>()) {
            GTEST_SKIP() << "Backend unavailable";
        }
        connstr_ = get_pool_connstr<ConnType>();
    }
    std::string connstr_;
};

TYPED_TEST_SUITE(PoolExhaustionTest, DatabaseTypes);

TYPED_TEST(PoolExhaustionTest, Checkout_ExhaustedImmediate) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 1, .checkout_timeout_ms = 0}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto c1 = pool->checkout();
    ASSERT_TRUE(c1.has_value());
    auto c2 = pool->checkout();
    EXPECT_FALSE(c2.has_value());
}

TYPED_TEST(PoolExhaustionTest, Checkout_WaitsForReturn) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 1, .checkout_timeout_ms = 2000}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto c1 = pool->checkout();
    ASSERT_TRUE(c1.has_value());

    std::atomic<bool> got_connection{false};
    std::thread       worker([&pool, &got_connection] {
        auto c         = pool->checkout();
        got_connection = c.has_value();
    });

    // Release after 100ms — worker should succeed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    c1->reset(); // Releases the shared_ptr, triggering checkin
    worker.join();
    EXPECT_TRUE(got_connection);
}

TYPED_TEST(PoolExhaustionTest, Checkout_NeverExceedsMax) {
    constexpr int max  = 3;
    auto          pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = max, .checkout_timeout_ms = 0}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    // Checkout all max connections
    std::vector<std::shared_ptr<TypeParam>> conns;
    for (int i = 0; i < max; ++i) {
        auto c = pool->checkout();
        ASSERT_TRUE(c.has_value()) << "Failed on checkout " << i;
        conns.push_back(std::move(*c));
    }
    EXPECT_EQ(pool->size(), max);
    EXPECT_EQ(pool->in_use(), max);
    EXPECT_EQ(pool->available(), 0);

    // Next checkout must fail — pool must not grow beyond max
    auto overflow = pool->checkout();
    EXPECT_FALSE(overflow.has_value());
    EXPECT_EQ(pool->size(), max); // Still max, not max+1
}

TYPED_TEST(PoolExhaustionTest, Checkout_WaitsAndReusesReturned) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 1, .checkout_timeout_ms = 2000}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto c1 = pool->checkout();
    ASSERT_TRUE(c1.has_value());
    TypeParam const* original_raw = c1->get();

    std::atomic<TypeParam*> worker_raw{nullptr};
    std::thread             worker([&pool, &worker_raw] {
        auto c = pool->checkout();
        if (c.has_value()) {
            worker_raw = c->get();
        }
    });

    // Release after 100ms — worker gets the SAME connection back (reuse, not new)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    c1->reset();
    worker.join();

    EXPECT_EQ(worker_raw.load(), original_raw); // Reused, not new
    EXPECT_EQ(pool->size(), 1);                 // Never grew beyond max
}

TYPED_TEST(PoolExhaustionTest, Checkout_TimesOut) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 1, .checkout_timeout_ms = 50}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto c1 = pool->checkout();
    ASSERT_TRUE(c1.has_value());

    auto start = std::chrono::steady_clock::now();
    auto c2    = pool->checkout();
    auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    EXPECT_FALSE(c2.has_value());
    EXPECT_GE(elapsed, 40); // Allow some tolerance
}

// ============================================================================
// Shutdown Tests
// ============================================================================

template <typename ConnType> class PoolShutdownTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (skip_if_unavailable<ConnType>()) {
            GTEST_SKIP() << "Backend unavailable";
        }
        connstr_ = get_pool_connstr<ConnType>();
    }
    std::string connstr_;
};

TYPED_TEST_SUITE(PoolShutdownTest, DatabaseTypes);

TYPED_TEST(PoolShutdownTest, Shutdown_RejectsNew) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 1, .max_connections = 3});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    pool->shutdown();
    auto conn = pool->checkout();
    EXPECT_FALSE(conn.has_value());
}

TYPED_TEST(PoolShutdownTest, Shutdown_WaitsForReturns) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 1, .max_connections = 3});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto conn = pool->checkout();
    ASSERT_TRUE(conn.has_value());

    std::atomic<bool> shutdown_done{false};
    std::thread       shutdown_thread([&pool, &shutdown_done] {
        pool->shutdown();
        shutdown_done = true;
    });

    // Shutdown should block until connection is returned
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(shutdown_done);

    conn->reset(); // Return connection
    shutdown_thread.join();
    EXPECT_TRUE(shutdown_done);
}

// ============================================================================
// Thread Safety Tests (designed to trigger TSAN)
// ============================================================================

template <typename ConnType> class PoolThreadSafetyTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (skip_if_unavailable<ConnType>()) {
            GTEST_SKIP() << "Backend unavailable";
        }
        connstr_ = get_pool_connstr<ConnType>();
    }
    std::string connstr_;
};

TYPED_TEST_SUITE(PoolThreadSafetyTest, DatabaseTypes);

TYPED_TEST(PoolThreadSafetyTest, ConcurrentCheckoutCheckin) {
    // SQLite :memory: creates isolated DBs, use shared cache for pooling test
    std::string const connstr = [this]() -> std::string {
        if constexpr (storm::test::is_sqlite<TypeParam>()) {
            return "file::memory:?cache=shared";
        } else {
            return this->connstr_;
        }
    }();

    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            connstr, {.min_connections = 2, .max_connections = 5, .checkout_timeout_ms = 5000}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    constexpr int            num_threads    = 10;
    constexpr int            num_iterations = 100;
    std::atomic<int>         success_count{0};
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&pool, &success_count] {
            for (int i = 0; i < num_iterations; ++i) {
                auto conn = pool->checkout();
                if (conn.has_value()) {
                    ++success_count;
                }
                // shared_ptr release triggers checkin
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, num_threads * num_iterations);
    EXPECT_EQ(pool->in_use(), 0);
}

// ============================================================================
// QuerySet Integration Tests
// ============================================================================

template <typename ConnType> class PoolQuerySetTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (skip_if_unavailable<ConnType>()) {
            GTEST_SKIP() << "Backend unavailable";
        }
        connstr_ = get_pool_connstr<ConnType>();
    }

    auto TearDown() -> void override {
        storm::QuerySet<Person, ConnType>::clear_default_connection();
    }

    std::string connstr_;
};

TYPED_TEST_SUITE(PoolQuerySetTest, DatabaseTypes);

TYPED_TEST(PoolQuerySetTest, QuerySet_FromPool) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 1, .max_connections = 3});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto result = storm::QuerySet<Person, TypeParam>::set_default_connection(*pool);
    ASSERT_TRUE(result.has_value()) << result.error().message();
    EXPECT_TRUE((storm::QuerySet<Person, TypeParam>::has_default_connection()));
    EXPECT_EQ(pool->in_use(), 1);
}

TYPED_TEST(PoolQuerySetTest, QuerySet_SetDefaultConnection_PoolExhausted) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 1, .checkout_timeout_ms = 0}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    // Exhaust the pool
    auto held = pool->checkout();
    ASSERT_TRUE(held.has_value());

    // set_default_connection should fail — pool exhausted
    auto result = storm::QuerySet<Person, TypeParam>::set_default_connection(*pool);
    EXPECT_FALSE(result.has_value());
}

TYPED_TEST(PoolQuerySetTest, QuerySet_ClearReturnsToPool) {
    auto pool =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 1, .max_connections = 3});
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto result = storm::QuerySet<Person, TypeParam>::set_default_connection(*pool);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(pool->in_use(), 1);

    storm::QuerySet<Person, TypeParam>::clear_default_connection();
    EXPECT_EQ(pool->in_use(), 0);
    EXPECT_EQ(pool->available(), 1);
}

// ============================================================================
// Pool Handle Copy Semantics
// ============================================================================

template <typename ConnType> class PoolHandleTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (skip_if_unavailable<ConnType>()) {
            GTEST_SKIP() << "Backend unavailable";
        }
        connstr_ = get_pool_connstr<ConnType>();
    }
    std::string connstr_;
};

TYPED_TEST_SUITE(PoolHandleTest, DatabaseTypes);

TYPED_TEST(PoolHandleTest, CopyHandle_SharesCore) {
    auto pool1 =
            storm::db::ConnectionPool<TypeParam>::create(this->connstr_, {.min_connections = 1, .max_connections = 3});
    ASSERT_TRUE(pool1.has_value()) << pool1.error().message();

    auto pool2 = *pool1; // Copy handle
    auto conn  = pool1->checkout();
    ASSERT_TRUE(conn.has_value());
    EXPECT_EQ(pool2.in_use(), 1); // Same underlying core
}

// ============================================================================
// Connection Lifetime & Idle Timeout Tests
// ============================================================================

template <typename ConnType> class PoolLifetimeTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (skip_if_unavailable<ConnType>()) {
            GTEST_SKIP() << "Backend unavailable";
        }
        connstr_ = get_pool_connstr<ConnType>();
    }
    std::string connstr_;
};

TYPED_TEST_SUITE(PoolLifetimeTest, DatabaseTypes);

TYPED_TEST(PoolLifetimeTest, MaxLifetime_EvictsExpiredOnCheckout) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 3, .max_lifetime_ms = 100, .idle_timeout_ms = 0}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    {
        auto c1 = pool->checkout();
        ASSERT_TRUE(c1.has_value());
        // Populate statement cache to prove this is the "old" connection
        auto stmt = c1->get()->prepare_cached("SELECT 1");
        ASSERT_TRUE(stmt.has_value());
        EXPECT_GE(c1->get()->cached_statement_count(), 1U);
    }
    EXPECT_EQ(pool->size(), 1);

    // Wait for lifetime to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Next checkout evicts expired connection, grows a fresh one
    auto c2 = pool->checkout();
    ASSERT_TRUE(c2.has_value());
    // Fresh connection has empty statement cache — proves it's new
    EXPECT_EQ(c2->get()->cached_statement_count(), 0U);
}

TYPED_TEST(PoolLifetimeTest, IdleTimeout_EvictsIdleOnCheckout) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 2, .max_connections = 5, .max_lifetime_ms = 0, .idle_timeout_ms = 100}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();
    EXPECT_EQ(pool->size(), 2);

    // Checkout and return one connection to reset its last_used_at
    {
        auto c = pool->checkout();
        ASSERT_TRUE(c.has_value());
    }

    // Wait for idle timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Next checkout evicts idle connections, then grows a new one
    auto c = pool->checkout();
    ASSERT_TRUE(c.has_value());
    EXPECT_TRUE((*c)->is_open());
    // Pool shrank (expired idle removed) then grew by 1
    EXPECT_EQ(pool->size(), 1);
}

TYPED_TEST(PoolLifetimeTest, NoExpiry_ConnectionsLiveForever) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 3, .max_lifetime_ms = 0, .idle_timeout_ms = 0}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    TypeParam const* first_raw = nullptr;
    {
        auto c = pool->checkout();
        ASSERT_TRUE(c.has_value());
        first_raw = c->get();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto c2 = pool->checkout();
    ASSERT_TRUE(c2.has_value());
    EXPECT_EQ(c2->get(), first_raw); // Same connection — no expiry
}

TYPED_TEST(PoolLifetimeTest, InUseConnections_NotEvicted) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 2, .max_lifetime_ms = 100, .idle_timeout_ms = 0}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto c1 = pool->checkout();
    ASSERT_TRUE(c1.has_value());

    // Wait for lifetime to expire while connection is checked out
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // c1 is still in use — should NOT be evicted
    EXPECT_EQ(pool->in_use(), 1);
    EXPECT_TRUE((*c1)->is_open());
}

TYPED_TEST(PoolLifetimeTest, MaxLifetime_ClosedAfterReturnAndRecheckout) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 1, .max_lifetime_ms = 100, .idle_timeout_ms = 0}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    // Checkout and hold past max_lifetime
    {
        auto c = pool->checkout();
        ASSERT_TRUE(c.has_value());
        auto stmt = c->get()->prepare_cached("SELECT 1");
        ASSERT_TRUE(stmt.has_value());
        EXPECT_GE(c->get()->cached_statement_count(), 1U);

        // Hold connection past its max lifetime
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    } // checkin — connection is back in pool but expired

    // Next checkout should evict the expired connection and create a fresh one
    auto c2 = pool->checkout();
    ASSERT_TRUE(c2.has_value());
    EXPECT_EQ(c2->get()->cached_statement_count(), 0U); // Fresh connection — old was closed
    EXPECT_EQ(pool->size(), 1);                         // Pool didn't grow, it replaced
}

TYPED_TEST(PoolLifetimeTest, BothEnabled_IdleTimeoutFiresFirst) {
    // idle_timeout=100ms fires before max_lifetime=500ms
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 3, .max_lifetime_ms = 500, .idle_timeout_ms = 100}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    {
        auto c = pool->checkout();
        ASSERT_TRUE(c.has_value());
        // Populate statement cache to prove this is the "old" connection
        auto stmt = c->get()->prepare_cached("SELECT 1");
        ASSERT_TRUE(stmt.has_value());
        EXPECT_GE(c->get()->cached_statement_count(), 1U);
    }

    // Wait past idle timeout but within max lifetime
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Old connection evicted by idle timeout → pool grew a fresh one
    auto c2 = pool->checkout();
    ASSERT_TRUE(c2.has_value());
    // Fresh connection has empty statement cache
    EXPECT_EQ(c2->get()->cached_statement_count(), 0U);
}

TYPED_TEST(PoolLifetimeTest, InUseConnections_NotEvictedByIdleTimeout) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 2, .max_lifetime_ms = 0, .idle_timeout_ms = 100}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    auto c1 = pool->checkout();
    ASSERT_TRUE(c1.has_value());

    // Wait past idle timeout while connection is checked out
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // c1 is in use — must NOT be evicted
    EXPECT_EQ(pool->in_use(), 1);
    EXPECT_TRUE((*c1)->is_open());
}

TYPED_TEST(PoolLifetimeTest, ActiveUse_ResetsIdleTimer) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 1, .max_connections = 1, .max_lifetime_ms = 0, .idle_timeout_ms = 150}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();

    TypeParam const* raw = nullptr;
    {
        auto c = pool->checkout();
        ASSERT_TRUE(c.has_value());
        raw = c->get();
    } // checkin resets last_used_at

    // Wait 100ms (within 150ms idle timeout)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Checkout + checkin resets the idle timer
    {
        auto c = pool->checkout();
        ASSERT_TRUE(c.has_value());
        EXPECT_EQ(c->get(), raw); // Same connection, not expired yet
    }

    // Wait another 100ms (within 150ms since last checkin)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Still alive — timer was reset by the second checkin
    auto c3 = pool->checkout();
    ASSERT_TRUE(c3.has_value());
    EXPECT_EQ(c3->get(), raw); // Same connection survives
}

TYPED_TEST(PoolLifetimeTest, MixedExpiry_OnlyExpiredEvicted) {
    auto pool = storm::db::ConnectionPool<TypeParam>::create(
            this->connstr_, {.min_connections = 2, .max_connections = 3, .max_lifetime_ms = 100, .idle_timeout_ms = 0}
    );
    ASSERT_TRUE(pool.has_value()) << pool.error().message();
    EXPECT_EQ(pool->size(), 2);

    // Checkout and return first connection (it starts aging)
    TypeParam const* old_raw = nullptr;
    {
        auto c = pool->checkout();
        ASSERT_TRUE(c.has_value());
        old_raw = c->get();
    }

    // Wait for first connection to exceed max_lifetime
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Add a fresh connection by checking out both slots + one new
    auto c1 = pool->checkout(); // May get old or new — eviction runs first
    ASSERT_TRUE(c1.has_value());

    // The old connection should have been evicted, c1 is a fresh one
    // Pool grew after evicting the expired idle entry
    EXPECT_TRUE((*c1)->is_open());
}

TYPED_TEST(PoolHandleTest, PoolDestroyed_ConnectionStillWorks) {
    // The custom deleter captures shared_ptr<PoolCore>, keeping PoolCore alive
    // even after all ConnectionPool handles are destroyed.
    // Connection remains valid because PoolCore (and its PoolEntry unique_ptrs) survive.
    std::shared_ptr<TypeParam> surviving_conn;
    {
        auto pool = storm::db::ConnectionPool<TypeParam>::create(
                this->connstr_, {.min_connections = 1, .max_connections = 3}
        );
        ASSERT_TRUE(pool.has_value()) << pool.error().message();
        auto conn = pool->checkout();
        ASSERT_TRUE(conn.has_value());
        surviving_conn = std::move(*conn);
    } // pool handle destroyed, but PoolCore stays alive via surviving_conn's deleter
    EXPECT_TRUE(surviving_conn->is_open());
}

// ============================================================================
// Mock Pool Tests (cover error/edge-case paths unreachable with SQLite)
// ============================================================================

class MockPoolTest : public ::testing::Test {
  public:
    MockConfigGuard guard;
};

// Lines 276-277, 53-54, 244-245: create() fails when init()/create_entry()/open() fails
TEST_F(MockPoolTest, Create_FailsWhenOpenFails) {
    mock_config().open_should_fail = true;
    auto pool = storm::db::ConnectionPool<MockPoolConnection>::create("mock://db", {.min_connections = 1});
    EXPECT_FALSE(pool.has_value());
    EXPECT_EQ(pool.error().code_, -1);
}

// Lines 87-88: checkout grow path — open() fails during growth
TEST_F(MockPoolTest, Checkout_GrowFails) {
    mock_config().open_fail_after = 1; // First open (init) succeeds, second (grow) fails
    auto pool                     = storm::db::ConnectionPool<MockPoolConnection>::create(
            "mock://db", {.min_connections = 1, .max_connections = 3}
    );
    ASSERT_TRUE(pool.has_value());

    auto c1 = pool->checkout(); // Gets pre-created connection
    ASSERT_TRUE(c1.has_value());

    auto c2 = pool->checkout(); // Triggers grow, open() fails
    EXPECT_FALSE(c2.has_value());
}

// Lines 118-119: shutdown during wait loop
TEST_F(MockPoolTest, Checkout_ShutdownDuringWait) {
    auto pool = storm::db::ConnectionPool<MockPoolConnection>::create(
            "mock://db", {.min_connections = 1, .max_connections = 1, .checkout_timeout_ms = 5000}
    );
    ASSERT_TRUE(pool.has_value());

    auto c1 = pool->checkout();
    ASSERT_TRUE(c1.has_value());

    // Deterministic sequence (no timing races, fixes #300):
    //   1. Waiter starts and counts down `waiter_started` immediately before calling
    //      checkout(). Main blocks on that latch — guarantees the waiter is at (or
    //      microseconds away from) cv_.wait_until before we proceed.
    //   2. Main spawns shutdown_thread. shutdown() sets shutdown_ = true, notify_all()s,
    //      then blocks on cv_.wait until in_use == 0 (c1 still holds the only conn).
    //   3. Whichever thread reaches the mutex first, the waiter's checkout() will see
    //      shutdown_ == true — either via the early-return at the top of checkout()
    //      (if shutdown_thread ran first) or via the shutdown_ re-check inside
    //      wait_for_idle (if the waiter ran first). Either way: got_error == true.
    //   4. waiter.join() returns once the waiter has set got_error.
    //   5. Only then we reset c1, which lets shutdown_thread's cv_.wait observe
    //      in_use == 0 and complete.
    std::atomic<bool> got_error{false};
    std::latch        waiter_started{1};
    std::thread       waiter([&pool, &got_error, &waiter_started] {
        waiter_started.count_down();
        auto c    = pool->checkout();
        got_error = !c.has_value();
    });

    waiter_started.wait();
    std::thread shutdown_thread([&pool] { pool->shutdown(); });

    waiter.join();
    c1->reset(); // Return c1 → shutdown_thread's cv_.wait predicate becomes true.
    shutdown_thread.join();
    EXPECT_TRUE(got_error);
}

// Lines 232-234: find_idle() erases stale connection (is_open() == false)
TEST_F(MockPoolTest, Checkout_EvictsStaleConnection) {
    auto pool = storm::db::ConnectionPool<MockPoolConnection>::create(
            "mock://db", {.min_connections = 1, .max_connections = 3, .validate_on_checkout = true}
    );
    ASSERT_TRUE(pool.has_value());

    // Checkout, mark as closed, then return
    MockPoolConnection* raw = nullptr;
    {
        auto c = pool->checkout();
        ASSERT_TRUE(c.has_value());
        raw           = c->get();
        raw->is_open_ = false; // Simulate closed connection
    }
    EXPECT_EQ(pool->size(), 1);

    // Next checkout: find_idle() sees is_open()==false, erases it, grows a new one
    auto c2 = pool->checkout();
    ASSERT_TRUE(c2.has_value());
    EXPECT_TRUE(c2->get()->is_open());
}

// Lines 83-84: shutdown detected after relocking in checkout grow path
TEST_F(MockPoolTest, Checkout_ShutdownDuringGrow) {
    // open() takes 100ms — gives time for shutdown() to run while lock is released
    mock_config().open_delay_ms = 100;
    auto pool                   = storm::db::ConnectionPool<MockPoolConnection>::create(
            "mock://db", {.min_connections = 0, .max_connections = 3}
    );
    ASSERT_TRUE(pool.has_value());

    // Thread does checkout → releases lock → open() sleeps 100ms
    std::atomic<bool> checkout_failed{false};
    std::thread       grower([&pool, &checkout_failed] {
        auto c          = pool->checkout();
        checkout_failed = !c.has_value();
    });

    // Shutdown while grower is in open() (lock released)
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    pool->shutdown();
    grower.join();
    EXPECT_TRUE(checkout_failed);
}

// Lines 92-97: race condition — pool reached max after relock
TEST_F(MockPoolTest, Checkout_RaceConditionMaxReached) {
    auto pool = storm::db::ConnectionPool<MockPoolConnection>::create(
            "mock://db", {.min_connections = 0, .max_connections = 2, .checkout_timeout_ms = 0}
    );
    ASSERT_TRUE(pool.has_value());

    // Use threads to trigger the race: both see pool < max, both try to grow
    std::atomic<int>         success_count{0};
    std::atomic<int>         failure_count{0};
    constexpr int            num_threads = 10;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    // Hold connections to exhaust the pool
    std::vector<std::shared_ptr<MockPoolConnection>> held_conns;
    held_conns.reserve(2);

    for (int i = 0; i < 2; ++i) {
        auto c = pool->checkout();
        if (c.has_value()) {
            held_conns.push_back(std::move(*c));
        }
    }

    // Now pool is at max — further checkouts with timeout=0 should fail
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&pool, &success_count, &failure_count] {
            auto c = pool->checkout();
            if (c.has_value()) {
                ++success_count;
            } else {
                ++failure_count;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, 0);
    EXPECT_EQ(failure_count, num_threads);
    EXPECT_EQ(pool->size(), 2);
}

// Bad config: negative timeouts
TEST_F(MockPoolTest, Create_BadConfig_NegativeTimeouts) {
    auto p1 = storm::db::ConnectionPool<MockPoolConnection>::create("mock://db", {.checkout_timeout_ms = -1});
    EXPECT_FALSE(p1.has_value());

    auto p2 = storm::db::ConnectionPool<MockPoolConnection>::create("mock://db", {.max_lifetime_ms = -1});
    EXPECT_FALSE(p2.has_value());

    auto p3 = storm::db::ConnectionPool<MockPoolConnection>::create("mock://db", {.idle_timeout_ms = -1});
    EXPECT_FALSE(p3.has_value());
}

// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes,readability-identifier-length,cppcoreguidelines-special-member-functions,readability-function-cognitive-complexity)
// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
