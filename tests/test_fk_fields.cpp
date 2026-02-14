#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <vector>;
import <expected>;

using namespace storm;

// Test models for FK relationships
struct FKUser {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string                               name;
    int                                       age{};
};

struct FKMessage {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::fk]] FKUser   sender;
    [[= storm::meta::FieldAttr::fk]] FKUser   receiver;
    std::string                               text;
};

// Test fixture for FK field operations — templated on database backend
template <typename ConnType> class FKFieldTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<FKUser, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<FKUser, ConnType>::get_default_connection();

        auto create_user_result = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE FKUser ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_user_result.has_value())
                << "Failed to create FKUser table: " << create_user_result.error().message();

        auto create_message_result = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE FKMessage ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "sender_id INTEGER NOT NULL, "
                "receiver_id INTEGER NOT NULL, "
                "text TEXT NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_message_result.has_value())
                << "Failed to create Message table: " << create_message_result.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"FKUser"});
    }

    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<FKUser, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<FKUser, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<FKUser, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(FKFieldTest, DatabaseTypes);

// Test: INSERT with FK field binds only the PK value
TYPED_TEST(FKFieldTest, InsertWithFKField) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users first
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};
    auto         alice_result = user_qs.insert(alice);
    auto         bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value()) << "Alice INSERT failed: " << alice_result.error().message();
    ASSERT_TRUE(bob_result.has_value()) << "Bob INSERT failed: " << bob_result.error().message();

    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert a message with FK to Alice (sender) and Bob (receiver)
    // Only sender.id and receiver.id are used, name and age are ignored
    FKMessage const msg{
            .id       = 0,
            .sender   = FKUser{.id = static_cast<int>(alice_id), .name = "ignored", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(bob_id), .name = "ignored", .age = 0},
            .text     = "Hello World"
    };

    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value()) << "FKMessage INSERT failed: " << msg_result.error().message();

    int64_t const msg_id = msg_result.value();
    EXPECT_GT(msg_id, 0) << "Expected valid message ID";

    // Verify FKs were stored correctly by querying database directly
    const auto& conn     = QuerySet<FKUser, TypeParam>::get_default_connection();
    auto        stmt_res = conn->prepare("SELECT sender_id, receiver_id FROM FKMessage WHERE id = ?");
    ASSERT_TRUE(stmt_res.has_value());

    auto stmt = std::move(stmt_res.value());
    ASSERT_TRUE(stmt.bind_int(1, msg_id).has_value());

    int const step_result = stmt.step_raw();
    ASSERT_EQ(step_result, decltype(stmt)::ROW_AVAILABLE);

    int64_t const stored_sender_id   = stmt.extract_int64(0);
    int64_t const stored_receiver_id = stmt.extract_int64(1);
    EXPECT_EQ(stored_sender_id, alice_id) << "Sender FK value should match Alice ID";
    EXPECT_EQ(stored_receiver_id, bob_id) << "Receiver FK value should match Bob ID";
}

// Test: SELECT with FK field populates only the PK
TYPED_TEST(FKFieldTest, SelectWithFKFieldPartialPopulation) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};
    FKUser const charlie{.id = 0, .name = "Charlie", .age = 35};
    auto         bob_result     = user_qs.insert(bob);
    auto         charlie_result = user_qs.insert(charlie);
    ASSERT_TRUE(bob_result.has_value());
    ASSERT_TRUE(charlie_result.has_value());
    int64_t const bob_id     = bob_result.value();
    int64_t const charlie_id = charlie_result.value();

    // Insert a message from Bob to Charlie
    FKMessage const msg{
            .id       = 0,
            .sender   = FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(charlie_id), .name = "", .age = 0},
            .text     = "Test message"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // SELECT messages
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value()) << "SELECT failed: " << select_result.error().message();

    const auto& messages = select_result.value();
    ASSERT_EQ(messages.size(), 1) << "Expected exactly one message";

    // Verify message fields
    auto it = messages.begin();
    EXPECT_EQ(it->text, "Test message");

    // Verify FK fields: only PK should be populated
    EXPECT_EQ(it->sender.id, bob_id) << "Sender FK PK should be populated";
    EXPECT_EQ(it->sender.name, "") << "Sender FK non-PK fields should remain default (empty string)";
    EXPECT_EQ(it->sender.age, 0) << "Sender FK non-PK fields should remain default (0)";

    EXPECT_EQ(it->receiver.id, charlie_id) << "Receiver FK PK should be populated";
    EXPECT_EQ(it->receiver.name, "") << "Receiver FK non-PK fields should remain default (empty string)";
    EXPECT_EQ(it->receiver.age, 0) << "Receiver FK non-PK fields should remain default (0)";
}

// Test: Batch INSERT with FK fields
TYPED_TEST(FKFieldTest, BatchInsertWithFKFields) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users (IDs will be auto-generated)
    std::vector<FKUser> users       = {{0, "Alice", 30}, {0, "Bob", 25}, {0, "Charlie", 35}, {0, "Dave", 40}};
    auto                user_result = user_qs.insert(users);
    ASSERT_TRUE(user_result.has_value());

    // SELECT to get auto-generated user IDs
    auto user_select = user_qs.select();
    ASSERT_TRUE(user_select.has_value());
    ASSERT_EQ(user_select.value().size(), 4);

    // Get first user's ID for FK references
    int const first_user_id = user_select.value().begin()->id;

    // Insert messages with FK references
    std::vector<FKMessage> messages =
            {{0,
              FKUser{.id = first_user_id, .name = "", .age = 0},
              FKUser{.id = first_user_id, .name = "", .age = 0},
              "FKMessage from Alice to Bob"},
             {0,
              FKUser{.id = first_user_id, .name = "", .age = 0},
              FKUser{.id = first_user_id, .name = "", .age = 0},
              "FKMessage from Charlie to Dave"}};

    auto msg_result = message_qs.insert(messages);
    ASSERT_TRUE(msg_result.has_value()) << "Batch INSERT failed: " << msg_result.error().message();

    // Verify messages were stored
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& retrieved_messages = select_result.value();
    ASSERT_EQ(retrieved_messages.size(), 2);

    // Verify FK values
    auto it = retrieved_messages.begin();
    EXPECT_EQ(it->sender.id, first_user_id);
    EXPECT_EQ(it->receiver.id, first_user_id);
    ++it;
    EXPECT_EQ(it->sender.id, first_user_id);
    EXPECT_EQ(it->receiver.id, first_user_id);
}

