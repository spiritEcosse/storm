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
import <array>;

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
            constexpr auto field_names_array = Base::get_filtered_fields();

            // Filter out empty string_views at runtime
            std::vector<std::string_view> field_names;
            for (const auto& name : field_names_array) {
                if (!name.empty()) {
                    field_names.push_back(name);
                }
            }

            // Create placeholders for one object: (?, ?, ?)
            auto create_placeholders = [](size_t count) {
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

            std::string sql = get_base_insert_sql();
            sql += " (";
            sql += utils::join(field_names, ", ");
            sql += ") VALUES ";
            sql += all_placeholders;
            sql += get_returning_count_clause();

            return std::expected<std::string, std::string>{sql};
        }

        /**
         * Get the RETURNING rowid clause for inserted IDs
         *
         * @return RETURNING rowid clause as a string_view
         */
        [[nodiscard]] static consteval std::string_view get_returning_count_clause() noexcept {
            return " RETURNING rowid";
        }

        /**
         * Generate the INSERT SQL prefix at compile time
         *
         * @return The base INSERT SQL string as a string_view
         */
        [[nodiscard]] static std::string get_base_insert_sql() noexcept {
            // Use std::string constructor with string_view to avoid temporaries
            return std::string("INSERT INTO ") + std::string(refl::reflect<T>::descriptor::name);
        }

        // Runtime check is done in build_sql via get_filtered_fields
    };

} // namespace storm
