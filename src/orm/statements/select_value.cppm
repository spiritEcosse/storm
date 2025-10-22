module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_select_value;

import storm_db_concept;
import storm_orm_statements_base;

import <expected>;
import <string>;
import <vector>;
import <tuple>;
import <array>;
import <meta>;

export namespace storm::orm::statements {

    // SelectValueStatement - executes SELECT DISTINCT on specified field(s) and returns tuple data
    // Supports 1+ fields with compile-time type safety
    template <typename T, storm::db::DatabaseConnection ConnType, auto... FieldPtrs>
    class SelectValueStatement : private BaseStatement<T> {
        using Base = BaseStatement<T>;

      public:
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        static constexpr size_t NumFields = sizeof...(FieldPtrs);
        static_assert(NumFields >= 1, "SelectValueStatement requires at least one field");

      private:
        // Helper to check if a field pointer is meta::info
        template <auto FP>
        static constexpr bool is_meta_info = std::is_same_v<decltype(FP), std::meta::info>;

        // Get member_info for a single field pointer (meta::info or member pointer)
        template <auto FP>
        static consteval std::meta::info get_member_info() {
            if constexpr (is_meta_info<FP>) {
                // FP is already a meta::info (default PK case)
                return FP;
            } else {
                // FP is a member pointer - find matching meta::info by type
                using MemberPtrFieldType = std::remove_cvref_t<decltype(std::declval<T>().*FP)>;
                auto members = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());

                // Get the primary key to exclude it from matching (unless it's the only match)
                constexpr auto pk = Base::primary_key_;

                // Normalize the target field type
                constexpr auto target_type = std::meta::dealias(^^MemberPtrFieldType);

                // First pass: find matching type that's NOT the primary key
                for (auto member : members) {
                    auto field_type = std::meta::dealias(std::meta::type_of(member));
                    if (field_type == target_type && member != pk) {
                        return member;
                    }
                }

                // Second pass: if no non-PK match, allow PK match
                for (auto member : members) {
                    auto field_type = std::meta::dealias(std::meta::type_of(member));
                    if (field_type == target_type) {
                        return member;
                    }
                }

                // Fallback to first member (should never reach here)
                return members[0];
            }
        }

        // Convert variadic FieldPtrs to array of member_info
        static constexpr auto member_infos_ = std::array{get_member_info<FieldPtrs>()...};

        // Deduce field types from member_info array
        template <size_t... Is>
        static consteval auto get_field_types_helper(std::index_sequence<Is...>) {
            return std::tuple<std::remove_cvref_t<decltype(std::declval<T>().[:member_infos_[Is]:])>...>{};
        }

        using FieldTypesTuple = decltype(get_field_types_helper(std::make_index_sequence<NumFields>{}));

      public:
        // Result type: vector of single field OR vector of tuple
        using ResultType = std::conditional_t<
            NumFields == 1,
            std::vector<std::tuple_element_t<0, FieldTypesTuple>>,
            std::vector<FieldTypesTuple>
        >;

        explicit SelectValueStatement(ConnType& conn) : conn_(conn) {}

        // Execute SELECT DISTINCT query on the specified field(s)
        [[nodiscard]] auto execute() -> std::expected<ResultType, Error> {
            // Build SQL: SELECT DISTINCT field1, field2, ... FROM table
            std::string field_list = build_field_list(std::make_index_sequence<NumFields>{});
            std::string sql = "SELECT DISTINCT " + field_list + " FROM " + std::string(Base::table_name_);

            // Prepare and execute statement
            auto prepare_result = conn_.prepare(sql);
            if (!prepare_result) {
                return std::unexpected(prepare_result.error());
            }

            auto stmt = std::move(prepare_result.value());
            ResultType results;
            results.reserve(100); // Initial capacity

            // Fetch rows
            while (true) {
                int step_result = stmt.step_raw();
                if (step_result == Statement::ROW_AVAILABLE) {
                    if constexpr (NumFields == 1) {
                        // Single field: extract directly
                        using FieldType = std::tuple_element_t<0, FieldTypesTuple>;
                        results.push_back(extract_column<FieldType>(stmt, 0));
                    } else {
                        // Multiple fields: extract into tuple
                        results.push_back(extract_row_as_tuple(stmt, std::make_index_sequence<NumFields>{}));
                    }
                } else if (step_result == Statement::NO_MORE_ROWS) {
                    break;
                } else {
                    return std::unexpected(Error{step_result, stmt.get_error_message()});
                }
            }

            return results;
        }

