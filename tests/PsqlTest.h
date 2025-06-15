// #pragma once

// #include <gtest/gtest.h>

// class PsqlTest {
// private:
//     DatabaseManager& dbManager;
//     const std::string poolName;

// public:
//     explicit PsqlTest(DatabaseManager& manager, const std::string& poolName = "test_pool")
//         : dbManager(manager), poolName(poolName) {}

//     bool testConnection() {
//         try {
//             // Get a connection from the pool
//             auto connection = dbManager.getConnection(poolName);

//             // Run a simple query to test the connection
//             const auto result = connection->execute("SELECT 1 AS test");

//             // Check if we received the expected result
//             if (result.size() > 0 && result[0]["test"] == "1") {
//                 std::cout << "Database connection test successful!" << std::endl;
//                 return true;
//             } else {
//                 std::cout << "Database connection test failed: unexpected result" << std::endl;
//                 return false;
//             }
//         } catch (const std::exception& e) {
//             std::cerr << "Database connection test failed: " << e.what() << std::endl;
//             return false;
//         }
//     }

//     bool testTableExists(const std::string& tableName) {
//         try {
//             auto connection = dbManager.getConnection(poolName);

//             // This query works for PostgreSQL to check if a table exists
//             std::string query =
//                 "SELECT EXISTS (SELECT 1 FROM information_schema.tables "
//                 "WHERE table_schema = 'public' AND table_name = '" + tableName + "')";

//             const auto result = connection->execute(query);

//             if (result.size() > 0 && result[0]["exists"] == "t") {
//                 std::cout << "Table '" << tableName << "' exists" << std::endl;
//                 return true;
//             } else {
//                 std::cout << "Table '" << tableName << "' does not exist" << std::endl;
//                 return false;
//             }
//         } catch (const std::exception& e) {
//             std::cerr << "Error checking if table exists: " << e.what() << std::endl;
//             return false;
//         }
//     }
// };
