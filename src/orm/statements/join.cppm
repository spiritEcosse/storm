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
import <memory>;

export namespace storm::orm::statements {

    using storm::orm::utilities::ConstexprString;

    // Abstract interface for type erasure in QuerySet
    class IJoinStatement {
      public:
        virtual ~IJoinStatement() = default;

        // Generate JOIN SQL clause(s)
        virtual std::string to_sql() const = 0;

        // Generate SELECT field list with table aliases
        virtual std::string build_qualified_select_fields() const = 0;

        // Extract joined row - must be virtual for polymorphic access
        virtual void extract_row(void* stmt, void* obj) const = 0;
    };

    // Unified JOIN statement - supports single or multiple FK fields
    // Usage:
    //   Single FK: JoinStatement<Message, ConnType, &Message::sender>
    //   Multi FK:  JoinStatement<Message, ConnType, &Message::sender, &Message::receiver>
    template <typename T, storm::db::DatabaseConnection ConnType, auto... FKFieldPtrs>
        requires(sizeof...(FKFieldPtrs) >= 1)
    class JoinStatement : public IJoinStatement, private BaseStatement<T> {
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

        // Calculate column offsets for each FK table
        template <size_t... Is> static constexpr auto calculate_column_offsets(std::index_sequence<Is...>) {
            std::array<size_t, fk_count_> offsets{};
            size_t                        current_offset = Base::field_count_;

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

        static std::string_view get_fk_field_name_at(size_t idx) {
            return fk_field_names_[idx];
        }

        // Helper to generate table alias (DRY - used in multiple places)
        static std::string table_alias(size_t idx) {
            return "t" + std::to_string(idx);
        }

        // Build qualified field list with table alias (e.g., "t1.id, t1.name")
        static std::string build_qualified_fields(std::string_view fields, std::string_view alias) {
            std::string result;
            size_t      pos = 0;
            while (pos < fields.size()) {
                result += alias;
                result += '.';

                size_t comma = fields.find(',', pos);
                if (comma == std::string_view::npos) {
                    result += fields.substr(pos);
                    break;
                }

                result += fields.substr(pos, comma - pos);
                result += ", ";
                pos = comma + 2;
            }
            return result;
        }

        // Build multi-JOIN SQL (works for single FK too)
        template <size_t... Is> std::string build_join_sql_impl(std::index_sequence<Is...>) const {
            std::string sql;
            sql.reserve(500 * fk_count_); // Pre-allocate

            // INNER JOIN clauses for each FK
            ((sql += " INNER JOIN ",
              sql += FKBase_at<Is>::table_name_,
              sql += " ",
              sql += table_alias(Is + 2),
              sql += " ON ",
              sql += table_alias(Is + 2),
              sql += ".",
              sql += FKBase_at<Is>::pk_name_,
              sql += " = t1.",
              sql += get_fk_field_name_at(Is),
              sql += "_id"),
             ...);

            return sql;
        }

        // Build qualified SELECT fields for all tables
        template <size_t... Is> std::string build_select_fields_impl(std::index_sequence<Is...>) const {
            std::string fields;
            fields.reserve(1000);

            // Base table fields
            fields += build_qualified_fields(Base::field_names_, "t1");

            // FK table fields
            ((fields += ", ", fields += build_qualified_fields(FKBase_at<Is>::field_names_, table_alias(Is + 2))), ...);

            return fields;
        }

        // Get FK pointer at index (reuses pack_element_at)
        template <size_t Idx> static constexpr auto get_fk_ptr_at = pack_element_at<Idx, FKFieldPtrs...>::value;

        // Cached SQL strings to avoid rebuilding on repeated queries
        mutable std::string cached_join_sql_;
        mutable std::string cached_select_fields_;

      public:
        JoinStatement() = default;

        // Virtual interface implementation with caching
        std::string to_sql() const override {
            if (cached_join_sql_.empty()) {
                cached_join_sql_ = build_join_sql_impl(std::make_index_sequence<fk_count_>{});
            }
            return cached_join_sql_;
        }

        std::string build_qualified_select_fields() const override {
            if (cached_select_fields_.empty()) {
                cached_select_fields_ = build_select_fields_impl(std::make_index_sequence<fk_count_>{});
            }
            return cached_select_fields_;
        }

        // Virtual extraction method for polymorphic access
        void extract_row(void* stmt_ptr, void* obj_ptr) const override {
            auto* stmt = static_cast<Statement*>(stmt_ptr);
            auto* obj  = static_cast<T*>(obj_ptr);
            extract_joined_row(stmt, *obj);
        }

        // Extract T fields from base table columns (skip FK fields)
        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_t_fields(Statement* stmt, T& obj, std::index_sequence<Is...>) noexcept {
            ((extract_column_at<Is>(stmt, obj, Is)), ...);
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
        template <size_t MemberIdx>
        __attribute__((always_inline)) static void extract_column_at(Statement* stmt, T& obj, int col_idx) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];

                if constexpr (Base::is_fk_field(member)) {
                    return; // FK fields populated by extract_all_fks
                } else {
                    extract_typed_field(stmt, obj.[:member:], col_idx);
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

        // Public method for SelectStatement to extract a complete joined row
        static void extract_joined_row(Statement* stmt, T& obj) noexcept {
            // Extract base table fields (non-FK)
            extract_t_fields(stmt, obj, typename Base::field_indices_t{});

            // Extract all FK objects
            extract_all_fks(stmt, obj, std::make_index_sequence<fk_count_>{});
        }
    };

} // namespace storm::orm::statements
