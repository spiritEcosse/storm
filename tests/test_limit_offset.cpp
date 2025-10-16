#include <gtest/gtest.h>

import storm;
import <string>;
import <vector>;
import <expected>;
import <algorithm>;

using namespace storm;

// Test struct for LIMIT/OFFSET operations
struct LimitTestPerson {
    [[=storm::meta::FieldAttr::primary]] int id;
    std::string                              name;
    int                                      age;

    // Equality operator for testing
    bool operator==(const LimitTestPerson& other) const {
        return id == other.id && name == other.name && age == other.age;
    }
};

class LimitOffsetTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Set up in-memory database
        auto result = QuerySet<LimitTestPerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to set default connection";

        auto& conn = QuerySet<LimitTestPerson>::get_default_connection();

        // Create table with AUTOINCREMENT
        auto exec_result = conn.execute(
                "CREATE TABLE LimitTestPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL)"
        );
        ASSERT_TRUE(exec_result.has_value()) << "Failed to create table";

        // Insert test data (20 records)
        for (int i = 1; i <= 20; ++i) {
            std::string name = "Person" + std::to_string(i);
            int age          = 20 + i;
            auto insert_result =
                    conn.execute("INSERT INTO LimitTestPerson (name, age) VALUES ('" + name + "', " + std::to_string(age) + ")");
            ASSERT_TRUE(insert_result.has_value()) << "Failed to insert test data";
        }
    }

    void TearDown() override {
        QuerySet<LimitTestPerson>::clear_default_connection();
    }
};

// Test LIMIT alone
TEST_F(LimitOffsetTest, LimitOnly) {
    QuerySet<LimitTestPerson> qs;

    auto result = qs.limit(5).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with LIMIT failed";

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 5);

    // Verify we got the first 5 records
    for (size_t i = 0; i < people.size(); ++i) {
        EXPECT_EQ(people[i].id, static_cast<int>(i + 1));
        EXPECT_EQ(people[i].name, "Person" + std::to_string(i + 1));
        EXPECT_EQ(people[i].age, static_cast<int>(21 + i));
    }
}

// Test OFFSET alone (should work with implicit large LIMIT)
TEST_F(LimitOffsetTest, OffsetOnly) {
    QuerySet<LimitTestPerson> qs;

    auto result = qs.offset(15).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with OFFSET failed";

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 5); // Records 16-20

    // Verify we got records starting from offset 15
    for (size_t i = 0; i < people.size(); ++i) {
        EXPECT_EQ(people[i].id, static_cast<int>(16 + i));
        EXPECT_EQ(people[i].name, "Person" + std::to_string(16 + i));
    }
}

// Test LIMIT + OFFSET together
TEST_F(LimitOffsetTest, LimitAndOffset) {
    QuerySet<LimitTestPerson> qs;

    auto result = qs.limit(5).offset(10).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with LIMIT and OFFSET failed";

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 5);

    // Verify we got records 11-15 (offset 10 means skip first 10)
    for (size_t i = 0; i < people.size(); ++i) {
        EXPECT_EQ(people[i].id, static_cast<int>(11 + i));
        EXPECT_EQ(people[i].name, "Person" + std::to_string(11 + i));
        EXPECT_EQ(people[i].age, static_cast<int>(31 + i));
    }
}

// Test OFFSET + LIMIT (different order)
TEST_F(LimitOffsetTest, OffsetAndLimit) {
    QuerySet<LimitTestPerson> qs;

    auto result = qs.offset(5).limit(3).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with OFFSET and LIMIT failed";

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 3);

    // Verify we got records 6-8
    for (size_t i = 0; i < people.size(); ++i) {
        EXPECT_EQ(people[i].id, static_cast<int>(6 + i));
        EXPECT_EQ(people[i].name, "Person" + std::to_string(6 + i));
    }
}

