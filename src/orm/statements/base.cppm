module;

#include <sqlite3.h>
#include <meta>

export module storm_orm_statements_base;

import storm_db_concept;
import storm_orm_utilities;

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

export namespace storm::orm::statements {

    // Import utilities for compile-time string building
    using storm::orm::utilities::ConstexprString;

    // Mirror of meta::FieldAttr from storm module - must match exactly
    namespace meta {
        enum class FieldAttr { primary, indexed, unique, fk };
    }

    // Shared reflection utilities for all statement types
    template <typename T> class BaseStatement {
      public:
        // Public accessors for optimization
        static constexpr auto get_primary_key() {
            return primary_key_;
        }
        static constexpr auto get_pk_name() {
            return pk_name_;
        }
        static constexpr auto get_table_name() {
            return table_name_;
        }

      protected:
        // Helper to find primary key using storm::meta attributes
        static consteval std::meta::info find_primary_key_impl() {
            for (std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::primary) {
                    return member;
                }
            }
            throw "Model must have exactly one field marked with [[=storm::meta::FieldAttr::primary]]";
        }

        // FK field detection utilities
        static consteval bool is_fk_field(std::meta::info member) {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk;
        }

        // Get database column name for FK field: User sender → "sender_id"
        static consteval std::string get_fk_column_name(std::meta::info member) {
            std::string field_name(std::meta::identifier_of(member));
            return field_name + "_id";
        }

