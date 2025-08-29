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
import <print>; // C++23
import <algorithm>;
import <utility>;

// Storm modules
import storm.statement.base;
import storm.expression;
import storm.connection;
import storm.reflect;
import storm.utils;
import storm.core_types;
import storm.basic_types;
import storm.field_desc;
import storm.where;
import storm.function;

export namespace storm {

    // Compile-time builders using storm::utils::fixed_string
    template <typename T> consteval auto build_select_fields_ct() -> storm::utils::fixed_string<2048> {
        storm::utils::fixed_string<2048> result{};
        std::size_t                      pos = 0;

        constexpr auto table_name = refl::reflect<T>::get_struct_name();

        auto append_sv = [&](std::string_view sv) {
            for (char c : sv) {
                if (pos + 1 >= sizeof(result.data))
                    break;
                result.data[pos++] = c;
            }
        };

        auto append_char = [&](char c) {
            if (pos + 1 < sizeof(result.data))
                result.data[pos++] = c;
        };

        refl::reflect<T>::for_each_member([&]<std::size_t I>(auto member) {
            if constexpr (I > 0) {
                append_char(',');
                append_char(' ');
            }
            append_sv(table_name);
            append_char('.');
            append_sv(member.get_name());
        });

        // null-terminate
        result.data[pos] = '\0';
        return result;
    }

    template <typename T> consteval auto build_base_query_ct() -> storm::utils::fixed_string<2080> {
        storm::utils::fixed_string<2080> result{};
        std::size_t                      pos = 0;

        constexpr auto table_name = refl::reflect<T>::get_struct_name();
        constexpr auto fields     = build_select_fields_ct<T>();

        auto append_sv = [&](std::string_view sv) constexpr {
            for (char c : sv) {
                if (pos + 1 >= sizeof(result.data))
                    break;
                result.data[pos++] = c;
            }
        };

        auto append_char = [&](char c) constexpr {
            if (pos + 1 < sizeof(result.data))
                result.data[pos++] = c;
        };

        append_sv("SELECT ");
        append_sv(fields.view());
        append_sv(" FROM ");
        append_sv(table_name);

        result.data[pos] = '\0';
        return result;
    }

    // Compile-time SQL builder returning fixed_string
    template <typename T> struct CompileTimeSqlBuilder {
        static consteval auto build_select_fields() {
            return build_select_fields_ct<T>();
        }
        static consteval auto build_base_query() {
            return build_base_query_ct<T>();
        }
    };

    // C++23: Improved SelectOptions with explicit object parameter
    struct SelectOptions {
        std::vector<std::string_view> joins{};
        std::vector<FieldDescView> distinct_fields{};
        std::vector<FieldDescView> only_fields{};
        std::vector<Function>      functions_set{};
        std::vector<OrderTerm>     order_terms{};
        std::vector<FieldDescView> group_by_fields{};
        int                        limit{};
        int                        offset{};
        std::optional<Where>       where_clause{};

        // C++23: Deducing this for perfect forwarding
        constexpr auto&& with_limit(this auto&& self, int value) noexcept {
            self.limit = value;
            return std::forward<decltype(self)>(self);
        }

        constexpr auto&& with_offset(this auto&& self, int value) noexcept {
            self.offset = value;
            return std::forward<decltype(self)>(self);
        }

        constexpr auto&& with_where(this auto&& self, Where clause) noexcept {
            self.where_clause = std::move(clause);
            return std::forward<decltype(self)>(self);
        }
    };