// Test: UPDATE with FK field
TYPED_TEST(FKFieldTest, UpdateWithFKField) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};
    FKUser const charlie{.id = 0, .name = "Charlie", .age = 35};
    FKUser const dave{.id = 0, .name = "Dave", .age = 40};

    auto alice_result   = user_qs.insert(alice);
    auto bob_result     = user_qs.insert(bob);
    auto charlie_result = user_qs.insert(charlie);
    auto dave_result    = user_qs.insert(dave);

    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    ASSERT_TRUE(charlie_result.has_value());
    ASSERT_TRUE(dave_result.has_value());

    int64_t const alice_id   = alice_result.value();
    int64_t const bob_id     = bob_result.value();
    int64_t const charlie_id = charlie_result.value();
    int64_t const dave_id    = dave_result.value();

    // Insert message from Alice to Bob
    FKMessage const msg{
            .id       = 0,
            .sender   = FKUser{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "Original message"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    int64_t const msg_id = msg_result.value();

    // Update message: change sender to Charlie, receiver to Dave, and text
    FKMessage const updated_msg{
            .id       = static_cast<int>(msg_id),
            .sender   = FKUser{.id = static_cast<int>(charlie_id), .name = "", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(dave_id), .name = "", .age = 0},
            .text     = "Updated message"
    };

    auto update_result = message_qs.update(updated_msg);
    ASSERT_TRUE(update_result.has_value()) << "UPDATE failed: " << update_result.error().message();

    // Verify update
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& messages = select_result.value();
    ASSERT_EQ(messages.size(), 1);

    EXPECT_EQ(messages.begin()->id, msg_id);
    EXPECT_EQ(messages.begin()->sender.id, charlie_id) << "Sender FK should be updated to Charlie";
    EXPECT_EQ(messages.begin()->receiver.id, dave_id) << "Receiver FK should be updated to Dave";
    EXPECT_EQ(messages.begin()->text, "Updated message");
}

// Test: DELETE with FK field
TYPED_TEST(FKFieldTest, DeleteWithFKField) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};
    auto         alice_result = user_qs.insert(alice);
    auto         bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert messages from Alice to Bob
    std::vector<FKMessage> messages =
            {{0,
              FKUser{.id = static_cast<int>(alice_id), .name = "", .age = 0},
              FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
              "FKMessage 1"},
             {0,
              FKUser{.id = static_cast<int>(alice_id), .name = "", .age = 0},
              FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
              "FKMessage 2"}};

    auto msg_result = message_qs.insert(messages);
    ASSERT_TRUE(msg_result.has_value());

    // SELECT to get auto-generated message IDs
    auto msg_select = message_qs.select();
    ASSERT_TRUE(msg_select.has_value());
    ASSERT_EQ(msg_select.value().size(), 2);

    // Get the first message's ID to delete
    auto      it           = msg_select.value().begin();
    int const first_msg_id = it->id;
    ++it;
    int const second_msg_id = it->id;

    // Delete first message
    FKMessage const to_delete{
            .id       = first_msg_id,
            .sender   = FKUser{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = ""
    };

    auto delete_result = message_qs.remove(to_delete);
    ASSERT_TRUE(delete_result.has_value()) << "DELETE failed: " << delete_result.error().message();

    // Verify only one message remains
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& remaining_messages = select_result.value();
    ASSERT_EQ(remaining_messages.size(), 1);
    EXPECT_EQ(remaining_messages.begin()->id, second_msg_id);
    EXPECT_EQ(remaining_messages.begin()->text, "FKMessage 2");
}

// Test: Multiple FK fields to same type
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(FKFieldTest, MultipleFKFieldsToSameType) {
    // Create a conversation struct with two FK fields to FKUser
    struct Conversation {
        [[= storm::meta::FieldAttr::primary]] int id{};
        [[= storm::meta::FieldAttr::fk]] FKUser   sender;
        [[= storm::meta::FieldAttr::fk]] FKUser   receiver;
        std::string                               message;
    };

    QuerySet<FKUser, TypeParam>       user_qs;
    QuerySet<Conversation, TypeParam> conv_qs;

    // Create conversation table
    const auto& conn               = QuerySet<FKUser, TypeParam>::get_default_connection();
    auto        create_conv_result = storm::test::ensure_table<TypeParam>(
            conn,
            "CREATE TABLE Conversation ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "sender_id INTEGER NOT NULL, "
                   "receiver_id INTEGER NOT NULL, "
                   "message TEXT NOT NULL"
                   ")"
    );
    ASSERT_TRUE(create_conv_result.has_value());

    // Insert users
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};

    auto alice_result = user_qs.insert(alice);
    auto bob_result   = user_qs.insert(bob);

    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());

    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert conversation from Alice to Bob
    Conversation const conv{
            .id       = 0,
            .sender   = FKUser{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .message  = "Hello Bob!"
    };

    auto conv_result = conv_qs.insert(conv);
    ASSERT_TRUE(conv_result.has_value()) << "Conversation INSERT failed: " << conv_result.error().message();

    // SELECT and verify both FK fields are populated
    auto select_result = conv_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& conversations = select_result.value();
    ASSERT_EQ(conversations.size(), 1);

    EXPECT_EQ(conversations.begin()->sender.id, alice_id) << "Sender FK should be Alice";
    EXPECT_EQ(conversations.begin()->receiver.id, bob_id) << "Receiver FK should be Bob";
    EXPECT_EQ(conversations.begin()->message, "Hello Bob!");
}

