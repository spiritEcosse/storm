module;

#include <meta>
#include <cassert>

export module storm_orm_statements_join;

import std;

import storm_orm_statements_base;
import storm_orm_statements_orderby;
import storm_orm_utilities;
import storm_orm_where;
import storm_db_concept;

export namespace storm::orm::statements {

    using storm::orm::utilities::ConstexprString;

    enum class JoinType : std::uint8_t { Inner, Left, Right };

    // Type alias for type-erased pointers used in polymorphic JOIN wrapper.
    // void* is intentional here: JoinStatementWrapper must work with any model type T
    // without knowing T at compile time. The actual T* conversion happens in the
    // function pointers stored in make_join_wrapper().
    using ErasedObjectPtr    = void*; // NOSONAR(cpp:S5008) - type erasure requires void*
    using ErasedStatementPtr = void*; // NOSONAR(cpp:S5008) - type erasure requires void*

    struct JoinStatementWrapper {
        auto (*get_complete_sql_fn)() -> const std::string&;
        // Per-row extractor for FK joins. nullptr for m2m wrappers (the two-query
        // m2m path extracts base rows via Base::extract_all_columns, never this).
        auto (*extract_row_fn)(ErasedStatementPtr, ErasedObjectPtr) -> void = nullptr;

        // Many-to-many two-query predicate-pushdown extension (#391) — all nullptr
        // for plain FK joins. build_q1_sql_fn / build_q2_sql_fn produce the base-entity
        // SELECT and the junction⋈related SELECT (related rows filtered by the same base
        // subquery). extract_q2_owner_pk_fn keys the client-side stitch into the Q1
        // pk→entity map; append_related_q2_fn fills the entity's container;
        // container_empty_fn drives the INNER-join drop of zero-relation entities.
        using ClauseSqlFn = auto (*)(
                const orm::where::ExpressionVariantPtr&,
                const std::optional<OrderByWrapper>&,
                const std::optional<int>&,
                const std::optional<int>&
        ) -> std::string;
        ClauseSqlFn build_q1_sql_fn                                               = nullptr;
        ClauseSqlFn build_q2_sql_fn                                               = nullptr;
        auto (*extract_q2_owner_pk_fn)(ErasedStatementPtr) -> std::int64_t        = nullptr;
        auto (*append_related_q2_fn)(ErasedStatementPtr, ErasedObjectPtr) -> void = nullptr;
        auto (*container_empty_fn)(ErasedObjectPtr) -> bool                       = nullptr;
        // LEFT keeps zero-relation entities; INNER drops them after the stitch.
        bool m2m_is_left = false;

        [[nodiscard]] auto is_m2m() const -> bool {
            return build_q2_sql_fn != nullptr;
        }

        // Get complete pre-computed SELECT...JOIN SQL
        [[nodiscard]] auto get_complete_sql() const -> const std::string& {
            return get_complete_sql_fn();
        }

        auto extract_row(ErasedStatementPtr stmt, ErasedObjectPtr obj) const -> void {
            extract_row_fn(stmt, obj);
        }
    };

    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, std::meta::info... FKFields>
        requires(sizeof...(FKFields) >= 1 && (FKFieldOf<T, FKFields> && ...))
    class JoinStatement : private BaseStatement<T> {
        friend class BaseStatement<T>;
        using Base      = BaseStatement<T>;
        using Error     = typename ConnType::Error;
        using Statement = typename ConnType::Statement;

        static constexpr std::size_t fk_count_ = sizeof...(FKFields);

        // C++26: Direct pack indexing
        // FK_type unwraps std::optional<FKModel> → FKModel so FKBase_at works correctly
        template <std::size_t Idx>
        using FK_type =
                utilities::optional_inner_type_t<std::remove_cvref_t<typename[:std::meta::type_of(FKFields...[Idx]):]>>;

        template <std::size_t Idx> using FKBase_at = BaseStatement<FK_type<Idx>>;

        static consteval auto count_non_fk_fields() -> std::size_t {
            std::size_t count = 0;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                if (!Base::is_fk_field(Base::all_members_[i])) {
                    count++;
                }
            }
            return count;
        }

        static constexpr std::size_t non_fk_field_count_ = count_non_fk_fields();