    template <typename T> class SelectStatement : public UnifiedStatementBase<SelectStatement<T>, T> {
      private:
        using Base = UnifiedStatementBase<SelectStatement<T>, T>;

        // C++23: Cached compile-time SQL strings (fixed_string)
        inline static constexpr auto base_sql_       = CompileTimeSqlBuilder<T>::build_base_query();
        inline static constexpr auto default_fields_ = CompileTimeSqlBuilder<T>::build_select_fields();

        // Join range into a single string (runtime)
        template <std::ranges::range R>
        [[nodiscard]] static std::string join_range(R&& range, std::string_view delimiter) {
            if (std::ranges::empty(range))
                return {};

            // Calculate total size for single allocation
            size_t total_size = 0;
            size_t count      = 0;
            for (const auto& item : range) {
                total_size += item.size();
                ++count;
            }
            total_size += (count - 1) * delimiter.size();

            std::string result;
            result.reserve(total_size);

            auto it = std::ranges::begin(range);
            result += *it;
            for (++it; it != std::ranges::end(range); ++it) {
                result += delimiter;
                result += *it;
            }

            return result;
        }

      public:
        explicit SelectStatement(std::shared_ptr<Connection> conn) : Base(std::move(conn)) {}

        SelectStatement(std::shared_ptr<Connection> conn, SelectOptions opts)
            : Base(std::move(conn), std::move(opts.where_clause))
            , joins_(std::move(opts.joins))
            , limit_(opts.limit)
            , offset_(opts.offset)
            , distinct_fields_(std::move(opts.distinct_fields))
            , only_fields_(std::move(opts.only_fields))
            , functions_set_(std::move(opts.functions_set))
            , order_by_sql_(build_order_by_sql(opts.order_terms))
            , group_by_sql_(build_group_by_sql(opts.group_by_fields)) {}

        // C++23: Simplified monadic execution
        [[nodiscard]] std::expected<std::vector<T>, std::string> execute_objects() noexcept {
            return build_sql().and_then(
                                      [this](const std::string& sql) { return this->set_sql(sql); }
            ).and_then([this]() {
                 return bind_where_parameters();
             }).and_then([this]() {
                   return this->Base::execute_all();
               }).and_then([](const std::vector<Row>& rows) { return rows_to_objects(rows); });
        }

        using ValueMap = std::map<std::string, SqlValue, std::less<>>;
        [[nodiscard]] std::expected<std::vector<ValueMap>, std::string> execute_values() noexcept {
            return build_sql().and_then(
                                      [this](const std::string& sql) { return this->set_sql(sql); }
            ).and_then([this]() {
                 return bind_where_parameters();
             }).and_then([this]() {
                   return this->Base::execute_all();
               }).and_then([](const std::vector<Row>& rows) { return rows_to_value_maps(rows); });
        }

      private:
        std::vector<std::string_view> joins_;
        mutable std::string        fields_clause_; // Cache for repeated builds
        std::optional<bool>        distinct_override_;
        int                        limit_{};
        int                        offset_{};
        std::vector<FieldDescView> distinct_fields_;
        std::vector<FieldDescView> only_fields_;
        std::vector<Function>      functions_set_;
        std::string                order_by_sql_;
        std::string                group_by_sql_;

        [[nodiscard]] static std::string build_select_list(
                std::span<const FieldDescView> distinct_fields,
                std::span<const FieldDescView> only_fields,
                std::span<const Function>      functions_set
        ) {
            // Use ranges views for lazy evaluation
            auto function_clauses = functions_set | std::views::transform([](const Function& f) { return f.toStr(); });

            if (!distinct_fields.empty() && only_fields.empty()) {
                auto field_strings = distinct_fields | std::views::transform([](const FieldDescView& desc) {
                                         return std::format("{}.{}", desc.table, desc.field);
                                     });

                return join_combined_clauses(field_strings, function_clauses);

            } else if (!only_fields.empty()) {
                auto field_strings =
                        only_fields | std::views::transform([](const FieldDescView& desc) {
                            return desc.alias.empty() ? std::format("{}.{}", desc.table, desc.field)
                                                      : std::format("{}.{} AS {}", desc.table, desc.field, desc.alias);
                        });

                return join_combined_clauses(field_strings, function_clauses);

            } else if (!std::ranges::empty(function_clauses)) {
                return join_range(function_clauses, ", ");
            } else {
                return std::string{default_fields_.view()};
            }
        }

        // Helper for combining field and function clauses
        template <std::ranges::range R1, std::ranges::range R2>
        static std::string join_combined_clauses(R1&& fields, R2&& functions) {
            const bool has_fields    = !std::ranges::empty(fields);
            const bool has_functions = !std::ranges::empty(functions);

            if (!has_functions)
                return join_range(fields, ", ");
            if (!has_fields)
                return join_range(functions, ", ");

            return std::format("{}, {}", join_range(fields, ", "), join_range(functions, ", "));
        }

        // C++23: consteval for compile-time lookup table
        [[nodiscard]] static consteval auto get_collation_strings() noexcept {
            return std::to_array<std::string_view>({"", "BINARY", "NOCASE", "RTRIM"});
        }

        [[nodiscard]] static std::string build_order_by_sql(std::span<const OrderTerm> terms) {
            if (terms.empty())
                return {};

            constexpr auto collation_strings = get_collation_strings();

            // Better size estimation
            size_t      estimated_size = 10 + terms.size() * 30;
            std::string result;
            result.reserve(estimated_size);
            result += " ORDER BY ";

            for (size_t i = 0; const auto& term : terms) {
                if (i++ > 0)
                    result += ", ";

                std::format_to(std::back_inserter(result), "{}.{}", term.table_name, term.field_name);

                const auto collation = collation_strings[static_cast<size_t>(term.collation)];
                if (!collation.empty()) {
                    std::format_to(std::back_inserter(result), " COLLATE {}", collation);
                }

                result += term.ascending ? " ASC" : " DESC";
            }

            return result;
        }

        [[nodiscard]] static std::string build_group_by_sql(std::span<const FieldDescView> fields) {
            if (fields.empty())
                return {};

            auto field_names = fields | std::views::transform([](const FieldDescView& desc) {
                                   return std::format("{}.{}", desc.table, desc.field);
                               });

            return std::format(" GROUP BY {}", join_range(field_names, ", "));
        }

        [[nodiscard]] std::expected<void, std::string> bind_where_parameters() noexcept {
            return this->_where_clause ? this->_where_clause->to_query().and_then([this](const auto& query_result) {
                return this->bind_parameters(*query_result.binder);
            })
                                       : std::expected<void, std::string>{};
        }

        // C++23: Optimized SQL building with compile-time fast path
        [[nodiscard]] std::expected<std::string, std::string> build_sql() noexcept {
            if (fields_clause_.empty()) {
                fields_clause_ = build_select_list(distinct_fields_, only_fields_, functions_set_);
            }

            if (fields_clause_.empty()) {
                return std::unexpected{"SelectStatement: fields clause is empty"};
            }

            // Fast path for simple queries using compile-time SQL
            const bool is_simple_query = (fields_clause_ == default_fields_.view()) && joins_.empty() &&
                                         !this->_where_clause && group_by_sql_.empty() && order_by_sql_.empty() &&
                                         limit_ == 0 && offset_ == 0;

            if (is_simple_query) {
                return std::string{base_sql_.view()};
            }

            // Runtime assembly for complex queries
            const bool auto_distinct = !distinct_fields_.empty() && only_fields_.empty();
            const bool use_distinct  = distinct_override_.value_or(auto_distinct);

            std::string sql;
            sql.reserve(512); // Larger default reservation

            sql = std::format(
                    "SELECT{} {} FROM {}", use_distinct ? " DISTINCT" : "", fields_clause_, this->table_name()
            );

            // Append clauses efficiently
            for (const auto& join : joins_) {
                sql.push_back(' ');
                sql.append(join);
            }

            if (this->_where_clause) {
                if (auto query_result = this->_where_clause->to_query()) {
                    std::format_to(std::back_inserter(sql), " WHERE {}", query_result->sql);
                }
            }

            sql += group_by_sql_;
            sql += order_by_sql_;

            if (limit_ > 0) {
                std::format_to(std::back_inserter(sql), " LIMIT {}", limit_);
            }
            if (offset_ > 0) {
                std::format_to(std::back_inserter(sql), " OFFSET {}", offset_);
            }

            return sql;
        }

        // C++23: Simplified type mapping with constexpr
        template <typename MemberType>
        static std::expected<void, std::string>
        set_member_value(T& obj, auto member, const Row& row, int idx, std::string_view name) noexcept {
            try {
                if constexpr (std::same_as<MemberType, std::string>) {
                    return member.set(obj, row.get_text(idx));
                } else if constexpr (std::same_as<MemberType, bool>) {
                    return member.set(obj, row.get_int(idx) != 0);
                } else if constexpr (std::floating_point<MemberType>) {
                    return member.set(obj, static_cast<MemberType>(row.get_double(idx)));
                } else if constexpr (std::integral<MemberType>) {
                    return member.set(obj, static_cast<MemberType>(row.get_int(idx)));
                } else if constexpr (std::is_enum_v<MemberType>) {
                    return member.set(obj, static_cast<MemberType>(row.get_int(idx)));
                } else {
                    return std::unexpected{std::format("Unsupported type for field '{}'", name)};
                }
            } catch (const std::exception& e) {
                return std::unexpected{std::format("Failed to set '{}': {}", name, e.what())};
            }
        }

        [[nodiscard]] static std::expected<std::vector<T>, std::string> rows_to_objects(std::span<const Row> rows) {
            std::vector<T> result;
            result.reserve(rows.size());

            for (const auto& row : rows) {
                auto obj = row_to_object(row);
                if (!obj)
                    return std::unexpected{obj.error()};
                result.emplace_back(std::move(*obj));
            }

            return result;
        }

        [[nodiscard]] static std::expected<T, std::string> row_to_object(const Row& row) {
            T obj{};

            const int                                 col_count = row.get_column_count();
            std::unordered_map<std::string_view, int> col_index;
            col_index.reserve(static_cast<size_t>(col_count));

            for (int i = 0; i < col_count; ++i) {
                col_index.emplace(row.get_column_name(i), i);
            }

            std::vector<std::string> errors;

            refl::reflect<T>::for_each_member([&]<size_t I>(auto member) {
                const auto name = member.get_name();
                if (auto it = col_index.find(name); it != col_index.end()) {
                    using MemberType = typename decltype(member)::member_type;

                    if (auto result = set_member_value<MemberType>(obj, member, row, it->second, name); !result) {
                        errors.emplace_back(result.error());
                    }
                }
            });

            return errors.empty() ? std::expected<T, std::string>{std::move(obj)}
                                  : std::unexpected{join_range(errors, "; ")};
        }

        // C++23: constexpr SQLite constants
        static constexpr int SQLITE_INTEGER = 1;
        static constexpr int SQLITE_FLOAT   = 2;
        static constexpr int SQLITE_TEXT    = 3;
        static constexpr int SQLITE_NULL    = 5;

        [[nodiscard]] static std::expected<std::vector<ValueMap>, std::string>
        rows_to_value_maps(std::span<const Row> rows) {
            std::vector<ValueMap> result;
            result.reserve(rows.size());

            for (const auto& row : rows) {
                ValueMap  map;
                const int col_count = row.get_column_count();

                for (int i = 0; i < col_count; ++i) {
                    const auto& name = row.get_column_name(i);

                    // C++23: Simplified switch with structured bindings potential
                    switch (const int type = row.get_column_type(i)) {
                    case SQLITE_INTEGER:
                        map.emplace(name, static_cast<int64_t>(row.get_int(i)));
                        break;
                    case SQLITE_FLOAT:
                        map.emplace(name, row.get_double(i));
                        break;
                    case SQLITE_TEXT:
                        map.emplace(name, row.get_text(i));
                        break;
                    case SQLITE_NULL:
                        map.emplace(name, std::nullopt);
                        break;
                    default:
                        map.emplace(name, std::monostate{});
                        break;
                    }
                }
                result.emplace_back(std::move(map));
            }

            return result;
        }
    };

} // namespace storm