// Test LIMIT larger than result set
TEST_F(LimitOffsetTest, LimitLargerThanResultSet) {
    QuerySet<LimitTestPerson> qs;

    auto result = qs.limit(100).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with large LIMIT failed";

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 20); // Should return all 20 records
}

// Test OFFSET larger than result set
TEST_F(LimitOffsetTest, OffsetLargerThanResultSet) {
    QuerySet<LimitTestPerson> qs;

    auto result = qs.offset(100).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with large OFFSET failed";

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 0); // Should return empty result
}

// Test LIMIT 0
TEST_F(LimitOffsetTest, LimitZero) {
    QuerySet<LimitTestPerson> qs;

    auto result = qs.limit(0).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with LIMIT 0 failed";

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 0); // Should return no records
}

// Test LIMIT 1 (common case)
TEST_F(LimitOffsetTest, LimitOne) {
    QuerySet<LimitTestPerson> qs;

    auto result = qs.limit(1).select();
    ASSERT_TRUE(result.has_value()) << "SELECT with LIMIT 1 failed";

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 1);
    EXPECT_EQ(people[0].id, 1);
    EXPECT_EQ(people[0].name, "Person1");
}

// Test repeated queries with different LIMIT/OFFSET (cache verification)
TEST_F(LimitOffsetTest, RepeatedQueriesWithDifferentLimits) {
    QuerySet<LimitTestPerson> qs;

    // First query: LIMIT 5
    auto result1 = qs.limit(5).select();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value().size(), 5);

    // Second query: LIMIT 10
    auto result2 = qs.limit(10).select();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 10);

    // Third query: LIMIT 5 OFFSET 5
    auto result3 = qs.limit(5).offset(5).select();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3.value().size(), 5);
    EXPECT_EQ(result3.value()[0].id, 6);

    // Fourth query: No LIMIT/OFFSET (should use different cache)
    auto result4 = qs.select();
    ASSERT_TRUE(result4.has_value());
    EXPECT_EQ(result4.value().size(), 20);
}

// Test LIMIT/OFFSET with empty table
TEST_F(LimitOffsetTest, EmptyTable) {
    // Create empty table
    auto& conn = QuerySet<LimitTestPerson>::get_default_connection();
    auto exec_result = conn.execute(
            "CREATE TABLE EmptyLimitTestPerson ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "age INTEGER NOT NULL)"
    );
    ASSERT_TRUE(exec_result.has_value());

    // Note: We can't test with a different table using the same struct
    // So we'll just delete all records from the existing table
    auto delete_result = conn.execute("DELETE FROM LimitTestPerson");
    ASSERT_TRUE(delete_result.has_value());

    QuerySet<LimitTestPerson> qs;

    auto result = qs.limit(10).select();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 0);
}

// Test method chaining doesn't affect non-LIMIT queries
TEST_F(LimitOffsetTest, MethodChainingIsolation) {
    QuerySet<LimitTestPerson> qs;

    // Query with LIMIT
    auto result1 = qs.limit(5).select();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1.value().size(), 5);

    // Next query without LIMIT should not be affected
    auto result2 = qs.select();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2.value().size(), 20);
}

// Test edge case: OFFSET at exactly the last record
TEST_F(LimitOffsetTest, OffsetAtLastRecord) {
    QuerySet<LimitTestPerson> qs;

    auto result = qs.offset(19).select();
    ASSERT_TRUE(result.has_value());

    const auto& people = result.value();
    EXPECT_EQ(people.size(), 1);
    EXPECT_EQ(people[0].id, 20);
    EXPECT_EQ(people[0].name, "Person20");
}

// Test pagination pattern
TEST_F(LimitOffsetTest, PaginationPattern) {
    QuerySet<LimitTestPerson> qs;
    size_t page_size   = 5;
    size_t total_pages = 4;

    for (size_t page = 0; page < total_pages; ++page) {
        auto result = qs.limit(page_size).offset(page * page_size).select();
        ASSERT_TRUE(result.has_value()) << "Pagination page " << page << " failed";

        const auto& people = result.value();
        EXPECT_EQ(people.size(), page_size);

        // Verify correct records for this page
        for (size_t i = 0; i < people.size(); ++i) {
            size_t expected_id = page * page_size + i + 1;
            EXPECT_EQ(people[i].id, static_cast<int>(expected_id));
        }
    }
}

