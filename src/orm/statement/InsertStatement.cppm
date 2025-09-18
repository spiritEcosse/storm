module;

export module storm.statement.insert;

import <string>;
import <string_view>;
import <vector>;
import <span>;
import <format>;
import <expected>;
import <ranges>;
import <memory>;
import <variant>;

// Import Storm modules
import storm.statement.base;
import storm.expression;
import storm.connection;
import storm.reflect;
import storm.utils;

export namespace storm {

    /**
     * Specialized statement class for SQL INSERT operations
     *
     * @tparam T The model type this statement operates on
     */
    template <typename T> class InsertStatement : public UnifiedStatementBase<InsertStatement<T>, T> {
      private:
        using Base = UnifiedStatementBase<InsertStatement<T>, T>;

      public:
        explicit InsertStatement(std::shared_ptr<Connection> conn) : Base(conn) {}

        /**
         * Execute INSERT for a batch of objects
         *
         * @param objects Span of objects to insert
         * @return Vector of inserted row IDs or an error message
         */
        [[nodiscard]] std::expected<std::vector<int>, std::string> execute(std::span<const T> objects) noexcept {
            if (objects.empty()) {
                return std::vector<int>{};
            }

            return build_sql(objects.size())
                    .and_then([this](const std::string& sql) -> std::expected<void, std::string> {
                        return this->set_sql(sql);
                    })
                    .and_then([this, objects]() -> std::expected<void, std::string> {
                        return bind_parameters(objects);
                    })
                    .and_then([this]() -> std::expected<std::vector<Row>, std::string> {
                        return this->Base::execute_all();
                    })
                    .and_then([](const auto& rows) -> std::expected<std::vector<int>, std::string> {
                        return rows | std::views::transform([](const auto& row) { return row.get_int(0); }) |
                               std::ranges::to<std::vector>();
                    });
        }

      private:
        /**
         * Bind parameters for all objects in the batch
         *
         * @param objects Span of objects to bind parameters from
         * @return Success or error message
         */
        [[nodiscard]] std::expected<void, std::string> bind_parameters(std::span<const T> objects) noexcept {
            int param_index = 1;

            // Bind all field values for each object using compile-time field filtering
            for (const auto& obj : objects) {
                if (auto result = this->bind_fields_with_descriptors(
                            obj, param_index, Base::field_is_not_primary_key_predicate
                    );
                    !result.has_value()) {
                    return std::move(result);
                }
            }

            return {};
        }

        [[nodiscard]] std::expected<std::string, std::string> build_sql(size_t num_objects) const noexcept {
            // Use compile-time field filtering
            constexpr auto fields_result = Base::get_filtered_fields();

            // We know this has a value because of the static_assert
            const auto& field_names = fields_result.value();
            return std::expected<std::string, std::string>{[&]() -> std::string {
                // Create placeholders for one object: (?, ?, ?)
                constexpr auto create_placeholders = [](size_t count) {
                    std::string placeholders = "(";
                    for (size_t i = 0; i < count; ++i) {
                        placeholders += (i > 0 ? ", ?" : "?");
                    }
                    placeholders += ")";
                    return placeholders;
                };

                // Create placeholders for all objects: (?, ?, ?), (?, ?, ?), ...
                std::string all_placeholders;
                std::string placeholders_template = create_placeholders(field_names.size());

                for (size_t i = 0; i < num_objects; ++i) {
                    if (i > 0)
                        all_placeholders += ", ";
                    all_placeholders += placeholders_template;
                }

                std::string sql = std::string{get_base_insert_sql()};
                sql += " (";
                sql += utils::join(field_names, ", ");
                sql += ") VALUES ";
                sql += all_placeholders;
                sql += get_returning_count_clause();
                return sql;
            }()};
        }

        /**
         * Get the RETURNING rowid clause for inserted IDs
         *
         * @return RETURNING rowid clause as a string_view
         */
        [[nodiscard]] consteval std::string_view get_returning_count_clause() const noexcept {
            return " RETURNING rowid";
        }

        /**
         * Generate the INSERT SQL prefix at compile time
         *
         * @return The base INSERT SQL string as a string_view
         */
        [[nodiscard]] static consteval std::string_view get_base_insert_sql() noexcept {
            constexpr auto             table_str = std::string{Base::table_name()};
            constexpr std::string_view prefix    = "INSERT INTO ";

            // Create compile-time concatenated string
            constexpr std::size_t total_size = prefix.size() + table_str.size() + 1;
            constexpr auto        sql_string = [prefix]() constexpr {
                utils::fixed_string<total_size> result{""};

                // Append prefix
                for (char c : prefix) {
                    result.data.push_back(c);
                }

                // Append table name
                for (char c : table_str) {
                    result.data.push_back(c);
                }

                return result;
            }();

            return sql_string.view();
        }

        // Check at compile time that we have fields to insert
        static constexpr bool has_insertable_fields = []() consteval {
            constexpr auto result = Base::get_filtered_fields();
            return result.has_value() && !result.value().empty();
        }();

        // Ensure we have at least one field to insert
        static_assert(has_insertable_fields, "No fields to insert after filtering");
    };

} // namespace storm
