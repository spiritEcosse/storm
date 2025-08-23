module;

// Module global fragment
#include <sqlite3.h>

// Define the module
export module storm.statement.base;

// Import standard header units
import <string>;
import <vector>;
import <stdexcept>;
import <atomic>;
import <memory>;
import <expected>;
import <ostream>;
import <string_view>;
import <concepts>;
import <type_traits>;
import <typeinfo>;
import <unordered_map>;
import <functional>;
import <any>;
import <span>;
import <format>;
import <variant>;

// Import required modules
import storm.connection;
import storm.sql_exceptions;
import storm.reflect;
import storm.parameter_binder;
import storm.where;
import storm.core_types;
import storm.utils;

export namespace storm {

class Row {
public:
    Row(sqlite3_stmt* stmt, int column_count);
    
    int get_int(int idx) const { return int_values[idx]; }
    double get_double(int idx) const { return double_values[idx]; }
    const std::string& get_text(int idx) const { return text_values[idx]; }
    int get_column_type(int idx) const { return column_types[idx]; }
    const std::string& get_column_name(int idx) const { return column_names[idx]; }
    int get_column_count() const { return static_cast<int>(column_types.size()); }
    
private:
    std::vector<int> int_values;
    std::vector<double> double_values;
    std::vector<std::string> text_values;
    std::vector<std::string> column_names; // Stores column names
    std::vector<int> column_types; // Stores SQLite column types (SQLITE_INTEGER, SQLITE_FLOAT, etc.)
};

// Define which types can be bound to SQL statements
template<typename T>
constexpr bool is_bindable_type_v = 
    std::is_arithmetic_v<T> ||
    std::is_same_v<T, std::string> ||
    std::is_same_v<T, std::string_view> ||
    std::is_same_v<T, const char*> ||
    std::is_same_v<T, bool>;

/**
 * Unified base class for SQL statements using CRTP pattern
 * 
 * @tparam Derived The derived statement class (CRTP pattern)
 * @tparam T The model type this statement operates on
 */
template <typename Derived, typename T>
class UnifiedStatementBase {
public:
    // Make the operator<< a hidden friend
    friend std::ostream& operator<<(std::ostream& os, const UnifiedStatementBase& statement) {
        os << statement.sql();
        return os;
    }
    
public:
    UnifiedStatementBase(std::shared_ptr<Connection> conn);
    
    // Returns the raw SQL query string
    std::string sql() const { return sql_; }
    
    /**
     * Set the SQL query string and prepare the statement
     * 
     * @param sql The SQL query string
     * @return Success or error message
     */
    std::expected<void, std::string> set_sql(const std::string& sql);

    ~UnifiedStatementBase();
    UnifiedStatementBase(const UnifiedStatementBase&) = delete;
    UnifiedStatementBase& operator=(const UnifiedStatementBase&) = delete;
    UnifiedStatementBase(UnifiedStatementBase&& other) noexcept;
    UnifiedStatementBase& operator=(UnifiedStatementBase&& other) noexcept;

    /**
     * Add a WHERE clause to the statement
     * 
     * @param where_clause The WHERE clause to use (shared)
     * @return Reference to the derived class for method chaining
     */
    Derived& where(std::shared_ptr<Where> where_clause) {
        _where_clause = std::move(where_clause);
        return *static_cast<Derived*>(this);
    }
    
    /**
     * Get the table name using compile-time reflection
     * 
     * @return The table name as a string_view
     */
    [[nodiscard]] consteval std::string_view table_name() const noexcept {
        return refl::reflect<T>::get_struct_name();
    }
    
    /**
     * Field filter predicate to exclude primary key fields
     * 
     * @return Lambda that filters out primary key fields
     */
    // Static predicate for compile-time field filtering using member descriptors
    static constexpr auto field_is_not_primary_key_predicate = [](auto member) consteval {
        return !refl::reflect<T>::template is_primary_key<member>();
    };
    
    // Compile-time field filtering function
    [[nodiscard]] static consteval auto get_filtered_fields() {
        return refl::reflect<T>::get_member_names_if(field_is_not_primary_key_predicate);
    }
    
    // Type alias for field filter predicate that works with member descriptors
    template<typename Member>
    using MemberDescriptorPredicate = bool(*)(Member);
    
    // Helper to get field name from member descriptor
    template<typename MemberDesc>
    static consteval std::string_view get_field_name(const MemberDesc& member) {
        return refl::reflect<T>::get_member_name(member);
    }
    
