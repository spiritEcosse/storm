#pragma once

/**
 * SELECT WHERE + JOIN Benchmark - JOIN operations with WHERE filtering
 *
 * Tests SELECT performance with JOIN clauses AND WHERE filtering.
 * Combines FK relationship joins with value-based filtering.
 *
 * Workflow:
 * 1. prepare(): Create users and messages with FK relationships
 * 2. execute(): Storm ORM join<FK>().where(...).select()
 * 3. execute_raw(): Raw SQLite SELECT...JOIN...WHERE - native sqlite3 API only
 *
 * FAIR COMPARISON: Both Storm ORM and raw SQLite use prepared statements
 * for JOIN operations with parameter binding for WHERE clause.
 */

#include "base.hpp"
#include <format>
#include <meta>
#include <plf_hive/plf_hive.h>

namespace storm::benchmark {

    using storm::orm::where::field;

    // SELECT WHERE + JOIN benchmark for FK relationships with filtering
    // BaseModel: model with FK field (e.g., FKMessage)
    // RelatedModel: model referenced by FK (e.g., User)
    // FKFieldPtr: compile-time pointer to FK field (e.g., &FKMessage::sender)
    // WhereFieldInfo: compile-time field info for WHERE clause (on RelatedModel)
    // Op: comparison operator as ConstexprString
    // ValueType: type of WHERE clause value
    template <
            typename BaseModel,
            typename RelatedModel,
            auto            FKFieldPtr,
            std::meta::info WhereFieldInfo,
            auto            Op,
            typename ValueType>
    class SelectWhereJoinBenchmark {
      private:
        QuerySet<BaseModel>    base_qs_;
        QuerySet<RelatedModel> related_qs_;
        int                    dataset_size_;
        ValueType              value_;

      public:
        explicit constexpr SelectWhereJoinBenchmark(ValueType value, int dataset_size = 1000)
            : dataset_size_(dataset_size), value_(value) {}

        // ====================================================================
        // print_info - SELECT WHERE + JOIN-specific info
        // ====================================================================
        void print_info() const {
            constexpr std::string_view field_name = std::meta::identifier_of(WhereFieldInfo);
            constexpr std::string_view op_str     = Op.view();

            std::cout << "Operation: SELECT WHERE + JOIN\n";
            std::cout << "  Filter: " << field_name << " " << op_str << " " << value_ << "\n";
            std::cout << "  Dataset: " << dataset_size_ << " messages with FK relationships\n";
        }

        // ====================================================================
        // prepare - Create related records and base records with FKs
        // ====================================================================
        void prepare([[maybe_unused]] int iterations) {
            auto&    conn = storm::QuerySet<BaseModel>::get_default_connection();
            sqlite3* db   = conn->get();
            if (!db)
                return;

            // Clear tables
            sqlite3_exec(db, "DELETE FROM FKMessage", nullptr, nullptr, nullptr);
            sqlite3_exec(db, "DELETE FROM User", nullptr, nullptr, nullptr);

            // Insert users (related model) with varied ages for filtering
            std::vector<RelatedModel> users;
            users.reserve(dataset_size_);
            for (int i = 0; i < dataset_size_; i++) {
                users.push_back(RelatedModel{.id = 0, .name = std::format("User{}", i + 1), .age = 20 + (i % 50)});
            }

            auto user_result = related_qs_.insert(users, storm::orm::statements::InsertOptions{.return_ids = true});
            if (!user_result.has_value()) {
                std::cerr << "Failed to insert users for WHERE + JOIN benchmark\n";
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
                std::cerr << "Failed to insert messages for WHERE + JOIN benchmark\n";
            }
        }

