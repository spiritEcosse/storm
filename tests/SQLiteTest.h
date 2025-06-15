#pragma once

#include <gtest/gtest.h>
#include <string>
#include <cstdio>
#include "../src/QuerySet.h"
#include "../src/Person.h"
#include <memory>

class SQLiteTest : public ::testing::Test {
protected:
    std::string db_name;

    void SetUp() override {
        // Use in-memory SQLite database for isolation
        db_name = ":memory:";
    }

    void TearDown() override {
        // Clean up any resources if needed
        // In-memory database is automatically destroyed when connection closes
    }
};

TEST_F(SQLiteTest, QuerySetCreateTable) {
    // Create shared connection to in-memory SQLite DB
    auto conn = std::make_shared<Connection>(db_name);
    orm::QuerySet<Person> persons(conn);

    // Call create_table (should not throw)
    ASSERT_NO_THROW(persons.create_table());
    
    // Verify table exists using a more robust approach
    sqlite3* db = conn->get();
    ASSERT_NE(db, nullptr) << "Database connection should not be null";
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='person';";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    ASSERT_EQ(rc, SQLITE_OK) << "Failed to prepare statement: " << sqlite3_errmsg(db);
    
    // Ensure statement is cleaned up even if test fails
    auto stmt_guard = [&stmt]() { 
        if (stmt) sqlite3_finalize(stmt); 
    };
    
    rc = sqlite3_step(stmt);
    ASSERT_EQ(rc, SQLITE_ROW) << "Failed to execute query: " << sqlite3_errmsg(db);
    
    int table_count = sqlite3_column_int(stmt, 0);
    EXPECT_EQ(table_count, 1) << "Expected exactly one 'person' table";
    
    stmt_guard(); // Clean up statement
}

// Additional test for table structure verification
TEST_F(SQLiteTest, QuerySetCreateTableStructure) {
    auto conn = std::make_shared<Connection>(db_name);
    orm::QuerySet<Person> persons(conn);
    
    ASSERT_NO_THROW(persons.create_table());
    
    sqlite3* db = conn->get();
    ASSERT_NE(db, nullptr);
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "PRAGMA table_info(person);";
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    ASSERT_EQ(rc, SQLITE_OK) << "Failed to prepare PRAGMA statement: " << sqlite3_errmsg(db);
    
    // Verify we can get column information (basic structure check)
    bool has_columns = false;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        has_columns = true;
        // Could add specific column checks here if needed
        // const char* column_name = (const char*)sqlite3_column_text(stmt, 1);
        // const char* column_type = (const char*)sqlite3_column_text(stmt, 2);
    }
    
    EXPECT_TRUE(has_columns) << "Table should have at least one column";
    ASSERT_EQ(rc, SQLITE_DONE) << "PRAGMA query should complete successfully";
    
    sqlite3_finalize(stmt);
}

// Test for duplicate table creation (should be idempotent)
TEST_F(SQLiteTest, QuerySetCreateTableIdempotent) {
    auto conn = std::make_shared<Connection>(db_name);
    orm::QuerySet<Person> persons(conn);
    
    // Create table twice - should not throw
    ASSERT_NO_THROW(persons.create_table());
    ASSERT_NO_THROW(persons.create_table());
    
    // Verify still only one table exists
    sqlite3* db = conn->get();
    sqlite3_stmt* stmt = nullptr;
    
    int rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='person';", -1, &stmt, nullptr);
    ASSERT_EQ(rc, SQLITE_OK);
    
    rc = sqlite3_step(stmt);
    ASSERT_EQ(rc, SQLITE_ROW);
    
    int table_count = sqlite3_column_int(stmt, 0);
    EXPECT_EQ(table_count, 1) << "Should still have exactly one table after duplicate creation";
    
    sqlite3_finalize(stmt);
}