// ===== Multiple LIMIT/OFFSET Calls Tests (Last Value Wins) =====

// Test fixture for multiple limit()/offset() calls
// Uses 100 records to test larger offsets
class MultipleLimitOffsetTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Create in-memory database
        auto result = QuerySet<LimitTestPerson>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to create connection";

        auto& conn = QuerySet<LimitTestPerson>::get_default_connection();

        // Create table
        auto create_result = conn.execute(
                "CREATE TABLE LimitTestPerson ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL)"
        );
        ASSERT_TRUE(create_result.has_value());

        // Insert 100 test records using QuerySet
        for (int i = 1; i <= 100; ++i) {
            auto qs = QuerySet<LimitTestPerson>{};
            std::string name = "Person" + std::to_string(i);
            auto insert_result = qs.insert(LimitTestPerson{0, name, 20 + i});
            ASSERT_TRUE(insert_result.has_value());
        }
    }

    void TearDown() override {
        QuerySet<LimitTestPerson>::clear_default_connection();
    }
};

// Test: Multiple limit() calls - last value wins
TEST_F(MultipleLimitOffsetTest, MultipleLimitCallsUseLastValue) {
    QuerySet<LimitTestPerson> qs;

    // Call limit() three times - should use the last value (5)
    auto result = qs.limit(10).limit(20).limit(5).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 5) << "Multiple limit() calls should use LAST value (5), not first (10) or middle (20)";

    // Verify we got the first 5 records
    for (size_t i = 0; i < result->size(); ++i) {
        EXPECT_EQ((*result)[i].id, static_cast<int>(i + 1));
        EXPECT_EQ((*result)[i].name, "Person" + std::to_string(i + 1));
    }
}

// Test: Multiple offset() calls - last value wins
TEST_F(MultipleLimitOffsetTest, MultipleOffsetCallsUseLastValue) {
    QuerySet<LimitTestPerson> qs;

    // Call offset() three times with limit - should use last offset value (30)
    auto result = qs.limit(5).offset(10).offset(20).offset(30).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 5) << "Should return 5 records (LIMIT 5)";

    // Verify we got records starting from offset 30 (records 31-35)
    for (size_t i = 0; i < result->size(); ++i) {
        EXPECT_EQ((*result)[i].id, static_cast<int>(31 + i));
        EXPECT_EQ((*result)[i].name, "Person" + std::to_string(31 + i));
    }
}

// Test: Mixed multiple limit() and offset() calls - both use last values
TEST_F(MultipleLimitOffsetTest, MultipleLimitAndOffsetCallsUseLast) {
    QuerySet<LimitTestPerson> qs;

    // Multiple calls to both - should use limit=3, offset=40
    auto result = qs
                          .limit(10)
                          .offset(5)
                          .limit(20)
                          .offset(40)
                          .limit(3)
                          .select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 3) << "Should use LIMIT 3 (last limit call)";

    // Verify we got records starting from offset 40 (records 41-43)
    for (size_t i = 0; i < result->size(); ++i) {
        EXPECT_EQ((*result)[i].id, static_cast<int>(41 + i));
        EXPECT_EQ((*result)[i].name, "Person" + std::to_string(41 + i));
    }
}

// Test: State resets after select() - each query is independent
TEST_F(MultipleLimitOffsetTest, StateResetsAfterMultipleSelects) {
    QuerySet<LimitTestPerson> qs;

    // First query with LIMIT 10
    auto result1 = qs.limit(10).select();
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(result1->size(), 10);

    // Second query without LIMIT/OFFSET - should return ALL rows (state reset)
    auto result2 = qs.select();
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(result2->size(), 100) << "State should reset after select() - no LIMIT should be applied";

    // Third query with different LIMIT
    auto result3 = qs.limit(5).select();
    ASSERT_TRUE(result3.has_value());
    EXPECT_EQ(result3->size(), 5);
}

