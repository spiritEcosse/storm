module;

#include <meta>

export module storm_orm_statements_base;

import storm_db_concept;
import storm_orm_utilities;
import storm_orm_statements_orderby;
import storm_orm_where;

import <expected>;
import <string>;
import <string_view>;
import <concepts>;
import <format>;
import <meta>;
import <array>;
import <utility>;
import <optional>;
import <vector>;
import <cstdint>;
import <memory>;

export namespace storm::orm::statements {

    // Import utilities for compile-time string building
    using storm::orm::utilities::ConstexprString;

    // Mirror of meta::FieldAttr from storm module - must match exactly
    namespace meta {
        enum class FieldAttr { primary, indexed, unique, fk };
    }

    // Concept: T must have at least one field annotated with FieldAttr::primary
    template <typename T>
    concept ModelWithPrimaryKey = []() consteval {
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            auto attr = std::meta::annotation_of_type<meta::FieldAttr>(m);
            if (attr.has_value() && attr.value() == meta::FieldAttr::primary)
                return true;
        }
        return false;
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
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::primary) {
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

        // Check if a field needs an index (indexed, unique, or fk — but not primary key)
        static consteval auto needs_index(std::meta::info member) -> bool {
            using enum meta::FieldAttr;
            if (member == primary_key_)
                return false;
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            if (!field_attr.has_value())
                return false;
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
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::primary) {
                    return member;
                }
            }
            std::unreachable(); // never reached: requires ModelWithPrimaryKey<...> guarantees a primary key exists
        }

        // Helper to get the number of fields
        static consteval auto get_field_count() -> size_t {
            return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()).size();
        }

        // Pre-compute all field members at compile-time
        template <size_t N> static consteval auto get_all_field_members() -> std::array<std::meta::info, N> {
            std::array<std::meta::info, N> result{};
            auto members = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());

            for (size_t i = 0; i < N && i < members.size(); ++i) {
                result[i] = members[i];
            }
            return result;
        }

        // Unified field name size calculation at compile-time
        // Template parameter controls whether to skip primary key (for INSERT vs SELECT)
        template <bool SkipPrimaryKey> static consteval auto calculate_field_names_size_impl() -> size_t {
            size_t size  = 0;
            bool   first = true;
            for (size_t i = 0; i < field_count_; ++i) {
                if constexpr (SkipPrimaryKey) {
                    if (all_members_[i] == primary_key_) {
                        continue;
                    }
                }
                if (!first) {
                    size += 2; // ", "
                }
                // Check if this is a FK field
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(all_members_[i]);
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                    size += std::meta::identifier_of(all_members_[i]).size() + 3; // +3 for "_id"
                } else {
                    size += std::meta::identifier_of(all_members_[i]).size();
                }
                first = false;
            }
            return size;
        }

        // Calculate size of all field names string at compile-time
        static consteval auto calculate_field_names_size() -> size_t {
            return calculate_field_names_size_impl<false>();
        }

        // Calculate size of non-PK field names string at compile-time
        static consteval auto calculate_non_pk_field_names_size() -> size_t {
            return calculate_field_names_size_impl<true>();
        }

        // Unified field name list builder at compile-time
        // Template parameter controls whether to skip primary key (for INSERT vs SELECT)
        template <bool SkipPrimaryKey> static consteval auto build_field_names_list_impl() {
            constexpr size_t      size = calculate_field_names_size_impl<SkipPrimaryKey>() + 10;
            ConstexprString<size> result;
            bool                  first = true;
            for (size_t i = 0; i < field_count_; ++i) {
                if constexpr (SkipPrimaryKey) {
                    if (all_members_[i] == primary_key_) {
                        continue;
                    }
                }
                if (!first) {
                    result.append(", ");
                }
                // Check if this is a FK field
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(all_members_[i]);
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                    result.append(std::meta::identifier_of(all_members_[i]));
                    result.append("_id");
                } else {
                    result.append(std::meta::identifier_of(all_members_[i]));
                }
                first = false;
            }
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
        static constexpr auto           field_count_       = get_field_count();
        static constexpr auto           all_members_       = get_all_field_members<field_count_>();
        static constexpr auto           field_names_array_ = build_all_field_names_list();
        static inline const std::string field_names_       = std::string(field_names_array_);

        // Reflection data - made public for JOIN statement access
        static constexpr auto primary_key_ = find_primary_key_impl();
        static constexpr auto pk_name_     = std::meta::identifier_of(primary_key_);
        static constexpr auto table_name_  = std::meta::identifier_of(^^T);

      protected:
        // Index sequence utilities for compile-time field binding
        using field_indices_t = std::make_index_sequence<field_count_>;

        // Helper template for compile-time field binding with index sequence
        template <typename ConnType, typename Statement, size_t... Is>
        [[nodiscard]] static auto
        bind_all_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...> /*unused*/) noexcept
                -> std::expected<void, typename ConnType::Error> {
            int                                           param_index = 1;
            std::expected<void, typename ConnType::Error> result{};
            ((result = bind_field_at_index<ConnType, Is>(&stmt, obj, param_index), result.has_value()) && ...);
            return result;
        }

        // Helper template for INSERT binding (skips primary key for auto-increment)
        template <typename ConnType, typename Statement, size_t... Is>
        [[nodiscard]] static auto
        bind_non_pk_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...> /*unused*/) noexcept
                -> std::expected<void, typename ConnType::Error> {
            int                                           param_index = 1;
            std::expected<void, typename ConnType::Error> result{};
            ((result = bind_field_at_index<ConnType, Is, true>(&stmt, obj, param_index), result.has_value()) && ...);
            return result;
        }

        // Unified field binder: binds a single field at compile-time index
        // SkipPK=true skips primary key fields (for INSERT/UPDATE non-PK binding)
        // Auto-increments param_index on successful bind
        template <typename ConnType, size_t Index, bool SkipPK = false>
        [[nodiscard]] __attribute__((always_inline)) static constexpr auto
        bind_field_at_index(typename ConnType::Statement* stmt, const T& obj, int& param_index) noexcept
                -> std::expected<void, typename ConnType::Error> {
            constexpr auto member = all_members_[Index];

            // Compile-time PK skip for INSERT/UPDATE non-PK paths
            if constexpr (SkipPK && member == primary_key_) {
                return {};
            }
            // FK field - extract and bind the PK value from the foreign object
            else if constexpr (is_fk_field(member)) {
                using FKType = std::remove_cvref_t<decltype(obj.[:member:])>;
                if constexpr (utilities::is_optional_v<FKType>) {
                    // Optional FK: bind NULL when empty, otherwise bind the inner PK value
                    std::expected<void, typename ConnType::Error> result{};
                    if (obj.[:member:].has_value()) {
                        constexpr auto fk_pk_member = find_fk_primary_key<FKType>();
                        auto           pk_value     = obj.[:member:].value().[:fk_pk_member:];
                        result                      = bind_value_by_type<ConnType>(*stmt, param_index, pk_value);
                    } else {
                        result = stmt->bind_null(param_index);
                    }
                    if (!result) {
                        return std::unexpected(result.error()); // LCOV_EXCL_LINE
                    } // LCOV_EXCL_LINE
                } else {
                    auto           fk_object    = obj.[:member:];
                    constexpr auto fk_pk_member = find_fk_primary_key<FKType>();
                    auto           pk_value     = fk_object.[:fk_pk_member:];
                    auto           result       = bind_value_by_type<ConnType>(*stmt, param_index, pk_value);
                    if (!result) {
                        return std::unexpected(result.error());
                    }
                }
                ++param_index;
                return {};
            } else {
                auto field_value = obj.[:member:];
                auto result      = bind_value_by_type<ConnType>(*stmt, param_index, field_value);
                if (!result) {
                    return std::unexpected(result.error());
                }
                ++param_index;
                return {};
            }
        }

        // Helper for bulk binding multiple objects with index sequence
        template <typename ConnType, typename Statement, typename ContainerType, size_t... Is>
        [[nodiscard]] static auto bind_all_objects_bulk_impl(
                Statement& stmt, const ContainerType& objects, std::index_sequence<Is...> /*unused*/
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            int param_index = 1;

            for (const auto& obj : objects) {
                std::expected<void, typename ConnType::Error> result{};
                ((result = bind_field_at_index<ConnType, Is>(&stmt, obj, param_index), result.has_value()) && ...);
                if (!result) {
                    return result;
                }
            }
            return {};
        }

        // Helper for bulk INSERT binding (skips PK for auto-increment)
        template <typename ConnType, typename Statement, typename ContainerType, size_t... Is>
        [[nodiscard]] static auto bind_non_pk_objects_bulk_impl(
                Statement& stmt, const ContainerType& objects, std::index_sequence<Is...> /*unused*/
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            int param_index = 1;

            for (const auto& obj : objects) {
                std::expected<void, typename ConnType::Error> result{};
                ((result = bind_field_at_index<ConnType, Is, true>(&stmt, obj, param_index), result.has_value()) &&
                 ...);
                if (!result) {
                    return result;
                }
            }
            return {};
        }

        // Common batch operation thresholds
        static constexpr size_t MAX_DB_VARIABLES = 999;

        // Adaptive threshold calculation based on batch size and field count
        // Returns the optimal threshold for deciding between bulk SQL and individual inserts
        static constexpr auto calculate_adaptive_threshold(size_t batch_size, size_t max_bulk_size) -> size_t {
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
            const size_t bulk_sweet_spot = std::max(FALLBACK_BATCH_SIZE, max_bulk_size / 2);

            if (batch_size <= bulk_sweet_spot) {
                return bulk_sweet_spot; // Use bulk SQL - most efficient
            }

            // For medium batches, try to use bulk if within 80% of SQLite limit
            const size_t bulk_max_safe = (max_bulk_size * 4) / 5; // 80% of max

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

        // Shared column extraction utility - returns value of specified type from given column index
        // Handles: int, int64_t, uint64_t, short, float, double, bool, string, optional<T>, vector<uint8_t>
        template <typename FieldType, typename Statement>
        [[nodiscard]] __attribute__((always_inline)) static auto
        extract_column_value(Statement* stmt, int col_idx) noexcept -> FieldType {
            // Handle std::optional types first
            if constexpr (utilities::is_optional_v<FieldType>) {
                using InnerType = typename FieldType::value_type;
                if (stmt->is_null(col_idx)) {
                    return std::nullopt;
                }
                InnerType inner_value = extract_column_value<InnerType>(stmt, col_idx);
                return FieldType{std::move(inner_value)};
            }
            // Boolean type (stored as INTEGER 0/1)
            else if constexpr (std::is_same_v<FieldType, bool>) {
                return stmt->extract_bool(col_idx);
            }
            // Integer types
            else if constexpr (std::is_same_v<FieldType, int>) {
                return stmt->extract_int(col_idx);
            } else if constexpr (std::is_same_v<FieldType, int64_t> || std::is_same_v<FieldType, long> ||
                                 std::is_same_v<FieldType, long long> || std::is_same_v<FieldType, uint64_t> ||
                                 std::is_same_v<FieldType, unsigned long> ||
                                 std::is_same_v<FieldType, unsigned long long>) {
                return static_cast<FieldType>(stmt->extract_int64(col_idx));
            } else if constexpr (std::is_same_v<FieldType, short> || std::is_same_v<FieldType, unsigned short> ||
                                 std::is_same_v<FieldType, unsigned int>) {
                return static_cast<FieldType>(stmt->extract_int(col_idx));
            }
            // Floating point types
            else if constexpr (std::is_same_v<FieldType, double>) {
                return stmt->extract_double(col_idx);
            } else if constexpr (std::is_same_v<FieldType, float>) {
                return stmt->extract_float(col_idx);
            }
            // BLOB types
            else if constexpr (std::is_same_v<FieldType, std::vector<uint8_t>> ||
                               std::is_same_v<FieldType, std::vector<unsigned char>>) {
                const void* blob = stmt->extract_blob_ptr(col_idx);
                const int   size = stmt->extract_bytes(col_idx);
                if (blob && size > 0) {
                    const auto* data = static_cast<const uint8_t*>(blob);
                    return FieldType(data, data + size);
                }
                return FieldType{};
            }
            // String types
            else if constexpr (std::is_same_v<FieldType, std::string>) {
                const unsigned char* text = stmt->extract_text_ptr(col_idx);
                if (text) {
                    int len = stmt->extract_bytes(col_idx);
                    return FieldType(reinterpret_cast<const char*>(text), len);
                }
                return FieldType{};
            } else {
                static_assert(
                        std::is_same_v<FieldType, int> || std::is_same_v<FieldType, std::string>,
                        "Unsupported field type for column extraction. Supported types: "
                        "int, int64_t, long, short, unsigned variants, "
                        "float, double, bool, std::string, "
                        "std::optional<T>, std::vector<uint8_t>"
                );
            }
        }

        // =====================================================================
        // COLUMN EXTRACTION HELPERS - Moved here from SelectStatement so that
        // constexpr access to all_members_[Index] happens in base.cppm context
        // (avoids P2996 experimental compiler limitation in select.cppm module)
        // =====================================================================

        // Extract optional FK column: set nullopt when NULL, otherwise extract inner PK
        template <size_t Index, typename Statement, typename FieldType>
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
        template <size_t Index, typename Statement>
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
        template <typename Statement, size_t... Is>
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
            size_t pos = 0;
            while ((pos = adapted.find(" ASC", pos)) != std::string::npos) {
                size_t after = pos + 4;
                if (adapted.substr(after, 6) != " NULLS") {
                    adapted.insert(after, " NULLS FIRST");
                }
                pos = after + 12;
            }
            pos = 0;
            while ((pos = adapted.find(" DESC", pos)) != std::string::npos) {
                size_t after = pos + 5;
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
        template <typename Statement, typename Error>
        [[nodiscard]] __attribute__((always_inline)) static auto
        bind_where_params(Statement* stmt_ptr, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<void, Error> {
            int  param_index = 1;
            auto bind_result = orm::where::bind_params_direct<Statement, Error>(*where_expr, stmt_ptr, param_index);
            if (!bind_result) [[unlikely]] {
                stmt_ptr->reset();
                return std::unexpected(bind_result.error());
            }
            return {};
        }

        // Helper: Bind HAVING expression parameters to statement
        // param_index continues from WHERE's last index (or starts at 1 if no WHERE)
        // LCOV_EXCL_START — tested via AggregateTest.Having*; C++26 module coverage gap
        template <typename Statement, typename Error>
        [[nodiscard]] __attribute__((always_inline)) static auto
        bind_having_params(Statement* stmt_ptr, const orm::where::ExpressionVariantPtr& having_expr, int& param_index)
                -> std::expected<void, Error> {
            auto bind_result = orm::where::bind_params_direct<Statement, Error>(*having_expr, stmt_ptr, param_index);
            if (!bind_result) [[unlikely]] {
                stmt_ptr->reset();
                return std::unexpected(bind_result.error());
            }
            return {};
        }
        // LCOV_EXCL_STOP
    };

} // namespace storm::orm::statements