    void bind(int idx, int value);
    void bind(int idx, long long value);
    void bind(int idx, double value);
    void bind(int idx, const std::string& value);
    void bind(int idx, const char* value);
    void bind_null(int idx);

    // ============================================================================
    // REFLECTION-BASED BINDING METHODS (moved from reflect system)
    // ============================================================================
    
    std::expected<void, std::string> bind_primary_key_field(const T& obj, int& param_index);
    
    // Bind fields using member descriptors directly - enables true compile-time filtering
    template<typename FieldFilter>
    std::expected<void, std::string> bind_fields_with_descriptors(const T& obj,
                                                              int& param_index,
                                                              FieldFilter filter);
    
    // Get field value as a variant for dynamic binding
    std::expected<SqlValue, std::string> get_bindable_value(const T& obj,
                                                          std::string_view field_name);
    
    // Bind parameters from expressions
    std::expected<bool, std::string> bind_parameters(const ParameterBinder& binder);

    std::expected<bool, std::string> execute();
    
    // Execute a query and return a single row (for SELECT statements expecting one result)
    std::expected<Row, std::string> execute_query();
    void reset();
    std::expected<std::vector<Row>, std::string> execute_all();

    int column_int(int idx) const;
    double column_double(int idx) const;
    std::string column_text(int idx) const;
    
    // Get the raw SQLite statement
    sqlite3_stmt* get_stmt() const { return stmt; }
    
    // Get the parameter index for a named parameter
    [[nodiscard]] int get_parameter_index(const std::string_view param_name) const {
        return sqlite3_bind_parameter_index(stmt, (":" + std::string(param_name)).c_str());
    }
    
    // Get the database connection
    [[nodiscard]] const std::shared_ptr<Connection>& connection() const noexcept {
        return conn;
    }
    
    /**
     * Convert value to SQL value variant
     * 
     * @tparam Value Type of the value
     * @param value Value to convert
     * @return SqlValue variant
     */
    template <typename Value>
    storm::SqlValue to_sql_value(Value&& value) {
        using CleanType = std::decay_t<Value>;
        
        // Handle all signed integer types
        if constexpr (std::is_integral_v<CleanType> && std::is_signed_v<CleanType>) {
            // Return as int if it fits in int range, otherwise as int64_t
            if constexpr (sizeof(CleanType) <= sizeof(int)) {
                return static_cast<int>(value);
            } else {
                return static_cast<int64_t>(value);
            }
        }
        // Handle all unsigned integer types
        else if constexpr (std::is_integral_v<CleanType> && std::is_unsigned_v<CleanType>) {
            // For unsigned types that fit in int, convert to int
            // For larger unsigned types, use uint64_t
            if constexpr (sizeof(CleanType) < sizeof(int)) {
                return static_cast<int>(value);
            } else {
                return static_cast<uint64_t>(value);
            }
        }
        // Handle floating point types
        else if constexpr (std::is_floating_point_v<CleanType>) {
            return static_cast<double>(value);
        }
        // Handle string types
        else if constexpr (std::is_same_v<CleanType, std::string> || 
                          std::is_same_v<CleanType, std::string_view> ||
                          std::is_same_v<CleanType, const char*>) {
            return std::string(value);
        }
        // Handle boolean
        else if constexpr (std::is_same_v<CleanType, bool>) {
            return static_cast<int>(value ? 1 : 0);
        }
        // Handle enums
        else if constexpr (std::is_enum_v<CleanType>) {
            return static_cast<int>(static_cast<std::underlying_type_t<CleanType>>(value));
        }
        // Unsupported type
        else {
            static_assert(!sizeof(Value), "Unsupported type for binding");
            return std::monostate{};
        }
    }

protected:
    std::shared_ptr<Connection> conn;
    sqlite3_stmt* stmt;
    std::string sql_;
    std::shared_ptr<Where> _where_clause;
    
    /**
     * Helper method to bind a value of any supported type to a parameter index
     * 
     * @tparam ValueType The type of value to bind
     * @param param_index Parameter index to bind to
     * @param value The value to bind
     * @param member_name Optional member name for error reporting
     * @return Success or error message
     */
    template <typename ValueType>
    std::expected<void, std::string> bind_value(int& param_index, const ValueType& value, 
                                              std::string_view member_name = "") {
        if constexpr (is_bindable_type_v<ValueType>) {
            bind(param_index, value);
        } else if constexpr (std::is_enum_v<ValueType>) {
            bind(param_index, static_cast<std::underlying_type_t<ValueType>>(value));
        } else if constexpr (std::is_convertible_v<ValueType, std::string>) {
            bind(param_index, std::string{value});
        } else {
            return std::unexpected{std::format(
                "Unsupported type for SQL binding: field '{}' of type '{}'",
                member_name, typeid(ValueType).name())};
        }
        ++param_index;
        return {};
    }

