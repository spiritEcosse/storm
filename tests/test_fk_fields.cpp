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

struct Message {
    [[= storm::meta::FieldAttr::primary]] int  id;
    [[= storm::meta::FieldAttr::fk]] User      sender;
    std::string                                text;
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
        auto create_user_result = conn.execute(
                "CREATE TABLE User ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT NOT NULL, "
                "age INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_user_result.has_value())
                << "Failed to create User table: " << create_user_result.error().message();

        // Create Message table with sender_id foreign key
        auto create_message_result = conn.execute(
                "CREATE TABLE Message ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "sender_id INTEGER NOT NULL, "
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
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert a user first
    User alice{0, "Alice", 30};
    auto user_result = user_qs.insert(alice);
    ASSERT_TRUE(user_result.has_value()) << "User INSERT failed: " << user_result.error().message();

    int64_t alice_id = user_result.value();

    // Insert a message with FK to Alice
    // Only sender.id is used, name and age are ignored
    Message msg{0, User{static_cast<int>(alice_id), "ignored", 0}, "Hello World"};

    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value()) << "Message INSERT failed: " << msg_result.error().message();

    int64_t msg_id = msg_result.value();
    EXPECT_GT(msg_id, 0) << "Expected valid message ID";

    // Verify FK was stored correctly by querying database directly
    auto& conn     = QuerySet<User>::get_default_connection();
    auto  stmt_res = conn.prepare("SELECT sender_id FROM Message WHERE id = ?");
    ASSERT_TRUE(stmt_res.has_value());

    auto stmt = std::move(stmt_res.value());
    ASSERT_TRUE(stmt.bind_int(1, msg_id).has_value());

    int step_result = stmt.step_raw();
    ASSERT_EQ(step_result, decltype(stmt)::ROW_AVAILABLE);

    int64_t stored_sender_id = stmt.extract_int64(0);
    EXPECT_EQ(stored_sender_id, alice_id) << "FK value should match user ID";
}

// Test: SELECT with FK field populates only the PK
TEST_F(FKFieldTest, SelectWithFKFieldPartialPopulation) {
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert a user
    User bob{0, "Bob", 25};
    auto user_result = user_qs.insert(bob);
    ASSERT_TRUE(user_result.has_value());
    int64_t bob_id = user_result.value();

    // Insert a message
    Message msg{0, User{static_cast<int>(bob_id), "", 0}, "Test message"};
    auto    msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // SELECT messages
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value()) << "SELECT failed: " << select_result.error().message();

    const auto& messages = select_result.value();
    ASSERT_EQ(messages.size(), 1) << "Expected exactly one message";

    // Verify message fields
    EXPECT_EQ(messages[0].text, "Test message");

    // Verify FK field: only PK should be populated
    EXPECT_EQ(messages[0].sender.id, bob_id) << "FK PK should be populated";
    EXPECT_EQ(messages[0].sender.name, "")
            << "FK non-PK fields should remain default (empty string)";
    EXPECT_EQ(messages[0].sender.age, 0) << "FK non-PK fields should remain default (0)";
}

// Test: Batch INSERT with FK fields
TEST_F(FKFieldTest, BatchInsertWithFKFields) {
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert users
    std::vector<User> users = {{1, "Alice", 30}, {2, "Bob", 25}};
    auto              user_result = user_qs.insert(std::span<const User>(users));
    ASSERT_TRUE(user_result.has_value());
    const auto& user_ids = user_result.value();

    // Insert messages with FK references
    std::vector<Message> messages = {
            {1, User{static_cast<int>(user_ids[0]), "", 0}, "Message from Alice"},
            {2, User{static_cast<int>(user_ids[1]), "", 0}, "Message from Bob"}};

    auto msg_result = message_qs.insert(std::span<const Message>(messages));
    ASSERT_TRUE(msg_result.has_value()) << "Batch INSERT failed: " << msg_result.error().message();

    const auto& msg_ids = msg_result.value();
    EXPECT_EQ(msg_ids.size(), 2) << "Expected 2 message IDs";

    // Verify messages were stored
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& retrieved_messages = select_result.value();
    ASSERT_EQ(retrieved_messages.size(), 2);

    // Verify FK values
    EXPECT_EQ(retrieved_messages[0].sender.id, user_ids[0]);
    EXPECT_EQ(retrieved_messages[1].sender.id, user_ids[1]);
}