      private:
        // Build comma-separated field list at compile-time
        template <size_t... Is>
        static std::string build_field_list(std::index_sequence<Is...>) {
            std::string result;
            auto append_field = [&result]<size_t I>() {
                if constexpr (I > 0) {
                    result += ", ";
                }
                // Check if this field is a FK - if so, use column name (field_name_id)
                constexpr auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member_infos_[I]);
                if constexpr (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                    result += std::string(std::meta::identifier_of(member_infos_[I])) + "_id";
                } else {
                    result += std::meta::identifier_of(member_infos_[I]);
                }
            };
            (append_field.template operator()<Is>(), ...);
            return result;
        }

        // Extract a single column value based on type
        template <typename FieldType>
        static FieldType extract_column(Statement& stmt, int col_idx) {
            FieldType value;
            if constexpr (std::is_same_v<FieldType, int>) {
                value = stmt.extract_int(col_idx);
            } else if constexpr (std::is_same_v<FieldType, int64_t> || std::is_same_v<FieldType, long> ||
                                 std::is_same_v<FieldType, long long>) {
                value = static_cast<FieldType>(stmt.extract_int64(col_idx));
            } else if constexpr (std::is_same_v<FieldType, double>) {
                value = stmt.extract_double(col_idx);
            } else if constexpr (std::is_same_v<FieldType, float>) {
                value = stmt.extract_float(col_idx);
            } else if constexpr (std::is_same_v<FieldType, bool>) {
                value = stmt.extract_bool(col_idx);
            } else if constexpr (std::is_same_v<FieldType, std::string>) {
                const unsigned char* text = stmt.extract_text_ptr(col_idx);
                if (text) {
                    int len = sqlite3_column_bytes(stmt.handle(), col_idx);
                    value.assign(reinterpret_cast<const char*>(text), len);
                } else {
                    value = std::string();
                }
            } else {
                static_assert(std::is_same_v<FieldType, int> || std::is_same_v<FieldType, std::string>,
                              "Unsupported field type for SELECT DISTINCT");
            }
            return value;
        }

        // Extract multiple columns into a tuple
        template <size_t... Is>
        FieldTypesTuple extract_row_as_tuple(Statement& stmt, std::index_sequence<Is...>) {
            return std::make_tuple(extract_column<std::tuple_element_t<Is, FieldTypesTuple>>(stmt, Is)...);
        }

        ConnType& conn_;
    };

} // namespace storm::orm::statements

// Export SelectValueQuery in storm namespace (query builder wrapper)
export namespace storm {

    // SelectValueQuery - lightweight wrapper around SelectValueStatement
    // Returned by QuerySet::distinct() to provide SELECT DISTINCT functionality
    template <class T, db::DatabaseConnection ConnType, auto... FieldPtrs>
    class SelectValueQuery {
        using Error         = typename ConnType::Error;
        using StatementType = orm::statements::SelectValueStatement<T, ConnType, FieldPtrs...>;

      public:
        // Expose result type from statement
        using ResultType = typename StatementType::ResultType;

        explicit SelectValueQuery(ConnType& conn) : conn_(conn) {}

        // Execute SELECT DISTINCT query on the specified field(s)
        [[nodiscard]] auto select() -> std::expected<ResultType, Error> {
            StatementType stmt(conn_);
            return stmt.execute();
        }

      private:
        ConnType& conn_;
    };

} // namespace storm
