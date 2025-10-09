module;

#include <sqlite3.h>
#include <meta>
#include <cassert>

export module storm_orm_statements_join;

import storm_orm_statements_base;
import storm_orm_utilities;
import storm_db_concept;

import <expected>;
import <string>;
import <vector>;
import <utility>;
import <meta>;
import <type_traits>;
import <array>;
import <optional>;

export namespace storm::orm::statements {

    using storm::orm::utilities::ConstexprString;

    enum class JoinType {
        Inner,
        Left,
        Right
    };

    struct JoinStatementWrapper {
        const std::string& (*get_join_sql_fn)();
        const std::string& (*get_select_fields_fn)();
        void (*extract_row_fn)(void*, void*);

        const std::string& to_sql() const {
            return get_join_sql_fn();
        }

        const std::string& build_qualified_select_fields() const {
            return get_select_fields_fn();
        }

        void extract_row(void* stmt, void* obj) const {
            extract_row_fn(stmt, obj);
        }
    };

    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, auto... FKFieldPtrs>
        requires(sizeof...(FKFieldPtrs) >= 1)
    class JoinStatement : private BaseStatement<T> {
        friend class BaseStatement<T>;
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        static constexpr size_t fk_count_ = sizeof...(FKFieldPtrs);

        // C++26: Direct pack indexing
        template <size_t Idx>
        using FK_type = std::remove_cvref_t<decltype(std::declval<T>().*FKFieldPtrs...[Idx])>;

        template <size_t Idx>
        using FKBase_at = BaseStatement<FK_type<Idx>>;

        // Compile-time validation
        static_assert((std::is_member_object_pointer_v<decltype(FKFieldPtrs)> && ...),
                      "FK pointers must be member object pointers");

        static consteval size_t count_non_fk_fields() {
            size_t count = 0;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                if (!Base::is_fk_field(Base::all_members_[i])) {
                    count++;
                }
            }
            return count;
        }

        static constexpr size_t non_fk_field_count_ = count_non_fk_fields();

        // Constexpr storage for column offsets
        static constexpr auto calculate_column_offsets() {
            std::array<size_t, fk_count_> offsets{};
            size_t current_offset = non_fk_field_count_;
            size_t idx = 0;

            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((offsets[idx++] = current_offset,
                  current_offset += FKBase_at<Is>::field_count_), ...);
            }(std::make_index_sequence<fk_count_>{});