// Test: Verify last-value-wins matches documented behavior
TEST_F(MultipleLimitOffsetTest, DocumentedBehaviorExample) {
    QuerySet<LimitTestPerson> qs;

    // Example from documentation: qs.limit(10).limit(20).select() uses LIMIT 20
    auto result = qs.limit(10).limit(20).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 20) << "Documentation example: should use LIMIT 20 (last value)";
}

// Test: Legitimate use case - override in same chain
TEST_F(MultipleLimitOffsetTest, LimitOverrideInChain) {
    QuerySet<LimitTestPerson> qs;

    // Chain multiple operations where later limit() overrides earlier one
    // This is useful when building queries programmatically
    auto result = qs.limit(100).limit(10).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 10) << "Should allow override of previous limit in chain (legitimate use case)";
}

// Test: Multiple offset() without limit - last value wins
TEST_F(MultipleLimitOffsetTest, MultipleOffsetWithoutLimit) {
    QuerySet<LimitTestPerson> qs;

    // Multiple offset() calls without limit - last value wins
    auto result = qs.offset(10).offset(20).offset(95).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 5) << "OFFSET 95 (last value) should skip first 95 records, return last 5";

    // Verify we got records 96-100
    for (size_t i = 0; i < result->size(); ++i) {
        EXPECT_EQ((*result)[i].id, static_cast<int>(96 + i));
        EXPECT_EQ((*result)[i].name, "Person" + std::to_string(96 + i));
    }
}

// Test: Multiple calls with very large offset
TEST_F(MultipleLimitOffsetTest, MultipleCallsLargeOffset) {
    QuerySet<LimitTestPerson> qs;

    // Multiple offset() calls where last value is very large
    auto result = qs.offset(50).offset(200).select();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 0) << "OFFSET 200 (last value) exceeds dataset, should return empty";
}

// ===== JOIN + LIMIT/OFFSET Tests =====

// Test fixture for JOIN + LIMIT/OFFSET
class JoinLimitOffsetTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<User>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to set default connection";

        auto& conn = QuerySet<User>::get_default_connection();

        // Create User table
        auto user_result = conn.execute(
                "CREATE TABLE User ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL)"
        );
        ASSERT_TRUE(user_result.has_value()) << "Failed to create User table";

        // Create Message table with sender and receiver FKs
        auto msg_result = conn.execute(
                "CREATE TABLE Message ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "sender_id INTEGER NOT NULL, "
                "receiver_id INTEGER NOT NULL, "
                "text TEXT NOT NULL)"
        );
        ASSERT_TRUE(msg_result.has_value()) << "Failed to create Message table";

        // Insert users
        QuerySet<User> user_qs;
        std::vector<User> users;
        for (int i = 1; i <= 10; ++i) {
            users.push_back({0, "User" + std::to_string(i), 20 + i});
        }
        auto insert_result = user_qs.insert(std::span<const User>(users));
        ASSERT_TRUE(insert_result.has_value()) << "Failed to insert users";
        user_ids = insert_result.value();

        // Insert 20 messages (various sender/receiver combinations)
        QuerySet<Message> message_qs;
        std::vector<Message> messages;
        for (int i = 0; i < 20; ++i) {
            int sender_idx = i % 10;
            int receiver_idx = (i + 1) % 10;
            messages.push_back({
                0,
                User{static_cast<int>(user_ids[sender_idx]), "", 0},
                User{static_cast<int>(user_ids[receiver_idx]), "", 0},
                "Message " + std::to_string(i + 1)
            });
        }
        auto msg_insert = message_qs.insert(std::span<const Message>(messages));
        ASSERT_TRUE(msg_insert.has_value()) << "Failed to insert messages";
    }

    void TearDown() override {
        QuerySet<User>::clear_default_connection();
    }

    // FK test structs
    struct User {
        [[=storm::meta::FieldAttr::primary]] int id;
        std::string name;
        int age;
    };

    struct Message {
        [[=storm::meta::FieldAttr::primary]] int id;
        [[=storm::meta::FieldAttr::fk]] User sender;
        [[=storm::meta::FieldAttr::fk]] User receiver;
        std::string text;
    };

    std::vector<int64_t> user_ids;
};

