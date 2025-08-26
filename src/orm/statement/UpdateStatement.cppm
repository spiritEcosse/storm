module;

export module storm.statement.update;

import <string>;
import <string_view>;
import <vector>;
import <span>;
import <format>;
import <expected>;
import <ranges>;
import <unordered_map>;
import <memory>;
import <variant>;
import <cstdint>;

// Import Storm modules
import storm.statement.base;
import storm.expression;
import storm.connection;
import storm.reflect;
import storm.utils;
import storm.core_types;

export namespace storm {

    /**
     * Specialized statement class for SQL UPDATE operations
     *
     * @tparam T The model type this statement operates on
     */
    template <typename T> class UpdateStatement : public UnifiedStatementBase<UpdateStatement<T>, T> {
      private:
        using Base = UnifiedStatementBase<UpdateStatement<T>, T>;

      public:
        explicit UpdateStatement(std::shared_ptr<Connection> conn) : Base(conn) {}

        /**
         * Execute UPDATE for a batch of objects by their primary keys
         *
         * @param objects Span of objects to update
         * @return Number of rows affected or an error message
         */
        [[nodiscard]] std::expected<bool, std::string> execute(std::span<const T> objects) noexcept {
            if (objects.empty()) {
                return 0;
            }

            return build_sql(objects.size())
                    .and_then([this](const std::string& sql) -> std::expected<void, std::string> {
                        return this->set_sql(sql);
                    })
                    .and_then([this, objects]() -> std::expected<void, std::string> {
                        return bind_parameters(objects);
                    })
                    .and_then([this]() -> std::expected<Row, std::string> { return this->Base::execute_query(); });
        }

      private:
        /**
         * Bind parameters from the WHERE clause if present
         *
         * @return Success or error message
         */
        [[nodiscard]] std::expected<void, std::string> bind_where_parameters() noexcept {
            if (!this->_where_clause) {
                return {}; // No WHERE clause, nothing to bind
            }

            auto query_result = this->_where_clause->to_query();
            return this->bind_parameters(*query_result.binder);
        }

        /**
         * Bind parameters for all objects in the batch
         *
         * @param objects Span of objects to bind parameters from
         * @return Success or error message
         */
        [[nodiscard]] std::expected<void, std::string> bind_parameters(std::span<const T> objects) noexcept {
            int                              param_index = 1;
            std::expected<void, std::string> result{};

            // For each object, bind its ID and all field values
            for (const auto& obj : objects) {
                // Use monadic operations to chain the binding operations
                result = this->bind_primary_key_field(obj, param_index)
                                 .and_then([this, &obj, &param_index]() -> std::expected<void, std::string> {
                                     // Then bind all field values for the THEN (?, ?) part using
                                     // compile-time filtering
                                     return this->bind_fields_with_descriptors(
                                             obj, param_index, Base::field_is_not_primary_key_predicate
                                     );
                                 });

                // Early return if we have an error
                if (!result.has_value()) {
                    return result;
                }
            }

            // Finally bind all IDs again for the WHERE id IN (?, ?) part
            for (const auto& obj : objects) {
                result = this->bind_primary_key_field(obj, param_index);
                if (!result.has_value()) {
                    return result;
                }
            }

            return result;
        }

        /**
         * Get the RETURNING COUNT(*) clause for affected rows
         *
         * @return RETURNING COUNT(*) clause as a string_view
         */
        [[nodiscard]] consteval std::string_view get_returning_count_clause() const noexcept {
            return "";
        }

        /**
         * Generate the UPDATE SQL prefix at compile time
         *
         * @return The base UPDATE SQL string as a string_view
         */
        [[nodiscard]] static consteval std::string_view get_base_update_sql() noexcept {
            constexpr auto             table_str = std::string{table_name()};
            constexpr std::string_view prefix    = "UPDATE ";
            constexpr std::string_view suffix    = " SET ";

            // Create compile-time concatenated string
            constexpr std::size_t total_size = prefix.size() + table_str.size() + suffix.size() + 1;
            constexpr auto        sql_string = [prefix, suffix]() constexpr {
                utils::fixed_string<total_size> result{""};

                // Append prefix
                for (char c : prefix) {
                    result.data.push_back(c);
                }

                // Append table name
                for (char c : table_str) {
                    result.data.push_back(c);
                }

                // Append suffix
                for (char c : suffix) {
                    result.data.push_back(c);
                }

                return result;
            }();

            return sql_string.view();
        }

        /**
         * Build SQL for batch update operation with CASE expressions
         *
         * @param count Number of objects to update
         * @return SQL string or error message
         */

        [[nodiscard]] std::expected<std::string, std::string> build_sql(size_t num_objects) const noexcept {
            // Use compile-time field filtering
            constexpr auto fields_result = Base::get_filtered_fields();

            // We know this has a value because of the static_assert
            const auto& field_names = fields_result.value();

            // Build the SQL string
            std::string sql;
            // sql.reserve(estimate_sql_size(field_names.size(), num_objects));

            sql = std::string{get_base_update_sql()};

            // Build SET clause with CASE expressions
            bool first_field = true;
            for (const auto& field_name : field_names) {
                if (first_field) {
                    first_field = false;
                } else {
                    std::format_to(std::back_inserter(sql), ", ");
                }

                std::format_to(
                        std::back_inserter(sql),
                        "{} = CASE {} ELSE {} END",
                        field_name,
                        build_case_clauses(num_objects),
                        field_name
                );
            }

            // Add WHERE and RETURNING clauses
            std::format_to(
                    std::back_inserter(sql),
                    " WHERE {} IN ({}){}",
                    this->get_primary_key_field(),
                    build_placeholders(num_objects),
                    this->get_returning_count_clause()
            );

            return sql;
        }

        /**
         * Build CASE clauses for a field in the UPDATE statement
         *
         * @param num_objects Number of objects to update
         * @return String with CASE clauses
         */
        std::string build_case_clauses(size_t num_objects) const {
            std::string clauses;
            clauses.reserve(num_objects * 25); // Estimate size for WHEN clauses

            for (size_t i : std::views::iota(0u, num_objects)) {
                std::format_to(std::back_inserter(clauses), "WHEN {} = ? THEN ? ", this->get_primary_key_field());
            }
            return clauses;
        }

        constexpr std::string build_placeholders(size_t count) const {
            if (count == 0)
                return "";
            if (count == 1)
                return "?";

            // Manual implementation since std::views::join_with is not available in
            // libc++
            std::string result;
            for (size_t i = 0; i < count; ++i) {
                if (i > 0)
                    result += ", ";
                result += "?";
            }
            return result;
        }

        static constexpr bool has_updateable_fields = []() consteval {
            constexpr auto result = Base::get_filtered_fields();
            return result.has_value() && !result.value().empty();
        }();

        static_assert(has_updateable_fields, "No fields to update after filtering");
    };

} // namespace storm
