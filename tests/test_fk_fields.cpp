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
    [[= storm::meta::FieldAttr::fk]] User      receiver;
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

        // Create Message table with sender_id and receiver_id foreign keys
        auto create_message_result = conn.execute(
                "CREATE TABLE Message ("
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
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert users first
    User alice{0, "Alice", 30};
    User bob{0, "Bob", 25};
    auto alice_result = user_qs.insert(alice);
    auto bob_result = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value()) << "Alice INSERT failed: " << alice_result.error().message();
    ASSERT_TRUE(bob_result.has_value()) << "Bob INSERT failed: " << bob_result.error().message();

    int64_t alice_id = alice_result.value();
    int64_t bob_id = bob_result.value();

    // Insert a message with FK to Alice (sender) and Bob (receiver)
    // Only sender.id and receiver.id are used, name and age are ignored
    Message msg{0, User{static_cast<int>(alice_id), "ignored", 0}, User{static_cast<int>(bob_id), "ignored", 0}, "Hello World"};

    auto msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value()) << "Message INSERT failed: " << msg_result.error().message();

    int64_t msg_id = msg_result.value();
    EXPECT_GT(msg_id, 0) << "Expected valid message ID";

    // Verify FKs were stored correctly by querying database directly
    auto& conn     = QuerySet<User>::get_default_connection();
    auto  stmt_res = conn.prepare("SELECT sender_id, receiver_id FROM Message WHERE id = ?");
    ASSERT_TRUE(stmt_res.has_value());

    auto stmt = std::move(stmt_res.value());
    ASSERT_TRUE(stmt.bind_int(1, msg_id).has_value());

    int step_result = stmt.step_raw();
    ASSERT_EQ(step_result, decltype(stmt)::ROW_AVAILABLE);

    int64_t stored_sender_id = stmt.extract_int64(0);
    int64_t stored_receiver_id = stmt.extract_int64(1);
    EXPECT_EQ(stored_sender_id, alice_id) << "Sender FK value should match Alice ID";
    EXPECT_EQ(stored_receiver_id, bob_id) << "Receiver FK value should match Bob ID";
}

// Test: SELECT with FK field populates only the PK
TEST_F(FKFieldTest, SelectWithFKFieldPartialPopulation) {
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert users
    User bob{0, "Bob", 25};
    User charlie{0, "Charlie", 35};
    auto bob_result = user_qs.insert(bob);
    auto charlie_result = user_qs.insert(charlie);
    ASSERT_TRUE(bob_result.has_value());
    ASSERT_TRUE(charlie_result.has_value());
    int64_t bob_id = bob_result.value();
    int64_t charlie_id = charlie_result.value();

    // Insert a message from Bob to Charlie
    Message msg{0, User{static_cast<int>(bob_id), "", 0}, User{static_cast<int>(charlie_id), "", 0}, "Test message"};
    auto    msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // SELECT messages
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value()) << "SELECT failed: " << select_result.error().message();

    const auto& messages = select_result.value();
    ASSERT_EQ(messages.size(), 1) << "Expected exactly one message";

    // Verify message fields
    EXPECT_EQ(messages[0].text, "Test message");

    // Verify FK fields: only PK should be populated
    EXPECT_EQ(messages[0].sender.id, bob_id) << "Sender FK PK should be populated";
    EXPECT_EQ(messages[0].sender.name, "")
            << "Sender FK non-PK fields should remain default (empty string)";
    EXPECT_EQ(messages[0].sender.age, 0) << "Sender FK non-PK fields should remain default (0)";

    EXPECT_EQ(messages[0].receiver.id, charlie_id) << "Receiver FK PK should be populated";
    EXPECT_EQ(messages[0].receiver.name, "")
            << "Receiver FK non-PK fields should remain default (empty string)";
    EXPECT_EQ(messages[0].receiver.age, 0) << "Receiver FK non-PK fields should remain default (0)";
}