        // ====================================================================
        // build_where_clause - extract WHERE clause building to separate function
        // ====================================================================
      private:
        auto build_where_clause() const {
            constexpr std::string_view op_str = Op.view();

            if constexpr (op_str == ">") {
                return field<WhereFieldInfo>() > value_;
            } else if constexpr (op_str == ">=") {
                return field<WhereFieldInfo>() >= value_;
            } else if constexpr (op_str == "<") {
                return field<WhereFieldInfo>() < value_;
            } else if constexpr (op_str == "<=") {
                return field<WhereFieldInfo>() <= value_;
            } else if constexpr (op_str == "==") {
                return field<WhereFieldInfo>() == value_;
            } else if constexpr (op_str == "!=") {
                return field<WhereFieldInfo>() != value_;
            }
        }

      public:
        // ====================================================================
        // execute - Storm ORM SELECT with JOIN and WHERE clause
        // FAIR COMPARISON: Join and WHERE setup done once, then select() in loop
        // ====================================================================
        int execute(int iterations) {
            int total_rows = 0;

            // Setup join and WHERE ONCE before loop
            auto where_clause = build_where_clause();
            base_qs_.template join<FKFieldPtr>();
            base_qs_.where(where_clause);

            for (int i = 0; i < iterations; i++) {
                auto results = base_qs_.select();
                if (results.has_value()) {
                    total_rows += results.value().size();
                }
            }
            base_qs_.reset();
            return total_rows;
        }

        // ====================================================================
        // execute_raw - Raw SQLite SELECT with JOIN and WHERE using ONLY native sqlite3 API
        // ====================================================================
      private:
        // Get FK field name at compile time
        static constexpr std::string_view get_fk_field_name() {
            constexpr auto member_ptr = FKFieldPtr;
            constexpr auto ptr_info   = ^^member_ptr;

            // Get the data member info from the pointer-to-member
            template for (constexpr auto dm :
                          std::meta::nonstatic_data_members_of(^^BaseModel, std::meta::access_context::unchecked())) {
                // Compare member pointers by checking if this is the FK field
                if constexpr (std::is_same_v<typename[:std::meta::type_of(dm):], RelatedModel>) {
                    // Check if the member offset matches (simplified check)
                    return std::meta::identifier_of(dm);
                }
            }
            return "";
        }

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

        // Build SQL with WHERE clause
        static std::string build_select_sql() {
            constexpr std::string_view where_field = std::meta::identifier_of(WhereFieldInfo);
            constexpr std::string_view op_str      = Op.view();

            std::string sql = "SELECT fm.id, fm.sender_id, fm.receiver_id, fm.text, "
                              "u.id, u.name, u.age "
                              "FROM FKMessage fm "
                              "INNER JOIN User u ON fm.sender_id = u.id "
                              "WHERE u.";
            sql += std::string(where_field);
            sql += " ";
            sql += std::string(op_str);
            sql += " ?";
            return sql;
        }

        // Helper: Bind WHERE clause value using native sqlite3 API
        static void bind_where_value(sqlite3_stmt* stmt, const ValueType& value) {
            if constexpr (std::is_same_v<ValueType, int>) {
                sqlite3_bind_int(stmt, 1, value);
            } else if constexpr (std::is_same_v<ValueType, double>) {
                sqlite3_bind_double(stmt, 1, value);
            } else if constexpr (std::is_same_v<ValueType, bool>) {
                sqlite3_bind_int(stmt, 1, value ? 1 : 0);
            } else if constexpr (std::is_same_v<ValueType, std::string> || std::is_same_v<ValueType, const char*>) {
                if constexpr (std::is_same_v<ValueType, std::string>) {
                    sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
                } else {
                    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
                }
            }
        }

      public:
        int execute_raw(int iterations) {
            auto&    conn = storm::QuerySet<BaseModel>::get_default_connection();
            sqlite3* db   = conn->get();
            if (!db)
                return 0;

            // Build JOIN + WHERE SQL
            const std::string sql = build_select_sql();

            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
                return 0;
            }

            // Bind WHERE value once
            bind_where_value(stmt, value_);

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
