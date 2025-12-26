#include <gtest/gtest.h>

import storm;
import <string>;
import <vector>;
import <expected>;

using namespace storm;

// Test models for FK relationships
struct User {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

struct FKMessage {
    [[= storm::meta::FieldAttr::primary]] int id;
    [[= storm::meta::FieldAttr::fk]] User     sender;
    [[= storm::meta::FieldAttr::fk]] User     receiver;
    std::string                               text;
};

// Test fixture for FK field operations
class FKFieldTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Set up in-memory SQLite database
        auto result = QuerySet<User>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        auto& conn = QuerySet<User>::get_default_connection();

        // Create User table
        auto create_user_result = conn->execute(
                "CREATE TABLE User ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_user_result.has_value())
                << "Failed to create User table: " << create_user_result.error().message();

        // Create FKMessage table with sender_id and receiver_id foreign keys
        auto create_message_result = conn->execute(
                "CREATE TABLE FKMessage ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "sender_id INTEGER NOT NULL, "
                "receiver_id INTEGER NOT NULL, "
                "text TEXT NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_message_result.has_value())
                << "Failed to create Message table: " << create_message_result.error().message();
    }

    void TearDown() override {
        QuerySet<User>::clear_default_connection();
    }
};

// Test: INSERT with FK field binds only the PK value
TEST_F(FKFieldTest, InsertWithFKField) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users first
    User const alice{.id = 0, .name = "Alice", .age = 30};
    User const bob{.id = 0, .name = "Bob", .age = 25};
    auto       alice_result = user_qs.insert(alice);
    auto       bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value()) << "Alice INSERT failed: " << alice_result.error().message();
    ASSERT_TRUE(bob_result.has_value()) << "Bob INSERT failed: " << bob_result.error().message();

    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert a message with FK to Alice (sender) and Bob (receiver)
    // Only sender.id and receiver.id are used, name and age are ignored
    FKMessage const msg{
            .id       = 0,
            .sender   = User{.id = static_cast<int>(alice_id), .name = "ignored", .age = 0},
            .receiver = User{.id = static_cast<int>(bob_id), .name = "ignored", .age = 0},
            .text     = "Hello World"
    };

    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value()) << "FKMessage INSERT failed: " << msg_result.error().message();

    int64_t const msg_id = msg_result.value();
    EXPECT_GT(msg_id, 0) << "Expected valid message ID";

    // Verify FKs were stored correctly by querying database directly
    auto& conn     = QuerySet<User>::get_default_connection();
    auto  stmt_res = conn->prepare("SELECT sender_id, receiver_id FROM FKMessage WHERE id = ?");
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
TEST_F(FKFieldTest, SelectWithFKFieldPartialPopulation) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users
    User const bob{.id = 0, .name = "Bob", .age = 25};
    User const charlie{.id = 0, .name = "Charlie", .age = 35};
    auto       bob_result     = user_qs.insert(bob);
    auto       charlie_result = user_qs.insert(charlie);
    ASSERT_TRUE(bob_result.has_value());
    ASSERT_TRUE(charlie_result.has_value());
    int64_t const bob_id     = bob_result.value();
    int64_t const charlie_id = charlie_result.value();

    // Insert a message from Bob to Charlie
    FKMessage const msg{
            .id       = 0,
            .sender   = User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .receiver = User{.id = static_cast<int>(charlie_id), .name = "", .age = 0},
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
TEST_F(FKFieldTest, BatchInsertWithFKFields) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users
    std::vector<User> users       = {{1, "Alice", 30}, {2, "Bob", 25}, {3, "Charlie", 35}, {4, "Dave", 40}};
    auto              user_result = user_qs.insert(users);
    ASSERT_TRUE(user_result.has_value());
    const auto& user_ids = user_result.value();

    // Insert messages with FK references (Alice to Bob, Charlie to Dave)
    std::vector<FKMessage> messages =
            {{1,
              User{.id = static_cast<int>(user_ids[0]), .name = "", .age = 0},
              User{.id = static_cast<int>(user_ids[0]), .name = "", .age = 0},
              "FKMessage from Alice to Bob"},
             {2,
              User{.id = static_cast<int>(user_ids[0]), .name = "", .age = 0},
              User{.id = static_cast<int>(user_ids[0]), .name = "", .age = 0},
              "FKMessage from Charlie to Dave"}};

    auto msg_result = message_qs.insert(messages);
    ASSERT_TRUE(msg_result.has_value()) << "Batch INSERT failed: " << msg_result.error().message();

    const auto& msg_ids = msg_result.value();
    EXPECT_EQ(msg_ids.size(), 2) << "Expected 2 message IDs";

    // Verify messages were stored
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& retrieved_messages = select_result.value();
    ASSERT_EQ(retrieved_messages.size(), 2);

    // Verify FK values
    auto it = retrieved_messages.begin();
    EXPECT_EQ(it->sender.id, user_ids[0]);
    EXPECT_EQ(it->receiver.id, user_ids[0]);
    ++it;
    EXPECT_EQ(it->sender.id, user_ids[0]);
    EXPECT_EQ(it->receiver.id, user_ids[0]);
}