        // LCOV_EXCL_START - compile-time only (initializes constexpr column_offsets_)
        // Constexpr storage for column offsets
        static constexpr auto calculate_column_offsets() {
            std::array<std::size_t, fk_count_> offsets{};
            std::size_t                        current_offset = non_fk_field_count_;
            std::size_t                        idx            = 0;

            [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                ((offsets[idx++] = current_offset, current_offset += FKBase_at<Is>::field_count_), ...);
            }(std::make_index_sequence<fk_count_>{});

            return offsets;
        }
        // LCOV_EXCL_STOP

        static constexpr auto column_offsets_ = calculate_column_offsets();

        // FK names derived per-info — independent of FK declaration order in T (#388)
        static constexpr std::array<std::string_view, fk_count_> fk_field_names_{std::meta::identifier_of(FKFields)...};

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
        static consteval auto calculate_join_sql_size() -> std::size_t {
            using utilities::numeric::digits_of;
            using utilities::sql_len::ON_EQUALS;
            using utilities::sql_len::SMALL_BUFFER;
            std::size_t total = 0;

            // Per FK: <keyword><table> t<alias> ON t<alias>.<pk> = t1.<fk>_id
            // The alias t<Is+2> appears twice — reserve its exact digit width both times.
            [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                ((total += get_join_keyword().size() + FKBase_at<Is>::table_name_.size() + 2 + digits_of(Is + 2) + 5 +
                           digits_of(Is + 2) + 1 + FKBase_at<Is>::pk_name_.size() + ON_EQUALS +
                           fk_field_names_[Is].size() + 3),
                 ...);
            }(std::make_index_sequence<fk_count_>{});

            return total + SMALL_BUFFER;
        }

        static consteval auto build_join_sql_array() {
            constexpr std::size_t     sql_size = calculate_join_sql_size();
            ConstexprString<sql_size> result;

            [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                ((result.append(get_join_keyword()),
                  result.append(FKBase_at<Is>::table_name_),
                  result.append(" t"),
                  result.append_uint(Is + 2),
                  result.append(" ON t"),
                  result.append_uint(Is + 2),
                  result.append("."),
                  result.append(FKBase_at<Is>::pk_name_),
                  result.append(" = t1."),
                  result.append(fk_field_names_[Is]),
                  result.append("_id")),
                 ...);
            }(std::make_index_sequence<fk_count_>{});

            return result;
        }

        // Iterate the FK indices [0, fk_count_) and invoke `body.template operator()<I>()` for each.
        // Used by the consteval SQL-builder helpers; encapsulates the index-sequence fold.
        template <typename Body> static consteval void for_each_fk_field(const Body& body) {
            [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                (body.template operator()<Is>(), ...);
            }(std::make_index_sequence<fk_count_>{});
        }

        static consteval auto calculate_select_fields_size() -> std::size_t {
            std::size_t total = 0;

            // Base fields
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                auto member = Base::all_members_[i];
                if (!Base::is_fk_field(member)) {
                    total += (total > 0 ? 2 : 0) + 3 + std::meta::identifier_of(member).size();
                }
            }

            // FK fields — each emits "t<alias>.<field>"; reserve the alias's exact digit width.
            for_each_fk_field([&]<std::size_t I>() {
                total += 2; // ", "
                [&]<std::size_t... FieldIs>(std::index_sequence<FieldIs...> /*unused*/) {
                    ((total += (FieldIs > 0 ? 2 : 0) + 2 + utilities::numeric::digits_of(I + 2) +
                               std::meta::identifier_of(FKBase_at<I>::all_members_[FieldIs]).size()),
                     ...);
                }(std::make_index_sequence<FKBase_at<I>::field_count_>{});
            });