            return offsets;
        }

        static constexpr auto column_offsets_ = calculate_column_offsets();

        static consteval auto build_fk_field_names() {
            std::array<std::string_view, fk_count_> names{};
            size_t fk_idx = 0;

            for (size_t member_idx = 0; member_idx < Base::field_count_ && fk_idx < fk_count_; ++member_idx) {
                auto member = Base::all_members_[member_idx];
                if (Base::is_fk_field(member)) {
                    names[fk_idx++] = std::meta::identifier_of(member);
                }
            }
            return names;
        }

        static constexpr auto fk_field_names_ = build_fk_field_names();

        static constexpr std::string_view get_join_keyword() {
            if constexpr (Type == JoinType::Inner) return " INNER JOIN ";
            else if constexpr (Type == JoinType::Left) return " LEFT JOIN ";
            else return " RIGHT JOIN ";
        }

        // Compile-time SQL generation with ConstexprString
        static consteval size_t calculate_join_sql_size() {
            size_t total = 0;

            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((total += get_join_keyword().size() +
                           FKBase_at<Is>::table_name_.size() + 2 + 1 + 4 + 1 + 1 + 1 +
                           FKBase_at<Is>::pk_name_.size() + 5 +
                           fk_field_names_[Is].size() + 3), ...);
            }(std::make_index_sequence<fk_count_>{});

            return total + 10; // Small buffer
        }

        static consteval auto build_join_sql_array() {
            constexpr size_t          sql_size = calculate_join_sql_size();
            ConstexprString<sql_size> result;

            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((result.append(get_join_keyword()),
                  result.append(FKBase_at<Is>::table_name_),
                  result.append(" t"),
                  result.append_digit(Is + 2),
                  result.append(" ON t"),
                  result.append_digit(Is + 2),
                  result.append("."),
                  result.append(FKBase_at<Is>::pk_name_),
                  result.append(" = t1."),
                  result.append(fk_field_names_[Is]),
                  result.append("_id")),
                 ...);
            }(std::make_index_sequence<fk_count_>{});

            return result;
        }

        static consteval size_t calculate_select_fields_size() {
            size_t total = 0;

            // Base fields
            for (size_t i = 0; i < Base::field_count_; ++i) {
                auto member = Base::all_members_[i];
                if (!Base::is_fk_field(member)) {
                    total += (total > 0 ? 2 : 0) + 3 + std::meta::identifier_of(member).size();
                }
            }

            // FK fields
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ([&]<size_t I>() {
                    total += 2; // ", "
                    [&]<size_t... FieldIs>(std::index_sequence<FieldIs...>) {
                        ((total += (FieldIs > 0 ? 2 : 0) + 3 + 1 +
                                   std::meta::identifier_of(FKBase_at<I>::all_members_[FieldIs]).size()),
                         ...);
                    }(std::make_index_sequence<FKBase_at<I>::field_count_>{});
                }.template operator()<Is>(), ...);
            }(std::make_index_sequence<fk_count_>{});

            return total + 10;
        }

        static consteval auto build_select_fields_array() {
            constexpr size_t             fields_size = calculate_select_fields_size();
            ConstexprString<fields_size> result;

            // Base fields
            bool first = true;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                auto member = Base::all_members_[i];
                if (!Base::is_fk_field(member)) {
                    if (!first) result.append(", ");
                    result.append("t1.");
                    result.append(std::meta::identifier_of(member));
                    first = false;
                }
            }

            // FK fields
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ([&]<size_t I>() {
                    result.append(", ");
                    [&]<size_t... FieldIs>(std::index_sequence<FieldIs...>) {
                        bool first_in_table = true;
                        (((first_in_table ? (void)0 : result.append(", ")),
                          result.append("t"),
                          result.append_digit(I + 2),
                          result.append("."),
                          result.append(std::meta::identifier_of(FKBase_at<I>::all_members_[FieldIs])),
                          first_in_table = false),
                         ...);
                    }(std::make_index_sequence<FKBase_at<I>::field_count_>{});
                }.template operator()<Is>(), ...);
            }(std::make_index_sequence<fk_count_>{});

            return result;
        }

        static constexpr auto join_sql_array      = build_join_sql_array();
        static constexpr auto select_fields_array = build_select_fields_array();

        // Lazy initialization to avoid duplicate storage
        static const std::string& get_join_sql_cached() {
            static const std::string str{join_sql_array.data.data(), join_sql_array.len};
            return str;
        }

        static const std::string& get_select_fields_cached() {
            static const std::string str{select_fields_array.data.data(), select_fields_array.len};
            return str;
        }

      public:
        static const std::string& get_join_sql() {
            return get_join_sql_cached();
        }

        static const std::string& get_select_fields() {
            return get_select_fields_cached();
        }

        // Enhanced type extraction with NULL handling
        template <typename FieldType>
        static void extract_typed_field(Statement* stmt, FieldType& field, int col_idx) noexcept {
            if (stmt->is_null(col_idx)) {
                if constexpr (requires { field = std::nullopt; }) {
                    field = std::nullopt;
                }
                return;
            }

            if constexpr (std::is_same_v<FieldType, int>) {
                field = stmt->extract_int(col_idx);
            } else if constexpr (std::is_same_v<FieldType, int64_t>) {
                field = stmt->extract_int64(col_idx);
            } else if constexpr (std::is_same_v<FieldType, double>) {
                field = stmt->extract_double(col_idx);
            } else if constexpr (std::is_same_v<FieldType, bool>) {
                field = static_cast<bool>(stmt->extract_int(col_idx));
            } else if constexpr (std::is_same_v<FieldType, std::string>) {
                const unsigned char* text = stmt->extract_text_ptr(col_idx);
                if (text) {
                    field = std::string(reinterpret_cast<const char*>(text));
                }
            } else if constexpr (requires { field.emplace(std::string{}); }) {
                const unsigned char* text = stmt->extract_text_ptr(col_idx);
                if (text) {
                    field.emplace(reinterpret_cast<const char*>(text));
                }
            }
        }

        template <size_t... Is>
        static void extract_t_fields(Statement* stmt, T& obj, std::index_sequence<Is...>) noexcept {
            int col_idx = 0;
            ((extract_column_at<Is>(stmt, obj, col_idx)), ...);
        }

        template <size_t MemberIdx>
        static void extract_column_at(Statement* stmt, T& obj, int& col_idx) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];

                if constexpr (Base::is_fk_field(member)) {
                    return;
                } else {
                    extract_typed_field(stmt, obj.[:member:], col_idx);
                    col_idx++;
                }
            }
        }

        template <size_t FKIdx, size_t... FieldIs>
        static void extract_fk_fields_impl(
            Statement* stmt, FK_type<FKIdx>& fk_obj, size_t col_offset, std::index_sequence<FieldIs...>
        ) noexcept {
            using FKBase = FKBase_at<FKIdx>;
            ((extract_fk_field_at<FKBase, FieldIs>(stmt, fk_obj, col_offset + FieldIs)), ...);
        }

        template <typename FKBase, size_t FieldIdx>
        static void extract_fk_field_at(Statement* stmt, auto& fk_obj, int col_idx) noexcept {
            if constexpr (FieldIdx < FKBase::field_count_) {
                constexpr auto member = FKBase::all_members_[FieldIdx];
                extract_typed_field(stmt, fk_obj.[:member:], col_idx);
            }
        }

        template <size_t Idx>
        static void extract_fk_at(Statement* stmt, T& obj) noexcept {
            constexpr auto FKPtr = FKFieldPtrs...[Idx]; // C++26 pack indexing
            auto& fk_obj = obj.*FKPtr;
            extract_fk_fields_impl<Idx>(
                stmt, fk_obj, column_offsets_[Idx],
                std::make_index_sequence<FKBase_at<Idx>::field_count_>{}
            );
        }

        template <size_t... Is>
        static void extract_all_fks(Statement* stmt, T& obj, std::index_sequence<Is...>) noexcept {
            (extract_fk_at<Is>(stmt, obj), ...);
        }

        static void extract_joined_row(Statement* stmt, T& obj) noexcept {
            #ifndef NDEBUG
            size_t expected_cols = non_fk_field_count_;
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((expected_cols += FKBase_at<Is>::field_count_), ...);
            }(std::make_index_sequence<fk_count_>{});

            assert(sqlite3_column_count(stmt->handle()) == static_cast<int>(expected_cols) && "Column count mismatch");
            #endif

            extract_t_fields(stmt, obj, typename Base::field_indices_t{});
            extract_all_fks(stmt, obj, std::make_index_sequence<fk_count_>{});
        }
    };

    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, auto... FKFieldPtrs>
    [[nodiscard]] auto make_join_wrapper() -> JoinStatementWrapper {
        using JS = JoinStatement<T, ConnType, Type, FKFieldPtrs...>;

        return JoinStatementWrapper{
            +[]() -> const std::string& { return JS::get_join_sql(); },
            +[]() -> const std::string& { return JS::get_select_fields(); },
            +[](void* stmt, void* obj) {
                JS::extract_joined_row(
                    static_cast<typename ConnType::Statement*>(stmt),
                    *static_cast<T*>(obj)
                );
            }
        };
    }

} // namespace storm::orm::statements