        // Find primary key of a FK type
        template <typename FKType> static consteval std::meta::info find_fk_primary_key() {
            for (std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^FKType, std::meta::access_context::unchecked())) {
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::primary) {
                    return member;
                }
            }
            throw "FK type must have exactly one field marked with [[=storm::meta::FieldAttr::primary]]";
        }

        // Helper to get the number of fields
        static consteval size_t get_field_count() {
            return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()).size();
        }

        // Pre-compute all field members at compile-time
        template <size_t N> static consteval std::array<std::meta::info, N> get_all_field_members() {
            std::array<std::meta::info, N> result{};
            auto members = std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked());

            for (size_t i = 0; i < N && i < members.size(); ++i) {
                result[i] = members[i];
            }
            return result;
        }

        // Calculate size of field names string at compile-time (for constexpr SQL size calculations)
        static consteval size_t calculate_field_names_size() {
            size_t size  = 0;
            bool   first = true;
            for (size_t i = 0; i < field_count_; ++i) {
                if (!first) {
                    size += 2; // ", "
                }
                // Check if this is a FK field
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(all_members_[i]);
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                    // FK field: field_name + "_id"
                    size += std::meta::identifier_of(all_members_[i]).size() + 3; // +3 for "_id"
                } else {
                    size += std::meta::identifier_of(all_members_[i]).size();
                }
                first = false;
            }
            return size;
        }

        // Calculate size of non-PK field names string at compile-time
        static consteval size_t calculate_non_pk_field_names_size() {
            size_t size  = 0;
            bool   first = true;
            for (size_t i = 0; i < field_count_; ++i) {
                // Skip primary key field
                if (all_members_[i] == primary_key_) {
                    continue;
                }
                if (!first) {
                    size += 2; // ", "
                }
                // Check if this is a FK field
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(all_members_[i]);
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                    // FK field: field_name + "_id"
                    size += std::meta::identifier_of(all_members_[i]).size() + 3; // +3 for "_id"
                } else {
                    size += std::meta::identifier_of(all_members_[i]).size();
                }
                first = false;
            }
            return size;
        }

        // Build comma-separated list of all field names (for SELECT statements)
        // FK fields are mapped to their column names (User sender → sender_id)
        static consteval auto build_all_field_names_list() {
            constexpr size_t      size = calculate_field_names_size() + 10; // Add buffer
            ConstexprString<size> result;
            bool                  first = true;
            for (size_t i = 0; i < field_count_; ++i) {
                if (!first) {
                    result.append(", ");
                }
                // Check if this is a FK field
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(all_members_[i]);
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                    // FK field: append field_name + "_id"
                    result.append(std::meta::identifier_of(all_members_[i]));
                    result.append("_id");
                } else {
                    result.append(std::meta::identifier_of(all_members_[i]));
                }
                first = false;
            }
            return result;
        }

        // Build comma-separated list of NON-PRIMARY KEY fields (for INSERT statements)
        // Excludes primary key to allow auto-increment
        static consteval auto build_non_pk_field_names_list() {
            constexpr size_t      size = calculate_non_pk_field_names_size() + 10; // Add buffer
            ConstexprString<size> result;
            bool                  first = true;
            for (size_t i = 0; i < field_count_; ++i) {
                // Skip primary key field
                if (all_members_[i] == primary_key_) {
                    continue;
                }

                if (!first) {
                    result.append(", ");
                }
                // Check if this is a FK field
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(all_members_[i]);
                if (field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk) {
                    // FK field: append field_name + "_id"
                    result.append(std::meta::identifier_of(all_members_[i]));
                    result.append("_id");
                } else {
                    result.append(std::meta::identifier_of(all_members_[i]));
                }
                first = false;
            }
            return result;
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
        bind_all_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...>) noexcept
                -> std::expected<void, typename ConnType::Error> {
            // Use fold expression to bind all fields at compile-time indices
            // Each field binds at parameter index (Is + 1) since SQLite parameters start at 1
            auto bind_result = (bind_field_at_index<ConnType, Is>(stmt, obj, Is + 1) && ...);
            if (!bind_result) {
                // Find which field failed (this could be optimized further if needed)
                return get_first_bind_error<ConnType, Statement, Is...>(stmt, obj);
            }
            return {};
        }

        // Helper template for INSERT binding (skips primary key for auto-increment)
        template <typename ConnType, typename Statement, size_t... Is>
        [[nodiscard]] static auto
        bind_non_pk_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...>) noexcept
                -> std::expected<void, typename ConnType::Error> {
            int  param_index = 1;
            bool bind_ok     = true;

            // Bind each field, skipping the PK
            ((bind_ok = bind_ok &&
                        [&]() {
                            if constexpr (Is < field_count_) {
                                if (all_members_[Is] != primary_key_) {
                                    bool result = bind_field_at_index<ConnType, Is>(stmt, obj, param_index);
                                    param_index++;
                                    return result;
                                }
                            }
                            return true;
                        }()),
             ...);

            if (!bind_ok) {
                return std::unexpected(typename ConnType::Error{-1, "Field binding failed"});
            }
            return {};
        }

        // Helper to bind a single field at compile-time index with error handling
        template <typename ConnType, size_t Index>
        [[nodiscard]] static auto
        bind_field_at_index(typename ConnType::Statement& stmt, const T& obj, int param_index) noexcept -> bool {
            if constexpr (Index < field_count_) {
                constexpr auto member = all_members_[Index];

                // Check if this is a FK field - if so, extract and bind the PK value
                if constexpr (is_fk_field(member)) {
                    auto fk_object              = obj.[:member:];
                    using FKType                = std::remove_cvref_t<decltype(fk_object)>;
                    constexpr auto fk_pk_member = find_fk_primary_key<FKType>();
                    auto           pk_value     = fk_object.[:fk_pk_member:];
                    auto           result       = bind_value_by_type<ConnType>(stmt, param_index, pk_value);
                    return result.has_value();
                } else {
                    auto field_value = obj.[:member:];
                    auto result      = bind_value_by_type<ConnType>(stmt, param_index, field_value);
                    return result.has_value();
                }
            }
            return true;
        }

        // Helper to get the first binding error when fold expression fails
        template <typename ConnType, typename Statement, size_t... Is>
        [[nodiscard]] static auto get_first_bind_error(Statement& stmt, const T& obj) noexcept
                -> std::expected<void, typename ConnType::Error> {
            // Try each field individually to find the first error
            auto try_bind = [&stmt, &obj](auto index_constant) -> std::expected<void, typename ConnType::Error> {
                constexpr size_t Index = decltype(index_constant)::value;
                if constexpr (Index < field_count_) {
                    constexpr auto member = all_members_[Index];
                    // Handle FK fields
                    if constexpr (is_fk_field(member)) {
                        auto fk_object              = obj.[:member:];
                        using FKType                = std::remove_cvref_t<decltype(fk_object)>;
                        constexpr auto fk_pk_member = find_fk_primary_key<FKType>();
                        auto           pk_value     = fk_object.[:fk_pk_member:];
                        return bind_value_by_type<ConnType>(stmt, Index + 1, pk_value);
                    } else {
                        auto field_value = obj.[:member:];
                        return bind_value_by_type<ConnType>(stmt, Index + 1, field_value);
                    }
                }
                return {};
            };

            // Try each field to find the first error
            std::expected<void, typename ConnType::Error> first_error{};
            ((first_error = try_bind(std::integral_constant<size_t, Is>{}), first_error.has_value()) && ...);
            return first_error;
        }

        // Helper for bulk binding multiple objects with index sequence
        template <typename ConnType, typename Statement, typename ContainerType, size_t... Is>
        [[nodiscard]] static auto
        bind_all_objects_bulk_impl(Statement& stmt, const ContainerType& objects, std::index_sequence<Is...>) noexcept
                -> std::expected<void, typename ConnType::Error> {
            int param_index = 1;

            // Bind each object's fields sequentially
            for (const auto& obj : objects) {
                // Use fold expression to bind all fields for this object
                auto bind_result = (bind_field_at_index<ConnType, Is>(stmt, obj, param_index + Is) && ...);
                if (!bind_result) {
                    // Find which field failed for this object
                    return get_bulk_bind_error<ConnType, Statement, Is...>(stmt, obj, param_index);
                }
                param_index += field_count_; // Move to next object's parameter range
            }
            return {};
        }

        // Helper for bulk INSERT binding (skips PK for auto-increment)
        template <typename ConnType, typename Statement, typename ContainerType, size_t... Is>
        [[nodiscard]] static auto bind_non_pk_objects_bulk_impl(
                Statement& stmt, const ContainerType& objects, std::index_sequence<Is...>
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            int              param_index  = 1;
            constexpr size_t non_pk_count = field_count_ - 1;

            // Bind each object's non-PK fields sequentially
            for (const auto& obj : objects) {
                int  field_param = param_index;
                bool bind_ok     = true;

                // Bind fields skipping PK
                ((bind_ok = bind_ok &&
                            [&]() {
                                if constexpr (Is < field_count_) {
                                    if (all_members_[Is] != primary_key_) {
                                        bool result = bind_field_at_index<ConnType, Is>(stmt, obj, field_param);
                                        field_param++;
                                        return result;
                                    }
                                }
                                return true;
                            }()),
                 ...);

                if (!bind_ok) {
                    return std::unexpected(typename ConnType::Error{-1, "Bulk bind failed"});
                }
                param_index += non_pk_count; // Move to next object's parameter range
            }
            return {};
        }

        // Helper to get binding error for bulk operations
        template <typename ConnType, typename Statement, size_t... Is>
        [[nodiscard]] static auto get_bulk_bind_error(Statement& stmt, const T& obj, int base_param_index) noexcept
                -> std::expected<void, typename ConnType::Error> {
            // Try each field individually to find the first error
            auto try_bind = [&stmt,
                             &obj,
                             base_param_index](auto index_constant) -> std::expected<void, typename ConnType::Error> {
                constexpr size_t Index = decltype(index_constant)::value;
                if constexpr (Index < field_count_) {
                    constexpr auto member = all_members_[Index];
                    // Handle FK fields
                    if constexpr (is_fk_field(member)) {
                        auto fk_object              = obj.[:member:];
                        using FKType                = std::remove_cvref_t<decltype(fk_object)>;
                        constexpr auto fk_pk_member = find_fk_primary_key<FKType>();
                        auto           pk_value     = fk_object.[:fk_pk_member:];
                        return bind_value_by_type<ConnType>(stmt, base_param_index + Index, pk_value);
                    } else {
                        auto field_value = obj.[:member:];
                        return bind_value_by_type<ConnType>(stmt, base_param_index + Index, field_value);
                    }
                }
                return {};
            };

            // Try each field to find the first error
            std::expected<void, typename ConnType::Error> first_error{};
            ((first_error = try_bind(std::integral_constant<size_t, Is>{}), first_error.has_value()) && ...);
            return first_error;
        }

        // Common batch operation thresholds
        static constexpr size_t MAX_SQLITE_VARIABLES = 999;

        // Adaptive threshold calculation based on batch size and field count
        // Returns the optimal threshold for deciding between bulk SQL and individual inserts
        static constexpr size_t calculate_adaptive_threshold(size_t batch_size, size_t max_bulk_size) {
            // For very small batches (≤10), always use bulk SQL up to the SQLite limit
            if (batch_size <= 10)
                return max_bulk_size;

            // Calculate safe thresholds based on max_bulk_size (which already accounts for field count)
            // max_bulk_size = 999 / field_count, so we scale our thresholds accordingly

            // For small-medium batches, use bulk SQL if safe
            // Use 50% of max_bulk_size as the sweet spot for bulk operations
            const size_t bulk_sweet_spot = std::max(size_t(50), max_bulk_size / 2);

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
            return 50; // Force individual insert path - safe for any field count
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
        [[nodiscard]] __attribute__((always_inline)) static inline FieldType
        extract_column_value(Statement& stmt, int col_idx) noexcept {
            // Handle std::optional types first
            if constexpr (utilities::is_optional_v<FieldType>) {
                using InnerType = typename FieldType::value_type;
                if (stmt.is_null(col_idx)) {
                    return std::nullopt;
                } else {
                    // Extract the inner value and wrap it in optional
                    InnerType inner_value = extract_column_value<InnerType>(stmt, col_idx);
                    return FieldType{std::move(inner_value)};
                }
            }
            // Boolean type (stored as INTEGER 0/1)
            else if constexpr (std::is_same_v<FieldType, bool>) {
                return stmt.extract_bool(col_idx);
            }
            // Integer types
            else if constexpr (std::is_same_v<FieldType, int>) {
                return stmt.extract_int(col_idx);
            } else if constexpr (std::is_same_v<FieldType, int64_t> || std::is_same_v<FieldType, long> ||
                                 std::is_same_v<FieldType, long long>) {
                return static_cast<FieldType>(stmt.extract_int64(col_idx));
            } else if constexpr (std::is_same_v<FieldType, uint64_t> || std::is_same_v<FieldType, unsigned long> ||
                                 std::is_same_v<FieldType, unsigned long long>) {
                return static_cast<FieldType>(stmt.extract_int64(col_idx));
            } else if constexpr (std::is_same_v<FieldType, short> || std::is_same_v<FieldType, unsigned short> ||
                                 std::is_same_v<FieldType, unsigned int>) {
                return static_cast<FieldType>(stmt.extract_int(col_idx));
            }
            // Floating point types
            else if constexpr (std::is_same_v<FieldType, double>) {
                return stmt.extract_double(col_idx);
            } else if constexpr (std::is_same_v<FieldType, float>) {
                return stmt.extract_float(col_idx);
            }
            // BLOB types
            else if constexpr (std::is_same_v<FieldType, std::vector<uint8_t>> ||
                               std::is_same_v<FieldType, std::vector<unsigned char>>) {
                auto [blob_ptr, blob_size] = stmt.extract_blob(col_idx);
                if (blob_ptr && blob_size > 0) {
                    const uint8_t* data = static_cast<const uint8_t*>(blob_ptr);
                    return FieldType(data, data + blob_size);
                } else {
                    return FieldType{};
                }
            }
            // String types
            else if constexpr (std::is_same_v<FieldType, std::string>) {
                const unsigned char* text = stmt.extract_text_ptr(col_idx);
                if (text) {
                    // OPTIMIZATION: Direct construction with known length (no strlen, no temporary)
                    int len = sqlite3_column_bytes(stmt.handle(), col_idx);
                    return FieldType(reinterpret_cast<const char*>(text), len);
                } else {
                    return FieldType{};
                }
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

        template <typename ConnType> static void rollback_transaction(ConnType& conn) noexcept { 
            // TODO: Remove this func in favour of TransactionGuard
            (void)conn.execute("ROLLBACK");
        }

        // Utility to determine if transaction should be used
        template <typename ContainerType> static constexpr bool should_use_transaction(const ContainerType& container) {
            return container.size() > 1;
        }

        // Unified statement execution logic for cached/non-cached connections
        template <typename ConnType, typename PrepareFunc, typename BindExecuteFunc>
        [[nodiscard]] static auto execute_statement(
                ConnType& conn, const std::string& sql, PrepareFunc&& prepare_func, BindExecuteFunc&& bind_execute_func
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
        // TODO: remove it
        template <typename ConnType, typename Operation>
        [[nodiscard]] static auto
        execute_with_transaction(ConnType& conn, bool use_transaction, Operation&& op) noexcept -> decltype(op()) {
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
        execute_with_statement(ConnType& conn, const std::string& sql, ExecuteFunc&& execute_func) noexcept
                -> decltype(execute_func(std::declval<typename ConnType::Statement&>())) {
            // Try cached statement first if available
            if constexpr (requires { conn.prepare_cached(sql); }) {
                return conn.prepare_cached(sql).and_then([&execute_func](auto* stmt) { return execute_func(*stmt); });
            } else {
                // Fallback to regular prepare
                return conn.prepare(sql).and_then([&execute_func](typename ConnType::Statement stmt) mutable {
                    return execute_func(stmt);
                });
            }
        }

        // Monadic helper for bind and execute operations
        template <typename BindResult, typename Statement>
        [[nodiscard]] static auto bind_and_execute(BindResult bind_result, Statement& stmt) noexcept -> BindResult {
            return bind_result.and_then([&stmt]() { return stmt.execute(); });
        }

        // Monadic helper for reset, bind, and execute sequence
        template <typename Statement, typename BindFunc>
        [[nodiscard]] static auto reset_bind_and_execute(Statement& stmt, BindFunc&& bind_func) noexcept
                -> decltype(bind_func(stmt)) {
            stmt.reset();
            return bind_func(stmt).and_then([&stmt]() { return stmt.execute(); });
        }

        // Generic batch execution template for statement classes
        // TODO: Remove it
        template <typename StatementType, typename Objects>
        [[nodiscard]] static auto execute_standard_batch(
                StatementType& statement_instance, Objects&& objects, size_t variables_per_object
        ) noexcept -> decltype(statement_instance.execute_individual_batch(objects)) {
            using ConnType = typename StatementType::Connection;
            return execute_batch_optimized<ConnType>(
                    *statement_instance.conn_,
                    objects,
                    variables_per_object,
                    [&statement_instance, &objects]() { return statement_instance.execute_bulk(objects); },
                    [&statement_instance, &objects]() { return statement_instance.execute_individual_batch(objects); }
            );
        }

        // Execute batch operations with optimal strategy selection
        // TODO: Remove it
        template <typename ConnType, typename ContainerType, typename BulkExecutor, typename IndividualExecutor> 
        [[nodiscard]] static auto execute_batch_optimized(
                ConnType&            conn,
                const ContainerType& items,
                size_t               items_per_variable, // How many variables each item uses
                BulkExecutor&&       bulk_executor,
                IndividualExecutor&& individual_executor
        ) noexcept -> decltype(individual_executor()) {
            if (items.empty()) {
                return {};
            }

            // Single item - use individual executor without transaction
            if (items.size() == 1) {
                return individual_executor();
            }

            // Calculate max bulk size based on SQLite variable limit
            const size_t max_bulk_size = MAX_SQLITE_VARIABLES / items_per_variable;

            // Use adaptive threshold based on actual batch size
            const size_t adaptive_threshold  = calculate_adaptive_threshold(items.size(), max_bulk_size);
            const size_t effective_threshold = std::min(adaptive_threshold, max_bulk_size);

            // Decide strategy: bulk SQL vs individual inserts with transaction
            if (items.size() <= effective_threshold) {
                return bulk_executor();
            }

            // Large batch - use individual statements with transaction
            return execute_with_transaction(conn, true, individual_executor);
        }
    };

} // namespace storm::orm::statements