// Test: Phase 2 - JOIN populates FK object fully
TYPED_TEST(FKFieldTest, JoinFullyPopulatesFKObject) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};
    auto         alice_result = user_qs.insert(alice);
    auto         bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert message from Alice to Bob
    FKMessage const msg{
            .id       = 0,
            .sender   = FKUser{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "Hello from JOIN!"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // Phase 2: JOIN to get fully populated sender
    auto join_result = message_qs.template join<&FKMessage::sender>().select();
    ASSERT_TRUE(join_result.has_value()) << "JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();
    ASSERT_EQ(messages.size(), 1);

    // Verify message fields
    auto it = messages.begin();
    EXPECT_EQ(it->text, "Hello from JOIN!");

    // Verify sender FK object is FULLY populated (not just PK!)
    EXPECT_EQ(it->sender.id, alice_id);
    EXPECT_EQ(it->sender.name, "Alice") << "JOIN should populate sender FK object's name!";
    EXPECT_EQ(it->sender.age, 30) << "JOIN should populate sender FK object's age!";

    // Verify receiver FK object is NOT populated (current JOIN limitation)
    // TODO: Future enhancement - populate non-JOINed FK fields with their IDs
    EXPECT_EQ(it->receiver.id, 0) << "Non-JOINed FK fields are not populated (current limitation)";
    EXPECT_EQ(it->receiver.name, "") << "Without JOIN, receiver name should be empty";
    EXPECT_EQ(it->receiver.age, 0) << "Without JOIN, receiver age should be 0";
}

// Test: Phase 3 - Multi-JOIN populates multiple FK objects fully
TYPED_TEST(FKFieldTest, JoinMultipleFKFields) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};
    auto         alice_result = user_qs.insert(alice);
    auto         bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert message from Alice to Bob
    FKMessage const msg{
            .id       = 0,
            .sender   = FKUser{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "Hello from multi-JOIN!"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // Phase 3: Multi-JOIN to get BOTH sender and receiver fully populated
    auto join_result = message_qs.template join<&FKMessage::sender, &FKMessage::receiver>().select();
    ASSERT_TRUE(join_result.has_value()) << "Multi-JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();
    ASSERT_EQ(messages.size(), 1);

    // Verify message fields
    auto it = messages.begin();
    EXPECT_EQ(it->text, "Hello from multi-JOIN!");

    // Verify sender FK object is FULLY populated
    EXPECT_EQ(it->sender.id, alice_id);
    EXPECT_EQ(it->sender.name, "Alice") << "Multi-JOIN should populate sender FK object's name!";
    EXPECT_EQ(it->sender.age, 30) << "Multi-JOIN should populate sender FK object's age!";

    // Verify receiver FK object is ALSO FULLY populated (Phase 3 improvement!)
    EXPECT_EQ(it->receiver.id, bob_id);
    EXPECT_EQ(it->receiver.name, "Bob") << "Multi-JOIN should populate receiver FK object's name!";
    EXPECT_EQ(it->receiver.age, 25) << "Multi-JOIN should populate receiver FK object's age!";
}

// Test: LEFT JOIN returns all messages even when FK doesn't match
TYPED_TEST(FKFieldTest, LeftJoinReturnsAllMessages) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert only one user (Alice)
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    auto         alice_result = user_qs.insert(alice);
    ASSERT_TRUE(alice_result.has_value());
    int64_t const alice_id = alice_result.value();

    // Insert a message with a non-existent receiver ID (999)
    // This simulates an orphaned FK reference
    const auto& conn        = QuerySet<FKUser, TypeParam>::get_default_connection();
    auto        stmt_result = conn->prepare("INSERT INTO FKMessage (sender_id, receiver_id, text) VALUES (?, ?, ?)");
    ASSERT_TRUE(stmt_result.has_value()) << "Prepare failed: " << stmt_result.error().message();

    auto stmt = std::move(stmt_result.value());
    ASSERT_TRUE(stmt.bind_int(1, alice_id).has_value());
    ASSERT_TRUE(stmt.bind_int(2, 999).has_value());
    ASSERT_TRUE(stmt.bind_text(3, "Orphaned message").has_value());

    int const step_result = stmt.step_raw();
    ASSERT_EQ(step_result, decltype(stmt)::NO_MORE_ROWS) << "Direct INSERT failed";

    // LEFT JOIN on sender - should return message even though receiver doesn't exist
    auto join_result = message_qs.template left_join<&FKMessage::sender>().select();
    ASSERT_TRUE(join_result.has_value()) << "LEFT JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();
    ASSERT_EQ(messages.size(), 1) << "LEFT JOIN should return all messages";

    // Verify sender is fully populated (exists)
    auto it = messages.begin();
    EXPECT_EQ(it->sender.id, alice_id);
    EXPECT_EQ(it->sender.name, "Alice") << "LEFT JOIN should populate existing FK";
    EXPECT_EQ(it->sender.age, 30);

    // Verify receiver is not populated (doesn't exist in FKUser table)
    EXPECT_EQ(it->receiver.id, 0) << "Non-JOINed FK should remain default";

    // Verify message text
    EXPECT_EQ(it->text, "Orphaned message");
}