// Test: JOIN + LIMIT
TEST_F(JoinLimitOffsetTest, JoinWithLimit) {
    QuerySet<Message> qs;

    auto result = qs.join<&Message::sender>().limit(5).select();
    ASSERT_TRUE(result.has_value()) << "JOIN + LIMIT failed";

    const auto& messages = result.value();
    EXPECT_EQ(messages.size(), 5) << "Should return exactly 5 messages";

    // Verify sender is fully populated (JOIN worked)
    for (const auto& msg : messages) {
        EXPECT_GT(msg.sender.id, 0) << "Sender ID should be populated";
        EXPECT_FALSE(msg.sender.name.empty()) << "Sender name should be populated by JOIN";
        EXPECT_GT(msg.sender.age, 0) << "Sender age should be populated by JOIN";
    }
}

// Test: JOIN + OFFSET
TEST_F(JoinLimitOffsetTest, JoinWithOffset) {
    QuerySet<Message> qs;

    auto result = qs.join<&Message::sender>().offset(15).select();
    ASSERT_TRUE(result.has_value()) << "JOIN + OFFSET failed";

    const auto& messages = result.value();
    EXPECT_EQ(messages.size(), 5) << "Should return 5 messages (20 total - 15 offset)";

    // Verify JOIN worked
    for (const auto& msg : messages) {
        EXPECT_FALSE(msg.sender.name.empty()) << "JOIN should populate sender";
    }
}

// Test: JOIN + LIMIT + OFFSET (pagination with JOIN)
TEST_F(JoinLimitOffsetTest, JoinWithLimitAndOffset) {
    QuerySet<Message> qs;

    // Page 2: Skip first 5, get next 5
    auto result = qs.join<&Message::sender>().limit(5).offset(5).select();
    ASSERT_TRUE(result.has_value()) << "JOIN + LIMIT + OFFSET failed";

    const auto& messages = result.value();
    EXPECT_EQ(messages.size(), 5) << "Should return exactly 5 messages";

    // Verify sender is fully populated
    for (const auto& msg : messages) {
        EXPECT_GT(msg.sender.id, 0);
        EXPECT_FALSE(msg.sender.name.empty()) << "JOIN should populate sender name";
        EXPECT_GT(msg.sender.age, 0);
    }
}

// Test: Multi-JOIN + LIMIT + OFFSET
TEST_F(JoinLimitOffsetTest, MultiJoinWithLimitAndOffset) {
    QuerySet<Message> qs;

    auto result = qs.join<&Message::sender, &Message::receiver>()
                    .limit(3)
                    .offset(2)
                    .select();
    ASSERT_TRUE(result.has_value()) << "Multi-JOIN + LIMIT + OFFSET failed";

    const auto& messages = result.value();
    EXPECT_EQ(messages.size(), 3) << "Should return exactly 3 messages";

    // Verify both sender AND receiver are fully populated
    for (const auto& msg : messages) {
        EXPECT_FALSE(msg.sender.name.empty()) << "Sender should be populated";
        EXPECT_FALSE(msg.receiver.name.empty()) << "Receiver should be populated";
        EXPECT_GT(msg.sender.age, 0);
        EXPECT_GT(msg.receiver.age, 0);
    }
}

