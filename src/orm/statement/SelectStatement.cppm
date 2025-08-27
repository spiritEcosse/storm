module;

export module storm.statement.select;

// Standard header units
import <string>;
import <string_view>;
import <vector>;
import <array>;
import <span>;
import <format>;
import <expected>;
import <ranges>;
import <memory>;
import <variant>;
import <unordered_map>;
import <map>;
import <cstdint>;
import <iterator>;
import <type_traits>;
import <optional>;
import <functional>;

// Storm modules
import storm.statement.base;
import storm.expression;
import storm.connection;
import storm.reflect;
import storm.utils;
import storm.core_types;
import storm.field_desc;
import storm.where;
import storm.function;

export namespace storm {
    // No dependencies on QuerySet to avoid module cycles.

    // SelectStatement is configured directly by higher-level builders (e.g., QuerySet)

    // Options aggregate to keep SelectStatement's constructor concise
    struct SelectOptions {
        std::vector<std::string> joins{};
        std::vector<FieldDesc>   distinct_fields{};
        std::vector<FieldDesc>   only_fields{};
        std::vector<Function>    functions_set{};
        std::vector<OrderTerm>   order_terms{};
        std::vector<FieldDesc>   group_by_fields{};
        int                      limit  = 0;
        int                      offset = 0;
        std::optional<Where>     where_clause{};
    };