// Test: LEFT JOIN with multiple FK fields
TYPED_TEST(FKFieldTest, LeftJoinMultipleFKFields) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};
    auto         alice_result = user_qs.insert(alice);
    auto         bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert message from Alice to Bob
    FKMessage const msg{
            .id       = 0,
            .sender   = FKUser{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "Hello with LEFT JOIN"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // LEFT JOIN on both sender and receiver
    auto join_result = message_qs.template left_join<&FKMessage::sender, &FKMessage::receiver>().select();
    ASSERT_TRUE(join_result.has_value()) << "Multi LEFT JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();
    ASSERT_EQ(messages.size(), 1);

    // Verify both FK objects are fully populated
    auto it = messages.begin();
    EXPECT_EQ(it->sender.id, alice_id);
    EXPECT_EQ(it->sender.name, "Alice");
    EXPECT_EQ(it->sender.age, 30);

    EXPECT_EQ(it->receiver.id, bob_id);
    EXPECT_EQ(it->receiver.name, "Bob");
    EXPECT_EQ(it->receiver.age, 25);

    EXPECT_EQ(it->text, "Hello with LEFT JOIN");
}

// Test: RIGHT JOIN behavior
// Note: RIGHT JOIN is less commonly used but should work symmetrically to LEFT JOIN
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(FKFieldTest, RightJoinBehavior) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users first
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};
    FKUser const charlie{.id = 0, .name = "Charlie", .age = 35}; // Charlie has no messages
    auto         alice_result   = user_qs.insert(alice);
    auto         bob_result     = user_qs.insert(bob);
    auto         charlie_result = user_qs.insert(charlie);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    ASSERT_TRUE(charlie_result.has_value());
    int64_t const alice_id   = alice_result.value();
    int64_t const bob_id     = bob_result.value();
    int64_t const charlie_id = charlie_result.value();

    // Insert messages from Alice to Bob (Charlie is not referenced)
    FKMessage const msg{
            .id       = 0,
            .sender   = FKUser{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "FKMessage to Bob"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // RIGHT JOIN on sender - should return all users in FKUser table as senders
    // This includes Charlie even though no message references him
    auto join_result = message_qs.template right_join<&FKMessage::sender>().select();
    ASSERT_TRUE(join_result.has_value()) << "RIGHT JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();

    // RIGHT JOIN returns:
    // - All rows from right table (FKUser)
    // - Matching rows from left table (FKMessage)
    // So we should get at least the message we inserted, possibly more depending on implementation
    EXPECT_GE(messages.size(), 1) << "RIGHT JOIN should return at least existing messages";

    // Find the message we inserted
    bool found = false; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& m : messages) {
        if (m.text == "FKMessage to Bob") {
            found = true;
            EXPECT_EQ(m.sender.id, alice_id);
            EXPECT_EQ(m.sender.name, "Alice");
            EXPECT_EQ(m.sender.age, 30);
        }
    }
    EXPECT_TRUE(found) << "RIGHT JOIN should include the message we inserted";
}

// Test: RIGHT JOIN with multiple FK fields
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(FKFieldTest, RightJoinMultipleFKFields) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};
    auto         alice_result = user_qs.insert(alice);
    auto         bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert message from Alice to Bob
    FKMessage const msg{
            .id       = 0,
            .sender   = FKUser{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = FKUser{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "Hello with RIGHT JOIN"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // RIGHT JOIN on both sender and receiver
    auto join_result = message_qs.template right_join<&FKMessage::sender, &FKMessage::receiver>().select();
    ASSERT_TRUE(join_result.has_value()) << "Multi RIGHT JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();
    EXPECT_GE(messages.size(), 1) << "RIGHT JOIN should return at least the inserted message";

    // Find and verify our message
    bool found = false; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& m : messages) {
        if (m.text == "Hello with RIGHT JOIN") {
            found = true;
            EXPECT_EQ(m.sender.id, alice_id);
            EXPECT_EQ(m.sender.name, "Alice");
            EXPECT_EQ(m.sender.age, 30);

            EXPECT_EQ(m.receiver.id, bob_id);
            EXPECT_EQ(m.receiver.name, "Bob");
            EXPECT_EQ(m.receiver.age, 25);
        }
    }
    EXPECT_TRUE(found) << "Should find the message we inserted";
}

// Test fixture for nullable FK fields — templated on database backend
template <typename ConnType> class NullableFKTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<FKUser, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<FKUser, ConnType>::get_default_connection();

        auto create_user_result = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE FKUser ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_user_result.has_value())
                << "Failed to create FKUser table: " << create_user_result.error().message();

        auto create_message_result = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE FKMessage ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "sender_id INTEGER, " // NULLABLE FK
                "receiver_id INTEGER NOT NULL, "
                "text TEXT NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_message_result.has_value())
                << "Failed to create FKMessage table: " << create_message_result.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"FKUser", "FKMessage"});
    }

    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<FKUser, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<FKUser, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<FKUser, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(NullableFKTest, DatabaseTypes);