// Test: Batch INSERT with FK fields
TEST_F(FKFieldTest, BatchInsertWithFKFields) {
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert users
    std::vector<User> users = {{1, "Alice", 30}, {2, "Bob", 25}, {3, "Charlie", 35}, {4, "Dave", 40}};
    auto              user_result = user_qs.insert(std::span<const User>(users));
    ASSERT_TRUE(user_result.has_value());
    const auto& user_ids = user_result.value();

    // Insert messages with FK references (Alice to Bob, Charlie to Dave)
    std::vector<Message> messages = {
            {1, User{static_cast<int>(user_ids[0]), "", 0}, User{static_cast<int>(user_ids[1]), "", 0}, "Message from Alice to Bob"},
            {2, User{static_cast<int>(user_ids[2]), "", 0}, User{static_cast<int>(user_ids[3]), "", 0}, "Message from Charlie to Dave"}};

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
    EXPECT_EQ(retrieved_messages[0].receiver.id, user_ids[1]);
    EXPECT_EQ(retrieved_messages[1].sender.id, user_ids[2]);
    EXPECT_EQ(retrieved_messages[1].receiver.id, user_ids[3]);
}

// Test: UPDATE with FK field
TEST_F(FKFieldTest, UpdateWithFKField) {
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert users
    User alice{0, "Alice", 30};
    User bob{0, "Bob", 25};
    User charlie{0, "Charlie", 35};
    User dave{0, "Dave", 40};

    auto alice_result = user_qs.insert(alice);
    auto bob_result   = user_qs.insert(bob);
    auto charlie_result = user_qs.insert(charlie);
    auto dave_result = user_qs.insert(dave);

    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    ASSERT_TRUE(charlie_result.has_value());
    ASSERT_TRUE(dave_result.has_value());

    int64_t alice_id = alice_result.value();
    int64_t bob_id   = bob_result.value();
    int64_t charlie_id = charlie_result.value();
    int64_t dave_id = dave_result.value();

    // Insert message from Alice to Bob
    Message msg{0, User{static_cast<int>(alice_id), "", 0}, User{static_cast<int>(bob_id), "", 0}, "Original message"};
    auto    msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    int64_t msg_id = msg_result.value();

    // Update message: change sender to Charlie, receiver to Dave, and text
    Message updated_msg{
            static_cast<int>(msg_id), User{static_cast<int>(charlie_id), "", 0}, User{static_cast<int>(dave_id), "", 0}, "Updated message"};

    auto update_result = message_qs.update(updated_msg);
    ASSERT_TRUE(update_result.has_value()) << "UPDATE failed: " << update_result.error().message();

    // Verify update
    auto select_result = message_qs.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& messages = select_result.value();
    ASSERT_EQ(messages.size(), 1);

    EXPECT_EQ(messages[0].id, msg_id);
    EXPECT_EQ(messages[0].sender.id, charlie_id) << "Sender FK should be updated to Charlie";
    EXPECT_EQ(messages[0].receiver.id, dave_id) << "Receiver FK should be updated to Dave";
    EXPECT_EQ(messages[0].text, "Updated message");
}

