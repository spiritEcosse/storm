#pragma once

/**
 * SELECT JOIN Benchmark - JOIN operations
 *
 * Tests SELECT performance with JOIN clauses for FK relationships.
 * Requires two models: base model with FK fields and related model.
 *
 * Workflow:
 * 1. prepare(): Create users and messages with FK relationships
 * 2. execute(): Storm ORM join<FK>().select()
 * 3. execute_raw(): Raw SQLite SELECT...JOIN - native sqlite3 API only
 *
 * FAIR COMPARISON: Both Storm ORM and raw SQLite use prepared statements
 * for JOIN operations.
 */

#include "base.hpp"
#include <format>
#include <plf_hive/plf_hive.h>

namespace storm::benchmark {

    // SELECT JOIN benchmark for FK relationships
    // BaseModel: model with FK field (e.g., FKMessage)
    // RelatedModel: model referenced by FK (e.g., User)
    // FKFieldPtr: compile-time pointer to FK field (e.g., &FKMessage::sender)
    template <typename BaseModel, typename RelatedModel, auto FKFieldPtr> class SelectJoinBenchmark {
      private:
        QuerySet<BaseModel>    base_qs_;
        QuerySet<RelatedModel> related_qs_;
        int                    dataset_size_;

      public:
        explicit constexpr SelectJoinBenchmark(int dataset_size = 1000) : dataset_size_(dataset_size) {}

        // ====================================================================
        // print_info - SELECT JOIN-specific info
        // ====================================================================
        void print_info() const {
            std::cout << "Operation: SELECT JOIN\n";
            std::cout << "  Dataset: " << dataset_size_ << " messages with FK relationships\n";
        }

        // ====================================================================
        // prepare - Create related records and base records with FKs
        // ====================================================================
        void prepare([[maybe_unused]] int iterations) {
            sqlite3* db = get_db<BaseModel>();
            if (!db)
                return;

            // Clear tables
            sqlite3_exec(db, "DELETE FROM FKMessage", nullptr, nullptr, nullptr);
            sqlite3_exec(db, "DELETE FROM User", nullptr, nullptr, nullptr);

            // Insert users (related model)
            std::vector<RelatedModel> users;
            users.reserve(dataset_size_);
            for (int i = 0; i < dataset_size_; i++) {
                users.push_back(RelatedModel{.id = 0, .name = std::format("User{}", i + 1), .age = 20 + (i % 50)});
            }

            auto user_result = related_qs_.insert(users, storm::orm::statements::InsertOptions{.return_ids = true});
            if (!user_result.has_value()) {
                std::cerr << "Failed to insert users for JOIN benchmark\n";
                return;
            }
            const auto& user_ids = user_result.value();

            // Insert messages with FK references
            std::vector<BaseModel> messages;
            messages.reserve(dataset_size_);
            for (int i = 0; i < dataset_size_; i++) {
                // Create user stubs with just IDs for FK references
                RelatedModel sender{static_cast<int>(user_ids[i % user_ids.size()]), "", 0};
                RelatedModel receiver{static_cast<int>(user_ids[(i + 1) % user_ids.size()]), "", 0};

                messages.push_back(
                        BaseModel{
                                .id = 0, .sender = sender, .receiver = receiver, .text = std::format("Message{}", i + 1)
                        }
                );
            }

            auto msg_result = base_qs_.insert(messages);
            if (!msg_result.has_value()) {
                std::cerr << "Failed to insert messages for JOIN benchmark\n";
            }
        }

        // ====================================================================
        // execute - Storm ORM SELECT with JOIN
        // FAIR COMPARISON: Join setup is done once, then select() is called in loop
        // ====================================================================
        int execute(int iterations) {
            int total_rows = 0;

            // Setup join ONCE before loop (same as raw SQLite prepares statement once)
            base_qs_.template join<FKFieldPtr>();

            for (int i = 0; i < iterations; i++) {
                total_rows += base_qs_.select()->size();
            }
            return total_rows;
        }

        // ====================================================================
        // execute_raw - Raw SQLite SELECT with JOIN using ONLY native sqlite3 API
        // ====================================================================
      private:
        // Row extraction - manual hardcoded extraction for JOIN result
        __attribute__((always_inline)) static BaseModel extract_row(sqlite3_stmt* stmt) {
            BaseModel obj;

            // Extract base model fields (FKMessage: id, sender, receiver, text)
            obj.id   = sqlite3_column_int64(stmt, 0);
            obj.text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            // Extract joined FK object (User: id, name, age)
            // Columns 4, 5, 6 are from the JOIN
            obj.sender.id   = sqlite3_column_int(stmt, 4);
            obj.sender.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            obj.sender.age  = sqlite3_column_int(stmt, 6);

            // receiver is not joined, just set sender_id/receiver_id
            obj.receiver.id = sqlite3_column_int(stmt, 2);

            return obj;
        }

      public:
        int execute_raw(int iterations) {
            sqlite3* db = get_db<BaseModel>();
            if (!db)
                return 0;

            const std::string sql = "SELECT fm.id, fm.sender_id, fm.receiver_id, fm.text, "
                                    "u.id, u.name, u.age "
                                    "FROM FKMessage fm "
                                    "INNER JOIN User u ON fm.sender_id = u.id";

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            int total_rows = 0;
            for (int i = 0; i < iterations; i++) {
                sqlite3_reset(stmt);

                plf::hive<BaseModel> results;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    results.insert(extract_row(stmt));
                }
                total_rows += results.size();
            }

            sqlite3_finalize(stmt);
            return total_rows;
        }
    };

} // namespace storm::benchmark
