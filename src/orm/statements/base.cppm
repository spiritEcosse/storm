module;

// Single cohesive class template; thresholds intentionally relaxed (see #264 finding).
// `duplicate` removed in #277 Phase 3 (for_each_field_name + bind_bulk_objects_impl + bind_expr_or_reset helpers).

#include <meta>

export module storm_orm_statements_base;

import std;

import storm_db_concept;
import storm_orm_utilities;
import storm_orm_statements_orderby;
import storm_orm_where;

export namespace storm::orm::statements {

    // Import utilities for compile-time string building
    using storm::orm::utilities::ConstexprString;

    // Mirror of meta::FieldAttr from storm module - must match exactly
    namespace meta {
        enum class FieldAttr : std::uint8_t {
            primary,
            primary_autoincrement,
            indexed,
            unique,
            fk,
            auto_create,
            auto_update
        };

        // A field is "a primary key" for either annotation variant: plain `primary`
        // (plain INTEGER PRIMARY KEY) or `primary_autoincrement` (the SQLite never-reuse
        // opt-in, #379). Every PK-detection site routes through here so the two variants
        // can never drift apart.
        consteval auto is_primary_attr(FieldAttr attr) -> bool {
            using enum FieldAttr;
            return attr == primary || attr == primary_autoincrement;
        }

        // Many-to-many annotation (#203). Phase 1 (auto-generated junction table):
        //   [[= storm::meta::many_to_many]] std::vector<Course> courses;
        // Phase 2 (explicit junction model):
        //   [[= storm::meta::many_to_many_through<Enrollment>]] std::vector<Course> courses;
        // FieldAttr is an enum, so a templated enumerator is impossible — a class-template
        // annotation carries the optional through-model type instead.
        template <typename Through = void> struct ManyToMany {
            using through_type = Through;
        };
        inline constexpr ManyToMany<>                                    many_to_many{};
        template <typename Through> inline constexpr ManyToMany<Through> many_to_many_through{};

        // Reflection of the ManyToMany<...> annotation TYPE carried by `member`, if any.
        consteval auto m2m_annotation_type_of(std::meta::info member) -> std::optional<std::meta::info> {
            for (const auto annotation : std::meta::annotations_of(member)) {
                const auto type = std::meta::type_of(annotation);
                if (std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^ManyToMany) {
                    return type;
                }
            }
            return std::nullopt;
        }

        consteval auto is_m2m_field(std::meta::info member) -> bool {
            return m2m_annotation_type_of(member).has_value();
        }

        // True for m2m members WITHOUT a through model (auto-generated junction table).
        consteval auto is_m2m_auto(std::meta::info member) -> bool {
            auto type = m2m_annotation_type_of(member);
            return type.has_value() && std::meta::dealias(std::meta::template_arguments_of(type.value())[0]) == ^^void;
        }

        // Through model of an m2m member (void = auto-generated junction table).
        template <std::meta::info Member>
        using m2m_through_t = typename[:std::meta::template_arguments_of(m2m_annotation_type_of(Member).value())[0]:];

        // Related model type extracted from a container field type via C++26 std::meta (#203):
        // vector<Course> → Course, plf::hive<Track> → Track,
        // vector<shared_ptr<Course>> / vector<unique_ptr<Course>> → Course.
        consteval auto related_type_from_container(std::meta::info container_type) -> std::meta::info {
            const auto first =
                    std::meta::dealias(std::meta::template_arguments_of(std::meta::dealias(container_type))[0]);
            if (std::meta::has_template_arguments(first)) {
                // ^^std::shared_ptr names a using-declarator under `import std;` and can't
                // be reflected directly — derive the canonical template from a concrete
                // specialization instead.
                const auto tmpl            = std::meta::template_of(first);
                const auto shared_ptr_tmpl = std::meta::template_of(std::meta::dealias(^^std::shared_ptr<int>));
                const auto unique_ptr_tmpl = std::meta::template_of(std::meta::dealias(^^std::unique_ptr<int>));
                if (tmpl == shared_ptr_tmpl || tmpl == unique_ptr_tmpl) {
                    return std::meta::dealias(std::meta::template_arguments_of(first)[0]);
                }
            }
            return first;
        }

        template <typename Container> using m2m_related_t = typename[:related_type_from_container(^^Container):];

        // Detect std::shared_ptr container elements (m2m append path wraps the
        // extracted related object in make_shared, #203).
        template <typename T> struct is_shared_ptr : std::false_type {};
        template <typename TValue> struct is_shared_ptr<std::shared_ptr<TValue>> : std::true_type {};
        template <typename TValue> constexpr bool is_shared_ptr_v = is_shared_ptr<TValue>::value;
    } // namespace meta