// Test: DELETE with FK field
TEST_F(FKFieldTest, DeleteWithFKField) {
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert users
    User alice{0, "Alice", 30};
    User bob{0, "Bob", 25};
    auto alice_result = user_qs.insert(alice);
    auto bob_result = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t alice_id = alice_result.value();
    int64_t bob_id = bob_result.value();

    // Insert messages from Alice to Bob
    std::vector<Message> messages = {
            {1, User{static_cast<int>(alice_id), "", 0}, User{static_cast<int>(bob_id), "", 0}, "Message 1"},
            {2, User{static_cast<int>(alice_id), "", 0}, User{static_cast<int>(bob_id), "", 0}, "Message 2"}};

    auto msg_result = message_qs.insert(std::span<const Message>(messages));
    ASSERT_TRUE(msg_result.has_value());
    const auto& msg_ids = msg_result.value();

    // Delete first message
    Message to_delete{static_cast<int>(msg_ids[0]), User{static_cast<int>(alice_id), "", 0}, User{static_cast<int>(bob_id), "", 0}, ""};

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

// Test: Phase 2 - JOIN populates FK object fully
TEST_F(FKFieldTest, JoinFullyPopulatesFKObject) {
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert users
    User alice{0, "Alice", 30};
    User bob{0, "Bob", 25};
    auto alice_result = user_qs.insert(alice);
    auto bob_result = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t alice_id = alice_result.value();
    int64_t bob_id = bob_result.value();

    // Insert message from Alice to Bob
    Message msg{0, User{static_cast<int>(alice_id), "", 0}, User{static_cast<int>(bob_id), "", 0}, "Hello from JOIN!"};
    auto    msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // Phase 2: JOIN to get fully populated sender
    auto join_result = message_qs.join<&Message::sender>().execute();
    ASSERT_TRUE(join_result.has_value()) << "JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();
    ASSERT_EQ(messages.size(), 1);

    // Verify message fields
    EXPECT_EQ(messages[0].text, "Hello from JOIN!");

    // Verify sender FK object is FULLY populated (not just PK!)
    EXPECT_EQ(messages[0].sender.id, alice_id);
    EXPECT_EQ(messages[0].sender.name, "Alice") << "JOIN should populate sender FK object's name!";
    EXPECT_EQ(messages[0].sender.age, 30) << "JOIN should populate sender FK object's age!";

    // Verify receiver FK object is NOT populated (current JOIN limitation)
    // TODO: Future enhancement - populate non-JOINed FK fields with their IDs
    EXPECT_EQ(messages[0].receiver.id, 0) << "Non-JOINed FK fields are not populated (current limitation)";
    EXPECT_EQ(messages[0].receiver.name, "") << "Without JOIN, receiver name should be empty";
    EXPECT_EQ(messages[0].receiver.age, 0) << "Without JOIN, receiver age should be 0";
}

// Test: Phase 3 - Multi-JOIN populates multiple FK objects fully
TEST_F(FKFieldTest, JoinMultipleFKFields) {
    QuerySet<User>    user_qs;
    QuerySet<Message> message_qs;

    // Insert users
    User alice{0, "Alice", 30};
    User bob{0, "Bob", 25};
    auto alice_result = user_qs.insert(alice);
    auto bob_result = user_qs.insert(bob);
    ASSERT_TRUE(alice_result.has_value());
    ASSERT_TRUE(bob_result.has_value());
    int64_t alice_id = alice_result.value();
    int64_t bob_id = bob_result.value();

    // Insert message from Alice to Bob
    Message msg{0, User{static_cast<int>(alice_id), "", 0}, User{static_cast<int>(bob_id), "", 0}, "Hello from multi-JOIN!"};
    auto    msg_result = message_qs.insert(msg);
    ASSERT_TRUE(msg_result.has_value());

    // Phase 3: Multi-JOIN to get BOTH sender and receiver fully populated
    auto join_result = message_qs.join<&Message::sender, &Message::receiver>().execute();
    ASSERT_TRUE(join_result.has_value()) << "Multi-JOIN failed: " << join_result.error().message();

    const auto& messages = join_result.value();
    ASSERT_EQ(messages.size(), 1);

    // Verify message fields
    EXPECT_EQ(messages[0].text, "Hello from multi-JOIN!");

    // Verify sender FK object is FULLY populated
    EXPECT_EQ(messages[0].sender.id, alice_id);
    EXPECT_EQ(messages[0].sender.name, "Alice") << "Multi-JOIN should populate sender FK object's name!";
    EXPECT_EQ(messages[0].sender.age, 30) << "Multi-JOIN should populate sender FK object's age!";

    // Verify receiver FK object is ALSO FULLY populated (Phase 3 improvement!)
    EXPECT_EQ(messages[0].receiver.id, bob_id);
    EXPECT_EQ(messages[0].receiver.name, "Bob") << "Multi-JOIN should populate receiver FK object's name!";
    EXPECT_EQ(messages[0].receiver.age, 25) << "Multi-JOIN should populate receiver FK object's age!";
}