// Test: SELECT with NULL FK values
TYPED_TEST(NullableFKTest, SelectWithNullFKField) {
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert message with NULL sender_id
    const auto& conn        = QuerySet<FKUser, TypeParam>::get_default_connection();
    auto        stmt_result = conn->prepare("INSERT INTO FKMessage (sender_id, receiver_id, text) VALUES (NULL, ?, ?)");
    ASSERT_TRUE(stmt_result.has_value());

    auto stmt = std::move(stmt_result.value());
    ASSERT_TRUE(stmt.bind_int(1, 1).has_value());
    ASSERT_TRUE(stmt.bind_text(2, "FKMessage with NULL sender").has_value());

    int const step_result = stmt.step_raw();
    ASSERT_EQ(step_result, decltype(stmt)::NO_MORE_ROWS);

    // SELECT should handle NULL FK gracefully
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value()) << "SELECT with NULL FK failed: " << select_result.error().message();

    const auto& messages = select_result.value();
    ASSERT_EQ(messages.size(), 1);

    // Verify FK field with NULL value is default-initialized
    auto it = messages.begin();
    EXPECT_EQ(it->sender.id, 0) << "NULL FK should result in default-initialized PK (0)";
    EXPECT_EQ(it->sender.name, "");
    EXPECT_EQ(it->sender.age, 0);
    EXPECT_EQ(it->text, "FKMessage with NULL sender");
}

// Test: LEFT JOIN with NULL FK values
TYPED_TEST(NullableFKTest, LeftJoinWithNullFKField) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert a user
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    auto         alice_result = user_qs.insert(alice);
    ASSERT_TRUE(alice_result.has_value());
    int64_t const alice_id = alice_result.value();

    // Insert message with NULL sender_id
    const auto& conn        = QuerySet<FKUser, TypeParam>::get_default_connection();
    auto        stmt_result = conn->prepare("INSERT INTO FKMessage (sender_id, receiver_id, text) VALUES (NULL, ?, ?)");
    ASSERT_TRUE(stmt_result.has_value());

    auto stmt = std::move(stmt_result.value());
    ASSERT_TRUE(stmt.bind_int(1, alice_id).has_value());
    ASSERT_TRUE(stmt.bind_text(2, "NULL sender message").has_value());

    int const step_result = stmt.step_raw();
    ASSERT_EQ(step_result, decltype(stmt)::NO_MORE_ROWS);

    // LEFT JOIN on sender - should return message even with NULL sender_id
    auto join_result = message_qs.template left_join<&FKMessage::sender>().select();
    ASSERT_TRUE(join_result.has_value()) << "LEFT JOIN with NULL FK failed: " << join_result.error().message();

    const auto& messages = join_result.value();
    ASSERT_EQ(messages.size(), 1) << "LEFT JOIN should return message with NULL FK";

    // Verify sender FK is default-initialized (NULL in DB = no JOIN match)
    auto it = messages.begin();
    EXPECT_EQ(it->sender.id, 0) << "NULL FK should not JOIN, remain default";
    EXPECT_EQ(it->sender.name, "") << "NULL FK should not populate name";
    EXPECT_EQ(it->sender.age, 0) << "NULL FK should not populate age";
    EXPECT_EQ(it->text, "NULL sender message");
}

// Test: LEFT JOIN with mix of NULL and valid FKs
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(NullableFKTest, LeftJoinWithMixedNullAndValidFKs) {
    QuerySet<FKUser, TypeParam>    user_qs;
    QuerySet<FKMessage, TypeParam> message_qs;

    // Insert users
    FKUser const alice{.id = 0, .name = "Alice", .age = 30};
    FKUser const bob{.id = 0, .name = "Bob", .age = 25};
    auto         alice_result = user_qs.insert(alice);
    auto         bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    const auto& conn = QuerySet<FKUser, TypeParam>::get_default_connection();

    // FKMessage 1: Valid sender (Alice)
    auto stmt1 = conn->prepare("INSERT INTO FKMessage (sender_id, receiver_id, text) VALUES (?, ?, ?)");
    ASSERT_TRUE(stmt1.has_value());
    auto stm1 = std::move(stmt1.value());
    ASSERT_TRUE(stm1.bind_int(1, alice_id).has_value());
    ASSERT_TRUE(stm1.bind_int(2, bob_id).has_value());
    ASSERT_TRUE(stm1.bind_text(3, "From Alice").has_value());
    ASSERT_EQ(stm1.step_raw(), decltype(stm1)::NO_MORE_ROWS);

    // FKMessage 2: NULL sender
    auto stmt2 = conn->prepare("INSERT INTO FKMessage (sender_id, receiver_id, text) VALUES (NULL, ?, ?)");
    ASSERT_TRUE(stmt2.has_value());
    auto stm2 = std::move(stmt2.value());
    ASSERT_TRUE(stm2.bind_int(1, bob_id).has_value());
    ASSERT_TRUE(stm2.bind_text(2, "Anonymous").has_value());
    ASSERT_EQ(stm2.step_raw(), decltype(stm2)::NO_MORE_ROWS);

    // LEFT JOIN should return both messages
    auto join_result = message_qs.template left_join<&FKMessage::sender>().select();
    ASSERT_TRUE(join_result.has_value());

    const auto& messages = join_result.value();
    ASSERT_EQ(messages.size(), 2) << "LEFT JOIN should return all messages";

    // Find and verify each message
    bool found_alice = false; // NOLINT(misc-const-correctness) - modified in loop
    bool found_null  = false; // NOLINT(misc-const-correctness) - modified in loop
    for (const auto& m : messages) {
        if (m.text == "From Alice") {
            found_alice = true;
            EXPECT_EQ(m.sender.id, alice_id);
            EXPECT_EQ(m.sender.name, "Alice");
            EXPECT_EQ(m.sender.age, 30);
        } else if (m.text == "Anonymous") {
            found_null = true;
            EXPECT_EQ(m.sender.id, 0) << "NULL sender should remain default";
            EXPECT_EQ(m.sender.name, "");
            EXPECT_EQ(m.sender.age, 0);
        }
    }
    EXPECT_TRUE(found_alice) << "Should find Alice's message";
    EXPECT_TRUE(found_null) << "Should find anonymous message";
}

