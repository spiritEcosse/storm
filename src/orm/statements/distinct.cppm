module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_distinct;

import storm_db_concept;
import storm_orm_statements_base;
import storm_orm_statements_join;
import storm_orm_utilities;
import storm_orm_where;

import <expected>;
import <string>;
import <vector>;
import <tuple>;
import <array>;
import <meta>;
import <optional>;
import <memory>;

export namespace storm::orm::statements {

    // Import utilities for compile-time SQL generation
    using storm::orm::utilities::ConstexprString;

    // DistinctStatement - executes SELECT DISTINCT on specified field(s) and returns tuple data
    // Supports 1+ fields with compile-time type safety
    // Always generates DISTINCT queries (for aggregates, use separate AggregateStatement)
    //
    // API: Use ^^ operator to pass reflected field information directly
    // Example: qs.distinct<^^Person::name>().select()
    //          qs.distinct<^^Person::name, ^^Person::age>().select()
    template <typename T, storm::db::DatabaseConnection ConnType, std::meta::info... FieldInfos>
        requires(sizeof...(FieldInfos) > 0)
    class DistinctStatement : private BaseStatement<T> {
        using Base = BaseStatement<T>;

      public:
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        static constexpr size_t NumFields = sizeof...(FieldInfos);

      private:
        // Field information is already std::meta::info - no conversion needed!
        // This eliminates the need for __PRETTY_FUNCTION__ string parsing
        static constexpr auto member_infos_ = std::array{FieldInfos...};

        // Deduce field types from member_info array
        template <size_t... Is> static consteval auto get_field_types_helper(std::index_sequence<Is...>) {
            return std::tuple<std::remove_cvref_t<decltype(std::declval<T>().[:member_infos_[Is]:])>...>{};
        }

        using FieldTypesTuple = decltype(get_field_types_helper(std::make_index_sequence<NumFields>{}));

        // Calculate field size at compile-time
        template <size_t I> static consteval size_t get_field_size() {
            size_t         size       = std::meta::identifier_of(member_infos_[I]).size();
            constexpr auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member_infos_[I]);
            if constexpr (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                size += 3; // "_id"
            }
            if constexpr (I > 0) {
                size += 2; // ", "
            }
            return size;
        }

        // Calculate total size of all fields
        template <size_t... Is> static consteval size_t calculate_field_list_size(std::index_sequence<Is...>) {
            return (get_field_size<Is>() + ...);
        }