// Test: LEFT JOIN + LIMIT
TEST_F(JoinLimitOffsetTest, LeftJoinWithLimit) {
    QuerySet<Message> qs;

    auto result = qs.left_join<&Message::sender>().limit(10).select();
    ASSERT_TRUE(result.has_value()) << "LEFT JOIN + LIMIT failed";

    const auto& messages = result.value();
    EXPECT_EQ(messages.size(), 10) << "Should return exactly 10 messages";

    // Verify sender is populated
    for (const auto& msg : messages) {
        EXPECT_FALSE(msg.sender.name.empty());
    }
}

// Test: RIGHT JOIN + LIMIT + OFFSET
TEST_F(JoinLimitOffsetTest, RightJoinWithLimitAndOffset) {
    QuerySet<Message> qs;

    auto result = qs.right_join<&Message::sender>().limit(5).offset(10).select();
    ASSERT_TRUE(result.has_value()) << "RIGHT JOIN + LIMIT + OFFSET failed";

    const auto& messages = result.value();
    EXPECT_LE(messages.size(), 5) << "Should return at most 5 messages";

    // Verify messages are valid
    for (const auto& msg : messages) {
        EXPECT_GT(msg.id, 0);
    }
}

// Test: JOIN pagination pattern (real-world use case)
TEST_F(JoinLimitOffsetTest, JoinPaginationPattern) {
    QuerySet<Message> qs;
    size_t page_size = 5;
    size_t total_messages = 20;
    size_t total_pages = total_messages / page_size;

    std::vector<int> seen_ids;

    for (size_t page = 0; page < total_pages; ++page) {
        auto result = qs.join<&Message::sender>()
                        .limit(page_size)
                        .offset(page * page_size)
                        .select();
        ASSERT_TRUE(result.has_value()) << "Pagination failed on page " << page;

        const auto& messages = result.value();
        EXPECT_EQ(messages.size(), page_size) << "Page " << page << " should have " << page_size << " messages";

        // Verify no duplicates across pages
        for (const auto& msg : messages) {
            EXPECT_EQ(std::count(seen_ids.begin(), seen_ids.end(), msg.id), 0)
                << "Message ID " << msg.id << " appeared in multiple pages";
            seen_ids.push_back(msg.id);

            // Verify JOIN worked
            EXPECT_FALSE(msg.sender.name.empty()) << "JOIN should populate sender";
        }
    }

    EXPECT_EQ(seen_ids.size(), total_messages) << "Should have seen all messages across all pages";
}

// Test: Method chaining order independence with JOIN
TEST_F(JoinLimitOffsetTest, JoinMethodChainingOrder) {
    QuerySet<Message> qs;

    // Try different chaining orders - all should work
    auto result1 = qs.join<&Message::sender>().limit(5).offset(2).select();
    auto result2 = qs.limit(5).join<&Message::sender>().offset(2).select();
    auto result3 = qs.offset(2).limit(5).join<&Message::sender>().select();

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());
    ASSERT_TRUE(result3.has_value());

    // All should return same size
    EXPECT_EQ(result1.value().size(), 5);
    EXPECT_EQ(result2.value().size(), 5);
    EXPECT_EQ(result3.value().size(), 5);
}

// Test: LIMIT 0 with JOIN (edge case)
TEST_F(JoinLimitOffsetTest, JoinWithLimitZero) {
    QuerySet<Message> qs;

    auto result = qs.join<&Message::sender>().limit(0).select();
    ASSERT_TRUE(result.has_value()) << "JOIN + LIMIT 0 should succeed";

    const auto& messages = result.value();
    EXPECT_EQ(messages.size(), 0) << "LIMIT 0 should return no messages even with JOIN";
}

// Test: Large OFFSET with JOIN (beyond result set)
TEST_F(JoinLimitOffsetTest, JoinWithLargeOffset) {
    QuerySet<Message> qs;

    auto result = qs.join<&Message::sender>().offset(100).select();
    ASSERT_TRUE(result.has_value()) << "JOIN + large OFFSET should succeed";

    const auto& messages = result.value();
    EXPECT_EQ(messages.size(), 0) << "Large OFFSET beyond result set should return empty";
}
