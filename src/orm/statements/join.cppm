module;

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

    enum class JoinType { Inner, Left, Right };

    // Type alias for type-erased pointers used in polymorphic JOIN wrapper.
    // void* is intentional here: JoinStatementWrapper must work with any model type T
    // without knowing T at compile time. The actual T* conversion happens in the
    // function pointers stored in make_join_wrapper().
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,bugprone-easily-swappable-parameters)
    using ErasedObjectPtr    = void*; // NOSONAR(cpp:S5008) - type erasure requires void*
    using ErasedStatementPtr = void*; // NOSONAR(cpp:S5008) - type erasure requires void*

    struct JoinStatementWrapper {
        auto (*get_complete_sql_fn)() -> const std::string&;
        auto (*extract_row_fn)(ErasedStatementPtr, ErasedObjectPtr) -> void;

        // Get complete pre-computed SELECT...JOIN SQL
        [[nodiscard]] auto get_complete_sql() const -> const std::string& {
            return get_complete_sql_fn();
        }

        auto extract_row(ErasedStatementPtr stmt, ErasedObjectPtr obj) const -> void {
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
        // FK_type unwraps std::optional<FKModel> → FKModel so FKBase_at works correctly
        template <size_t Idx>
        using FK_type =
                utilities::optional_inner_type_t<std::remove_cvref_t<decltype(std::declval<T>().*FKFieldPtrs...[Idx])>>;

        template <size_t Idx> using FKBase_at = BaseStatement<FK_type<Idx>>;

        // Compile-time validation
        static_assert(
                (std::is_member_object_pointer_v<decltype(FKFieldPtrs)> && ...),
                "FK pointers must be member object pointers"
        );

        static consteval auto count_non_fk_fields() -> size_t {
            size_t count = 0;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                if (!Base::is_fk_field(Base::all_members_[i])) {
                    count++;
                }
            }
            return count;
        }

        static constexpr size_t non_fk_field_count_ = count_non_fk_fields();

        // LCOV_EXCL_START - compile-time only (initializes constexpr column_offsets_)
        // Constexpr storage for column offsets
        static constexpr auto calculate_column_offsets() {
            std::array<size_t, fk_count_> offsets{};
            size_t                        current_offset = non_fk_field_count_;
            size_t                        idx            = 0;

            [&]<size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                ((offsets[idx++] = current_offset, current_offset += FKBase_at<Is>::field_count_), ...);
            }(std::make_index_sequence<fk_count_>{});

            return offsets;
        }
        // LCOV_EXCL_STOP

        static constexpr auto column_offsets_ = calculate_column_offsets();

        static consteval auto build_fk_field_names() {
            std::array<std::string_view, fk_count_> names{};
            size_t                                  fk_idx = 0;

            for (size_t member_idx = 0; member_idx < Base::field_count_ && fk_idx < fk_count_; ++member_idx) {
                auto member = Base::all_members_[member_idx];
                if (Base::is_fk_field(member)) {
                    names[fk_idx++] = std::meta::identifier_of(member);
                }
            }
            return names;
        }

        static constexpr auto fk_field_names_ = build_fk_field_names();

        // LCOV_EXCL_START - compile-time only (called from consteval functions)
        static constexpr auto get_join_keyword() -> std::string_view {
            if constexpr (Type == JoinType::Inner) {
                return " INNER JOIN ";
            } else if constexpr (Type == JoinType::Left) {
                return " LEFT JOIN ";
            } else {
                return " RIGHT JOIN ";
            }
        }
        // LCOV_EXCL_STOP

        // Compile-time SQL generation with ConstexprString
        static consteval auto calculate_join_sql_size() -> size_t {
            using utilities::sql_len::ON_EQUALS;
            using utilities::sql_len::SMALL_BUFFER;
            // NOLINTNEXTLINE(misc-const-correctness) - total IS modified in fold expression below
            size_t total = 0;

            [&]<size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                ((total += get_join_keyword().size() + FKBase_at<Is>::table_name_.size() + 2 + 1 + 4 + 1 + 1 + 1 +
                           FKBase_at<Is>::pk_name_.size() + ON_EQUALS + fk_field_names_[Is].size() + 3),
                 ...);
            }(std::make_index_sequence<fk_count_>{});

            return total + SMALL_BUFFER;
        }

        static consteval auto build_join_sql_array() {
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t          sql_size = calculate_join_sql_size();
            ConstexprString<sql_size> result;

            [&]<size_t... Is>(std::index_sequence<Is...> /*unused*/) {
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

        static consteval auto calculate_select_fields_size() -> size_t {
            size_t total = 0;

            // Base fields
            for (size_t i = 0; i < Base::field_count_; ++i) {
                auto member = Base::all_members_[i];
                if (!Base::is_fk_field(member)) {
                    total += (total > 0 ? 2 : 0) + 3 + std::meta::identifier_of(member).size();
                }
            }

            // FK fields
            [&]<size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                (
                        [&]<size_t I>() {
                            total += 2; // ", "
                            [&]<size_t... FieldIs>(std::index_sequence<FieldIs...> /*unused*/) {
                                ((total += (FieldIs > 0 ? 2 : 0) + 3 + 1 +
                                           std::meta::identifier_of(FKBase_at<I>::all_members_[FieldIs]).size()),
                                 ...);
                            }(std::make_index_sequence<FKBase_at<I>::field_count_>{});
                        }.template operator()<Is>(),
                        ...
                );
            }(std::make_index_sequence<fk_count_>{});

            return total + utilities::sql_len::SMALL_BUFFER;
        }

        static consteval auto build_select_fields_array() {
            constexpr size_t             fields_size = calculate_select_fields_size();
            ConstexprString<fields_size> result;

            // Base fields
            bool first = true;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                auto member = Base::all_members_[i];
                if (!Base::is_fk_field(member)) {
                    if (!first) {
                        result.append(", ");
                    }
                    result.append("t1.");
                    result.append(std::meta::identifier_of(member));
                    first = false;
                }
            }

            // FK fields
            [&]<size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                (
                        [&]<size_t I>() {
                            result.append(", ");
                            [&]<size_t... FieldIs>(std::index_sequence<FieldIs...> /*unused*/) {
                                // NOLINTNEXTLINE(misc-const-correctness) - first_in_table IS modified in fold expression
                                bool first_in_table = true;
                                (((first_in_table ? (void)0 : result.append(", ")),
                                  result.append("t"),
                                  result.append_digit(I + 2),
                                  result.append("."),
                                  result.append(std::meta::identifier_of(FKBase_at<I>::all_members_[FieldIs])),
                                  first_in_table = false),
                                 ...);
                            }(std::make_index_sequence<FKBase_at<I>::field_count_>{});
                        }.template operator()<Is>(),
                        ...
                );
            }(std::make_index_sequence<fk_count_>{});

            return result;
        }

        static constexpr auto join_sql_array      = build_join_sql_array();
        static constexpr auto select_fields_array = build_select_fields_array();

        // NEW: Compile-time complete SQL generation
        static consteval auto calculate_complete_sql_size() -> size_t {
            using utilities::sql_len::FROM;
            using utilities::sql_len::SELECT;
            using utilities::sql_len::SMALL_BUFFER;
            size_t total = 0;
            total += SELECT; // "SELECT "
            total += calculate_select_fields_size();
            total += FROM; // " FROM "
            total += Base::table_name_.size();
            total += 3; // " t1"
            total += calculate_join_sql_size();
            return total + SMALL_BUFFER;
        }

        static consteval auto build_complete_sql_array() {
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t          sql_size = calculate_complete_sql_size();
            ConstexprString<sql_size> result;

            result.append("SELECT ");
            result.append(select_fields_array);
            result.append(" FROM ");
            result.append(Base::table_name_);
            result.append(" t1");
            result.append(join_sql_array);

            return result;
        }

        static constexpr auto complete_sql_array = build_complete_sql_array();

      public:
        // Get complete pre-computed SELECT...JOIN SQL with lazy initialization
        static auto get_complete_sql() -> const std::string& {
            static const std::string str{complete_sql_array.data.data(), complete_sql_array.len};
            return str;
        }

      private:
        // =====================================================================
        // DATABASE-AGNOSTIC EXTRACTION - Uses Statement template methods
        // =====================================================================

        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_t_fields(Statement* stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            int col_idx = 0;
            ((extract_column_at<Is>(stmt, obj, col_idx)), ...);
        }

        template <size_t MemberIdx>
        __attribute__((always_inline)) static void extract_column_at(Statement* stmt, T& obj, int& col_idx) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];

                if constexpr (Base::is_fk_field(member)) {
                    return;
                } else {
                    using FieldType = std::remove_cvref_t<decltype(obj.[:member:])>;
                    obj.[:member:]  = Base::template extract_column_value<FieldType>(stmt, col_idx);
                    col_idx++;
                }
            }
        }

        template <size_t FKIdx, size_t... FieldIs>
        __attribute__((always_inline)) static void extract_fk_fields_impl(
                Statement* stmt, FK_type<FKIdx>& fk_obj, size_t col_offset, std::index_sequence<FieldIs...> /*unused*/
        ) noexcept {
            using FKBase = FKBase_at<FKIdx>;
            ((extract_fk_field_at<FKBase, FieldIs>(stmt, fk_obj, col_offset + FieldIs)), ...);
        }

        template <typename FKBase, size_t FieldIdx>
        __attribute__((always_inline)) static void
        extract_fk_field_at(Statement* stmt, auto& fk_obj, int col_idx) noexcept {
            if constexpr (FieldIdx < FKBase::field_count_) {
                constexpr auto member = FKBase::all_members_[FieldIdx];
                using FieldType       = std::remove_cvref_t<decltype(fk_obj.[:member:])>;
                fk_obj.[:member:]     = Base::template extract_column_value<FieldType>(stmt, col_idx);
            }
        }

        template <size_t Idx>
        __attribute__((always_inline)) static void extract_fk_at(Statement* stmt, T& obj) noexcept {
            constexpr auto FKPtr         = FKFieldPtrs...[Idx]; // C++26 pack indexing
            using RawFKFieldType         = std::remove_cvref_t<decltype(obj.*FKPtr)>;
            constexpr size_t field_count = FKBase_at<Idx>::field_count_;

            if constexpr (utilities::is_optional_v<RawFKFieldType>) {
                // Optional FK: NULL first joined column means no match → set nullopt
                const int first_col = static_cast<int>(column_offsets_[Idx]);
                if (stmt->is_null(first_col)) {
                    obj.*FKPtr = std::nullopt;
                } else {
                    FK_type<Idx> fk_inner{};
                    extract_fk_fields_impl<Idx>(
                            stmt, fk_inner, column_offsets_[Idx], std::make_index_sequence<field_count>{}
                    );
                    obj.*FKPtr = std::move(fk_inner);
                }
            } else {
                auto& fk_obj = obj.*FKPtr;
                extract_fk_fields_impl<Idx>(
                        stmt, fk_obj, column_offsets_[Idx], std::make_index_sequence<field_count>{}
                );
            }
        }

        template <size_t... Is>
        __attribute__((always_inline)) static void
        extract_all_fks(Statement* stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            (extract_fk_at<Is>(stmt, obj), ...);
        }

        // FIX: Initialize all FK fields to default values before extraction
        // This ensures non-JOINed FK fields have proper default values instead of garbage
        template <size_t MemberIdx> __attribute__((always_inline)) static void init_fk_field_at(T& obj) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];
                if constexpr (Base::is_fk_field(member)) {
                    using FieldType = std::remove_cvref_t<decltype(obj.[:member:])>;
                    obj.[:member:]  = FieldType{}; // Default-construct FK object
                }
            }
        }

        template <size_t... Is>
        __attribute__((always_inline)) static void
        init_all_fk_fields(T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            (init_fk_field_at<Is>(obj), ...);
        }

      public:
        // Database-agnostic API: Uses Statement template methods for extraction
        // Template methods enable cross-module inlining without LTO
        __attribute__((hot)) __attribute__((flatten)) static auto extract_joined_row(Statement* stmt, T& obj) noexcept
                -> void {
            // Initialize ALL FK fields to defaults first
            init_all_fk_fields(obj, typename Base::field_indices_t{});
            // Extract base fields and FK fields using Statement methods
            extract_t_fields(stmt, obj, typename Base::field_indices_t{});
            extract_all_fks(stmt, obj, std::make_index_sequence<fk_count_>{});
        }
    };

    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, auto... FKFieldPtrs>
    [[nodiscard]] auto make_join_wrapper() -> JoinStatementWrapper {
        using JS = JoinStatement<T, ConnType, Type, FKFieldPtrs...>;

        return JoinStatementWrapper{
                +[]() -> const std::string& { return JS::get_complete_sql(); },
                +[](ErasedStatementPtr stmt, ErasedObjectPtr obj) -> void {
                    JS::extract_joined_row(static_cast<typename ConnType::Statement*>(stmt), *static_cast<T*>(obj));
                }
        };
    }

} // namespace storm::orm::statements