            return total + utilities::sql_len::SMALL_BUFFER;
        }

        static consteval auto build_select_fields_array() {
            constexpr std::size_t        fields_size = calculate_select_fields_size();
            ConstexprString<fields_size> result;

            // Base fields
            bool first = true;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
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
            for_each_fk_field([&]<std::size_t I>() {
                result.append(", ");
                [&]<std::size_t... FieldIs>(std::index_sequence<FieldIs...> /*unused*/) {
                    bool first_in_table = true;
                    (((first_in_table ? (void)0 : result.append(", ")),
                      result.append("t"),
                      result.append_uint(I + 2),
                      result.append("."),
                      result.append(std::meta::identifier_of(FKBase_at<I>::all_members_[FieldIs])),
                      first_in_table = false),
                     ...);
                }(std::make_index_sequence<FKBase_at<I>::field_count_>{});
            });

            return result;
        }

        static constexpr auto join_sql_array      = build_join_sql_array();
        static constexpr auto select_fields_array = build_select_fields_array();

        // NEW: Compile-time complete SQL generation
        static consteval auto calculate_complete_sql_size() -> std::size_t {
            using utilities::sql_len::FROM;
            using utilities::sql_len::SELECT;
            using utilities::sql_len::SMALL_BUFFER;
            std::size_t total = 0;
            total += SELECT; // "SELECT "
            total += calculate_select_fields_size();
            total += FROM; // " FROM "
            total += Base::table_name_.size();
            total += 3; // " t1"
            total += calculate_join_sql_size();
            return total + SMALL_BUFFER;
        }

        static consteval auto build_complete_sql_array() {
            constexpr std::size_t     sql_size = calculate_complete_sql_size();
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

        template <std::size_t... Is>
        __attribute__((always_inline)) static void
        extract_t_fields(Statement* stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            int col_idx = 0;
            ((extract_column_at<Is>(stmt, obj, col_idx)), ...);
        }

        template <std::size_t MemberIdx>
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

        template <std::size_t FKIdx, std::size_t... FieldIs>
        __attribute__((always_inline)) static void extract_fk_fields_impl(
                Statement*      stmt,
                FK_type<FKIdx>& fk_obj,
                std::size_t     col_offset,
                std::index_sequence<FieldIs...> /*unused*/
        ) noexcept {
            using FKBase = FKBase_at<FKIdx>;
            ((extract_fk_field_at<FKBase, FieldIs>(stmt, fk_obj, col_offset + FieldIs)), ...);
        }

        template <typename FKBase, std::size_t FieldIdx>
        __attribute__((always_inline)) static void
        extract_fk_field_at(Statement* stmt, auto& fk_obj, int col_idx) noexcept {
            if constexpr (FieldIdx < FKBase::field_count_) {
                constexpr auto member = FKBase::all_members_[FieldIdx];
                using FieldType       = std::remove_cvref_t<decltype(fk_obj.[:member:])>;
                fk_obj.[:member:]     = Base::template extract_column_value<FieldType>(stmt, col_idx);
            }
        }

        template <std::size_t Idx>
        __attribute__((always_inline)) static void extract_fk_at(Statement* stmt, T& obj) noexcept {
            constexpr auto FKField            = FKFields...[Idx]; // C++26 pack indexing
            using RawFKFieldType              = std::remove_cvref_t<decltype(obj.[:FKField:])>;
            constexpr std::size_t field_count = FKBase_at<Idx>::field_count_;

            if constexpr (utilities::is_optional_v<RawFKFieldType>) {
                // Optional FK: NULL first joined column means no match → set nullopt
                const auto first_col = static_cast<int>(column_offsets_[Idx]);
                if (stmt->is_null(first_col)) {
                    obj.[:FKField:] = std::nullopt;
                } else {
                    FK_type<Idx> fk_inner{};
                    extract_fk_fields_impl<Idx>(
                            stmt, fk_inner, column_offsets_[Idx], std::make_index_sequence<field_count>{}
                    );
                    obj.[:FKField:] = std::move(fk_inner);
                }
            } else {
                auto& fk_obj = obj.[:FKField:];
                extract_fk_fields_impl<Idx>(
                        stmt, fk_obj, column_offsets_[Idx], std::make_index_sequence<field_count>{}
                );
            }
        }

        template <std::size_t... Is>
        __attribute__((always_inline)) static void
        extract_all_fks(Statement* stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            (extract_fk_at<Is>(stmt, obj), ...);
        }

        // FIX: Initialize all FK fields to default values before extraction
        // This ensures non-JOINed FK fields have proper default values instead of garbage
        template <std::size_t MemberIdx> __attribute__((always_inline)) static void init_fk_field_at(T& obj) noexcept {
            if constexpr (MemberIdx < Base::field_count_) {
                constexpr auto member = Base::all_members_[MemberIdx];
                if constexpr (Base::is_fk_field(member)) {
                    using FieldType = std::remove_cvref_t<decltype(obj.[:member:])>;
                    obj.[:member:]  = FieldType{}; // Default-construct FK object
                }
            }
        }

        template <std::size_t... Is>
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

    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, std::meta::info... FKFields>
        requires(sizeof...(FKFields) >= 1 && (FKFieldOf<T, FKFields> && ...))
    [[nodiscard]] auto make_join_wrapper() -> JoinStatementWrapper {
        using JS = JoinStatement<T, ConnType, Type, FKFields...>;

        return JoinStatementWrapper{
                +[]() -> const std::string& { return JS::get_complete_sql(); },
                +[](ErasedStatementPtr stmt, ErasedObjectPtr obj) -> void {
                    JS::extract_joined_row(static_cast<typename ConnType::Statement*>(stmt), *static_cast<T*>(obj));
                }
        };
    }

    // =========================================================================
    // MANY-TO-MANY JOIN (#203 model + schema; #391 two-query execution).
    //
    // Eager load runs as TWO queries (SelectStatement::execute_m2m_2query),
    // stitched client-side by a pk→entity hash map:
    //
    //   Q1 (build_base_subquery): the base entities to load —
    //     SELECT <base cols> FROM <Base> [WHERE …] [ORDER BY …] [LIMIT/OFFSET]
    //
    //   Q2 (build_q2_sql): their related rows, filtered by the same subquery —
    //     SELECT t2.<owner>_id, t3.<related cols>
    //     FROM <junction> t2 INNER JOIN <Related> t3 ON t2.<related>_id = t3.<rpk>
    //     WHERE t2.<owner>_id IN (<the SAME base subquery>)
    //
    // WHERE/ORDER BY/LIMIT/OFFSET select WHICH base entities load — they live in
    // Q1 and inside Q2's IN-subquery so both pick the same set. No outer ORDER BY,
    // no pk-adjacency contract: the stitch is a hash map (#391). INNER drops
    // zero-relation entities after the stitch; LEFT keeps them.
    //
    // get_complete_sql() still builds the modifier-free 3-table join — aggregates
    // (COUNT-over-m2m) count (base, related) PAIRS over it (select_prefix_arr_ +
    // join_suffix_arr_).
    //
    // Junction descriptor: auto mode (Through = void) uses <Base>_<Related> with
    // <Base>_id / <Related>_id columns; through mode uses the through model's
    // table with its FK field names (<field>_id).
    // =========================================================================
    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, std::meta::info M2MField>
        requires M2MFieldOf<T, M2MField> && (Type == JoinType::Inner || Type == JoinType::Left)
    class M2MJoinStatement : private BaseStatement<T> {
        friend class BaseStatement<T>;
        using Base      = BaseStatement<T>;
        using Statement = typename ConnType::Statement;

        // Re-derive the member from ^^T by identifier — annotation reads on a
        // reflection that crossed a BMI boundary segfault clang-p2996 (#262).
        static constexpr auto m2m_member_ = []() consteval {
            for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                if (std::meta::identifier_of(m) == std::meta::identifier_of(M2MField)) {
                    return m;
                }
            }
            std::unreachable(); // M2MFieldOf<T, M2MField> guarantees a match
        }();

        using ContainerType = std::remove_cvref_t<typename[:std::meta::type_of(m2m_member_):]>;
        using Related       = meta::m2m_related_t<ContainerType>;
        using Through       = meta::m2m_through_t<m2m_member_>;
        using RelatedBase   = BaseStatement<Related>;

        static_assert(!std::same_as<Related, T>, "self-referential many-to-many is not supported (#203)");

        // if constexpr keeps the concept from being checked with Through = void
        // (nonstatic_data_members_of(^^void) is not a constant expression).
        static consteval auto through_is_valid() -> bool {
            if constexpr (std::same_as<Through, void>) {
                return true;
            } else {
                return ThroughWithFKTo<Through, T> && ThroughWithFKTo<Through, Related>;
            }
        }
        static_assert(
                through_is_valid(), "through model must have exactly one FieldAttr::fk field for each side (#203)"
        );

        // ---- Junction descriptor ---------------------------------------------
        template <typename Side> static consteval auto find_through_fk() -> std::meta::info {
            for (auto m : std::meta::nonstatic_data_members_of(^^Through, std::meta::access_context::unchecked())) {
                auto attr = std::meta::annotation_of_type<meta::FieldAttr>(m);
                if (attr.has_value() && attr.value() == meta::FieldAttr::fk &&
                    std::meta::dealias(std::meta::type_of(m)) == std::meta::dealias(^^Side)) {
                    return m;
                }
            }
            std::unreachable(); // ThroughWithFKTo guarantees a match
        }

        // Auto-junction table name "<Base>_<Related>" needs static storage.
        static constexpr auto junction_table_arr_ = []() consteval {
            ConstexprString<Base::table_name_.size() + RelatedBase::table_name_.size() + 2> name;
            name.append(Base::table_name_);
            name.append("_");
            name.append(RelatedBase::table_name_);
            return name;
        }();

        static consteval auto junction_table_name() -> std::string_view {
            if constexpr (std::same_as<Through, void>) {
                return junction_table_arr_.view();
            } else {
                // ^^Through names the local alias — dealias to the model's own name
                return std::meta::identifier_of(std::meta::dealias(^^Through));
            }
        }

        // Junction column base names (the "_id" suffix is appended in the builder).
        static consteval auto owner_col_name() -> std::string_view {
            if constexpr (std::same_as<Through, void>) {
                return Base::table_name_;
            } else {
                return std::meta::identifier_of(find_through_fk<T>());
            }
        }
        static consteval auto related_col_name() -> std::string_view {
            if constexpr (std::same_as<Through, void>) {
                return RelatedBase::table_name_;
            } else {
                return std::meta::identifier_of(find_through_fk<Related>());
            }
        }

        static consteval auto join_keyword() -> std::string_view {
            return Type == JoinType::Inner ? " INNER JOIN " : " LEFT JOIN ";
        }

        // ---- Pre-computed SQL fragments --------------------------------------
        // select_prefix_: "SELECT t1.<c…>, t3.<r…> FROM (SELECT <cols> FROM <Base>"
        static consteval auto calculate_select_prefix_size() -> std::size_t {
            std::size_t total = 7; // "SELECT "
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                total += 2 + 3 + std::meta::identifier_of(Base::all_members_[i]).size() + 3;
            }
            for (std::size_t i = 0; i < RelatedBase::field_count_; ++i) {
                total += 2 + 3 + std::meta::identifier_of(RelatedBase::all_members_[i]).size() + 3;
            }
            total += 14 + Base::field_names_array_.len + 6 + Base::table_name_.size();
            return total + utilities::sql_len::SMALL_BUFFER;
        }

        // Append ", t3.<col>[_id]" for every related member to `result`. Shared
        // by build_select_prefix (1-query) and build_q2_prefix (2-query) — both
        // emit the related block as "t3"-aliased columns, leading-comma-separated.
        static consteval auto append_related_columns(auto& result) -> void {
            for (std::size_t i = 0; i < RelatedBase::field_count_; ++i) {
                result.append(", t3.");
                result.append(std::meta::identifier_of(RelatedBase::all_members_[i]));
                if (Base::is_fk_field(RelatedBase::all_members_[i])) {
                    result.append("_id");
                }
            }
        }

        static consteval auto build_select_prefix() {
            ConstexprString<calculate_select_prefix_size()> result;
            result.append("SELECT ");
            bool first = true;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                if (!first) {
                    result.append(", ");
                }
                result.append("t1.");
                result.append(std::meta::identifier_of(Base::all_members_[i]));
                if (Base::is_fk_field(Base::all_members_[i])) {
                    result.append("_id");
                }
                first = false;
            }
            append_related_columns(result);
            result.append(" FROM (SELECT ");
            result.append(Base::field_names_array_);
            result.append(" FROM ");
            result.append(Base::table_name_);
            return result;
        }

        // join_suffix_: ") t1 <KW> <junction> t2 ON t1.<pk> = t2.<owner>_id <KW> <Related> t3 ON t2.<related>_id =
        // t3.<rpk>"
        static consteval auto calculate_join_suffix_size() -> std::size_t {
            return 4 + (2 * join_keyword().size()) + junction_table_name().size() + 10 + Base::pk_name_.size() + 6 +
                   owner_col_name().size() + 3 + RelatedBase::table_name_.size() + 10 + related_col_name().size() + 9 +
                   RelatedBase::pk_name_.size() + utilities::sql_len::SMALL_BUFFER;
        }

        static consteval auto build_join_suffix() {
            ConstexprString<calculate_join_suffix_size()> result;
            result.append(") t1");
            result.append(join_keyword());
            result.append(junction_table_name());
            result.append(" t2 ON t1.");
            result.append(Base::pk_name_);
            result.append(" = t2.");
            result.append(owner_col_name());
            result.append("_id");
            result.append(join_keyword());
            result.append(RelatedBase::table_name_);
            result.append(" t3 ON t2.");
            result.append(related_col_name());
            result.append("_id = t3.");
            result.append(RelatedBase::pk_name_);
            return result;
        }

        static constexpr auto select_prefix_arr_ = build_select_prefix();
        static constexpr auto join_suffix_arr_   = build_join_suffix();

      public:
        // Modifier-free complete SQL — consumed by aggregates/set-ops via the
        // wrapper's get_complete_sql_fn (a COUNT over it counts (base, related) pairs).
        static auto get_complete_sql() -> const std::string& {
            static const std::string str =
                    std::string(select_prefix_arr_.view()) + std::string(join_suffix_arr_.view());
            return str;
        }

        // Append the base-entity clauses — WHERE / ORDER BY / LIMIT / OFFSET — to
        // `sql`. Shared by build_base_subquery (Q1) and build_q2_sql (Q2's
        // IN-subquery): in both, these clauses select WHICH base entities load.
        static auto append_base_clauses(
                std::string&                            sql,
                const orm::where::ExpressionVariantPtr& where_expr,
                const std::optional<OrderByWrapper>&    order_by,
                const std::optional<int>&               limit,
                const std::optional<int>&               offset
        ) -> void {
            if (where_expr) {
                sql += " WHERE ";
                sql += orm::where::to_sql(*where_expr);
            }
            Base::template append_order_by<ConnType>(sql, order_by);
            Base::template append_limit_offset<ConnType>(sql, limit, offset);
        }

        // =====================================================================
        // TWO-QUERY PREDICATE-PUSHDOWN PATH (#391)
        //
        // Q1: SELECT <base cols> FROM <Base> [WHERE …][ORDER BY …][LIMIT/OFFSET]
        //     — the entities to load (a plain base SELECT; no join, no sorter).
        // Q2: SELECT t2.<owner>_id, t3.<related cols>
        //     FROM <junction> t2 INNER JOIN <Related> t3 ON t2.<related>_id = t3.<rpk>
        //     WHERE t2.<owner>_id IN (<the SAME base subquery as Q1>)
        //     — related rows for exactly the loaded entities, no base columns
        //       duplicated, no ORDER BY.
        //
        // SelectStatement runs both inside a transaction, builds a pk→entity hash
        // map from Q1, and stitches each Q2 row by owner pk. INNER drops
        // zero-relation entities after the stitch; LEFT keeps them (Q1 already
        // yielded them, Q2 just leaves their container empty). Q2 is always an
        // INNER junction⋈related join — the INNER/LEFT distinction is purely a
        // post-stitch filter, never an SQL difference.
        // =====================================================================

        // Q1 — the base entity subquery. Identical text to the subquery embedded
        // in Q2's IN clause, so the two stay in lockstep.
        static auto build_base_subquery(
                const orm::where::ExpressionVariantPtr& where_expr,
                const std::optional<OrderByWrapper>&    order_by,
                const std::optional<int>&               limit,
                const std::optional<int>&               offset
        ) -> std::string {
            std::string sql = "SELECT ";
            sql += Base::field_names_array_.view();
            sql += " FROM ";
            sql += Base::table_name_;
            append_base_clauses(sql, where_expr, order_by, limit, offset);
            return sql;
        }

        // q2_prefix_: "SELECT t2.<owner>_id, t3.<r…> FROM <junction> t2
        //              INNER JOIN <Related> t3 ON t2.<related>_id = t3.<rpk>
        //              WHERE t2.<owner>_id IN (SELECT <base.pk> FROM <Base>"
        static consteval auto calculate_q2_prefix_size() -> std::size_t {
            std::size_t total = 7 + 3 + owner_col_name().size() + 3; // "SELECT " + "t2." + owner + "_id"
            for (std::size_t i = 0; i < RelatedBase::field_count_; ++i) {
                total += 2 + 3 + std::meta::identifier_of(RelatedBase::all_members_[i]).size() + 3;
            }
            total += 6 + junction_table_name().size() + 13 + RelatedBase::table_name_.size() + 10 +
                     related_col_name().size() + 9 + RelatedBase::pk_name_.size();   // FROM…ON…
            total += 10 + owner_col_name().size() + 16 + Base::pk_name_.size() + 6 + // WHERE…IN (SELECT pk FROM
                     Base::table_name_.size();
            return total + utilities::sql_len::SMALL_BUFFER;
        }

        static consteval auto build_q2_prefix() {
            ConstexprString<calculate_q2_prefix_size()> result;
            result.append("SELECT t2.");
            result.append(owner_col_name());
            result.append("_id");
            append_related_columns(result);
            result.append(" FROM ");
            result.append(junction_table_name());
            result.append(" t2 INNER JOIN ");
            result.append(RelatedBase::table_name_);
            result.append(" t3 ON t2.");
            result.append(related_col_name());
            result.append("_id = t3.");
            result.append(RelatedBase::pk_name_);
            result.append(" WHERE t2.");
            result.append(owner_col_name());
            result.append("_id IN (SELECT ");
            result.append(Base::pk_name_);
            result.append(" FROM ");
            result.append(Base::table_name_);
            return result;
        }

        static constexpr auto q2_prefix_arr_ = build_q2_prefix();

        // Q2 — related rows for the entities selected by the same base subquery.
        // The WHERE/ORDER BY/LIMIT/OFFSET live inside the IN-subquery so they
        // bound which entities load (never the related collection), exactly as in
        // the 1-query design. The trailing ")" closes the IN-subquery.
        static auto build_q2_sql(
                const orm::where::ExpressionVariantPtr& where_expr,
                const std::optional<OrderByWrapper>&    order_by,
                const std::optional<int>&               limit,
                const std::optional<int>&               offset
        ) -> std::string {
            std::string sql{q2_prefix_arr_.view()};
            append_base_clauses(sql, where_expr, order_by, limit, offset);
            sql += ")";
            return sql;
        }

        // Q2 row owner pk (column 0) — keys the stitch into the Q1 hash map.
        static auto extract_q2_owner_pk(Statement* stmt) noexcept -> std::int64_t {
            return stmt->extract_int64(0);
        }

        // Append the Q2 row's related object into obj's container. Q2 related
        // columns start at index 1 (after the owner pk). Always present — Q2 is
        // an INNER join, so there is never a NULL related row to skip.
        static auto append_related_q2(Statement* stmt, T& obj) noexcept -> void {
            insert_related<1>(stmt, obj);
        }

        // True when this entity has at least one related row — drives the
        // INNER-join post-stitch drop of zero-relation entities.
        static auto container_empty(const T& obj) noexcept -> bool {
            return obj.[:m2m_member_:].empty();
        }

      private:
        template <typename C, typename V> static auto insert_into(C& container, V&& value) -> void {
            if constexpr (requires { container.push_back(std::forward<V>(value)); }) {
                container.push_back(std::forward<V>(value));
            } else {
                container.insert(std::forward<V>(value)); // plf::hive
            }
        }

        // Extract one Related from the row (columns start at RelColOffset) and
        // append it to obj's container — wrapping in shared_ptr if the container
        // holds shared_ptr elements. Used by append_related_q2 (Q2 related columns
        // start at offset 1, after the owner pk).
        template <int RelColOffset> static auto insert_related(Statement* stmt, T& obj) noexcept -> void {
            Related rel{};
            extract_related<RelColOffset>(stmt, rel, std::make_index_sequence<RelatedBase::field_count_>{});
            using Elem = typename ContainerType::value_type;
            if constexpr (meta::is_shared_ptr_v<Elem>) {
                insert_into(obj.[:m2m_member_:], std::make_shared<Related>(std::move(rel)));
            } else {
                insert_into(obj.[:m2m_member_:], std::move(rel));
            }
        }

        // RelColOffset = column index where the related block starts. The 1-query
        // path puts related columns after the base columns (offset Base::field_count_);
        // the 2-query Q2 path puts them after the single owner-pk column (offset 1).
        template <int RelColOffset, std::size_t... Is>
        static auto extract_related(Statement* stmt, Related& rel, std::index_sequence<Is...> /*unused*/) noexcept
                -> void {
            ((extract_related_at<RelColOffset, Is>(stmt, rel)), ...);
        }

        // Mirrors plain-SELECT semantics per related member: regular columns extract
        // by value; FK columns hold only the foreign pk (pk-only FK object).
        template <int RelColOffset, std::size_t I>
        static auto extract_related_at(Statement* stmt, Related& rel) noexcept -> void {
            constexpr auto member = RelatedBase::all_members_[I];
            constexpr int  col    = RelColOffset + static_cast<int>(I);
            using FieldType       = std::remove_cvref_t<decltype(rel.[:member:])>;
            if constexpr (Base::is_fk_field(member)) {
                extract_related_fk<FieldType, member>(stmt, rel, col);
            } else {
                rel.[:member:] = Base::template extract_column_value<FieldType>(stmt, col);
            }
        }

        template <typename FieldType, std::meta::info Member>
        static auto extract_related_fk(Statement* stmt, Related& rel, int col) noexcept -> void {
            using InnerFK        = utilities::optional_inner_type_t<FieldType>;
            constexpr auto fk_pk = Base::template find_fk_primary_key<FieldType>();
            using PKType         = std::remove_cvref_t<decltype(std::declval<InnerFK>().[:fk_pk:])>;
            if constexpr (utilities::is_optional_v<FieldType>) {
                if (stmt->is_null(col)) {
                    rel.[:Member:] = std::nullopt;
                    return;
                }
            }
            InnerFK inner{};
            inner.[:fk_pk:] = Base::template extract_column_value<PKType>(stmt, col);
            rel.[:Member:]  = std::move(inner);
        }
    };

    template <typename T, storm::db::DatabaseConnection ConnType, JoinType Type, std::meta::info M2MField>
        requires M2MFieldOf<T, M2MField>
    [[nodiscard]] auto make_m2m_join_wrapper() -> JoinStatementWrapper {
        using JS = M2MJoinStatement<T, ConnType, Type, M2MField>;
        return JoinStatementWrapper{
                .get_complete_sql_fn = +[]() -> const std::string& { return JS::get_complete_sql(); },
                // Two-query path (#391) — see JoinStatementWrapper for the contract.
                .build_q1_sql_fn = +[](const orm::where::ExpressionVariantPtr& where_expr,
                                       const std::optional<OrderByWrapper>&    order_by,
                                       const std::optional<int>&               limit,
                                       const std::optional<int>&               offset) -> std::string {
                    return JS::build_base_subquery(where_expr, order_by, limit, offset);
                },
                .build_q2_sql_fn = +[](const orm::where::ExpressionVariantPtr& where_expr,
                                       const std::optional<OrderByWrapper>&    order_by,
                                       const std::optional<int>&               limit,
                                       const std::optional<int>&               offset) -> std::string {
                    return JS::build_q2_sql(where_expr, order_by, limit, offset);
                },
                .extract_q2_owner_pk_fn = +[](ErasedStatementPtr stmt) -> std::int64_t {
                    return JS::extract_q2_owner_pk(static_cast<typename ConnType::Statement*>(stmt));
                },
                .append_related_q2_fn = +[](ErasedStatementPtr stmt, ErasedObjectPtr obj) -> void {
                    JS::append_related_q2(static_cast<typename ConnType::Statement*>(stmt), *static_cast<T*>(obj));
                },
                .container_empty_fn =
                        +[](ErasedObjectPtr obj) -> bool { return JS::container_empty(*static_cast<T*>(obj)); },
                .m2m_is_left = (Type == JoinType::Left)
        };
    }

} // namespace storm::orm::statements