// Test: UPDATE with FK field
TEST_F(FKFieldTest, UpdateWithFKField) {
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert users
    User alice{0, "Alice", 30};
    User bob{0, "Bob", 25};

    auto alice_result = user_qs.insert(alice);
    auto bob_result   = user_qs.insert(bob);

    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());

    int64_t alice_id = alice_result.value();
    int64_t bob_id   = bob_result.value();

    // Insert message from Alice
    Message msg{0, User{static_cast<int>(alice_id), "", 0}, "Original message"};
    auto    msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    int64_t msg_id = msg_result.value();

    // Update message: change sender to Bob and text
    Message updated_msg{
            static_cast<int>(msg_id), User{static_cast<int>(bob_id), "", 0}, "Updated message"};

    auto update_result = message_qs.update(updated_msg);
    ASSERT_TRUE(update_result.has_value()) << "UPDATE failed: " << update_result.error().message();

    // Verify update
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& messages = select_result.value();
    ASSERT_EQ(messages.size(), 1);

    EXPECT_EQ(messages[0].id, msg_id);
    EXPECT_EQ(messages[0].sender.id, bob_id) << "FK should be updated to Bob";
    EXPECT_EQ(messages[0].text, "Updated message");
}

// Test: DELETE with FK field
TEST_F(FKFieldTest, DeleteWithFKField) {
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert user
    User charlie{0, "Charlie", 35};
    auto user_result = user_qs.insert(charlie);
    ASSERT_TRUE(user_result.has_value());
    int64_t charlie_id = user_result.value();

    // Insert messages
    std::vector<Message> messages = {
            {1, User{static_cast<int>(charlie_id), "", 0}, "Message 1"},
            {2, User{static_cast<int>(charlie_id), "", 0}, "Message 2"}};

    auto msg_result = message_qs.insert(std::span<const Message>(messages));
    ASSERT_TRUE(msg_result.has_value());
    const auto& msg_ids = msg_result.value();

    // Delete first message
    Message to_delete{static_cast<int>(msg_ids[0]), User{static_cast<int>(charlie_id), "", 0}, ""};

    auto delete_result = message_qs.remove(to_delete);
    ASSERT_TRUE(delete_result.has_value()) << "DELETE failed: " << delete_result.error().message();

    // Verify only one message remains
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& remaining_messages = select_result.value();
    ASSERT_EQ(remaining_messages.size(), 1);
    EXPECT_EQ(remaining_messages[0].id, msg_ids[1]);
    EXPECT_EQ(remaining_messages[0].text, "Message 2");
}

// Test: Multiple FK fields to same type
TEST_F(FKFieldTest, MultipleFKFieldsToSameType) {
    // Create a conversation struct with two FK fields to User
    struct Conversation {
        [[= storm::meta::FieldAttr::primary]] int  id;
        [[= storm::meta::FieldAttr::fk]] User      sender;
        [[= storm::meta::FieldAttr::fk]] User      receiver;
        std::string                                message;
    };

    QuerySet<User>         user_qs;
    QuerySet<Conversation> conv_qs;

    // Create conversation table
    auto& conn         = QuerySet<User>::get_default_connection();
    auto  create_conv_result = conn.execute(
            "CREATE TABLE Conversation ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "sender_id INTEGER NOT NULL, "
            "receiver_id INTEGER NOT NULL, "
            "message TEXT NOT NULL"
            ")"
    );
    ASSERT_TRUE(create_conv_result.has_value());

    // Insert users
    User alice{0, "Alice", 30};
    User bob{0, "Bob", 25};

    auto alice_result = user_qs.insert(alice);
    auto bob_result   = user_qs.insert(bob);

    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());

    int64_t alice_id = alice_result.value();
    int64_t bob_id   = bob_result.value();

    // Insert conversation from Alice to Bob
    Conversation conv{
            0,
            User{static_cast<int>(alice_id), "", 0},
            User{static_cast<int>(bob_id), "", 0},
            "Hello Bob!"};

    auto conv_result = conv_qs.insert(conv);
    ASSERT_TRUE(conv_result.has_value()) << "Conversation INSERT failed: " << conv_result.error().message();

    // SELECT and verify both FK fields are populated
    auto select_result = conv_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& conversations = select_result.value();
    ASSERT_EQ(conversations.size(), 1);

    EXPECT_EQ(conversations[0].sender.id, alice_id) << "Sender FK should be Alice";
    EXPECT_EQ(conversations[0].receiver.id, bob_id) << "Receiver FK should be Bob";
    EXPECT_EQ(conversations[0].message, "Hello Bob!");
}
