#include <gtest/gtest.h>
#include "test_db_helpers.h"

import storm;
import <string>;
import <vector>;
import <expected>;
import <span>;
import <format>;

using namespace storm;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"
#include "test_select_runner.h"
#include "test_write_runner.h"
#include "test_yaml_register.h"
#include "test_parser.hpp"

inline constexpr auto UNIFIED_TESTS = storm::test::load_unified_tests();

using storm::test::PEOPLE_25; // NOLINT(misc-unused-using-decls) - used in seed helpers via #include

// Fixture for unified YAML tests
template <typename ConnType>
class UnifiedYamlTest : public StormTestFixture<Person, ConnType, SimpleRecord, Message> {};

// Partial specialization of YamlTestInstance for UnifiedYamlTest
template <size_t I, typename ConnType>
class storm::test::YamlTestInstance<I, UnifiedYamlTest<ConnType>, ConnType> : public UnifiedYamlTest<ConnType> {
    static constexpr auto& tc = UNIFIED_TESTS[I];

    auto reset_tables() -> void {
        std::shared_ptr<ConnType> conn = // NOSONAR(S1659)
                storm::QuerySet<Person, ConnType>::get_default_connection();
        ASSERT_NE(conn, nullptr);
        storm::test::rollback_test_txn<ConnType>(conn);
        ASSERT_TRUE((storm::test::ensure_tables<ConnType, Person, SimpleRecord, Message>(conn)));
        storm::QuerySet<Person, ConnType>       cp;
        storm::QuerySet<SimpleRecord, ConnType> cs;
        storm::QuerySet<Message, ConnType>      cm;
        ASSERT_TRUE(cp.erase_all().execute().has_value());
        ASSERT_TRUE(cs.erase_all().execute().has_value());
        ASSERT_TRUE(cm.erase_all().execute().has_value());
    }