        // Compile-time field list generation (returns ConstexprString)
        template <size_t... Is> static consteval auto build_field_list_constexpr(std::index_sequence<Is...>) {
            constexpr size_t total_size = calculate_field_list_size(std::make_index_sequence<NumFields>{});
            ConstexprString<total_size + 10> result;
            auto                             append_field = [&result]<size_t I>() {
                if constexpr (I > 0) {
                    result.append(", ");
                }
                // Check if this field is a FK - if so, use column name (field_name_id)
                constexpr auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member_infos_[I]);
                if constexpr (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                    result.append(std::meta::identifier_of(member_infos_[I]));
                    result.append("_id");
                } else {
                    result.append(std::meta::identifier_of(member_infos_[I]));
                }
            };
            (append_field.template operator()<Is>(), ...);
            return result;
        }

        // Calculate SQL size at compile-time
        static consteval size_t calculate_select_sql_size() {
            constexpr auto field_list = build_field_list_constexpr(std::make_index_sequence<NumFields>{});
            size_t         size       = 0;
            size += 16; // "SELECT DISTINCT " (max length, DISTINCT is optional)
            size += field_list.len;
            size += 6; // " FROM "
            size += Base::table_name_.size();
            size += 1; // null terminator
            return size;
        }

        // Build SELECT DISTINCT at compile-time
        static consteval auto build_distinct_sql_array() {
            constexpr size_t          sql_size = calculate_select_sql_size() + 50; // Safety buffer
            ConstexprString<sql_size> result;

            result.append("SELECT DISTINCT ");
            constexpr auto field_list = build_field_list_constexpr(std::make_index_sequence<NumFields>{});
            result.append(field_list);
            result.append(" FROM ");
            result.append(Base::table_name_);

            return result;
        }

        // Pre-computed SQL generated at compile-time (always DISTINCT)
        static constexpr auto distinct_sql_array = build_distinct_sql_array();

      public:
        // Result type: vector of single field OR vector of tuple
        using ResultType = std::conditional_t<
                NumFields == 1,
                std::vector<std::tuple_element_t<0, FieldTypesTuple>>,
                std::vector<FieldTypesTuple>>;

        explicit DistinctStatement(ConnType& conn) : conn_(conn) {}

        // Alias for execute() - provides familiar QuerySet-like API
        [[nodiscard]] auto select() -> std::expected<ResultType, Error> {
            return execute();
        }

        // Execute SELECT DISTINCT query on the specified field(s)
        [[nodiscard]] auto execute() -> std::expected<ResultType, Error> {
            // Use compile-time generated SQL (always includes DISTINCT)
            static const std::string sql{distinct_sql_array.data.data(), distinct_sql_array.len};

            // Cache statement on first use (following SelectStatement pattern)
            if (!cached_stmt_) {
                auto prepare_result = conn_.prepare_cached(sql);
                if (!prepare_result) [[unlikely]] {
                    return std::unexpected(prepare_result.error());
                }
                cached_stmt_ = *prepare_result;
            }

            // OPTIMIZATION: Hybrid allocation strategy based on field complexity
            // - Single field: resize() is 1.7x faster (cheap default construction)
            // - Multi-field: reserve() + emplace_back() avoids pre-allocating tuples with heap members
            ResultType results;
            int        step_result = Statement::NO_MORE_ROWS;

            if constexpr (NumFields == 1) {
                // Single field: use resize() optimization (pre-construct for direct assignment)
                results.resize(1000);

                size_t row_count = 0;
                using FieldType  = std::tuple_element_t<0, FieldTypesTuple>;

                // Fetch rows into pre-constructed elements
                while ((step_result = cached_stmt_->step_raw()) == Statement::ROW_AVAILABLE &&
                       row_count < results.size()) {
                    results[row_count] = Base::template extract_column_value<FieldType>(*cached_stmt_, 0);
                    row_count++;
                }

                // Handle overflow
                while (step_result == Statement::ROW_AVAILABLE) {
                    if (row_count >= results.size()) {
                        results.resize(results.size() * 2);
                    }
                    results[row_count] = Base::template extract_column_value<FieldType>(*cached_stmt_, 0);
                    row_count++;
                    step_result = cached_stmt_->step_raw();
                }

                results.resize(row_count);
            } else {
                // Multi-field: use reserve() + emplace_back() to avoid pre-constructing tuples
                // DISTINCT typically returns fewer rows (avg ~100), tuples have heap overhead
                results.reserve(100);

                // Fetch rows with in-place construction
                while ((step_result = cached_stmt_->step_raw()) == Statement::ROW_AVAILABLE) {
                    emplace_tuple_from_columns(results, std::make_index_sequence<NumFields>{});
                }
            }

            // Check for errors
            if (step_result != Statement::NO_MORE_ROWS) {
                cached_stmt_->reset();
                return std::unexpected(Error{step_result, cached_stmt_->get_error_message()});
            }

            cached_stmt_->reset();
            return results;
        }

      private:
        // OPTIMIZATION: Emplace tuple by extracting columns in-place (for reserve() strategy)
        // Constructs tuple directly in vector without intermediate temporaries
        // Template parameter R delays evaluation until method is called (avoids void& when NumFields == 0)
        template <size_t... Is, typename R = ResultType>
        void emplace_tuple_from_columns(R& results, std::index_sequence<Is...>)
            requires(NumFields > 0)
        {
            results.emplace_back(
                    Base::template extract_column_value<std::tuple_element_t<Is, FieldTypesTuple>>(*cached_stmt_, Is)...
            );
        }

        ConnType&          conn_;
        mutable Statement* cached_stmt_ = nullptr; // Statement caching for repeated DISTINCT queries
    };

} // namespace storm::orm::statements