    template <typename T> class SelectStatement : public UnifiedStatementBase<SelectStatement<T>, T> {
      private:
        using Base = UnifiedStatementBase<SelectStatement<T>, T>;

        // Helper to combine field strings and function clauses
        [[nodiscard]] static std::string build_combined_clause(
                const std::vector<std::string>& field_strings, const std::vector<std::string>& function_clauses
        ) {
            if (function_clauses.empty()) {
                return std::format("{}", storm::utils::join(field_strings, ", "));
            }

            if (field_strings.empty()) {
                return std::format("{}", storm::utils::join(function_clauses, ", "));
            }

            return std::format(
                    "{}, {}", storm::utils::join(field_strings, ", "), storm::utils::join(function_clauses, ", ")
            );
        }

        // Build default fields clause using compile-time reflection
        template <refl::reflectable U> [[nodiscard]] static std::string build_default_fields_clause() {
            std::vector<std::string> field_strings;
            field_strings.reserve(refl::reflect<U>::member_count());

            constexpr auto table_name_str = std::string{refl::reflect<U>::get_struct_name()};

            refl::reflect<U>::for_each_member([&]<size_t I>(auto member) {
                const std::string field_name{member.get_name()};
                field_strings.emplace_back(std::format("{}.{}", table_name_str, field_name));
            });

            return storm::utils::join(field_strings, ", ");
        }

      public:
        explicit SelectStatement(std::shared_ptr<Connection> conn) : Base(std::move(conn)) {}

        // Constructor: all options bundled into SelectOptions for consistency
        SelectStatement(std::shared_ptr<Connection> conn, SelectOptions opts)
            : Base(std::move(conn), std::move(opts.where_clause))
            , joins_(std::move(opts.joins))
            , fields_clause_()
            , distinct_override_()
            , limit_(opts.limit)
            , offset_(opts.offset)
            , order_by_sql_(build_order_by_sql(opts.order_terms))
            , group_by_sql_(build_group_by_sql(opts.group_by_fields))
            , distinct_fields_(std::move(opts.distinct_fields))
            , only_fields_(std::move(opts.only_fields))
            , functions_set_(std::move(opts.functions_set)) {}

        // Execute and materialize as objects of T
        [[nodiscard]] std::expected<std::vector<T>, std::string> execute_objects() noexcept {
            return build_sql()
                    .and_then([this](const std::string& sql) -> std::expected<void, std::string> {
                        return this->set_sql(sql);
                    })
                    .and_then([this]() -> std::expected<void, std::string> { return bind_where_parameters(); })
                    .and_then([this]() -> std::expected<std::vector<Row>, std::string> {
                        return this->Base::execute_all();
                    })
                    .and_then([this](const std::vector<Row>& rows) { return rows_to_objects(rows); });
        }

        // Execute and return generic value maps (for select_values)
        using ValueMap = std::map<std::string, SqlValue, std::less<>>;
        [[nodiscard]] std::expected<std::vector<ValueMap>, std::string> execute_values() noexcept {
            return build_sql()
                    .and_then([this](const std::string& sql) -> std::expected<void, std::string> {
                        return this->set_sql(sql);
                    })
                    .and_then([this]() -> std::expected<void, std::string> { return bind_where_parameters(); })
                    .and_then([this]() -> std::expected<std::vector<Row>, std::string> {
                        return this->Base::execute_all();
                    })
                    .and_then([this](const std::vector<Row>& rows) { return rows_to_value_maps(rows); });
        }

      private:
        std::vector<std::string> joins_{};
        std::string              fields_clause_{};
        std::optional<bool>      distinct_override_{};
        int                      limit_  = 0;
        int                      offset_ = 0;
        std::string              order_by_sql_{};
        std::string              group_by_sql_{};
        std::vector<FieldDesc>   distinct_fields_{};
        std::vector<FieldDesc>   only_fields_{};
        std::vector<Function>    functions_set_{};

        // Build the SELECT list (fields/expressions) for type T from QuerySet-like state
        // This centralizes SELECT-list construction in SelectStatement.
        [[nodiscard]] static std::string build_select_list(
                std::span<const FieldDesc> distinct_fields,
                std::span<const FieldDesc> only_fields,
                std::span<const Function>  functions_set
        ) {
            using namespace std::string_literals;

            // Map functions to strings first
            auto function_clauses = functions_set | std::views::transform([](const auto& f) { return f.toStr(); }) |
                                    std::ranges::to<std::vector<std::string>>();

            std::string fields_clause;

            if (!distinct_fields.empty() && only_fields.empty()) {
                // Use distinct fields
                auto field_strings = distinct_fields |
                                     std::views::transform([](const auto& desc) { return desc.full_name(); }) |
                                     std::ranges::to<std::vector<std::string>>();

                fields_clause = build_combined_clause(field_strings, function_clauses);

            } else if (!only_fields.empty()) {
                // Use only fields with optional aliases
                auto field_strings = only_fields | std::views::transform([](const auto& desc) {
                                         return desc.alias.empty()
                                                        ? desc.full_name()
                                                        : std::format("{} AS {}", desc.full_name(), desc.alias);
                                     }) |
                                     std::ranges::to<std::vector<std::string>>();

                fields_clause = build_combined_clause(field_strings, function_clauses);

            } else if (!function_clauses.empty()) {
                // Only functions specified
                fields_clause = std::format("{}", storm::utils::join(function_clauses, ", "));

            } else {
                // Default: use reflection to get all fields
                fields_clause = build_default_fields_clause<T>();
            }

            return fields_clause;
        }

        // Build ORDER BY clause from OrderTerm vector
        [[nodiscard]] static std::string build_order_by_sql(const std::vector<OrderTerm>& terms) {
            if (terms.empty()) {
                return {};
            }

            // Constexpr collation mapping for compile-time optimization
            static constexpr std::array<std::string_view, 4> collation_strings = {"", "BINARY", "NOCASE", "RTRIM"};

            // Estimate string size to minimize reallocations
            size_t estimated_size = 10; // " ORDER BY "
            for (const auto& t : terms) {
                estimated_size += t.table_name.size() + t.field_name.size() + 10; // . space COLLATE space ASC/DESC
                if (t.collation != Collation::NONE) {
                    estimated_size += 10 + collation_strings[static_cast<size_t>(t.collation)].size();
                }
            }
            if (!terms.empty()) {
                estimated_size += (terms.size() - 1) * 2; // ", " separators
            }

            std::string order_by_sql;
            order_by_sql.reserve(estimated_size);

            std::format_to(std::back_inserter(order_by_sql), " ORDER BY ");
            bool first = true;
            for (const auto& t : terms) {
                if (!first) {
                    std::format_to(std::back_inserter(order_by_sql), ", ");
                }
                first = false;
                std::format_to(std::back_inserter(order_by_sql), "{}.{}", t.table_name, t.field_name);
                auto coll = collation_strings[static_cast<size_t>(t.collation)];
                if (!coll.empty()) {
                    std::format_to(std::back_inserter(order_by_sql), " COLLATE {}", coll);
                }
                if (t.ascending) {
                    std::format_to(std::back_inserter(order_by_sql), " ASC");
                } else {
                    std::format_to(std::back_inserter(order_by_sql), " DESC");
                }
            }
            return order_by_sql;
        }

        // Build GROUP BY clause from FieldDesc vector
        [[nodiscard]] static std::string build_group_by_sql(const std::vector<FieldDesc>& fields) {
            if (fields.empty()) {
                return {};
            }

            std::string group_by_sql = " GROUP BY ";
            bool        first        = true;
            for (const auto& desc : fields) {
                if (!first)
                    group_by_sql += ", ";
                first = false;
                group_by_sql += desc.full_name();
            }
            return group_by_sql;
        }

        [[nodiscard]] std::expected<void, std::string> bind_where_parameters() noexcept {
            if (!this->_where_clause)
                return {};
            auto query_result = this->_where_clause->to_query();
            auto r            = this->bind_parameters(*query_result.binder);
            if (!r)
                return std::unexpected(r.error());
            return {};
        }

        [[nodiscard]] std::expected<std::string, std::string> build_sql() noexcept {
            // Lazily construct fields clause if not provided
            if (fields_clause_.empty()) {
                fields_clause_ = build_select_list(
                        std::span<const FieldDesc>{distinct_fields_},
                        std::span<const FieldDesc>{only_fields_},
                        std::span<const Function>{functions_set_}
                );
            }

            if (fields_clause_.empty()) {
                return std::unexpected{"SelectStatement: fields clause is empty"};
            }

            std::string sql;
            sql.reserve(128 + fields_clause_.size());

            // SELECT + DISTINCT (auto-infer unless manually overridden) + fields
            const bool auto_distinct = (!distinct_fields_.empty()) && (only_fields_.empty());
            const bool use_distinct  = distinct_override_.value_or(auto_distinct);
            if (use_distinct) {
                std::format_to(
                        std::back_inserter(sql), "SELECT DISTINCT {} FROM {}", fields_clause_, this->table_name()
                );
            } else {
                std::format_to(std::back_inserter(sql), "SELECT {} FROM {}", fields_clause_, this->table_name());
            }

            // JOINs
            if (!joins_.empty()) {
                for (const auto& j : joins_) {
                    std::format_to(std::back_inserter(sql), " {}", j);
                }
            }

            // WHERE
            if (this->_where_clause) {
                auto q = this->_where_clause->to_query();
                std::format_to(std::back_inserter(sql), " WHERE {}", q.sql);
            }

            // GROUP BY
            std::format_to(std::back_inserter(sql), "{}", group_by_sql_);

            // ORDER BY
            std::format_to(std::back_inserter(sql), "{}", order_by_sql_);

            if (limit_ > 0) {
                std::format_to(std::back_inserter(sql), " LIMIT {}", limit_);
            }
            if (offset_ > 0) {
                std::format_to(std::back_inserter(sql), " OFFSET {}", offset_);
            }

            return sql;
        }

        [[nodiscard]] static std::expected<std::vector<T>, std::string> rows_to_objects(const std::vector<Row>& rows) {
            std::vector<T> out;
            out.reserve(rows.size());

            for (const auto& row : rows) {
                auto obj_res = row_to_object(row);
                if (!obj_res)
                    return std::unexpected(obj_res.error());
                out.emplace_back(std::move(*obj_res));
            }
            return out;
        }

        [[nodiscard]] static std::expected<T, std::string> row_to_object(const Row& row) {
            T obj{};

            // Build column name -> index map
            const int                                 col_count = row.get_column_count();
            std::unordered_map<std::string_view, int> col_index;
            col_index.reserve(static_cast<size_t>(col_count));
            for (int i = 0; i < col_count; ++i) {
                col_index.emplace(row.get_column_name(i), i);
            }

            // Assign by member names
            std::vector<std::string> errors;
            refl::reflect<T>::for_each_member([&]<size_t I>(auto member) {
                const std::string_view name = member.get_name();
                if (auto it = col_index.find(name); it != col_index.end()) {
                    const int idx    = it->second;
                    using MemberType = typename decltype(member)::member_type;
                    if constexpr (std::is_same_v<MemberType, std::string>) {
                        auto res = decltype(member)::set(obj, row.get_text(idx));
                        if (!res)
                            errors.push_back(std::format("set '{}' failed: {}", name, res.error()));
                    } else if constexpr (std::is_same_v<MemberType, bool>) {
                        auto res = decltype(member)::set(obj, row.get_int(idx) != 0);
                        if (!res)
                            errors.push_back(std::format("set '{}' failed: {}", name, res.error()));
                    } else if constexpr (std::is_floating_point_v<MemberType>) {
                        auto res = decltype(member)::set(obj, static_cast<MemberType>(row.get_double(idx)));
                        if (!res)
                            errors.push_back(std::format("set '{}' failed: {}", name, res.error()));
                    } else if constexpr (std::is_integral_v<MemberType>) {
                        auto res = decltype(member)::set(obj, static_cast<MemberType>(row.get_int(idx)));
                        if (!res)
                            errors.push_back(std::format("set '{}' failed: {}", name, res.error()));
                    } else if constexpr (std::is_enum_v<MemberType>) {
                        auto res = decltype(member)::set(obj, static_cast<MemberType>(row.get_int(idx)));
                        if (!res)
                            errors.push_back(std::format("set '{}' failed: {}", name, res.error()));
                    } else {
                        // Unsupported type - ignore silently for now
                    }
                }
            });

            if (!errors.empty()) {
                return std::unexpected(storm::utils::join(errors, "; "));
            }
            return obj;
        }

        [[nodiscard]] static std::expected<std::vector<ValueMap>, std::string>
        rows_to_value_maps(const std::vector<Row>& rows) {
            std::vector<ValueMap> out;
            out.reserve(rows.size());

            for (const auto& row : rows) {
                ValueMap  m;
                const int col_count = row.get_column_count();
                for (int i = 0; i < col_count; ++i) {
                    const std::string& name = row.get_column_name(i);
                    const int          typ  = row.get_column_type(i);
                    switch (typ) {
                    case 1: // SQLITE_INTEGER
                        m.emplace(name, static_cast<int64_t>(row.get_int(i)));
                        break;
                    case 2: // SQLITE_FLOAT
                        m.emplace(name, row.get_double(i));
                        break;
                    case 3: // SQLITE_TEXT
                        m.emplace(name, row.get_text(i));
                        break;
                    case 5: // SQLITE_NULL
                        m.emplace(name, std::nullopt);
                        break;
                    default:
                        // BLOB and others -> not supported, insert monostate
                        m.emplace(name, std::monostate{});
                        break;
                    }
                }
                out.emplace_back(std::move(m));
            }
            return out;
        }
    };

} // namespace storm