    consteval std::string_view get_primary_key_field() const noexcept {
        constexpr auto pk_member = refl::reflect<T>::get_primary_key_member();
        return pk_member.get_name();
    }
};

// Implementation of the template methods
template <typename Derived, typename T>
UnifiedStatementBase<Derived, T>::UnifiedStatementBase(std::shared_ptr<Connection> conn) 
    : conn(std::move(conn)), stmt(nullptr), sql_("") {
    // Don't prepare statement here - we'll do it when set_sql is called
    // Connection validity will be checked when set_sql is called
}

template <typename Derived, typename T>
UnifiedStatementBase<Derived, T>::~UnifiedStatementBase() {
    if (stmt) {
        sqlite3_finalize(stmt);
    }
}

template <typename Derived, typename T>
UnifiedStatementBase<Derived, T>::UnifiedStatementBase(UnifiedStatementBase&& other) noexcept 
    : conn(other.conn), stmt(other.stmt), sql_(std::move(other.sql_)), 
      _where_clause(std::move(other._where_clause)) {
    other.stmt = nullptr;
}

template <typename Derived, typename T>
UnifiedStatementBase<Derived, T>& UnifiedStatementBase<Derived, T>::operator=(UnifiedStatementBase&& other) noexcept {
    if (this != &other) {
        if (stmt) sqlite3_finalize(stmt);
        conn = std::move(other.conn);
        stmt = other.stmt;
        sql_ = std::move(other.sql_);
        _where_clause = std::move(other._where_clause);
        other.stmt = nullptr;
    }
    return *this;
}

template <typename Derived, typename T>
std::expected<void, std::string> UnifiedStatementBase<Derived, T>::set_sql(const std::string& sql) {
    // Check connection validity
    if (!this->conn || !this->conn->get()) {
        return std::unexpected("Connection not open");
    }
    
    // Finalize any existing statement
    if (stmt) {
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }
    
    // Check for empty SQL
    if (sql.empty()) {
        return std::unexpected("Cannot set empty SQL");
    }
    
    // Set the new SQL
    sql_ = sql;
    
    // Prepare the new statement
    int rc = sqlite3_prepare_v2(this->conn->get(), sql_.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string errMsg = sqlite3_errmsg(this->conn->get());
        return std::unexpected(std::format("Failed to prepare statement: {}", errMsg));
    }
    
    return {}; // Success
}

template <typename Derived, typename T>
void UnifiedStatementBase<Derived, T>::bind(int idx, int value) {
    sqlite3_bind_int(stmt, idx, value);
}

template <typename Derived, typename T>
void UnifiedStatementBase<Derived, T>::bind(int idx, long long value) {
    sqlite3_bind_int64(stmt, idx, value);
}

template <typename Derived, typename T>
void UnifiedStatementBase<Derived, T>::bind(int idx, double value) {
    sqlite3_bind_double(stmt, idx, value);
}

template <typename Derived, typename T>
void UnifiedStatementBase<Derived, T>::bind(int idx, const std::string& value) {
    sqlite3_bind_text(stmt, idx, value.c_str(), -1, SQLITE_TRANSIENT);
}

template <typename Derived, typename T>
void UnifiedStatementBase<Derived, T>::bind(int idx, const char* value) {
    sqlite3_bind_text(stmt, idx, value, -1, SQLITE_TRANSIENT);
}

template <typename Derived, typename T>
void UnifiedStatementBase<Derived, T>::bind_null(int idx) {
    sqlite3_bind_null(stmt, idx);
}

template <typename Derived, typename T>
std::expected<bool, std::string> UnifiedStatementBase<Derived, T>::execute() {
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        return true;
    } else if (rc == SQLITE_ROW) {
        // For queries that return rows, we consider this a success too
        return true;
    } else {
        std::string errMsg = sqlite3_errmsg(conn->get());
        return std::unexpected(errMsg);
    }
}