// Test fixture for extended type support in JOINs — templated on database backend
template <typename ConnType> class ExtendedTypesJoinTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<FKUser, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value());

        const auto& conn = QuerySet<FKUser, ConnType>::get_default_connection();

        auto create_employee_result = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE Employee ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "salary REAL NOT NULL, "
                "is_active INTEGER NOT NULL, "
                "nickname TEXT" // NULL allowed for optional
                ")"
        );
        ASSERT_TRUE(create_employee_result.has_value())
                << "Failed to create Employee table: " << create_employee_result.error().message();

        auto create_project_result = storm::test::ensure_table<ConnType>(
                conn,
                "CREATE TABLE Project ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "manager_id INTEGER NOT NULL, "
                "title TEXT NOT NULL, "
                "budget REAL NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_project_result.has_value())
                << "Failed to create Project table: " << create_project_result.error().message();

        storm::test::begin_test_txn<ConnType>(conn, {"Employee", "Project"});
    }

    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<FKUser, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<FKUser, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<FKUser, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(ExtendedTypesJoinTest, DatabaseTypes);

// Test: JOIN with extended types (double, bool, optional)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(ExtendedTypesJoinTest, JoinWithExtendedTypes) {
    // Define structs with extended types
    struct Employee {
        [[= storm::meta::FieldAttr::primary]] int id{};
        std::string                               name;
        double                                    salary{};
        bool                                      is_active{};
        std::optional<std::string>                nickname;
    };

    struct Project {
        [[= storm::meta::FieldAttr::primary]] int id{};
        [[= storm::meta::FieldAttr::fk]] Employee manager;
        std::string                               title;
        double                                    budget{};
    };

    QuerySet<Employee, TypeParam> employee_qs;
    QuerySet<Project, TypeParam>  project_qs;

    // Insert employees with extended types
    Employee const alice{.id = 0, .name = "Alice Smith", .salary = 95000.50, .is_active = true, .nickname = "Ally"};
    Employee const bob{
            .id = 0, .name = "Bob Johnson", .salary = 87500.75, .is_active = false, .nickname = std::nullopt
    };

    auto alice_result = employee_qs.insert(alice);
    auto bob_result   = employee_qs.insert(bob);

    ASSERT_TRUE(alice_result.has_value()) << "Failed to insert Alice: " << alice_result.error().message();
    ASSERT_TRUE(bob_result.has_value()) << "Failed to insert Bob: " << bob_result.error().message();

    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert projects managed by Alice and Bob
    Project const proj1{
            .id = 0,
            .manager =
                    Employee{
                            .id        = static_cast<int>(alice_id),
                            .name      = "",
                            .salary    = 0.0,
                            .is_active = false,
                            .nickname  = std::nullopt
                    },
            .title  = "Web Redesign",
            .budget = 50000.0
    };
    Project const proj2{
            .id = 0,
            .manager =
                    Employee{
                            .id        = static_cast<int>(bob_id),
                            .name      = "",
                            .salary    = 0.0,
                            .is_active = false,
                            .nickname  = std::nullopt
                    },
            .title  = "Mobile App",
            .budget = 75000.0
    };

    auto proj1_result = project_qs.insert(proj1);
    auto proj2_result = project_qs.insert(proj2);

    ASSERT_TRUE(proj1_result.has_value()) << "Failed to insert project 1: " << proj1_result.error().message();
    ASSERT_TRUE(proj2_result.has_value()) << "Failed to insert project 2: " << proj2_result.error().message();

    // JOIN to get projects with fully populated manager (Employee) objects
    auto join_result = project_qs.template join<&Project::manager>().select();
    ASSERT_TRUE(join_result.has_value()) << "JOIN failed: " << join_result.error().message();

    const auto& projects = join_result.value();
    ASSERT_EQ(projects.size(), 2) << "Should retrieve both projects";

    // Find Alice's project and verify all extended types
    bool found_alice_project = false; // NOLINT(misc-const-correctness) - modified in loop
    bool found_bob_project   = false; // NOLINT(misc-const-correctness) - modified in loop

    for (const auto& proj : projects) {
        if (proj.title == "Web Redesign") {
            found_alice_project = true;

            // Verify manager FK is fully populated with all extended types
            EXPECT_EQ(proj.manager.id, alice_id) << "Manager ID should be Alice";
            EXPECT_EQ(proj.manager.name, "Alice Smith") << "Manager name should be populated";

            // Test double type
            EXPECT_DOUBLE_EQ(proj.manager.salary, 95000.50) << "Double field (salary) should be populated correctly";

            // Test bool type
            EXPECT_TRUE(proj.manager.is_active) << "Bool field (is_active) should be true for Alice";

            // Test optional<string> with value
            ASSERT_TRUE(proj.manager.nickname.has_value()) << "Optional nickname should have value for Alice";
            EXPECT_EQ(proj.manager.nickname.value(), "Ally") << "Optional nickname value should be 'Ally'";

            // Verify project fields
            EXPECT_DOUBLE_EQ(proj.budget, 50000.0) << "Project budget should be correct";
        } else if (proj.title == "Mobile App") {
            found_bob_project = true;

            // Verify Bob's extended types
            EXPECT_EQ(proj.manager.id, bob_id) << "Manager ID should be Bob";
            EXPECT_EQ(proj.manager.name, "Bob Johnson") << "Manager name should be populated";

            // Test double type
            EXPECT_DOUBLE_EQ(proj.manager.salary, 87500.75) << "Double field should be populated correctly for Bob";

            // Test bool type (false)
            EXPECT_FALSE(proj.manager.is_active) << "Bool field should be false for Bob";

            // Test optional<string> without value (NULL in DB)
            EXPECT_FALSE(proj.manager.nickname.has_value())
                    << "Optional nickname should be empty (nullopt) for Bob when NULL in DB";

            // Verify project fields
            EXPECT_DOUBLE_EQ(proj.budget, 75000.0) << "Project budget should be correct";
        }
    }

    EXPECT_TRUE(found_alice_project) << "Should find Alice's project with extended types";
    EXPECT_TRUE(found_bob_project) << "Should find Bob's project with extended types";
}