    auto seed_main_msg() -> void {
        ASSERT_TRUE(
                (storm::test::batch_insert<Person, ConnType>(std::vector<Person>(PEOPLE_25.begin(), PEOPLE_25.end())))
        );
        storm::QuerySet<Person, ConnType> pqs;
        auto                              people_result = pqs.template order_by<^^Person::name>().select().execute();
        ASSERT_TRUE(people_result.has_value()) << people_result.error().message();
        ASSERT_EQ(people_result.value().size(), 25U);
        std::array<int, 4> sender_ids{};
        for (const auto& p : people_result.value()) {
            if (p.name == "Alice") {
                sender_ids[0] = p.id;
            } else if (p.name == "Bob") {
                sender_ids[1] = p.id;
            } else if (p.name == "Charlie") {
                sender_ids[2] = p.id;
            } else if (p.name == "Diana") {
                sender_ids[3] = p.id;
            }
        }
        const std::vector<Message> msgs = {
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
    }

    auto seed_dataset() -> void {
        if constexpr (tc.dataset == "main") {
            ASSERT_TRUE((
                    storm::test::batch_insert<Person, ConnType>(std::vector<Person>(PEOPLE_25.begin(), PEOPLE_25.end()))
            ));
        } else if constexpr (tc.dataset == "main_msg") {
            seed_main_msg();
        } else if constexpr (tc.bench.dataset_size > 0 && tc.dataset.empty()) {
            std::vector<Person> seed;
            seed.reserve(static_cast<size_t>(tc.bench.dataset_size));
            for (int i = 1; i <= tc.bench.dataset_size; ++i) {
                seed.emplace_back(Person{.id = 0, .name = std::format("P{}", i), .age = 20 + i});
            }
            ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(seed)));
        }
    }

    auto dispatch_select() -> void {
        if constexpr (tc.bench.model == "person") {
            storm::test::SelectRunner<Person, ConnType> runner;
            runner.template run<tc>();
        }
    }

    auto dispatch_aggregate() -> void { // NOSONAR(S3776)
        if constexpr (tc.bench.model == "person") {
            storm::test::AggregateRunner<Person, ConnType> runner;
            runner.template run<tc>();
        } else if constexpr (tc.bench.model == "message") {
            storm::test::AggregateRunner<Message, ConnType> runner;
            runner.template run<tc>();
        }
    }

    auto dispatch_chain() -> void {
        if constexpr (tc.bench.model == "person") {
            storm::test::ChainAggRunner<Person, ConnType> runner;
            runner.template run<tc>();
        } else if constexpr (tc.bench.model == "message") {
            storm::test::ChainAggRunner<Message, ConnType> runner;
            runner.template run<tc>();
        }
    }

    auto dispatch_group_by() -> void {
        if constexpr (tc.bench.model == "person") {
            storm::test::GroupByRunner<Person, ConnType> runner;
            runner.template run<tc>();
        } else if constexpr (tc.bench.model == "message") {
            storm::test::GroupByRunner<Message, ConnType> runner;
            runner.template run<tc>();
        }
    }

    auto dispatch_insert() -> void {
        if constexpr (tc.bench.model == "person") {
            storm::test::InsertRunner<Person, ConnType> runner;
            runner.template run<tc>();
        } else if constexpr (tc.bench.model == "simple") {
            storm::test::InsertRunner<SimpleRecord, ConnType> runner;
            runner.template run<tc>();
        }
    }

    auto dispatch_update() -> void {
        if constexpr (tc.bench.model == "simple") {
            storm::test::UpdateRunner<SimpleRecord, ConnType> runner;
            runner.template run<tc>();
        } else if constexpr (tc.bench.model == "person") {
            storm::test::UpdateRunner<Person, ConnType> runner;
            runner.template run<tc>();
        }
    }

    auto dispatch_erase() -> void {
        if constexpr (tc.bench.model == "person") {
            storm::test::EraseRunner<Person, ConnType> runner;
            runner.template run<tc>();
        } else if constexpr (tc.bench.model == "simple") {
            storm::test::EraseRunner<SimpleRecord, ConnType> runner;
            runner.template run<tc>();
        }
    }

    auto dispatch_distinct() -> void {
        if constexpr (tc.bench.model == "person") {
            storm::test::DistinctRunner<Person, ConnType> runner;
            runner.template run<tc>();
        }
    }

    auto dispatch_grouped_family() -> void {
        if constexpr (tc.query_type == "chain") {
            dispatch_chain();
        } else if constexpr (tc.query_type == "distinct") {
            dispatch_distinct();
        } else {
            dispatch_group_by();
        }
    }

    static consteval auto is_grouped_op() -> bool {
        constexpr auto qt = tc.query_type.view();
        return qt == "chain" || qt == "distinct" || qt == "group_count" || qt == "group_sum" || qt == "group_avg" ||
               qt == "group_min" || qt == "group_max";
    }

    auto dispatch_select_family() -> void {
        if constexpr (tc.query_type == "select" || tc.query_type == "first" || tc.query_type == "one") {
            dispatch_select();
        } else if constexpr (is_grouped_op()) {
            dispatch_grouped_family();
        } else {
            dispatch_aggregate();
        }
    }

    auto dispatch_write_family() -> void {
        if constexpr (tc.query_type == "insert_one" || tc.query_type == "insert_batch") {
            dispatch_insert();
        } else if constexpr (tc.query_type == "update_batch") {
            dispatch_update();
        } else if constexpr (tc.query_type == "erase_all" || tc.query_type == "erase_batch") {
            dispatch_erase();
        }
    }

    static consteval auto is_write_op() -> bool {
        constexpr auto qt = tc.query_type.view();
        return qt == "insert_one" || qt == "insert_batch" || qt == "update_batch" || qt == "erase_all" ||
               qt == "erase_batch";
    }

    void TestBody() override {
        reset_tables();
        seed_dataset();
        if constexpr (is_write_op()) {
            dispatch_write_family();
        } else {
            dispatch_select_family();
        }
    }
};

namespace {
    [[maybe_unused]] const bool unified_yaml_registered_ =
            storm::test::register_both_backends<UNIFIED_TESTS, UnifiedYamlTest>("UnifiedYamlTest");
} // namespace