    // Concept: T must have at least one field annotated with FieldAttr::primary.
    //
    // Because a primary key is itself a non-static data member, satisfying this concept
    // also guarantees `field_count_ >= 1`. That invariant is what makes the INSERT batch
    // divides `MAX_DB_VARIABLES / field_count_` (insert.cppm) safe from division by zero —
    // the divisor can never be 0 for any T that reaches a statement class (issue #362, item A).
    template <typename T>
    concept ModelWithPrimaryKey = []() consteval {
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            auto attr = std::meta::annotation_of_type<meta::FieldAttr>(m);
            if (attr.has_value() && meta::is_primary_attr(attr.value())) {
                return true;
            }
        }
        return false;
    }();

    // A field carrying auto_create/auto_update must be a system_clock::time_point (#209).
    // Referenced by a static_assert in bind_field_at_index so a wrong-typed timestamp field
    // fails to compile with a clear message rather than deep inside parameter binding.
    template <std::meta::info Member>
    concept ValidTimestampField = std::
            same_as<std::remove_cvref_t<typename[:std::meta::type_of(Member):]>, std::chrono::system_clock::time_point>;

    // A JOIN field selector must reflect a non-static data member of T annotated with
    // FieldAttr::fk (#388). Constrains QuerySet::join/left_join/right_join and
    // JoinStatement so a non-member or non-FK argument fails at the call site with a
    // clear constraint violation.
    //
    // The annotation is read from the member re-derived out of ^^T (matched by
    // identifier), NOT from Member itself: annotation_of_type on a reflection that
    // crossed a BMI boundary segfaults clang-p2996 (#262), while structural queries
    // (is_nonstatic_data_member / parent_of / identifier_of) are safe on it.
    template <typename T, std::meta::info Member>
    concept FKFieldOf = []() consteval {
        if (!std::meta::is_nonstatic_data_member(Member) || !std::meta::has_identifier(Member)) {
            return false;
        }
        if (std::meta::parent_of(Member) != ^^T) {
            return false;
        }
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(m) == std::meta::identifier_of(Member)) {
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(m);
                return field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk;
            }
        }
        return false;
    }();

    // A many-to-many JOIN field selector must reflect a non-static data member of T
    // carrying a ManyToMany annotation (#203). Same BMI-boundary discipline as
    // FKFieldOf: the annotation is read from the member re-derived out of ^^T.
    template <typename T, std::meta::info Member>
    concept M2MFieldOf = []() consteval {
        if (!std::meta::is_nonstatic_data_member(Member) || !std::meta::has_identifier(Member)) {
            return false;
        }
        if (std::meta::parent_of(Member) != ^^T) {
            return false;
        }
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(m) == std::meta::identifier_of(Member)) {
                return meta::is_m2m_field(m);
            }
        }
        return false;
    }();

    // A through model must carry exactly one FieldAttr::fk member of type Side
    // (#203 Phase 2) — that member names the junction FK column (<identifier>_id).
    template <typename Through, typename Side>
    concept ThroughWithFKTo = []() consteval {
        std::size_t count = 0;
        for (auto m : std::meta::nonstatic_data_members_of(^^Through, std::meta::access_context::unchecked())) {
            auto attr = std::meta::annotation_of_type<meta::FieldAttr>(m);
            if (attr.has_value() && attr.value() == meta::FieldAttr::fk &&
                std::meta::dealias(std::meta::type_of(m)) == std::meta::dealias(^^Side)) {
                ++count;
            }
        }
        return count == 1;
    }();

    // Shared reflection utilities for all statement types
    template <typename T>
        requires ModelWithPrimaryKey<T>
    class BaseStatement { // NOSONAR(cpp:S1448) - statement base centralises all shared reflection utilities; splitting
                          // would scatter compile-time SQL logic
      public:
        // Compile-time accessor for table name (used in SQL generation)
        static consteval auto get_table_name() -> std::string_view {
            return table_name_;
        }

      protected:
        // Helper to find primary key using storm::meta attributes
        static consteval auto find_primary_key_impl() -> std::meta::info {
            for (const std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
                if (field_attr.has_value() && meta::is_primary_attr(field_attr.value())) {
                    return member;
                }
            }
            std::unreachable(); // never reached: ModelWithPrimaryKey<T> guarantees a primary key exists
        }

        // FK field detection utilities
        static consteval auto is_fk_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk;
        }

        // Unique field detection
        static consteval auto is_unique_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::unique;
        }

        // Indexed field detection (explicit [[= FieldAttr::indexed]])
        static consteval auto is_indexed_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::indexed;
        }

        // Auto-timestamp detection (#209): auto_create stamps now() on INSERT only;
        // auto_update stamps now() on both INSERT and UPDATE.
        static consteval auto is_auto_create_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::auto_create;
        }

        static consteval auto is_auto_update_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::auto_update;
        }

        // True when `member` should be stamped with now() on this operation: auto_update
        // always; auto_create on INSERT only (IsUpdate=false). auto_create on UPDATE is
        // false here so it binds the object's stored value (preserving created_at).
        static consteval auto stamps_now(std::meta::info member, bool is_update) -> bool {
            return is_auto_update_field(member) || (is_auto_create_field(member) && !is_update);
        }

        // Check if a field needs an index (indexed, unique, or fk — but not primary key)
        static consteval auto needs_index(std::meta::info member) -> bool {
            using enum meta::FieldAttr;
            if (member == primary_key_) {
                return false;
            }
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            if (!field_attr.has_value()) {
                return false;
            }
            auto val = field_attr.value();
            return val == indexed || val == unique || val == fk;
        }

        // Get database column name for FK field: User sender → "sender_id"
        static consteval auto get_fk_column_name(std::meta::info member) -> std::string {
            const std::string field_name(std::meta::identifier_of(member));
            return field_name + "_id";
        }

        // Find primary key of a FK type (unwraps std::optional<T> → T first)
        template <typename FKType>
            requires ModelWithPrimaryKey<utilities::optional_inner_type_t<FKType>>
        static consteval auto find_fk_primary_key() -> std::meta::info {
            using InnerType = utilities::optional_inner_type_t<FKType>;
            for (const std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^InnerType, std::meta::access_context::unchecked())) {
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
                if (field_attr.has_value() && meta::is_primary_attr(field_attr.value())) {
                    return member;
                }
            }
            std::unreachable(); // never reached: requires ModelWithPrimaryKey<...> guarantees a primary key exists
        }

        // Number of PERSISTED fields. Many-to-many container members map to a junction
        // table, not to a column, so they are invisible to INSERT/SELECT/UPDATE/SCHEMA (#203).
        static consteval auto get_field_count() -> std::size_t {
            std::size_t count = 0;
            for (const auto member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                if (!meta::is_m2m_field(member)) {
                    ++count;
                }
            }
            return count;
        }

        // Pre-compute all persisted field members at compile-time (m2m members filtered, #203)
        template <std::size_t N> static consteval auto get_all_field_members() -> std::array<std::meta::info, N> {
            std::array<std::meta::info, N> result{};
            std::size_t                    idx = 0;
            for (const auto member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                if (!meta::is_m2m_field(member) && idx < N) {
                    result[idx++] = member;
                }
            }
            return result;
        }

        // Shared iterator over data members, honouring SkipPrimaryKey, invoking
        // `body(i, is_fk, name)` per emitted field. The size-calculator and the
        // list-builder used to spell out this loop independently.
        template <bool SkipPrimaryKey, typename Body> static consteval auto for_each_field_name(Body body) -> void {
            bool first = true;
            for (std::size_t i = 0; i < field_count_; ++i) {
                if constexpr (SkipPrimaryKey) {
                    if (all_members_[i] == primary_key_) {
                        continue;
                    }
                }
                auto       field_attr = std::meta::annotation_of_type<meta::FieldAttr>(all_members_[i]);
                bool const is_fk      = field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk;
                body(i, is_fk, !first);
                first = false;
            }
        }

        // Unified field name size calculation at compile-time
        // Template parameter controls whether to skip primary key (for INSERT vs SELECT)
        template <bool SkipPrimaryKey> static consteval auto calculate_field_names_size_impl() -> std::size_t {
            std::size_t size = 0;
            for_each_field_name<SkipPrimaryKey>([&](std::size_t i, bool is_fk, bool needs_comma) {
                if (needs_comma) {
                    size += 2; // ", "
                }
                size += std::meta::identifier_of(all_members_[i]).size();
                if (is_fk) {
                    size += 3; // "_id"
                }
            });
            return size;
        }

        // Calculate size of all field names string at compile-time
        static consteval auto calculate_field_names_size() -> std::size_t {
            return calculate_field_names_size_impl<false>();
        }

        // Calculate size of non-PK field names string at compile-time
        static consteval auto calculate_non_pk_field_names_size() -> std::size_t {
            return calculate_field_names_size_impl<true>();
        }

        // Unified field name list builder at compile-time
        // Template parameter controls whether to skip primary key (for INSERT vs SELECT)
        template <bool SkipPrimaryKey> static consteval auto build_field_names_list_impl() {
            constexpr std::size_t size = calculate_field_names_size_impl<SkipPrimaryKey>() + 10;
            ConstexprString<size> result;
            for_each_field_name<SkipPrimaryKey>([&](std::size_t i, bool is_fk, bool needs_comma) {
                if (needs_comma) {
                    result.append(", ");
                }
                result.append(std::meta::identifier_of(all_members_[i]));
                if (is_fk) {
                    result.append("_id");
                }
            });
            return result;
        }

        // Build comma-separated list of all field names (for SELECT statements)
        // FK fields are mapped to their column names (User sender → sender_id)
        static consteval auto build_all_field_names_list() {
            return build_field_names_list_impl<false>();
        }

        // Build comma-separated list of NON-PRIMARY KEY fields (for INSERT statements)
        // Excludes primary key to allow auto-increment
        static consteval auto build_non_pk_field_names_list() {
            return build_field_names_list_impl<true>();
        }

      public:
        // Pre-computed field information - made public for QuerySet and JOIN optimization
        static constexpr auto field_count_ = get_field_count();
        // Makes the ModelWithPrimaryKey invariant explicit: a model with a primary-key
        // member always has >= 1 field, so the INSERT divides MAX_DB_VARIABLES / field_count_
        // can never divide by zero (issue #362, item A). Fires here if the concept is ever
        // loosened, instead of producing UB at the divide.
        static_assert(field_count_ >= 1, "A model must have at least one field (its primary key)");
        static constexpr auto           all_members_       = get_all_field_members<field_count_>();
        static constexpr auto           field_names_array_ = build_all_field_names_list();
        static inline const std::string field_names_       = std::string(field_names_array_);

        // True if T has any auto_create/auto_update field (#209). Gates the per-operation
        // system_clock::now() read so models with no timestamp fields pay zero overhead.
        static constexpr bool has_auto_timestamp_field_ = []() consteval {
            for (const auto m : all_members_) {
                if (is_auto_create_field(m) || is_auto_update_field(m)) {
                    return true;
                }
            }
            return false;
        }();

        // True if T has any many-to-many container field (#203/#391). Gates the
        // two-query eager-load path so models WITHOUT an m2m field never instantiate
        // it (scans raw members — m2m fields are filtered out of all_members_).
        static constexpr bool has_m2m_field_ = []() consteval {
            return std::ranges::any_of(
                    std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()),
                    [](std::meta::info m) { return meta::is_m2m_field(m); }
            );
        }();

        // One clock read per operation, but only for models that actually have a timestamp
        // field — otherwise the call compiles away entirely (no regression on plain models).
        [[nodiscard]] __attribute__((always_inline)) static auto batch_now() noexcept
                -> std::chrono::system_clock::time_point {
            if constexpr (has_auto_timestamp_field_) {
                return std::chrono::system_clock::now();
            } else {
                return {};
            }
        }

        // Reflection data - made public for JOIN statement access
        static constexpr auto primary_key_ = find_primary_key_impl();
        static constexpr auto pk_name_     = std::meta::identifier_of(primary_key_);
        static constexpr auto table_name_  = std::meta::identifier_of(^^T);

      protected:
        // Index sequence utilities for compile-time field binding
        using field_indices_t = std::make_index_sequence<field_count_>;

        // Helper template for compile-time field binding with index sequence
        template <typename ConnType, typename Statement, std::size_t... Is>
        [[nodiscard]] static auto
        bind_all_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...> /*unused*/) noexcept
                -> std::expected<void, typename ConnType::Error> {
            int                                           param_index = 1;
            std::expected<void, typename ConnType::Error> result{};
            const auto now = batch_now(); // shared by all fields of this object (compiles away if none)
            ((result = bind_field_at_index<ConnType, Is>(&stmt, obj, param_index, now), result.has_value()) && ...);
            return result;
        }

        // Helper template for INSERT binding (skips primary key for auto-increment)
        template <typename ConnType, typename Statement, std::size_t... Is>
        [[nodiscard]] static auto
        bind_non_pk_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...> /*unused*/) noexcept
                -> std::expected<void, typename ConnType::Error> {
            int                                           param_index = 1;
            std::expected<void, typename ConnType::Error> result{};
            const auto now = batch_now(); // shared by all fields of this object (compiles away if none)
            ((result = bind_field_at_index<ConnType, Is, true>(&stmt, obj, param_index, now), result.has_value()) &&
             ...);
            return result;
        }

        // Unified field binder: binds a single field at compile-time index.
        // SkipPK=true skips primary key fields (for INSERT/UPDATE non-PK binding).
        // IsUpdate=true marks the UPDATE path so auto_create fields bind the object's
        // stored value instead of now() (#209). `now` is read once per operation by the
        // caller and threaded in so every row in a batch shares the same timestamp.
        // Auto-increments param_index on successful bind.
        template <typename ConnType, std::size_t Index, bool SkipPK = false, bool IsUpdate = false>
        [[nodiscard]] __attribute__((always_inline)) static constexpr auto bind_field_at_index(
                typename ConnType::Statement*         stmt,
                const T&                              obj,
                int&                                  param_index,
                std::chrono::system_clock::time_point now = {}
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            constexpr auto member = all_members_[Index];

            // Auto-timestamp fields (#209) must be system_clock::time_point — binding now()
            // to any other type is a model error. Fires at the call site with a clear message.
            if constexpr (is_auto_create_field(member) || is_auto_update_field(member)) {
                static_assert(
                        ValidTimestampField<member>,
                        "auto_create / auto_update fields must be std::chrono::system_clock::time_point"
                );
            }

            // Compile-time PK skip for INSERT/UPDATE non-PK paths
            if constexpr (SkipPK && member == primary_key_) {
                return {};
            }
            // Auto-timestamp (#209): stamp now() for auto_update (always) and auto_create
            // (INSERT only). auto_create on UPDATE is not stamped here and falls through to
            // the normal bind below, preserving the object's stored created_at.
            else if constexpr (stamps_now(member, IsUpdate)) {
                return bind_one<ConnType>(stmt, param_index, now);
            }
            // FK field - extract and bind the PK value from the foreign object
            else if constexpr (is_fk_field(member)) {
                return bind_fk_field_at_index<ConnType, Index>(stmt, obj, param_index);
            } else {
                return bind_one<ConnType>(stmt, param_index, obj.[:member:]);
            }
        }

        // Bind one value at param_index and advance it on success. Shared tail used by the
        // plain-field and auto-timestamp branches of bind_field_at_index.
        template <typename ConnType>
        [[nodiscard]] __attribute__((always_inline)) static constexpr auto
        bind_one(typename ConnType::Statement* stmt, int& param_index, const auto& value) noexcept
                -> std::expected<void, typename ConnType::Error> {
            auto result = bind_value_by_type<ConnType>(*stmt, param_index, value);
            if (!result) {
                return std::unexpected(result.error());
            }
            ++param_index;
            return {};
        }

        // Extract and bind the foreign object's PK value (NULL for an empty optional FK).
        template <typename ConnType, std::size_t Index>
        [[nodiscard]] __attribute__((always_inline)) static constexpr auto
        bind_fk_field_at_index(typename ConnType::Statement* stmt, const T& obj, int& param_index) noexcept
                -> std::expected<void, typename ConnType::Error> {
            constexpr auto member = all_members_[Index];
            using FKType          = std::remove_cvref_t<decltype(obj.[:member:])>;
            if constexpr (utilities::is_optional_v<FKType>) {
                // Optional FK: bind NULL when empty, otherwise bind the inner PK value
                if (!obj.[:member:].has_value()) {
                    auto null_result = stmt->bind_null(param_index);
                    if (!null_result) {
                        return std::unexpected(null_result.error());
                    }
                    ++param_index;
                    return {};
                }
                constexpr auto fk_pk_member = find_fk_primary_key<FKType>();
                return bind_one<ConnType>(stmt, param_index, obj.[:member:].value().[:fk_pk_member:]);
            } else {
                constexpr auto fk_pk_member = find_fk_primary_key<FKType>();
                return bind_one<ConnType>(stmt, param_index, obj.[:member:].[:fk_pk_member:]);
            }
        }

        // Bulk binding scaffolding: iterate objects, fold the per-field bind over the
        // index sequence, return on first error. Bound to bind_field_at_index<...>
        // through the SkipPrimaryKey template parameter so a single body serves
        // both the all-fields and non-PK variants.
        template <bool SkipPrimaryKey, typename ConnType, typename Statement, typename ContainerType, std::size_t... Is>
        [[nodiscard]] static auto bind_bulk_objects_impl(
                Statement& stmt, const ContainerType& objects, std::index_sequence<Is...> /*unused*/
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            int param_index = 1;
            // One now() per batch, reused for every row (#209): all rows share the same
            // created_at/updated_at, matching the issue's batch-timestamp intent.
            const auto now = batch_now();

            for (const auto& obj : objects) {
                std::expected<void, typename ConnType::Error> result{};
                ((result = bind_field_at_index<ConnType, Is, SkipPrimaryKey>(&stmt, obj, param_index, now),
                  result.has_value()) &&
                 ...);
                if (!result) {
                    return result;
                }
            }
            return {};
        }

        // Helper for bulk binding multiple objects with index sequence
        template <typename ConnType, typename Statement, typename ContainerType, std::size_t... Is>
        [[nodiscard]] static auto bind_all_objects_bulk_impl(
                Statement& stmt, const ContainerType& objects, std::index_sequence<Is...> seq
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            return bind_bulk_objects_impl<false, ConnType, Statement, ContainerType, Is...>(stmt, objects, seq);
        }

        // Helper for bulk INSERT binding (skips PK for auto-increment)
        template <typename ConnType, typename Statement, typename ContainerType, std::size_t... Is>
        [[nodiscard]] static auto bind_non_pk_objects_bulk_impl(
                Statement& stmt, const ContainerType& objects, std::index_sequence<Is...> seq
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            return bind_bulk_objects_impl<true, ConnType, Statement, ContainerType, Is...>(stmt, objects, seq);
        }

        // Common batch operation thresholds
        static constexpr std::size_t MAX_DB_VARIABLES = 999;

        // Adaptive threshold calculation based on batch size and field count
        // Returns the optimal threshold for deciding between bulk SQL and individual inserts
        static constexpr auto calculate_adaptive_threshold(std::size_t batch_size, std::size_t max_bulk_size)
                -> std::size_t {
            using utilities::batch::FALLBACK_BATCH_SIZE;
            using utilities::batch::SMALL_THRESHOLD;

            // For very small batches, always use bulk SQL up to the SQLite limit
            if (batch_size <= SMALL_THRESHOLD) {
                return max_bulk_size;
            }

            // Calculate safe thresholds based on max_bulk_size (which already accounts for field count)
            // max_bulk_size = 999 / field_count, so we scale our thresholds accordingly

            // For small-medium batches, use bulk SQL if safe
            // Use 50% of max_bulk_size as the sweet spot for bulk operations
            const std::size_t bulk_sweet_spot = std::max(FALLBACK_BATCH_SIZE, max_bulk_size / 2);

            if (batch_size <= bulk_sweet_spot) {
                return bulk_sweet_spot; // Use bulk SQL - most efficient
            }

            // For medium batches, try to use bulk if within 80% of SQLite limit
            const std::size_t bulk_max_safe = (max_bulk_size * 4) / 5; // 80% of max

            if (batch_size <= bulk_max_safe) {
                return bulk_max_safe; // Push bulk SQL to near SQLite limit
            }

            // For large batches (>80% of SQLite limit), use individual inserts with transaction
            // Avoids hitting SQLite variable limits and better memory usage
            return FALLBACK_BATCH_SIZE; // Force individual insert path - safe for any field count
        }

        // Common binding utilities for different types
        // Delegates to unified bind_parameter_value in utilities
        template <typename ConnType>
        [[nodiscard]] static auto
        bind_value_by_type(typename ConnType::Statement& stmt, int param_index, const auto& value) noexcept
                -> std::expected<void, typename ConnType::Error> {
            return utilities::bind_parameter_value<typename ConnType::Statement, typename ConnType::Error>(
                    stmt, param_index, value
            );
        }

        // ---- Storage-class predicates ---------------------------------------------------
        // Each predicate groups the source types that share one extract_* backend call.
        // Used by extract_column_value() to dispatch by storage class, not by source type.

        template <typename FT>
        static constexpr bool is_int_stored_v =
                std::is_same_v<FT, int> || std::is_same_v<FT, short> || std::is_same_v<FT, unsigned short> ||
                std::is_same_v<FT, unsigned int> || std::is_same_v<FT, signed char> ||
                std::is_same_v<FT, unsigned char> || std::is_same_v<FT, char>;

        template <typename FT>
        static constexpr bool is_int64_stored_v =
                std::is_same_v<FT, std::int64_t> || std::is_same_v<FT, long> || std::is_same_v<FT, long long> ||
                std::is_same_v<FT, std::uint64_t> || std::is_same_v<FT, unsigned long> ||
                std::is_same_v<FT, unsigned long long>;

        template <typename FT> static constexpr bool is_integer_stored_v = is_int_stored_v<FT> || is_int64_stored_v<FT>;

        template <typename FT>
        static constexpr bool is_floating_stored_v = std::is_same_v<FT, double> || std::is_same_v<FT, float>;

        template <typename FT>
        static constexpr bool is_blob_stored_v =
                std::is_same_v<FT, std::vector<std::uint8_t>> || std::is_same_v<FT, std::vector<unsigned char>> ||
                std::is_same_v<FT, std::vector<std::byte>>;

        template <typename FT>
        static constexpr bool is_text_stored_v = std::is_same_v<FT, std::chrono::year_month_day> ||
                                                 std::is_same_v<FT, std::chrono::system_clock::time_point> ||
                                                 std::is_same_v<FT, std::filesystem::path> ||
                                                 std::is_same_v<FT, utilities::UUID> || std::is_same_v<FT, std::string>;

        // ---- Storage-class extraction helpers --------------------------------------------
        // Each helper handles one storage class. extract_column_value() dispatches to
        // exactly one helper per FieldType. All marked always_inline so the call site
        // collapses to the same machine code as the inlined branch did before.

        template <typename Statement>
        [[nodiscard]] __attribute__((always_inline)) static auto read_text_view(Statement* stmt, int col_idx) noexcept
                -> std::string_view {
            const unsigned char* text = stmt->extract_text_ptr(col_idx);
            if (text == nullptr) {
                return {};
            }
            return std::string_view(reinterpret_cast<const char*>(text), stmt->extract_bytes(col_idx));
        }

        template <typename FieldType, typename Statement>
        [[nodiscard]] __attribute__((always_inline)) static auto extract_int_like(Statement* stmt, int col_idx) noexcept
                -> FieldType {
            if constexpr (is_int_stored_v<FieldType>) {
                return static_cast<FieldType>(stmt->extract_int(col_idx));
            } else {
                static_assert(is_int64_stored_v<FieldType>, "extract_int_like: caller must pre-check storage class");
                return static_cast<FieldType>(stmt->extract_int64(col_idx));
            }
        }

        template <typename FieldType, typename Statement>
        [[nodiscard]] __attribute__((always_inline)) static auto
        extract_floating_like(Statement* stmt, int col_idx) noexcept -> FieldType {
            if constexpr (std::is_same_v<FieldType, double>) {
                return stmt->extract_double(col_idx);
            } else {
                static_assert(
                        std::is_same_v<FieldType, float>, "extract_floating_like: caller must pre-check storage class"
                );
                return stmt->extract_float(col_idx);
            }
        }

        template <typename FieldType, typename Statement>
        [[nodiscard]] __attribute__((always_inline)) static auto extract_enum(Statement* stmt, int col_idx) noexcept
                -> FieldType {
            using Underlying = std::underlying_type_t<FieldType>;
            if constexpr (sizeof(Underlying) <= sizeof(int)) {
                return static_cast<FieldType>(stmt->extract_int(col_idx));
            } else {
                return static_cast<FieldType>(stmt->extract_int64(col_idx));
            }
        }

        template <typename FieldType, typename Statement>
        [[nodiscard]] __attribute__((always_inline)) static auto
        extract_text_like(Statement* stmt, int col_idx) noexcept -> FieldType {
            const std::string_view view = read_text_view(stmt, col_idx);
            if constexpr (std::is_same_v<FieldType, std::chrono::year_month_day>) {
                // LCOV_EXCL_START — NOT NULL column, defensive fallback for empty text
                if (view.empty()) {
                    return FieldType{};
                }
                // LCOV_EXCL_STOP
                return utilities::chrono_conv::string_to_ymd(view);
            } else if constexpr (std::is_same_v<FieldType, std::chrono::system_clock::time_point>) {
                // LCOV_EXCL_START — NOT NULL column, defensive fallback for empty text
                if (view.empty()) {
                    return FieldType{};
                }
                // LCOV_EXCL_STOP
                return utilities::chrono_conv::string_to_tp(view);
            } else if constexpr (std::is_same_v<FieldType, std::filesystem::path>) {
                // LCOV_EXCL_START — NOT NULL column, defensive fallback for empty text
                if (view.empty()) {
                    return FieldType{};
                }
                // LCOV_EXCL_STOP
                return std::filesystem::path(std::string(view));
            } else if constexpr (std::is_same_v<FieldType, utilities::UUID>) {
                // LCOV_EXCL_START — NOT NULL column, defensive fallback for empty text
                if (view.empty()) {
                    return FieldType{};
                }
                // LCOV_EXCL_STOP
                return utilities::UUID{std::string(view)};
            } else {
                static_assert(std::is_same_v<FieldType, std::string>, "extract_text_like: unhandled text storage type");
                return FieldType(view);
            }
        }

        template <typename FieldType, typename Statement>
        [[nodiscard]] __attribute__((always_inline)) static auto
        extract_blob_like(Statement* stmt, int col_idx) noexcept -> FieldType {
            const void* blob = stmt->extract_blob_ptr(col_idx);
            const int   size = stmt->extract_bytes(col_idx);
            if (blob == nullptr || size <= 0) {
                return FieldType{};
            }
            using Byte       = typename FieldType::value_type;
            const auto* data = static_cast<const Byte*>(blob);
            return FieldType(data, data + size);
        }

        // Shared column extraction utility — returns value of specified type from given
        // column index. Dispatches by storage class to one of the extract_* helpers above.
        // Supported FieldType groups: optional<T>, bool, int-stored ints, int64-stored
        // ints, enum, double, float, chrono duration, text-stored types, blob-stored types.
        template <typename FieldType, typename Statement>
        [[nodiscard]] __attribute__((always_inline)) static auto
        extract_column_value(Statement* stmt, int col_idx) noexcept -> FieldType {
            if constexpr (utilities::is_optional_v<FieldType>) {
                using InnerType = typename FieldType::value_type;
                if (stmt->is_null(col_idx)) {
                    return std::nullopt;
                }
                return FieldType{extract_column_value<InnerType>(stmt, col_idx)};
            } else if constexpr (std::is_same_v<FieldType, bool>) {
                return stmt->extract_bool(col_idx);
            } else if constexpr (is_integer_stored_v<FieldType>) {
                return extract_int_like<FieldType>(stmt, col_idx);
            } else if constexpr (std::is_enum_v<FieldType>) {
                return extract_enum<FieldType>(stmt, col_idx);
            } else if constexpr (is_floating_stored_v<FieldType>) {
                return extract_floating_like<FieldType>(stmt, col_idx);
            } else if constexpr (utilities::is_chrono_duration_v<FieldType>) {
                return FieldType{static_cast<typename FieldType::rep>(stmt->extract_int64(col_idx))};
            } else if constexpr (is_text_stored_v<FieldType>) {
                return extract_text_like<FieldType>(stmt, col_idx);
            } else if constexpr (is_blob_stored_v<FieldType>) {
                return extract_blob_like<FieldType>(stmt, col_idx);
            } else {
                // FieldType-dependent false so the assertion only fires when this
                // branch is selected (i.e. when none of the supported groups matched).
                static_assert(
                        !std::is_same_v<FieldType, FieldType>,
                        "Unsupported field type for column extraction. Supported types: "
                        "int, int64_t, long, short, char, unsigned variants, enum, "
                        "float, double, bool, std::string, chrono types, "
                        "filesystem::path, UUID, std::optional<T>, "
                        "std::vector<uint8_t>, std::vector<std::byte>"
                );
            }
        }

        // =====================================================================
        // COLUMN EXTRACTION HELPERS - Moved here from SelectStatement so that
        // constexpr access to all_members_[Index] happens in base.cppm context
        // (avoids P2996 experimental compiler limitation in select.cppm module)
        // =====================================================================

        // Extract optional FK column: set nullopt when NULL, otherwise extract inner PK
        template <std::size_t Index, typename Statement, typename FieldType>
        __attribute__((always_inline)) static void extract_optional_fk_column(Statement* stmt, T& obj) noexcept {
            constexpr auto member       = all_members_[Index];
            using InnerFKType           = utilities::optional_inner_type_t<FieldType>;
            constexpr auto fk_pk_member = find_fk_primary_key<FieldType>();
            using PKType                = std::remove_cvref_t<decltype(std::declval<InnerFKType>().[:fk_pk_member:])>;
            if (stmt->is_null(Index)) {
                obj.[:member:] = std::nullopt;
            } else {
                InnerFKType fk_inner{};
                fk_inner.[:fk_pk_member:] = extract_column_value<PKType>(stmt, Index);
                obj.[:member:]            = std::move(fk_inner);
            }
        }

        // Extract single column into obj at compile-time index
        // Statement is deduced from stmt pointer; all_members_[Index] is valid here
        template <std::size_t Index, typename Statement>
        __attribute__((always_inline)) static void extract_column_fast(Statement* stmt, T& obj) noexcept {
            if constexpr (Index < field_count_) {
                constexpr auto member = all_members_[Index];
                using FieldType       = std::remove_cvref_t<decltype(obj.[:member:])>;
                if constexpr (is_fk_field(member)) {
                    if constexpr (utilities::is_optional_v<FieldType>) {
                        extract_optional_fk_column<Index, Statement, FieldType>(stmt, obj);
                    } else {
                        obj.[:member:]              = FieldType{};
                        constexpr auto fk_pk_member = find_fk_primary_key<FieldType>();
                        using PKType                = std::remove_cvref_t<decltype(obj.[:member:].[:fk_pk_member:])>;
                        obj.[:member:].[:fk_pk_member:] = extract_column_value<PKType>(stmt, Index);
                    }
                } else {
                    obj.[:member:] = extract_column_value<FieldType>(stmt, Index);
                }
            }
        }

        // Expand index sequence and extract each column
        template <typename Statement, std::size_t... Is>
        __attribute__((always_inline)) static void
        extract_all_columns_impl(Statement* stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            ((extract_column_fast<Is>(stmt, obj)), ...);
        }

        // Entry point: extract all columns into obj using field_indices_t
        template <typename Statement>
        __attribute__((always_inline)) static void extract_all_columns(Statement* stmt, T& obj) noexcept {
            extract_all_columns_impl(stmt, obj, field_indices_t{});
        }

        // Transaction management utilities
        template <typename ConnType>
        [[nodiscard]] static auto begin_transaction(ConnType& conn) noexcept
                -> std::expected<void, typename ConnType::Error> {
            return conn.execute("BEGIN TRANSACTION");
        }

        template <typename ConnType>
        [[nodiscard]] static auto commit_transaction(ConnType& conn) noexcept
                -> std::expected<void, typename ConnType::Error> {
            return conn.execute("COMMIT");
        }

        template <typename ConnType> static auto rollback_transaction(ConnType& conn) noexcept -> void {
            (void)conn.execute("ROLLBACK");
        }

        // Utility to determine if transaction should be used
        template <typename ContainerType> // NOSONAR(cpp:S6024) - static member needed for access to class template
                                          // context
        static constexpr auto should_use_transaction(const ContainerType& container) -> bool {
            return container.size() > 1;
        }

        // Unified statement execution logic for cached/non-cached connections
        template <typename ConnType, typename PrepareFunc, typename BindExecuteFunc>
        [[nodiscard]] static auto execute_statement(
                ConnType&                      conn,
                const std::string&             sql,
                [[maybe_unused]] PrepareFunc&& prepare_func,
                BindExecuteFunc&&              bind_execute_func
        ) noexcept -> decltype(bind_execute_func(std::declval<typename ConnType::Statement>())) {
            // Use cached prepared statement if available
            if constexpr (requires { conn.prepare_cached(sql); }) {
                return conn.prepare_cached(sql).and_then(
                        [bind_execute_func = std::forward<BindExecuteFunc>(bind_execute_func)](
                                auto* stmt
                        ) -> decltype(bind_execute_func(std::declval<typename ConnType::Statement>())) {
                            return bind_execute_func(*stmt);
                        }
                );
            } else {
                // Fallback to regular prepare
                return conn.prepare(sql).and_then(
                        [bind_execute_func = std::forward<BindExecuteFunc>(bind_execute_func)](
                                typename ConnType::Statement stmt
                        ) -> decltype(bind_execute_func(std::move(stmt))) { return bind_execute_func(std::move(stmt)); }
                );
            }
        }

        // Unified transaction wrapper for batch operations
        template <typename ConnType, typename Operation>
        [[nodiscard]] static auto
        execute_with_transaction(ConnType& conn, bool use_transaction, const Operation& op) noexcept -> decltype(op()) {
            if (!use_transaction) {
                return op();
            }

            // Begin transaction with monadic composition
            return begin_transaction(conn).and_then([&op, &conn]() -> decltype(op()) {
                auto op_result = op();
                if (!op_result) {
                    rollback_transaction(conn);
                    return op_result;
                }

                // Commit transaction
                if (auto commit_result = commit_transaction(conn); !commit_result) {
                    rollback_transaction(conn);
                    return std::unexpected(commit_result.error());
                }

                return op_result;
            });
        }

        // Generic helper for executing with cached or non-cached statements
        template <typename ConnType, typename ExecuteFunc>
        [[nodiscard]] static auto
        execute_with_statement(ConnType& conn, const std::string& sql, const ExecuteFunc& execute_func) noexcept
                -> decltype(execute_func(std::declval<typename ConnType::Statement&>())) {
            // Try cached statement first if available
            if constexpr (requires { conn.prepare_cached(sql); }) {
                return conn.prepare_cached(sql).and_then([&execute_func](auto* stmt) -> decltype(auto) {
                    return execute_func(*stmt);
                });
            } else {
                // Fallback to regular prepare
                return conn.prepare(sql).and_then(
                        [&execute_func](typename ConnType::Statement stmt) mutable -> decltype(auto) {
                            return execute_func(stmt);
                        }
                );
            }
        }

        // Monadic helper for bind and execute operations
        template <typename BindResult, typename Statement>
        [[nodiscard]] static auto bind_and_execute(BindResult bind_result, Statement& stmt) noexcept -> BindResult {
            return bind_result.and_then([&stmt]() -> decltype(auto) { return stmt.execute(); });
        }

        // Monadic helper for reset, bind, and execute sequence
        template <typename Statement, typename BindFunc>
        [[nodiscard]] static auto reset_bind_and_execute(Statement& stmt, const BindFunc& bind_func) noexcept
                -> decltype(bind_func(stmt)) {
            stmt.reset();
            return bind_func(stmt).and_then([&stmt]() -> decltype(auto) { return stmt.execute(); });
        }

        // Dispatch helper for WHERE/JOIN execution paths
        // Eliminates repeated branching logic in aggregate statements
        template <typename SimpleF, typename WhereF, typename JoinF, typename WhereJoinF>
        [[nodiscard]] static auto dispatch_execute(
                bool              has_join,
                bool              has_where,
                const SimpleF&    simple_fn,
                const WhereF&     where_fn,
                const JoinF&      join_fn,
                const WhereJoinF& where_join_fn
        ) -> decltype(simple_fn()) {
            if (has_join && has_where) {
                return where_join_fn();
            }
            if (has_join) {
                return join_fn();
            }
            if (has_where) {
                return where_fn();
            }
            return simple_fn();
        }

        // =====================================================================
        // SQL CLAUSE HELPERS - Shared across SELECT, DISTINCT, AGGREGATE
        // =====================================================================

        // LCOV_EXCL_START — PostgreSQL-only; covered by CI PG tests, not local SQLite mock
        // Helper: Adapt ORDER BY SQL for PostgreSQL NULL ordering semantics
        // Adds NULLS FIRST after ASC and NULLS LAST after DESC to match SQLite behavior
        static void adapt_order_by_for_pg(std::string& adapted) {
            std::size_t pos = 0;
            while ((pos = adapted.find(" ASC", pos)) != std::string::npos) {
                std::size_t const after = pos + 4;
                if (adapted.substr(after, 6) != " NULLS") {
                    adapted.insert(after, " NULLS FIRST");
                }
                pos = after + 12;
            }
            pos = 0;
            while ((pos = adapted.find(" DESC", pos)) != std::string::npos) {
                std::size_t const after = pos + 5;
                if (adapted.substr(after, 6) != " NULLS") {
                    adapted.insert(after, " NULLS LAST");
                }
                pos = after + 11;
            }
        }
        // LCOV_EXCL_STOP

        // Helper: Append ORDER BY clause to SQL from wrapper
        // NOTE: ORDER BY must come before LIMIT/OFFSET in SQLite
        // For PostgreSQL, adds NULLS FIRST/LAST to match SQLite semantics
        template <typename ConnTypeForDialect = void>
        __attribute__((always_inline)) static void
        append_order_by(std::string& sql, const std::optional<OrderByWrapper>& order_by_wrapper) {
            if (order_by_wrapper.has_value() && !order_by_wrapper->empty()) {
                const auto& order_sql = order_by_wrapper->get_order_by_sql();
                if constexpr (requires { ConnTypeForDialect::uses_pg_dialect; }) {
                    std::string adapted = order_sql; // LCOV_EXCL_LINE — PG-only
                    adapt_order_by_for_pg(adapted);  // LCOV_EXCL_LINE — PG-only
                    sql += adapted;                  // LCOV_EXCL_LINE — PG-only
                } else {
                    sql += order_sql;
                }
            }
        }

        // Helper: Append LIMIT/OFFSET clauses to SQL
        // NOTE: SQLite requires LIMIT when using OFFSET, so we use LIMIT -1 (meaning unlimited)
        // PostgreSQL uses LIMIT ALL for unlimited rows
        template <typename ConnTypeForDialect = void>
        __attribute__((always_inline)) static void
        append_limit_offset(std::string& sql, const std::optional<int>& limit, const std::optional<int>& offset) {
            if (limit.has_value()) {
                sql += " LIMIT ";
                sql += std::to_string(limit.value());
            } else if (offset.has_value()) {
                // Need LIMIT when using OFFSET
                if constexpr (requires { ConnTypeForDialect::supports_limit_all; }) {
                    if constexpr (ConnTypeForDialect::supports_limit_all) {
                        sql += " LIMIT ALL";
                    } else {
                        sql += " LIMIT -1";
                    }
                } else {
                    sql += " LIMIT -1"; // Default: SQLite-compatible
                }
            }

            if (offset.has_value()) {
                sql += " OFFSET ";
                sql += std::to_string(offset.value());
            }
        }

        // Helper: Bind WHERE expression parameters to statement
        // Returns std::expected<void, Error> - resets statement on failure
        // Helper: bind a WHERE-style expression and convert the result. On
        // failure, reset the statement and propagate as std::unexpected.
        // Used by both bind_where_params (starts at param_index = 1) and
        // bind_having_params (continues from WHERE's last index).
        template <typename Statement, typename Error>
        [[nodiscard]] __attribute__((always_inline)) static auto
        bind_expr_or_reset(Statement* stmt_ptr, const orm::where::ExpressionVariantPtr& expr, int& param_index)
                -> std::expected<void, Error> {
            auto bind_result = orm::where::bind_params_direct<Statement, Error>(*expr, stmt_ptr, param_index);
            if (!bind_result) [[unlikely]] {
                stmt_ptr->reset();
                return std::unexpected(bind_result.error());
            }
            return {};
        }

        template <typename Statement, typename Error>
        [[nodiscard]] __attribute__((always_inline)) static auto
        bind_where_params(Statement* stmt_ptr, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<void, Error> {
            int param_index = 1;
            return bind_expr_or_reset<Statement, Error>(stmt_ptr, where_expr, param_index);
        }

        // Helper: Bind HAVING expression parameters to statement
        // param_index continues from WHERE's last index (or starts at 1 if no WHERE)
        template <typename Statement, typename Error>
        [[nodiscard]] __attribute__((always_inline)) static auto
        bind_having_params(Statement* stmt_ptr, const orm::where::ExpressionVariantPtr& having_expr, int& param_index)
                -> std::expected<void, Error> {
            return bind_expr_or_reset<Statement, Error>(stmt_ptr, having_expr, param_index);
        }
    };

} // namespace storm::orm::statements