// Test: Multi-JOIN with extended types
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(ExtendedTypesJoinTest, MultiJoinWithExtendedTypes) {
    struct Employee {
        [[= storm::meta::FieldAttr::primary]] int id{};
        std::string                               name;
        double                                    salary{};
        bool                                      is_active{};
        std::optional<std::string>                nickname;
    };

    struct Task {
        [[= storm::meta::FieldAttr::primary]] int id{};
        [[= storm::meta::FieldAttr::fk]] Employee assignee;
        [[= storm::meta::FieldAttr::fk]] Employee reviewer;
        std::string                               description;
    };

    QuerySet<Employee, TypeParam> employee_qs;
    QuerySet<Task, TypeParam>     task_qs;

    // Create Task table
    const auto& conn               = QuerySet<FKUser, TypeParam>::get_default_connection();
    auto        create_task_result = storm::test::ensure_table<TypeParam>(
            conn,
            "CREATE TABLE Task ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "assignee_id INTEGER NOT NULL, "
                   "reviewer_id INTEGER NOT NULL, "
                   "description TEXT NOT NULL"
                   ")"
    );
    ASSERT_TRUE(create_task_result.has_value());

    // Insert employees
    Employee const alice{.id = 0, .name = "Alice", .salary = 95000.0, .is_active = true, .nickname = "Ally"};
    Employee const bob{.id = 0, .name = "Bob", .salary = 87500.0, .is_active = false, .nickname = std::nullopt};

    auto alice_result = employee_qs.insert(alice);
    auto bob_result   = employee_qs.insert(bob);

    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());

    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert task: Alice assigned, Bob reviewing
    Task const task{
            .id = 0,
            .assignee =
                    Employee{
                            .id        = static_cast<int>(alice_id),
                            .name      = "",
                            .salary    = 0.0,
                            .is_active = false,
                            .nickname  = std::nullopt
                    },
            .reviewer =
                    Employee{
                            .id        = static_cast<int>(bob_id),
                            .name      = "",
                            .salary    = 0.0,
                            .is_active = false,
                            .nickname  = std::nullopt
                    },
            .description = "Implement feature X"
    };

    auto task_result = task_qs.insert(task);
    ASSERT_TRUE(task_result.has_value());

    // Multi-JOIN to populate both assignee and reviewer
    // NOLINTNEXTLINE(readability-isolate-declaration) - false positive with template
    auto join_result = task_qs.template join<&Task::assignee, &Task::reviewer>().select();
    ASSERT_TRUE(join_result.has_value()) << "Multi-JOIN failed: " << join_result.error().message();

    const auto& tasks = join_result.value();
    ASSERT_EQ(tasks.size(), 1);

    // Verify assignee (Alice) - all extended types
    EXPECT_EQ(tasks.begin()->assignee.id, alice_id);
    EXPECT_EQ(tasks.begin()->assignee.name, "Alice");
    EXPECT_DOUBLE_EQ(tasks.begin()->assignee.salary, 95000.0);
    EXPECT_TRUE(tasks.begin()->assignee.is_active);
    ASSERT_TRUE(tasks.begin()->assignee.nickname.has_value());
    EXPECT_EQ(tasks.begin()->assignee.nickname.value(), "Ally");

    // Verify reviewer (Bob) - all extended types
    EXPECT_EQ(tasks.begin()->reviewer.id, bob_id);
    EXPECT_EQ(tasks.begin()->reviewer.name, "Bob");
    EXPECT_DOUBLE_EQ(tasks.begin()->reviewer.salary, 87500.0);
    EXPECT_FALSE(tasks.begin()->reviewer.is_active);
    EXPECT_FALSE(tasks.begin()->reviewer.nickname.has_value()) << "Bob's nickname should be nullopt";

    EXPECT_EQ(tasks.begin()->description, "Implement feature X");
}