template <typename Derived, typename T>
std::expected<Row, std::string> UnifiedStatementBase<Derived, T>::execute_query() {
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int column_count = sqlite3_column_count(stmt);
        return Row(stmt, column_count);
    } else if (rc == SQLITE_DONE) {
        return std::unexpected("No rows returned");
    } else {
        std::string errMsg = sqlite3_errmsg(conn->get());
        return std::unexpected(errMsg);
    }
}

template <typename Derived, typename T>
void UnifiedStatementBase<Derived, T>::reset() {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
}

template <typename Derived, typename T>
int UnifiedStatementBase<Derived, T>::column_int(int idx) const {
    return sqlite3_column_int(stmt, idx);
}

template <typename Derived, typename T>
double UnifiedStatementBase<Derived, T>::column_double(int idx) const {
    return sqlite3_column_double(stmt, idx);
}

template <typename Derived, typename T>
std::string UnifiedStatementBase<Derived, T>::column_text(int idx) const {
    const unsigned char* text = sqlite3_column_text(stmt, idx);
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

template <typename Derived, typename T>
std::expected<std::vector<Row>, std::string> UnifiedStatementBase<Derived, T>::execute_all() {
    std::vector<Row> rows;
    
    while (true) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            int column_count = sqlite3_column_count(stmt);
            rows.emplace_back(stmt, column_count);
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            std::string errMsg = sqlite3_errmsg(conn->get());
            return std::unexpected(errMsg);
        }
    }
    
    return rows;
}

// ============================================================================
// REFLECTION-BASED BINDING METHOD IMPLEMENTATIONS
// ============================================================================

template <typename Derived, typename T>
std::expected<void, std::string> UnifiedStatementBase<Derived, T>::bind_primary_key_field(
    const T& obj, int& param_index) {
    
    constexpr auto pk_member = refl::reflect<T>::get_primary_key_member();
    auto value = pk_member.get(obj);
    return bind_value(param_index, value, pk_member.get_name());
}

// New version of bind_fields that works with member descriptors directly
template <typename Derived, typename T>
template<typename FieldFilter>
std::expected<void, std::string> UnifiedStatementBase<Derived, T>::bind_fields_with_descriptors(
    const T& obj, int& param_index, FieldFilter filter) {
    
    std::expected<void, std::string> result{};
    
    [&]<size_t... Is>(std::index_sequence<Is...>) {
        using members_tuple = typename refl::reflect<T>::descriptor::members_tuple;
        
        ((result.has_value() && [&]() {
            auto member = std::get<Is>(members_tuple{});
            if constexpr (filter(member)) {
                constexpr auto member_name = member.get_name();
                auto value = member.get(obj);
                result = bind_value(param_index, value, member_name);
            }
            return result.has_value();
        }()) && ...);
        
    }(std::make_index_sequence<refl::reflect<T>::descriptor::member_count>{});
    
    return result;
}

template <typename Derived, typename T>
std::expected<SqlValue, std::string> UnifiedStatementBase<Derived, T>::get_bindable_value(
    const T& obj, std::string_view field_name) {
    
    std::expected<SqlValue, std::string> result =
        std::unexpected{std::format("Field '{}' not found", field_name)};
    
    refl::reflect<T>::visit_members(obj, [&](std::string_view member_name, const auto& value) {
        if (member_name == field_name) {
            using ValueType = std::decay_t<decltype(value)>;
            
            if constexpr (is_bindable_type_v<ValueType>) {
                result = SqlValue{value};
            } else if constexpr (std::is_enum_v<ValueType>) {
                result = SqlValue{static_cast<std::underlying_type_t<ValueType>>(value)};
            } else if constexpr (std::is_convertible_v<ValueType, std::string>) {
                result = SqlValue{std::string{value}};
            } else {
                result = std::unexpected{std::format(
                    "Field '{}' has unsupported type for binding", field_name)};
            }
        }
    });
    
    return result;
}

template <typename Derived, typename T>
std::expected<bool, std::string> UnifiedStatementBase<Derived, T>::bind_parameters(
    const ParameterBinder& binder) {
    
    const auto& parameters = binder.get_parameters();
    
    for (const auto& [param_name, param_value] : parameters) {
        int param_index = get_parameter_index(param_name);
        if (param_index <= 0) {
            return std::unexpected("Parameter not found in statement: " + param_name);
        }
        
        // Use the bind_variant function for consistent binding
        auto bind_result = bind_variant(*static_cast<Derived*>(this), param_index, param_value);
        if (!bind_result) {
            return std::unexpected("Failed to bind parameter '" + param_name + "': " + bind_result.error());
        }
    }
    
    return true;
}

} // namespace storm