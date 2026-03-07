#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <vector>;
import <expected>;
import <span>;
import <format>;

using namespace storm;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"
#include "test_query_dispatch.h"
#include "test_query_runner_base.h"
#include "test_select_runner.h"
#include "test_write_runner.h"
#include "test_yaml_register.h"
#include "test_parser.hpp"

inline constexpr auto UNIFIED_TESTS = storm::test::load_unified_tests();

using storm::test::MESSAGES_8;
using storm::test::PEOPLE_25;

// Fixture for unified YAML tests
template <typename ConnType> class UnifiedYamlTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        ASSERT_TRUE((storm::test::ensure_table<Person, ConnType>(conn).has_value())) << "Failed to create Person table";
        ASSERT_TRUE((storm::test::ensure_table<SimpleRecord, ConnType>(conn).has_value()))
                << "Failed to create SimpleRecord table";
        ASSERT_TRUE((storm::test::ensure_table<Message, ConnType>(conn).has_value()))
                << "Failed to create Message table";
    }
};

// Partial specialization of YamlTestInstance for UnifiedYamlTest
template <size_t I, typename ConnType>
class storm::test::YamlTestInstance<I, UnifiedYamlTest<ConnType>, ConnType> : public UnifiedYamlTest<ConnType> {
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void TestBody() override {
        constexpr auto& tc = UNIFIED_TESTS[I];

        // --- Per-test isolation: clean all tables ---
        auto conn = storm::QuerySet<Person, ConnType>::get_default_connection();
        ASSERT_NE(conn, nullptr);
        storm::test::rollback_test_txn<ConnType>(conn);
        ASSERT_TRUE((storm::test::ensure_table<Person, ConnType>(conn).has_value()));
        ASSERT_TRUE((storm::test::ensure_table<SimpleRecord, ConnType>(conn).has_value()));
        ASSERT_TRUE((storm::test::ensure_table<Message, ConnType>(conn).has_value()));
        {
            storm::QuerySet<Person, ConnType>       cp;
            storm::QuerySet<SimpleRecord, ConnType> cs;
            storm::QuerySet<Message, ConnType>      cm;
            ASSERT_TRUE(cp.remove_all().execute().has_value());
            ASSERT_TRUE(cs.remove_all().execute().has_value());
            ASSERT_TRUE(cm.remove_all().execute().has_value());
        }

        // --- Seed dataset ---
        if constexpr (tc.dataset == "main") {
            ASSERT_TRUE((
                    storm::test::batch_insert<Person, ConnType>(std::vector<Person>(PEOPLE_25.begin(), PEOPLE_25.end()))
            ));
        } else if constexpr (tc.dataset == "main_msg") {
            ASSERT_TRUE((
                    storm::test::batch_insert<Person, ConnType>(std::vector<Person>(PEOPLE_25.begin(), PEOPLE_25.end()))
            ));
            // Query back actual IDs for FK safety (PostgreSQL may assign different IDs)
            storm::QuerySet<Person, ConnType> pqs;
            auto people_result = pqs.template order_by<^^Person::name>().select().execute();
            ASSERT_TRUE(people_result.has_value()) << people_result.error().message();
            ASSERT_EQ(people_result.value().size(), 25u);
            std::array<int, 4> sender_ids{}; // Alice, Bob, Charlie, Diana
            for (const auto& p : people_result.value()) {
                if (p.name == "Alice")
                    sender_ids[0] = p.id;
                else if (p.name == "Bob")
                    sender_ids[1] = p.id;
                else if (p.name == "Charlie")
                    sender_ids[2] = p.id;
                else if (p.name == "Diana")
                    sender_ids[3] = p.id;
            }
            std::vector<Message> msgs = {
                    {.content = "Hello", .value = 10, .sender = {.id = sender_ids[0]}},
                    {.content = "World", .value = 20, .sender = {.id = sender_ids[0]}},
                    {.content = "Hi there", .value = 30, .sender = {.id = sender_ids[0]}},
                    {.content = "Goodbye", .value = 40, .sender = {.id = sender_ids[1]}},
                    {.content = "Testing", .value = 50, .sender = {.id = sender_ids[1]}},
                    {.content = "Greetings", .value = 60, .sender = {.id = sender_ids[2]}},
                    {.content = "Reply", .value = 70, .sender = {.id = sender_ids[2]}},
                    {.content = "Quick note", .value = 80, .sender = {.id = sender_ids[3]}},
            };
            ASSERT_TRUE((storm::test::batch_insert<Message, ConnType>(msgs)));
        } else if constexpr (tc.dataset_size > 0 && tc.dataset.empty()) {
            std::vector<Person> seed;
            seed.reserve(static_cast<size_t>(tc.dataset_size));
            for (int i = 1; i <= tc.dataset_size; ++i)
                seed.emplace_back(Person{.id = 0, .name = "P" + std::to_string(i), .age = 20 + i});
            ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(seed)));
        }
        // dataset == "custom" or "empty": seeding handled by runners or no data needed

        // --- Dispatch based on query_type ---
        if constexpr (tc.query_type == "select" || tc.query_type == "first" || tc.query_type == "one") {
            // SELECT / first() queries
            if constexpr (tc.model == "person") {
                storm::test::SelectRunner<Person, ConnType> runner;
                runner.template run<tc>();
            }

        } else if constexpr (tc.query_type == "count" || tc.query_type == "count_distinct" ||
                             tc.query_type == "count_field" || tc.query_type == "sum" || tc.query_type == "avg" ||
                             tc.query_type == "min" || tc.query_type == "max") {
            // Scalar aggregates
            if constexpr (tc.model == "person") {
                storm::test::AggregateRunner<Person, ConnType> runner;
                runner.template run<tc>();
            } else if constexpr (tc.model == "message") {
                storm::test::AggregateRunner<Message, ConnType> runner;
                runner.template run<tc>();
            }

        } else if constexpr (tc.query_type == "chain") {
            // Multi-aggregate chain
            if constexpr (tc.model == "person") {
                storm::test::ChainAggRunner<Person, ConnType> runner;
                runner.template run<tc>();
            } else if constexpr (tc.model == "message") {
                storm::test::ChainAggRunner<Message, ConnType> runner;
                runner.template run<tc>();
            }

        } else if constexpr (tc.query_type == "distinct") {
            if constexpr (tc.model == "person") {
                storm::test::DistinctRunner<Person, ConnType> runner;
                runner.template run<tc>();
            }

        } else if constexpr (tc.query_type == "group_count" || tc.query_type == "group_sum" ||
                             tc.query_type == "group_avg" || tc.query_type == "group_min" ||
                             tc.query_type == "group_max") {
            // GROUP BY aggregates
            if constexpr (tc.model == "person") {
                storm::test::GroupByRunner<Person, ConnType> runner;
                runner.template run<tc>();
            } else if constexpr (tc.model == "message") {
                storm::test::GroupByRunner<Message, ConnType> runner;
                runner.template run<tc>();
            }

        } else if constexpr (tc.query_type == "insert_one" || tc.query_type == "insert_batch") {
            // INSERT operations
            if constexpr (tc.model == "person") {
                storm::test::InsertRunner<Person, ConnType> runner;
                runner.template run<tc>();
            } else if constexpr (tc.model == "simple") {
                storm::test::InsertRunner<SimpleRecord, ConnType> runner;
                runner.template run<tc>();
            }

        } else if constexpr (tc.query_type == "update_batch") {
            // UPDATE operations
            if constexpr (tc.model == "simple") {
                storm::test::UpdateRunner<SimpleRecord, ConnType> runner;
                runner.template run<tc>();
            } else if constexpr (tc.model == "person") {
                storm::test::UpdateRunner<Person, ConnType> runner;
                runner.template run<tc>();
            }

        } else if constexpr (tc.query_type == "remove_all" || tc.query_type == "remove_batch") {
            // REMOVE operations
            if constexpr (tc.model == "person") {
                storm::test::RemoveRunner<Person, ConnType> runner;
                runner.template run<tc>();
            } else if constexpr (tc.model == "simple") {
                storm::test::RemoveRunner<SimpleRecord, ConnType> runner;
                runner.template run<tc>();
            }
        }
    }
};

namespace {
    [[maybe_unused]] const bool unified_yaml_registered_ =
            storm::test::register_both_backends<UNIFIED_TESTS, UnifiedYamlTest>("UnifiedYamlTest");
} // namespace

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