// Test: UPDATE with FK field
TEST_F(FKFieldTest, UpdateWithFKField) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users
    User const alice{.id = 0, .name = "Alice", .age = 30};
    User const bob{.id = 0, .name = "Bob", .age = 25};
    User const charlie{.id = 0, .name = "Charlie", .age = 35};
    User const dave{.id = 0, .name = "Dave", .age = 40};

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
            .sender   = User{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "Original message"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    int64_t const msg_id = msg_result.value();

    // Update message: change sender to Charlie, receiver to Dave, and text
    FKMessage const updated_msg{
            .id       = static_cast<int>(msg_id),
            .sender   = User{.id = static_cast<int>(charlie_id), .name = "", .age = 0},
            .receiver = User{.id = static_cast<int>(dave_id), .name = "", .age = 0},
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
TEST_F(FKFieldTest, DeleteWithFKField) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users
    User const alice{.id = 0, .name = "Alice", .age = 30};
    User const bob{.id = 0, .name = "Bob", .age = 25};
    auto       alice_result = user_qs.insert(alice);
    auto       bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert messages from Alice to Bob
    std::vector<FKMessage> messages =
            {{1,
              User{.id = static_cast<int>(alice_id), .name = "", .age = 0},
              User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
              "FKMessage 1"},
             {2,
              User{.id = static_cast<int>(alice_id), .name = "", .age = 0},
              User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
              "FKMessage 2"}};

    auto msg_result = message_qs.insert(messages);
    ASSERT_TRUE(msg_result.has_value());
    const auto& msg_ids = msg_result.value();

    // Delete first message
    FKMessage const to_delete{
            .id       = static_cast<int>(msg_ids[0]),
            .sender   = User{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = ""
    };

    auto delete_result = message_qs.remove(to_delete);
    ASSERT_TRUE(delete_result.has_value()) << "DELETE failed: " << delete_result.error().message();

    // Verify only one message remains
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& remaining_messages = select_result.value();
    ASSERT_EQ(remaining_messages.size(), 1);
    EXPECT_EQ(remaining_messages.begin()->id, msg_ids[1]);
    EXPECT_EQ(remaining_messages.begin()->text, "FKMessage 2");
}

// Test: Multiple FK fields to same type
TEST_F(FKFieldTest, MultipleFKFieldsToSameType) {
    // Create a conversation struct with two FK fields to User
    struct Conversation {
        [[= storm::meta::FieldAttr::primary]] int id;
        [[= storm::meta::FieldAttr::fk]] User     sender;
        [[= storm::meta::FieldAttr::fk]] User     receiver;
        std::string                               message;
    };

    QuerySet<User>         user_qs;
    QuerySet<Conversation> conv_qs;

    // Create conversation table
    auto& conn               = QuerySet<User>::get_default_connection();
    auto  create_conv_result = conn->execute(
            "CREATE TABLE Conversation ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
             "sender_id INTEGER NOT NULL, "
             "receiver_id INTEGER NOT NULL, "
             "message TEXT NOT NULL"
             ")"
    );
    ASSERT_TRUE(create_conv_result.has_value());

    // Insert users
    User const alice{.id = 0, .name = "Alice", .age = 30};
    User const bob{.id = 0, .name = "Bob", .age = 25};

    auto alice_result = user_qs.insert(alice);
    auto bob_result   = user_qs.insert(bob);

    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());

    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert conversation from Alice to Bob
    Conversation const conv{
            .id       = 0,
            .sender   = User{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
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
TEST_F(FKFieldTest, JoinFullyPopulatesFKObject) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users
    User const alice{.id = 0, .name = "Alice", .age = 30};
    User const bob{.id = 0, .name = "Bob", .age = 25};
    auto       alice_result = user_qs.insert(alice);
    auto       bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert message from Alice to Bob
    FKMessage const msg{
            .id       = 0,
            .sender   = User{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "Hello from JOIN!"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // Phase 2: JOIN to get fully populated sender
    auto join_result = message_qs.join<&FKMessage::sender>().select();
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
TEST_F(FKFieldTest, JoinMultipleFKFields) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users
    User const alice{.id = 0, .name = "Alice", .age = 30};
    User const bob{.id = 0, .name = "Bob", .age = 25};
    auto       alice_result = user_qs.insert(alice);
    auto       bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert message from Alice to Bob
    FKMessage const msg{
            .id       = 0,
            .sender   = User{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "Hello from multi-JOIN!"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // Phase 3: Multi-JOIN to get BOTH sender and receiver fully populated
    auto join_result = message_qs.join<&FKMessage::sender, &FKMessage::receiver>().select();
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
TEST_F(FKFieldTest, LeftJoinReturnsAllMessages) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert only one user (Alice)
    User const alice{.id = 0, .name = "Alice", .age = 30};
    auto       alice_result = user_qs.insert(alice);
    ASSERT_TRUE(alice_result.has_value());
    int64_t const alice_id = alice_result.value();

    // Insert a message with a non-existent receiver ID (999)
    // This simulates an orphaned FK reference
    auto& conn        = QuerySet<User>::get_default_connection();
    auto  stmt_result = conn->prepare("INSERT INTO FKMessage (sender_id, receiver_id, text) VALUES (?, ?, ?)");
    ASSERT_TRUE(stmt_result.has_value()) << "Prepare failed: " << stmt_result.error().message();

    auto stmt = std::move(stmt_result.value());
    ASSERT_TRUE(stmt.bind_int(1, alice_id).has_value());
    ASSERT_TRUE(stmt.bind_int(2, 999).has_value());
    ASSERT_TRUE(stmt.bind_text(3, "Orphaned message").has_value());

    int const step_result = stmt.step_raw();
    ASSERT_EQ(step_result, decltype(stmt)::NO_MORE_ROWS) << "Direct INSERT failed";

    // LEFT JOIN on sender - should return message even though receiver doesn't exist
    auto join_result = message_qs.left_join<&FKMessage::sender>().select();
    ASSERT_TRUE(join_result.has_value()) << "LEFT JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();
    ASSERT_EQ(messages.size(), 1) << "LEFT JOIN should return all messages";

    // Verify sender is fully populated (exists)
    auto it = messages.begin();
    EXPECT_EQ(it->sender.id, alice_id);
    EXPECT_EQ(it->sender.name, "Alice") << "LEFT JOIN should populate existing FK";
    EXPECT_EQ(it->sender.age, 30);

    // Verify receiver is not populated (doesn't exist in User table)
    EXPECT_EQ(it->receiver.id, 0) << "Non-JOINed FK should remain default";

    // Verify message text
    EXPECT_EQ(it->text, "Orphaned message");
}

// Test: LEFT JOIN with multiple FK fields
TEST_F(FKFieldTest, LeftJoinMultipleFKFields) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users
    User const alice{.id = 0, .name = "Alice", .age = 30};
    User const bob{.id = 0, .name = "Bob", .age = 25};
    auto       alice_result = user_qs.insert(alice);
    auto       bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert message from Alice to Bob
    FKMessage const msg{
            .id       = 0,
            .sender   = User{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "Hello with LEFT JOIN"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // LEFT JOIN on both sender and receiver
    auto join_result = message_qs.left_join<&FKMessage::sender, &FKMessage::receiver>().select();
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
TEST_F(FKFieldTest, RightJoinBehavior) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users first
    User const alice{.id = 0, .name = "Alice", .age = 30};
    User const bob{.id = 0, .name = "Bob", .age = 25};
    User const charlie{.id = 0, .name = "Charlie", .age = 35}; // Charlie has no messages
    auto       alice_result   = user_qs.insert(alice);
    auto       bob_result     = user_qs.insert(bob);
    auto       charlie_result = user_qs.insert(charlie);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    ASSERT_TRUE(charlie_result.has_value());
    int64_t const alice_id   = alice_result.value();
    int64_t const bob_id     = bob_result.value();
    int64_t const charlie_id = charlie_result.value();

    // Insert messages from Alice to Bob (Charlie is not referenced)
    FKMessage const msg{
            .id       = 0,
            .sender   = User{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "FKMessage to Bob"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // RIGHT JOIN on sender - should return all users in User table as senders
    // This includes Charlie even though no message references him
    auto join_result = message_qs.right_join<&FKMessage::sender>().select();
    ASSERT_TRUE(join_result.has_value()) << "RIGHT JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();

    // RIGHT JOIN returns:
    // - All rows from right table (User)
    // - Matching rows from left table (FKMessage)
    // So we should get at least the message we inserted, possibly more depending on implementation
    EXPECT_GE(messages.size(), 1) << "RIGHT JOIN should return at least existing messages";

    // Find the message we inserted
    bool found = false;
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
TEST_F(FKFieldTest, RightJoinMultipleFKFields) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users
    User const alice{.id = 0, .name = "Alice", .age = 30};
    User const bob{.id = 0, .name = "Bob", .age = 25};
    auto       alice_result = user_qs.insert(alice);
    auto       bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    // Insert message from Alice to Bob
    FKMessage const msg{
            .id       = 0,
            .sender   = User{.id = static_cast<int>(alice_id), .name = "", .age = 0},
            .receiver = User{.id = static_cast<int>(bob_id), .name = "", .age = 0},
            .text     = "Hello with RIGHT JOIN"
    };
    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // RIGHT JOIN on both sender and receiver
    auto join_result = message_qs.right_join<&FKMessage::sender, &FKMessage::receiver>().select();
    ASSERT_TRUE(join_result.has_value()) << "Multi RIGHT JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();
    EXPECT_GE(messages.size(), 1) << "RIGHT JOIN should return at least the inserted message";

    // Find and verify our message
    bool found = false;
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

// Test fixture for nullable FK fields
class NullableFKTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<User>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        auto& conn = QuerySet<User>::get_default_connection();

        // Create User table
        auto create_user_result = conn->execute(
                "CREATE TABLE User ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_user_result.has_value());

        // Create FKMessage table with NULLABLE sender_id (allows NULL)
        auto create_message_result = conn->execute(
                "CREATE TABLE FKMessage ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "sender_id INTEGER, " // NULLABLE FK
                "receiver_id INTEGER NOT NULL, "
                "text TEXT NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_message_result.has_value());
    }

    void TearDown() override {
        QuerySet<User>::clear_default_connection();
    }
};

// Test: SELECT with NULL FK values
TEST_F(NullableFKTest, SelectWithNullFKField) {
    QuerySet<FKMessage> message_qs;

    // Insert message with NULL sender_id
    auto& conn        = QuerySet<User>::get_default_connection();
    auto  stmt_result = conn->prepare("INSERT INTO FKMessage (sender_id, receiver_id, text) VALUES (NULL, ?, ?)");
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
TEST_F(NullableFKTest, LeftJoinWithNullFKField) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert a user
    User const alice{.id = 0, .name = "Alice", .age = 30};
    auto       alice_result = user_qs.insert(alice);
    ASSERT_TRUE(alice_result.has_value());
    int64_t const alice_id = alice_result.value();

    // Insert message with NULL sender_id
    auto& conn        = QuerySet<User>::get_default_connection();
    auto  stmt_result = conn->prepare("INSERT INTO FKMessage (sender_id, receiver_id, text) VALUES (NULL, ?, ?)");
    ASSERT_TRUE(stmt_result.has_value());

    auto stmt = std::move(stmt_result.value());
    ASSERT_TRUE(stmt.bind_int(1, alice_id).has_value());
    ASSERT_TRUE(stmt.bind_text(2, "NULL sender message").has_value());

    int const step_result = stmt.step_raw();
    ASSERT_EQ(step_result, decltype(stmt)::NO_MORE_ROWS);

    // LEFT JOIN on sender - should return message even with NULL sender_id
    auto join_result = message_qs.left_join<&FKMessage::sender>().select();
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
TEST_F(NullableFKTest, LeftJoinWithMixedNullAndValidFKs) {
    QuerySet<User>      user_qs;
    QuerySet<FKMessage> message_qs;

    // Insert users
    User const alice{.id = 0, .name = "Alice", .age = 30};
    User const bob{.id = 0, .name = "Bob", .age = 25};
    auto       alice_result = user_qs.insert(alice);
    auto       bob_result   = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t const alice_id = alice_result.value();
    int64_t const bob_id   = bob_result.value();

    auto& conn = QuerySet<User>::get_default_connection();

    // FKMessage 1: Valid sender (Alice)
    auto stmt1 = conn->prepare("INSERT INTO FKMessage (sender_id, receiver_id, text) VALUES (?, ?, ?)");
    ASSERT_TRUE(stmt1.has_value());
    auto s1 = std::move(stmt1.value());
    ASSERT_TRUE(s1.bind_int(1, alice_id).has_value());
    ASSERT_TRUE(s1.bind_int(2, bob_id).has_value());
    ASSERT_TRUE(s1.bind_text(3, "From Alice").has_value());
    ASSERT_EQ(s1.step_raw(), decltype(s1)::NO_MORE_ROWS);

    // FKMessage 2: NULL sender
    auto stmt2 = conn->prepare("INSERT INTO FKMessage (sender_id, receiver_id, text) VALUES (NULL, ?, ?)");
    ASSERT_TRUE(stmt2.has_value());
    auto s2 = std::move(stmt2.value());
    ASSERT_TRUE(s2.bind_int(1, bob_id).has_value());
    ASSERT_TRUE(s2.bind_text(2, "Anonymous").has_value());
    ASSERT_EQ(s2.step_raw(), decltype(s2)::NO_MORE_ROWS);

    // LEFT JOIN should return both messages
    auto join_result = message_qs.left_join<&FKMessage::sender>().select();
    ASSERT_TRUE(join_result.has_value());

    const auto& messages = join_result.value();
    ASSERT_EQ(messages.size(), 2) << "LEFT JOIN should return all messages";

    // Find and verify each message
    bool found_alice = false;
    bool found_null  = false;
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

// Test fixture for extended type support in JOINs
class ExtendedTypesJoinTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<User>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        auto& conn = QuerySet<User>::get_default_connection();

        // Create Employee table with extended types
        auto create_employee_result = conn->execute(
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

        // Create Project table with FK to Employee
        auto create_project_result = conn->execute(
                "CREATE TABLE Project ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "manager_id INTEGER NOT NULL, "
                "title TEXT NOT NULL, "
                "budget REAL NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_project_result.has_value())
                << "Failed to create Project table: " << create_project_result.error().message();
    }

    void TearDown() override {
        QuerySet<User>::clear_default_connection();
    }
};

// Test: JOIN with extended types (double, bool, optional)
TEST_F(ExtendedTypesJoinTest, JoinWithExtendedTypes) {
    // Define structs with extended types
    struct Employee {
        [[= storm::meta::FieldAttr::primary]] int id;
        std::string                               name;
        double                                    salary;
        bool                                      is_active;
        std::optional<std::string>                nickname;
    };

    struct Project {
        [[= storm::meta::FieldAttr::primary]] int id;
        [[= storm::meta::FieldAttr::fk]] Employee manager;
        std::string                               title;
        double                                    budget;
    };

    QuerySet<Employee> employee_qs;
    QuerySet<Project>  project_qs;

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
    auto join_result = project_qs.join<&Project::manager>().select();
    ASSERT_TRUE(join_result.has_value()) << "JOIN failed: " << join_result.error().message();

    const auto& projects = join_result.value();
    ASSERT_EQ(projects.size(), 2) << "Should retrieve both projects";

    // Find Alice's project and verify all extended types
    bool found_alice_project = false;
    bool found_bob_project   = false;

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
TEST_F(ExtendedTypesJoinTest, MultiJoinWithExtendedTypes) {
    struct Employee {
        [[= storm::meta::FieldAttr::primary]] int id;
        std::string                               name;
        double                                    salary;
        bool                                      is_active;
        std::optional<std::string>                nickname;
    };

    struct Task {
        [[= storm::meta::FieldAttr::primary]] int id;
        [[= storm::meta::FieldAttr::fk]] Employee assignee;
        [[= storm::meta::FieldAttr::fk]] Employee reviewer;
        std::string                               description;
    };

    QuerySet<Employee> employee_qs;
    QuerySet<Task>     task_qs;

    // Create Task table
    auto& conn               = QuerySet<User>::get_default_connection();
    auto  create_task_result = conn->execute(
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
    auto join_result = task_qs.join<&Task::assignee, &Task::reviewer>().select();
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