// Test: JOIN with float and long long types (coverage for extract_typed_field)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(ExtendedTypesJoinTest, JoinWithFloatAndLongLongTypes) {
    // Define structs with float and long long types
    struct Measurement {
        [[= storm::meta::FieldAttr::primary]] int id{};
        std::string                               sensor_name;
        float                                     temperature{};
        long long                                 timestamp{};
    };

    struct Reading {
        [[= storm::meta::FieldAttr::primary]] int    id{};
        [[= storm::meta::FieldAttr::fk]] Measurement measurement;
        std::string                                  reading_type;
        float                                        value{};
    };

    QuerySet<Measurement, TypeParam> measurement_qs;
    QuerySet<Reading, TypeParam>     reading_qs;

    // Create tables
    const auto& conn = QuerySet<FKUser, TypeParam>::get_default_connection();

    auto create_measurement_result = storm::test::ensure_table<TypeParam>(
            conn,
            "CREATE TABLE Measurement ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "sensor_name TEXT NOT NULL, "
            "temperature REAL NOT NULL, "
            "timestamp INTEGER NOT NULL"
            ")"
    );
    ASSERT_TRUE(create_measurement_result.has_value())
            << "Failed to create Measurement table: " << create_measurement_result.error().message();

    auto create_reading_result = storm::test::ensure_table<TypeParam>(
            conn,
            "CREATE TABLE Reading ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "measurement_id INTEGER NOT NULL, "
            "reading_type TEXT NOT NULL, "
            "value REAL NOT NULL"
            ")"
    );
    ASSERT_TRUE(create_reading_result.has_value())
            << "Failed to create Reading table: " << create_reading_result.error().message();

    // Insert measurement with float and long long
    Measurement const meas{
            .id          = 0,
            .sensor_name = "TempSensor1",
            .temperature = 23.5F,
            .timestamp   = 1700000000000LL // Large timestamp
    };

    auto meas_result = measurement_qs.insert(meas);
    ASSERT_TRUE(meas_result.has_value()) << "Failed to insert measurement: " << meas_result.error().message();

    int64_t const meas_id = meas_result.value();

    // Insert reading referencing the measurement
    Reading const reading{
            .id = 0,
            .measurement =
                    Measurement{
                            .id = static_cast<int>(meas_id), .sensor_name = "", .temperature = 0.0F, .timestamp = 0LL
                    },
            .reading_type = "Humidity",
            .value        = 65.3F
    };

    auto reading_result = reading_qs.insert(reading);
    ASSERT_TRUE(reading_result.has_value()) << "Failed to insert reading: " << reading_result.error().message();

    // JOIN to get readings with fully populated measurement
    auto join_result = reading_qs.template join<&Reading::measurement>().select();
    ASSERT_TRUE(join_result.has_value()) << "JOIN failed: " << join_result.error().message();

    const auto& readings = join_result.value();
    ASSERT_EQ(readings.size(), 1) << "Should retrieve one reading";

    // Verify float type extraction
    EXPECT_FLOAT_EQ(readings.begin()->measurement.temperature, 23.5F) << "Float field (temperature) should be correct";
    EXPECT_FLOAT_EQ(readings.begin()->value, 65.3F) << "Float field (value) should be correct";

    // Verify long long type extraction
    EXPECT_EQ(readings.begin()->measurement.timestamp, 1700000000000LL)
            << "Long long field (timestamp) should be correct";

    // Verify other fields
    EXPECT_EQ(readings.begin()->measurement.id, meas_id);
    EXPECT_EQ(readings.begin()->measurement.sensor_name, "TempSensor1");
    EXPECT_EQ(readings.begin()->reading_type, "Humidity");
}

// Test: JOIN with long type (tests separate code path from int64_t)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TYPED_TEST(ExtendedTypesJoinTest, JoinWithLongType) {
    // Define structs with long type
    struct Counter {
        [[= storm::meta::FieldAttr::primary]] int id{};
        std::string                               name;
        long                                      count{};
    };

    struct Summary {
        [[= storm::meta::FieldAttr::primary]] int id{};
        [[= storm::meta::FieldAttr::fk]] Counter  counter;
        std::string                               report_type;
    };

    QuerySet<Counter, TypeParam> counter_qs;
    QuerySet<Summary, TypeParam> summary_qs;

    // Create tables
    const auto& conn = QuerySet<FKUser, TypeParam>::get_default_connection();

    auto create_counter_result = storm::test::ensure_table<TypeParam>(
            conn,
            "CREATE TABLE Counter ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "count INTEGER NOT NULL"
            ")"
    );
    ASSERT_TRUE(create_counter_result.has_value())
            << "Failed to create Counter table: " << create_counter_result.error().message();

    auto create_summary_result = storm::test::ensure_table<TypeParam>(
            conn,
            "CREATE TABLE Summary ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "counter_id INTEGER NOT NULL, "
            "report_type TEXT NOT NULL"
            ")"
    );
    ASSERT_TRUE(create_summary_result.has_value())
            << "Failed to create Summary table: " << create_summary_result.error().message();

    // Insert counter with long value
    Counter const cnt{.id = 0, .name = "PageViews", .count = 9876543210L};

    auto cnt_result = counter_qs.insert(cnt);
    ASSERT_TRUE(cnt_result.has_value()) << "Failed to insert counter: " << cnt_result.error().message();

    int64_t const cnt_id = cnt_result.value();

    // Insert summary referencing the counter
    Summary const sum{
            .id = 0, .counter = Counter{.id = static_cast<int>(cnt_id), .name = "", .count = 0L}, .report_type = "Daily"
    };

    auto sum_result = summary_qs.insert(sum);
    ASSERT_TRUE(sum_result.has_value()) << "Failed to insert summary: " << sum_result.error().message();

    // JOIN to get summaries with fully populated counter
    auto join_result = summary_qs.template join<&Summary::counter>().select();
    ASSERT_TRUE(join_result.has_value()) << "JOIN failed: " << join_result.error().message();

    const auto& summaries = join_result.value();
    ASSERT_EQ(summaries.size(), 1) << "Should retrieve one summary";

    // Verify long type extraction
    EXPECT_EQ(summaries.begin()->counter.count, 9876543210L) << "Long field (count) should be correct";

    // Verify other fields
    EXPECT_EQ(summaries.begin()->counter.id, cnt_id);
    EXPECT_EQ(summaries.begin()->counter.name, "PageViews");
    EXPECT_EQ(summaries.begin()->report_type, "Daily");
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
