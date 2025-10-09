module;

#include <sqlite3.h>
#include <meta>

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

    // Type-erased wrapper for JOIN operations (replaces IJoinStatement abstract base)
    // Uses function pointers instead of virtual dispatch for zero-overhead abstraction
    struct JoinStatementWrapper {
        // Function pointers to static methods returning compile-time generated SQL
        const std::string& (*get_join_sql_fn)();
        const std::string& (*get_select_fields_fn)();
        void (*extract_row_fn)(void*, void*);

        // Public interface (calls through function pointers)
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

    // Unified JOIN statement - supports single or multiple FK fields
    // Now generates SQL at compile-time and uses static methods (no virtual dispatch)
    // Usage:
    //   Single FK: JoinStatement<Message, ConnType, &Message::sender>
    //   Multi FK:  JoinStatement<Message, ConnType, &Message::sender, &Message::receiver>
    template <typename T, storm::db::DatabaseConnection ConnType, auto... FKFieldPtrs>
        requires(sizeof...(FKFieldPtrs) >= 1)
    class JoinStatement : private BaseStatement<T> {
        friend class BaseStatement<T>;
        using Base       = BaseStatement<T>;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Number of FK fields to join
        static constexpr size_t fk_count_ = sizeof...(FKFieldPtrs);

        // Extract FK type from member pointer at compile-time
        template <auto FKPtr> using FK_type = std::remove_cvref_t<decltype(std::declval<T>().*FKPtr)>;

        // Generic parameter pack element access (DRY - used for both types and values)
        template <size_t Idx, auto... Pack> struct pack_element_at;

        template <auto First, auto... Rest> struct pack_element_at<0, First, Rest...> {
            static constexpr auto value = First;
        };

        template <size_t Idx, auto First, auto... Rest>
            requires(Idx > 0)
        struct pack_element_at<Idx, First, Rest...> {
            static constexpr auto value = pack_element_at<Idx - 1, Rest...>::value;
        };

        // Get BaseStatement type for FK at index
        template <size_t Idx> using FKBase_at = BaseStatement<FK_type<pack_element_at<Idx, FKFieldPtrs...>::value>>;

        // Count non-FK fields in base table
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

        // Calculate column offsets for each FK table (starting after non-FK base fields)
        template <size_t... Is> static constexpr auto calculate_column_offsets(std::index_sequence<Is...>) {
            std::array<size_t, fk_count_> offsets{};
            size_t                        current_offset = non_fk_field_count_;

            auto get_fk_field_count = []<size_t I>() constexpr { return FKBase_at<I>::field_count_; };

            ((offsets[Is] = current_offset, current_offset += get_fk_field_count.template operator()<Is>()), ...);

            return offsets;
        }

        static constexpr auto column_offsets_ = calculate_column_offsets(std::make_index_sequence<fk_count_>{});

        // Build FK field names at compile time
        template <size_t MemberIdx, size_t FKIdx>
        static consteval void fill_fk_name(std::array<std::string_view, fk_count_>& names) {
            if constexpr (MemberIdx < Base::field_count_ && FKIdx < fk_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];
                if constexpr (Base::is_fk_field(member)) {
                    names[FKIdx] = std::meta::identifier_of(member);
                    fill_fk_name<MemberIdx + 1, FKIdx + 1>(names);
                } else {
                    fill_fk_name<MemberIdx + 1, FKIdx>(names);
                }
            }
        }

        static consteval auto build_fk_field_names() {
            std::array<std::string_view, fk_count_> names{};
            fill_fk_name<0, 0>(names);
            return names;
        }

        static constexpr auto fk_field_names_ = build_fk_field_names();

        // Get FK pointer at index (reuses pack_element_at)
        template <size_t Idx> static constexpr auto get_fk_ptr_at = pack_element_at<Idx, FKFieldPtrs...>::value;

        // ==================== COMPILE-TIME SQL GENERATION ====================

        // Calculate JOIN SQL size at compile-time
        static consteval size_t calculate_join_sql_size() {
            size_t total = 0;
            // For each FK: " INNER JOIN table tN ON tN.pk = t1.fk_id"
            auto calc_one_join = []<size_t I>() constexpr {
                size_t size = 0;
                size += 12; // " INNER JOIN "
                size += FKBase_at<I>::table_name_.size();
                size += 2; // " t"
                size += 1; // digit (2-9)
                size += 4; // " ON "
                size += 1; // "t"
                size += 1; // digit
                size += 1; // "."
                size += FKBase_at<I>::pk_name_.size();
                size += 5; // " = t1."
                size += fk_field_names_[I].size();
                size += 3; // "_id"
                return size;
            };

            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((total += calc_one_join.template operator()<Is>()), ...);
            }(std::make_index_sequence<fk_count_>{});

            return total + 50; // Add buffer for safety
        }

        // Build JOIN SQL at compile-time using ConstexprString
        static consteval auto build_join_sql_array() {
            constexpr size_t          sql_size = calculate_join_sql_size();
            ConstexprString<sql_size> result;

            // Build JOIN clauses for each FK using fold expression
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((result.append(" INNER JOIN "),
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

        // Calculate SELECT fields size at compile-time
        static consteval size_t calculate_select_fields_size() {
            size_t total = 0;

            // Base table fields: "t1.field1, t1.field2, ..." (skip FK fields)
            auto calc_base_fields = []<size_t... Is>(std::index_sequence<Is...>) constexpr {
                size_t size = 0;
                size_t field_count = 0;
                ((
                    Base::is_fk_field(Base::all_members_[Is]) ? (void)0 :
                    ((void)(size += (field_count > 0 ? 2 : 0)),  // ", " separator
                     (void)(size += 3),                           // "t1."
                     (void)(size += std::meta::identifier_of(Base::all_members_[Is]).size()),
                     (void)(field_count++))
                ), ...);
                return size;
            };

            total += calc_base_fields(std::make_index_sequence<Base::field_count_>{});

            // FK table fields using fold expressions
            auto calc_fk_fields = []<size_t I, size_t... FieldIs>(std::index_sequence<FieldIs...>) constexpr {
                size_t size = 2; // ", " before FK fields
                ((size += (FieldIs > 0 ? 2 : 0),  // ", " separator
                  size += 2,                       // "tN."
                  size += 1,                       // digit
                  size += std::meta::identifier_of(FKBase_at<I>::all_members_[FieldIs]).size()),
                 ...);
                return size;
            };

            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((total += calc_fk_fields.template operator()<Is>(
                          std::make_index_sequence<FKBase_at<Is>::field_count_>{})),
                 ...);
            }(std::make_index_sequence<fk_count_>{});

            return total + 100; // Add buffer
        }

        // Build SELECT fields at compile-time
        static consteval auto build_select_fields_array() {
            constexpr size_t          fields_size = calculate_select_fields_size();
            ConstexprString<fields_size> result;

            // Base table fields (t1.field1, t1.field2, ...) - skip FK fields
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                bool first = true;
                ((
                    Base::is_fk_field(Base::all_members_[Is]) ? (void)0 :
                    ((void)(first ? (void)0 : (void)result.append(", ")),
                     (void)result.append("t1."),
                     (void)result.append(std::meta::identifier_of(Base::all_members_[Is])),
                     (void)(first = false))
                ), ...);
            }(std::make_index_sequence<Base::field_count_>{});

            // FK table fields - iterate over all FK tables
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ([&]<size_t I>() {
                    result.append(", ");
                    [&]<size_t... FieldIs>(std::index_sequence<FieldIs...>) {
                        bool first_in_table = true;
                        ((
                            (first_in_table ? (void)0 : result.append(", ")),
                            result.append("t"),
                            result.append_digit(I + 2),
                            result.append("."),
                            result.append(std::meta::identifier_of(FKBase_at<I>::all_members_[FieldIs])),
                            first_in_table = false
                        ), ...);
                    }(std::make_index_sequence<FKBase_at<I>::field_count_>{});
                }.template operator()<Is>(), ...);
            }(std::make_index_sequence<fk_count_>{});

            return result;
        }

        // Pre-computed SQL strings (compile-time generated, static storage)
        static constexpr auto           join_sql_array        = build_join_sql_array();
        static constexpr auto           select_fields_array   = build_select_fields_array();
        static inline const std::string join_sql_string       = std::string(join_sql_array);
        static inline const std::string select_fields_string  = std::string(select_fields_array);

      public:
        // Static accessors returning compile-time generated SQL (called via function pointers)
        static const std::string& get_join_sql() {
            return join_sql_string;
        }

        static const std::string& get_select_fields() {
            return select_fields_string;
        }

        // ==================== ROW EXTRACTION (now static) ====================

        // Extract T fields from base table columns (skip FK fields)
        // Note: Column index != field index because FK fields are skipped in SELECT
        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_t_fields(Statement* stmt, T& obj, std::index_sequence<Is...>) noexcept {
            int col_idx = 0;
            ((extract_column_at<Is>(stmt, obj, col_idx)), ...);
        }

        // Extract field value based on type - DRY helper shared by T and FK extraction
        template <typename FieldType>
        __attribute__((always_inline)) static void
        extract_typed_field(Statement* stmt, FieldType& field, int col_idx) noexcept {
            if constexpr (std::is_same_v<FieldType, int>) {
                field = stmt->extract_int(col_idx);
            } else if constexpr (std::is_same_v<FieldType, int64_t>) {
                field = stmt->extract_int64(col_idx);
            } else if constexpr (std::is_same_v<FieldType, std::string>) {
                const unsigned char* text = stmt->extract_text_ptr(col_idx);
                if (text) {
                    field = std::string(reinterpret_cast<const char*>(text));
                }
            }
        }

        // Extract single column for T (skip FK fields - they're populated separately)
        // col_idx is passed by reference and incremented only for non-FK fields
        template <size_t MemberIdx>
        __attribute__((always_inline)) static void extract_column_at(Statement* stmt, T& obj, int& col_idx) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];

                if constexpr (Base::is_fk_field(member)) {
                    return; // FK fields populated by extract_all_fks
                } else {
                    extract_typed_field(stmt, obj.[:member:], col_idx);
                    col_idx++; // Increment only for non-FK fields
                }
            }
        }

        // Extract FK fields for FK at index FKIdx
        template <size_t FKIdx, auto FKPtr, size_t... FieldIs>
        __attribute__((always_inline)) static void extract_fk_fields_impl(
                Statement* stmt, FK_type<FKPtr>& fk_obj, size_t col_offset, std::index_sequence<FieldIs...>
        ) noexcept {
            using FKBase = FKBase_at<FKIdx>;
            ((extract_fk_field_at<FKBase, FieldIs>(stmt, fk_obj, col_offset + FieldIs)), ...);
        }

        // Extract single FK field (reuses extract_typed_field)
        template <typename FKBase, size_t FieldIdx>
        __attribute__((always_inline)) static void
        extract_fk_field_at(Statement* stmt, auto& fk_obj, int col_idx) noexcept {
            if constexpr (FieldIdx < FKBase::field_count_) {
                constexpr auto member = FKBase::all_members_[FieldIdx];
                extract_typed_field(stmt, fk_obj.[:member:], col_idx);
            }
        }

        // Extract FK at specific index
        template <size_t Idx>
        __attribute__((always_inline)) static void extract_fk_at(Statement* stmt, T& obj) noexcept {
            constexpr auto FKPtr  = get_fk_ptr_at<Idx>;
            auto&          fk_obj = obj.*FKPtr;
            extract_fk_fields_impl<Idx, FKPtr>(
                    stmt, fk_obj, column_offsets_[Idx], std::make_index_sequence<FKBase_at<Idx>::field_count_>{}
            );
        }

        // Extract all FK objects using fold expression
        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_all_fks(Statement* stmt, T& obj, std::index_sequence<Is...>) noexcept {
            (extract_fk_at<Is>(stmt, obj), ...);
        }

        // Public static method for SelectStatement to extract a complete joined row
        static void extract_joined_row(Statement* stmt, T& obj) noexcept {
            // Extract base table fields (non-FK)
            extract_t_fields(stmt, obj, typename Base::field_indices_t{});

            // Extract all FK objects
            extract_all_fks(stmt, obj, std::make_index_sequence<fk_count_>{});
        }
    };

    // Factory function to create type-erased wrapper (called by QuerySet::join<>())
    template <typename T, storm::db::DatabaseConnection ConnType, auto... FKFieldPtrs>
    [[nodiscard]] auto make_join_wrapper() -> JoinStatementWrapper {
        using JS = JoinStatement<T, ConnType, FKFieldPtrs...>;

        return JoinStatementWrapper{
                // Function pointers to static methods returning compile-time generated SQL
                +[]() -> const std::string& { return JS::get_join_sql(); },
                +[]() -> const std::string& { return JS::get_select_fields(); },
                +[](void* stmt, void* obj) {
                    JS::extract_joined_row(
                            static_cast<typename ConnType::Statement*>(stmt), *static_cast<T*>(obj)
                    );
                }};
    }

} // namespace storm::orm::statements
