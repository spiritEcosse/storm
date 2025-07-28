#pragma once

#include <string>
#include <utility>
#include <vector>
#include <variant>
#include <any>
#include <map>
#include "Function.h"
#include "Where.h"
#include <ranges>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <cstdint>
#include "Connection.h"
#include "Statement.h"
#include "Reflect.h"
#include "StringUtils.h"
#include <sstream>
#include <expected>
#include <print>
#include <ranges>
#include <string_view>

namespace storm {
    // Define ValueVariant before using it
    using ValueVariant = std::variant<
        std::monostate,  // For null values
        int,             // For integer types
        double,          // For floating point types
        bool,            // For boolean values
        std::string      // For text values
    >;

    // Utility function to convert Row column value to any type
    template<typename T>
    T to_value(const Row& row, int columnIndex = 0) {
        int columnType = row.get_column_type(columnIndex);
        
        if constexpr (std::is_same_v<T, ValueVariant>) {
            // Handle ValueVariant specially
            switch (columnType) {
                case SQLITE_INTEGER:
                    return row.get_int(columnIndex);
                case SQLITE_FLOAT:
                    return row.get_double(columnIndex);
                case SQLITE_TEXT:
                    return row.get_text(columnIndex);
                case SQLITE_NULL:
                    return std::monostate{};
                default:
                    return std::monostate{};
            }
        } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, long> || 
                      std::is_same_v<T, long long> || std::is_same_v<T, unsigned int> || 
                      std::is_same_v<T, unsigned long> || std::is_same_v<T, unsigned long long> || 
                      std::is_same_v<T, bool>) {
            return static_cast<T>(row.get_int(columnIndex));
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            return static_cast<T>(row.get_double(columnIndex));
        } else if constexpr (std::is_same_v<T, std::string>) {
            return row.get_text(columnIndex);
        } else if constexpr (std::is_same_v<T, std::monostate>) {
            return std::monostate{};
        } else {
            // This will cause a compile-time error for unsupported types
            static_assert(!sizeof(T), "Unsupported type for to_value");
            return T{}; // This line will never be reached
        }
    }

    using ValueMap = std::map<std::string, ValueVariant, std::less<>>;
    using ValueVectorMap = std::vector<ValueMap>;
    using ExpectedValueVectorMap = std::expected<ValueVectorMap, std::string>;

    template<typename T>
    struct member_pointer_traits;

    template<typename T, typename C>
    struct member_pointer_traits<T C::*> {
        using type = T;
        using class_type = C;
    };

    inline std::string addExtraQuotes(const std::string& str) {
        std::string result;
        for(char c: str) {
            if(c == '\'') {
                result += "''";
            } else {
                result += c;
            }
        }
        return result;
    }

    struct JoinInfo {
        enum class JoinType { INNER, LEFT, RIGHT };

        struct JoinEntry {
            std::string tableName;
            std::string alias;
            std::string condition;
            JoinType type;
            
            JoinEntry(std::string tableName, std::string alias, std::string condition, JoinType type)
                : tableName(std::move(tableName)), alias(std::move(alias)), condition(std::move(condition)), type(type) {}
        };

        std::vector<JoinEntry> joins;
    };

    static std::string joinTypeToString(const JoinInfo::JoinType type) {
        switch(type) {
            using enum JoinInfo::JoinType;
            case INNER:
                return "INNER";
            case LEFT:
                return "LEFT";
            case RIGHT:
                return "RIGHT";
        }
        return "INNER";
    }

    // Base class for type erasure
    struct FieldAliasBase {
        std::string alias;
        
        explicit FieldAliasBase(std::string alias = "") 
            : alias(std::move(alias)) {}
        
        virtual ~FieldAliasBase() = default;
        [[nodiscard]] virtual std::string getFullFieldName() const = 0;
        [[nodiscard]] virtual std::string getFieldName() const = 0;
        [[nodiscard]] virtual std::string getTableName() const = 0;
        [[nodiscard]] virtual std::string getAlias() const = 0;
        virtual bool isStringField() const = 0;
        virtual bool isBoolField() const = 0;
        virtual bool isNumericField() const = 0;
        // Clone method for polymorphic copying
        [[nodiscard]] virtual FieldAliasBase* clone() const = 0;
    };

    template<auto MemberPtr>
    struct FieldAlias : public FieldAliasBase {
        // Constructor for string literals and std::string
        explicit FieldAlias(std::string alias = "") 
            : FieldAliasBase(std::move(alias)) {}
        
        ~FieldAlias() = default;
        
        [[nodiscard]] std::string getFullFieldName() const override {
            using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
            std::string fieldName = getFieldNameFromMemberPtr<MemberPtr>();
            std::string tableName = Reflect<ClassType>::get_struct_name();
            return fmt::format(R"("{}"."{}")", tableName, fieldName);
        }
        
        [[nodiscard]] std::string getFieldName() const override {
            return getFieldNameFromMemberPtr<MemberPtr>();
        }
        
        [[nodiscard]] std::string getTableName() const override {
            using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
            return Reflect<ClassType>::get_struct_name();
        }
        
        [[nodiscard]] std::string getAlias() const override {
            return alias;
        }
        
        [[nodiscard]] bool isStringField() const override {
            using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::type;
            return std::is_same_v<FieldType, std::string>;
        }
        
        [[nodiscard]] bool isBoolField() const override {
            using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::type;
            return std::is_same_v<FieldType, bool>;
        }
        
        [[nodiscard]] bool isNumericField() const override {
            using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::type;
            return std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>;
        }
        
        [[nodiscard]] FieldAliasBase* clone() const override {
            return new FieldAlias<MemberPtr>(alias);
        }
    };
    
    // Helper function to create a unique_ptr from a FieldAliasBase using clone
    template <typename T>
    std::unique_ptr<FieldAliasBase> make_field_alias_unique(const T& field) {
        // We can't use std::make_unique directly with FieldAliasBase because it's abstract
        // Instead, we use the clone method which returns a concrete derived type
        return std::unique_ptr<FieldAliasBase>(field->clone());
    }
    
    // String-based field alias for runtime field references
    class StringFieldAlias : public FieldAliasBase {
    private:
        std::string tableName;
        std::string fieldName;
        
    public:
        // Constructor for string-based field references
        explicit StringFieldAlias(std::string tableName, std::string fieldName, std::string alias = "") 
            : FieldAliasBase(std::move(alias)), tableName(std::move(tableName)), fieldName(std::move(fieldName)) {}
        
        ~StringFieldAlias() override = default;
        
        [[nodiscard]] std::string getFullFieldName() const override {
            return utils::formatFieldName(tableName, fieldName);
        }
        
        [[nodiscard]] std::string getFieldName() const override {
            return fieldName;
        }
        
        [[nodiscard]] std::string getTableName() const override {
            return tableName;
        }
        
        [[nodiscard]] std::string getAlias() const override {
            return alias;
        }
        
        // For string-based fields, we assume it could be any type
        [[nodiscard]] bool isStringField() const override {
            return true;  // Conservative assumption
        }
        
        [[nodiscard]] bool isBoolField() const override {
            return false;  // Conservative assumption
        }
        
        [[nodiscard]] bool isNumericField() const override {
            return false;  // Conservative assumption
        }
        
        [[nodiscard]] FieldAliasBase* clone() const override {
            return new StringFieldAlias(tableName, fieldName, alias);
        }
    };

    template<class T>
    class QuerySet {
    private:
        using ExpectedT = std::expected<T, std::string>;
        using ExpectedVectorT = std::expected<std::vector<T>, std::string>;
        std::shared_ptr<Connection> conn;
        std::optional<storm::Where> _whereExpression;
        JoinInfo joinInfo;
        // Order fields with direction (field, ascending/descending)
        struct OrderFieldInfo {
            std::unique_ptr<FieldAliasBase> field;
            bool ascending;
            Collation collation = Collation::NONE;
            
            // Constructor to fix emplace_back issue
            OrderFieldInfo(std::unique_ptr<FieldAliasBase> f, bool asc, Collation coll = Collation::NONE) 
                : field(std::move(f)), ascending(asc), collation(coll) {}
                
            // Copy constructor for deep copy of the unique_ptr
            OrderFieldInfo(const OrderFieldInfo& other)
                : ascending(other.ascending), collation(other.collation) {
                // Clone the field if it exists
                if (other.field) {
                    field = std::unique_ptr<FieldAliasBase>(other.field->clone());
                }
            }
            
            // Copy assignment operator
            OrderFieldInfo& operator=(const OrderFieldInfo& other) {
                if (this != &other) {
                    ascending = other.ascending;
                    collation = other.collation;
                    if (other.field) {
                        field = std::unique_ptr<FieldAliasBase>(other.field->clone());
                    } else {
                        field.reset();
                    }
                }
                return *this;
            }
        };
        std::vector<OrderFieldInfo> orderFields;
        std::vector<std::unique_ptr<FieldAliasBase>> distinctFields;
        std::string _jsonFields;
        std::vector<std::unique_ptr<FieldAliasBase>> onlyFields;
        std::vector<Function> functionsSet;
        std::vector<std::unique_ptr<FieldAliasBase>> groupByFields; // For compile-time field names
        int _limit{};
        int _offset{};
        std::string _alias;
        bool _one{};
        bool _doAndCheck{};
        bool _returnInMain{};

        [[nodiscard]] std::string getAlias() const {
            return _alias;
        }

        [[nodiscard]] std::string aliasQueryMain() const {
            std::string query;
            if(!_returnInMain) {
                return query;
            }
            if(_one) {
                query += fmt::format("'{0}', (SELECT * FROM {0} ),", _alias);
            } else {
                query += fmt::format("'{0}', COALESCE((SELECT json_agg({0}.*) FROM ( SELECT * FROM {0} "
                                        ") as {0}), '[]'),",
                                        _alias);
            }
            return query;
        }

        // Helper function to bind any value to a statement
        template<typename ValueType>
        static void bind_sql_value(Statement& stmt, int param_index, const ValueType& value) {
            if constexpr (std::is_same_v<ValueType, std::string>) {
                stmt.bind(param_index, value);
            } else if constexpr (std::is_same_v<ValueType, bool>) {
                stmt.bind(param_index, value ? 1 : 0);
            } else if constexpr (std::is_integral_v<ValueType> && !std::is_same_v<ValueType, bool>) {
                stmt.bind(param_index, static_cast<long long>(value));
            } else if constexpr (std::is_floating_point_v<ValueType>) {
                stmt.bind(param_index, static_cast<double>(value));
            } else if constexpr (std::is_null_pointer_v<ValueType> || std::is_same_v<ValueType, std::nullopt_t>) {
                stmt.bind_null(param_index);
            } else if constexpr (std::is_same_v<ValueType, SqlValue>) {
                // Handle SqlValue variant by visiting it
                std::visit([&stmt, param_index](const auto& v) {
                    bind_sql_value(stmt, param_index, v);
                }, value);
            } else {
                static_assert(sizeof(ValueType) == 0, "Unsupported type for SQLite binding");
            }
        }

        // Helper function to bind parameters from a QueryResult
        static void bind_query_parameters(Statement& stmt, const QueryResult& query_result) {
            if (query_result.binder) {
                for (const auto& [param_name, value] : query_result.parameters()) {
                    // Get the parameter index in the SQLite statement
                    int param_index = stmt.get_parameter_index(param_name);
                    if (param_index > 0) { // SQLite parameter indices are 1-based
                        bind_sql_value(stmt, param_index, value);
                    }
                }
            }
        }
        
        // Helper function to bind a specific field value by name
        void bind_field_value(Statement& stmt, const T& obj, const std::string& field_name, int& param_index) const {
            refl::reflect<T>::bind_field_value(stmt, obj, field_name, param_index);
        }

        // Bind all object values
        void bind_object_values(Statement& stmt, const T& obj, int& param_index) const {
            refl::reflect<T>::bind_object_values(stmt, obj, param_index);
        }

        // Field name mapping using your reflection system
        template<auto MemberPtr>
        requires std::is_member_object_pointer_v<decltype(MemberPtr)>
        std::string get_field_name() const {
            using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
            
            // Using new reflection API
            std::string tableName = refl::reflect<ClassType>::get_struct_name();
            std::string fieldName = refl::reflect<ClassType>::get_field_name_from_member_ptr<MemberPtr>();
            
            return storm::utils::formatFieldName(tableName, fieldName);
        }

        std::string addExtraQuotes(const std::string& query) const {
            // Your existing implementation
            return query;
        }

        // Helper function to create field name generator (compile-time)
        template<auto Field>
        auto createFieldNameGenerator() const {
            return [this]() {
                static_assert(std::is_member_pointer_v<decltype(Field)>, 
                            "Field must be a member pointer");
                using ClassType = typename member_pointer_class<decltype(Field)>::type;
                std::string fieldName = getFieldNameFromMemberPtr<Field>();
                std::string tableName = this->template get_table_name<ClassType>();
                return utils::formatFieldName(tableName, fieldName);
            };
        }
    
    public:
        // =============================
        // =============================
        // Constructor and Copy/Move Semantics
        // =============================
        explicit QuerySet(std::shared_ptr<Connection> conn, [[maybe_unused]] const std::string_view alias = "") 
            : conn(std::move(conn)) {
        }
        
        // Custom copy constructor for proper deep copy of all state
        QuerySet(const QuerySet& other)
            : conn(other.conn),
              // _whereExpression is handled separately below to ensure proper deep copying
              // Create an empty joinInfo and manually copy the joins for proper deep copying
              _jsonFields(other._jsonFields),
              functionsSet(other.functionsSet),
              _limit(other._limit),
              _offset(other._offset),
              _alias(other._alias),
              _one(other._one),
              _doAndCheck(other._doAndCheck),
              _returnInMain(other._returnInMain) {
              
            // Deep copy joinInfo.joins
            joinInfo.joins.reserve(other.joinInfo.joins.size());
            for (const auto& join : other.joinInfo.joins) {
                joinInfo.joins.emplace_back(
                    join.tableName,
                    join.alias,
                    join.condition,
                    join.type
                );
            }
              
            // Handle _whereExpression separately to ensure proper deep copying
            if (other._whereExpression) {
                _whereExpression = *other._whereExpression; // This will use Where's copy constructor for deep copying
            }
            
            // Deep copy for orderFields
            orderFields.reserve(other.orderFields.size());
            for (const auto& field : other.orderFields) {
                orderFields.emplace_back(field); // Uses the copy constructor we defined
            }
            
            // Deep copy for distinctFields
            distinctFields.reserve(other.distinctFields.size());
            for (const auto& field : other.distinctFields) {
                if (field) {
                    distinctFields.push_back(make_field_alias_unique(field));
                }
            }
            
            // Deep copy for onlyFields
            onlyFields.reserve(other.onlyFields.size());
            for (const auto& field : other.onlyFields) {
                if (field) {
                    onlyFields.push_back(make_field_alias_unique(field));
                }
            }
            
            // Deep copy for groupByFields
            groupByFields.reserve(other.groupByFields.size());
            for (const auto& field : other.groupByFields) {
                if (field) {
                    groupByFields.push_back(make_field_alias_unique(field));
                }
            }
        }
        
        // Custom copy assignment operator
        QuerySet& operator=(const QuerySet& other) {
            if (this != &other) {
                // Copy simple members
                conn = other.conn;
                // Handle _whereExpression separately to ensure proper deep copying
                if (other._whereExpression) {
                    _whereExpression = *other._whereExpression; // This will use Where's copy constructor for deep copying
                } else {
                    _whereExpression.reset(); // Clear if the other doesn't have a where expression
                }
                // Deep copy joinInfo.joins
                joinInfo.joins.clear();
                joinInfo.joins.reserve(other.joinInfo.joins.size());
                for (const auto& join : other.joinInfo.joins) {
                    joinInfo.joins.emplace_back(
                        join.tableName,
                        join.alias,
                        join.condition,
                        join.type
                    );
                }
                _jsonFields = other._jsonFields;
                functionsSet = other.functionsSet;
                _limit = other._limit;
                _offset = other._offset;
                _alias = other._alias;
                _one = other._one;
                _doAndCheck = other._doAndCheck;
                _returnInMain = other._returnInMain;
                
                // Clear and deep copy for orderFields
                orderFields.clear();
                orderFields.reserve(other.orderFields.size());
                for (const auto& field : other.orderFields) {
                    orderFields.emplace_back(field); // Uses the copy constructor we defined
                }
                
                // Clear and deep copy for distinctFields
                distinctFields.clear();
                distinctFields.reserve(other.distinctFields.size());
                for (const auto& field : other.distinctFields) {
                    if (field) {
                        distinctFields.push_back(make_field_alias_unique(field));
                    }
                }
                
                // Clear and deep copy for onlyFields
                onlyFields.clear();
                onlyFields.reserve(other.onlyFields.size());
                for (const auto& field : other.onlyFields) {
                    if (field) {
                        onlyFields.push_back(make_field_alias_unique(field));
                    }
                }
                
                // Clear and deep copy for groupByFields
                groupByFields.clear();
                groupByFields.reserve(other.groupByFields.size());
                for (const auto& field : other.groupByFields) {
                    if (field) {
                        groupByFields.push_back(make_field_alias_unique(field));
                    }   
                }
            }
            return *this;
        }
        
        // Allow moving
        QuerySet(QuerySet&&) noexcept = default;
        QuerySet& operator=(QuerySet&&) noexcept = default;
        
        // Virtual destructor for proper cleanup
        virtual ~QuerySet() = default;

        // =============================
        // =============================
        // INSERT
        // =============================
        std::expected<std::vector<int>, std::string> insert(const std::vector<T>& objs) {
            if (objs.empty()) return {};
            
            try {
                auto field_names = get_insert_field_names();
                if (field_names.empty()) return {};
                
                // Convert to const pointer vector
                std::vector<const T*> obj_ptrs;
                obj_ptrs.reserve(objs.size());
                for (const auto& obj : objs) {
                    obj_ptrs.emplace_back(&obj);
                }
                
                return execute_insert(obj_ptrs, field_names);
                
            } catch (const std::exception& e) {
                std::cerr << "Exception in insert: " << e.what() << "\n";
                return {};
            }
        }
        
        // Single insert method
        std::expected<int, std::string> insert(const T& obj) {
            try {
                auto field_names = get_insert_field_names();
                if (field_names.empty()) return -1;
                
                auto ids = execute_insert({&obj}, field_names);
                return ids.empty() ? -1 : ids[0];
            } catch (const std::exception& e) {
                std::cerr << "Exception in insert: " << e.what() << "\n";
                return -1;
            }
        }
        
        // Returns a Statement object for the insert query without executing it
        Statement stmt_insert(const T& obj) {
            auto field_names = get_insert_field_names();
            if (field_names.empty()) {
                return Statement(conn, "");
            }
            
            std::string sql = build_insert_sql(field_names, 1, true); // Use RETURNING id
            if (sql.empty()) {
                return Statement(conn, "");
            }
            
            auto stmt = Statement(conn, sql);
            
            // Bind all values for the object
            int param_index = 1;
            bind_object_values(stmt, obj, param_index);
            
            return stmt;
        }
        
        // Returns a Statement object for multiple inserts without executing it
        Statement stmt_insert(const std::vector<T>& objs) {
            if (objs.empty()) return Statement(conn, "");
            
            auto field_names = get_insert_field_names();
            if (field_names.empty()) return Statement(conn, "");
            
            std::string sql = build_insert_sql(field_names, objs.size(), true); // Use RETURNING id
            if (sql.empty()) return Statement(conn, "");
            
            auto stmt = Statement(conn, sql);
            
            // Bind all values for all objects
            int param_index = 1;
            for (const auto& obj : objs) {
                bind_object_values(stmt, obj, param_index);
            }
            
            return stmt;
        }
        
    private:
        std::vector<int> execute_insert(const std::vector<const T*>& obj_ptrs, const std::vector<std::string>& field_names) {
            if (obj_ptrs.empty()) return {};
            
            // Convert to vector of objects for stmt_insert
            std::vector<T> objs;
            objs.reserve(obj_ptrs.size());
            for (const T* obj_ptr : obj_ptrs) {
                objs.emplace_back(*obj_ptr);
            }
            
            // Use stmt_insert to get the prepared statement
            auto stmt = stmt_insert(objs);
            
            std::vector<int> generated_ids;
            
            // Execute and collect all returned IDs
            auto rows = stmt.execute_all();
            
            // Extract IDs from the returned rows
            for (const auto& row : rows) {
                generated_ids.emplace_back(row.get_int(0)); // ID is in the first column
            }
            
            return generated_ids;
        }

        std::string build_insert_sql(const std::vector<std::string>& field_names, size_t num_objects = 1, bool returning_id = false) {
            if (field_names.empty()) return "";
            
            std::vector<std::string> placeholders(field_names.size(), "?");
            std::string single_group = fmt::format("({})", fmt::join(placeholders, ", "));
            
            std::vector<std::string> value_groups(num_objects, single_group);
            
            std::string returning_clause = returning_id ? " RETURNING id" : "";
            
            return fmt::format(
                "INSERT INTO {} ({}) VALUES {}{};",
                this->template get_table_name<T>(),
                fmt::join(field_names, ", "),
                fmt::join(value_groups, ", "),
                returning_clause
            );
        }

        // Helper function to get field names (excluding 'id')
        std::vector<std::string> get_insert_field_names() const {
            auto field_names = refl::reflect<T>::field_names();
            
            // Remove "id" field if it exists
            std::erase(field_names, "id");
            
            return field_names;
        }
        
    public:
        // =============================
        // =============================
        // UPDATE
        // =============================
        std::expected<bool, std::string> update(const T& obj) {
            try {
                auto field_names = get_update_field_names();
                if (field_names.empty()) return false;
                
                return execute_update({&obj}, field_names);
                
            } catch (const std::exception& e) {
                std::cerr << "Exception in update: " << e.what() << "\n";
                return false;
            }
        }
        
        // Update multiple objects (batch update)
        std::expected<bool, std::string> update(const std::vector<T>& objs) {
            if (objs.empty()) return true;
            
            try {
                auto field_names = get_update_field_names();
                if (field_names.empty()) return false;
                
                // Convert to pointer vector
                std::vector<const T*> obj_ptrs;
                obj_ptrs.reserve(objs.size());
                for (const auto& obj : objs) {
                    obj_ptrs.emplace_back(&obj);
                }
                
                return execute_update(obj_ptrs, field_names);
                
            } catch (const std::exception& e) {
                std::cerr << "Exception in update: " << e.what() << "\n";
                return false;
            }
        }

    private:
        std::expected<std::string, std::string> build_update_sql(const std::vector<std::string>& field_names, size_t num_objects) {
            if (field_names.empty() || num_objects == 0) return "";
            
            // For SQLite, use a single UPDATE with CASE expressions for each field
            // This is more efficient for larger batches
            std::vector<std::string> set_clauses;
            
            // For each field, create a CASE expression
            for (const auto& field : field_names) {
                std::string case_expr = fmt::format("{} = CASE id\n", field);
                
                // Add WHEN ? THEN ? for each object
                for (size_t i = 0; i < num_objects; ++i) {
                    case_expr += "        WHEN ? THEN ?\n";
                }
                
                // Close with ELSE and END
                case_expr += fmt::format("        ELSE {}\n    END", field);
                set_clauses.emplace_back(case_expr);
            }
            
            // Create placeholders for the WHERE IN clause
            std::vector<std::string> id_placeholders(num_objects, "?");
            return fmt::format(
                "UPDATE {}\nSET\n    {}\nWHERE id IN ({});",
                this->template get_table_name<T>(),
                fmt::join(set_clauses, ",\n    "),
                fmt::join(id_placeholders, ", ")
            );
        }

        std::expected<bool, std::string> execute_update(const std::vector<const T*>& obj_ptrs, const std::vector<std::string>& field_names) {
            if (obj_ptrs.empty()) return std::unexpected("Empty object pointers vector");
            
            auto sql = build_update_sql(field_names, obj_ptrs.size());
            if (!sql.has_value()) return std::unexpected(sql.error());
            
            auto stmt = Statement(conn, sql.value());
            
            // Parameter binding for CASE-based batch update is different:
            // For each field, we need to bind all object IDs and their values
            int param_index = 1;
            
            // For each field, bind WHEN id THEN value pairs
            for (const auto& field : field_names) {
                for (const T* obj_ptr : obj_ptrs) {
                    // Bind ID for WHEN clause
                    stmt.bind(param_index++, obj_ptr->id);
                    
                    // Bind field value for THEN clause
                    bind_field_value(stmt, *obj_ptr, field, param_index);
                }
            }
            
            // Finally, bind all IDs for the WHERE IN clause
            for (const T* obj_ptr : obj_ptrs) {
                stmt.bind(param_index++, obj_ptr->id);
            }
            
            return stmt.execute();
        }
    
        std::vector<std::string> get_update_field_names() const {
            return get_insert_field_names();
        }

    public:
        // =============================
        // =============================
        // DELETE
        // =============================
        std::expected<bool, std::string> remove(const T& obj) {
            try {
                return execute_delete({&obj});
            } catch (const std::exception& e) {
                std::cerr << "Exception in delete: " << e.what() << "\n";
                return false;
            }
        }
        
        // Delete records based on where conditions without requiring an object
        std::expected<bool, std::string> remove() {
            try {
                auto sql = build_delete_by_condition_sql();
                if (!sql.has_value()) return std::unexpected(sql.error());
                
                auto stmt = Statement(conn, sql.value());
                
                // Bind parameters from where expression
                if (_whereExpression.has_value()) {
                    auto query_result = _whereExpression.value().to_query();
                    bind_query_parameters(stmt, query_result);
                }
                
                return stmt.execute();
            } catch (const std::exception& e) {
                std::cerr << "Exception in delete by condition: " << e.what() << "\n";
                return false;
            }
        }
        
        std::expected<bool, std::string> remove(const std::vector<T>& objs) {
            if (objs.empty()) return std::unexpected("Empty objects vector");
            
            try {
                // Convert to pointer vector
                std::vector<const T*> obj_ptrs;
                obj_ptrs.reserve(objs.size());
                for (const auto& obj : objs) {
                    obj_ptrs.emplace_back(&obj);
                }
                
                return execute_delete(obj_ptrs);
                
            } catch (const std::exception& e) {
                std::cerr << "Exception in delete: " << e.what() << "\n";
                return false;
            }
        }

    private:
        std::expected<std::string, std::string> build_delete_sql() {
            return fmt::format("DELETE FROM {} WHERE id = ?;", this->template get_table_name<T>());
        }
        
        std::expected<std::string, std::string> build_delete_by_condition_sql() {
            std::string sql = fmt::format("DELETE FROM {}", this->template get_table_name<T>());
            
            if (_whereExpression.has_value()) {
                auto query_result = _whereExpression.value().to_query();
                sql += " WHERE " + query_result.sql;
            }
            
            sql += ";";
            return sql;
        }

        std::expected<std::string, std::string> build_batch_delete_sql(size_t count) {
            std::vector<std::string> placeholders(count, "?");
            return fmt::format(
                "DELETE FROM {} WHERE id IN ({});",
                this->template get_table_name<T>(),
                fmt::join(placeholders, ", ")
            );
        }

        std::expected<bool, std::string> execute_delete(const std::vector<const T*>& obj_ptrs) {
            if (obj_ptrs.empty()) return std::unexpected("Empty object pointers vector");
            
            if (obj_ptrs.size() == 1) {
                // Single delete
                auto sql = build_delete_sql();
                if (!sql.has_value()) return std::unexpected(sql.error());
                
                auto stmt = Statement(conn, sql.value());
                stmt.bind(1, obj_ptrs[0]->id);
                return stmt.execute();
            } else {
                // Batch delete using IN clause
                auto sql = build_batch_delete_sql(obj_ptrs.size());
                if (!sql.has_value()) return std::unexpected(sql.error());
                
                auto stmt = Statement(conn, sql.value());
                
                int param_index = 1;
                for (const T* obj_ptr : obj_ptrs) {
                    stmt.bind(param_index++, obj_ptr->id);
                }
                
                return stmt.execute();
            }
        }

    public:
        // =============================
        // =============================
        // ALIAS
        // =============================
        [[nodiscard]] std::string alias() const {
            return this->_alias;
        }

    public:
        // =============================
        // =============================
        // GROUP BY
        // =============================
        
        // Compile-time version for multiple member pointers
        template<auto... Fields>
        QuerySet& group_by() {
            static_assert(sizeof...(Fields) > 0, "Must provide at least one field");
            static_assert((std::is_member_pointer_v<decltype(Fields)> && ...), 
                        "All fields must be member pointers");
            
            // Reserve capacity for efficiency
            this->groupByFields.reserve(sizeof...(Fields));
            
            // Process each field using a fold expression
            auto addField = [this]<auto Field>() {
                this->groupByFields.emplace_back(std::make_unique<FieldAlias<Field>>());
            };
            
            // Apply the lambda to each field in the parameter pack
            (addField.template operator()<Fields>(), ...);
            
            return *this;
        }

    private:
        [[nodiscard]] std::string generateGroupBySQL() const {
            if (groupByFields.empty()) {
                return "";
            }
            
            std::vector<std::string> fieldStrings;
            fieldStrings.reserve(groupByFields.size());
            
            auto fieldNames = groupByFields | std::ranges::views::transform([](const auto& field) {
                return field->getFullFieldName();
            });
            fieldStrings.assign(fieldNames.begin(), fieldNames.end());
            return fmt::format("GROUP BY {}", fmt::join(fieldStrings, ", "));
        }
    
    public:
        // =============================
        // =============================
        // ORDER BY
        // =============================
        // Convenience overload for a single field (defaults to ascending order)
        template<auto Field>
        QuerySet& order_by(Collation collation = Collation::NONE) {
            return add_order_field<Field>(true, collation);
        }

        // Modern compile-time version for multiple field-direction pairs
        template<auto Field, auto Direction, auto... Rest>
        QuerySet& order_by() {
            // Ensure we have valid field-direction pairs
            static_assert(std::is_member_pointer_v<decltype(Field)>, 
                        "Field must be a member pointer");
            static_assert(std::is_same_v<decltype(Direction), bool>, 
                        "Direction must be a boolean value");
            static_assert(sizeof...(Rest) % 2 == 0, 
                        "Must provide field-direction pairs (field, bool, field, bool, ...)");
            
            // Reserve capacity for all pairs
            this->orderFields.reserve(this->orderFields.size() + (sizeof...(Rest) / 2 + 1));
            
            // Add the first field-direction pair
            this->orderFields.emplace_back(
                std::make_unique<FieldAlias<Field>>(),
                Direction,
                Collation::NONE // Default to no collation
            );
            
            // Process remaining pairs if any
            if constexpr (sizeof...(Rest) > 0) {
                processOrderByPairs<Rest...>();
            }
            
            return *this;
        }
        
        // Modern compile-time version with collation support
        template<auto Field, auto Direction, auto Coll, auto... Rest>
        QuerySet& order_by_collate() {
            // Ensure we have valid field-direction-collation triplets
            static_assert(std::is_member_pointer_v<decltype(Field)>, 
                        "Field must be a member pointer");
            static_assert(std::is_same_v<decltype(Direction), bool>, 
                        "Direction must be a boolean value");
            static_assert(std::is_same_v<decltype(Coll), Collation>, 
                        "Collation must be a Collation enum value");
            static_assert(sizeof...(Rest) % 3 == 0, 
                        "Must provide field-direction-collation triplets (field, bool, collation, ...)");
            
            // Reserve capacity for all triplets
            this->orderFields.reserve(this->orderFields.size() + (sizeof...(Rest) / 3 + 1));
            
            // Add the first field-direction-collation triplet
            this->orderFields.emplace_back(
                std::make_unique<FieldAlias<Field>>(),
                Direction,
                Coll
            );
            
            // Process remaining triplets if any
            if constexpr (sizeof...(Rest) > 0) {
                processOrderByCollationPairs<Rest...>();
            }
            
            return *this;
        }
                
    private:
        // Helper to add an order field with direction and collation
        template<auto Field>
        QuerySet& add_order_field(bool ascending, Collation collation) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, 
                        "Field must be a member pointer");
            
            // Add field to order criteria with specified direction and collation
            this->orderFields.emplace_back(
                std::make_unique<FieldAlias<Field>>(),
                ascending,
                collation
            );
            
            return *this;
        }
        
        // Helper to process field-direction pairs recursively
        template<auto Field, auto Direction, auto... Rest>
        void processOrderByPairs() {
            static_assert(std::is_member_pointer_v<decltype(Field)>, 
                        "Field must be a member pointer");
            static_assert(std::is_same_v<decltype(Direction), bool>, 
                        "Direction must be a boolean value");
            
            // Add the current field-direction pair using the helper
            add_order_field<Field>(Direction, Collation::NONE);
            
            // Process remaining pairs if any
            if constexpr (sizeof...(Rest) > 0) {
                processOrderByPairs<Rest...>();
            }
        }
        
        // Helper to process field-direction-collation triplets recursively
        template<auto Field, auto Direction, auto Coll, auto... Rest>
        void processOrderByCollationPairs() {
            static_assert(std::is_member_pointer_v<decltype(Field)>, 
                        "Field must be a member pointer");
            static_assert(std::is_same_v<decltype(Direction), bool>, 
                        "Direction must be a boolean value");
            static_assert(std::is_same_v<decltype(Coll), Collation>, 
                        "Collation must be a Collation enum value");
            
            // Add the current field-direction-collation triplet
            this->orderFields.emplace_back(
                std::make_unique<FieldAlias<Field>>(),
                Direction,
                Coll
            );
            
            // Process remaining triplets if any
            if constexpr (sizeof...(Rest) > 0) {
                processOrderByCollationPairs<Rest...>();
            }
        }
    
        [[nodiscard]] std::string buildOrderFields() const {
            if(orderFields.empty()) {
                return "";
            }
            
            std::vector<std::string> fieldStrings;
            fieldStrings.reserve(orderFields.size());
            
            auto formattedFields = orderFields | std::ranges::views::transform([](const auto& orderInfo) {
                std::string fieldName = orderInfo.field->getFullFieldName();
                std::string direction = orderInfo.ascending ? "ASC" : "DESC";
                std::string collation = "";
                
                // Add collation if specified
                if (orderInfo.collation != Collation::NONE) {
                    collation = fmt::format(" COLLATE {}", collation_to_sql(orderInfo.collation));
                }
                
                return fmt::format("{}{} {}", fieldName, collation, direction);
            });
            fieldStrings.assign(formattedFields.begin(), formattedFields.end());
            
            return fmt::format(" ORDER BY {}", fmt::join(fieldStrings, ", "));
        }
    
    public:
        // =============================
        // =============================
        // WHERE
        // =============================
    
        // Method 1: Accept Storm Where objects directly (most flexible)
        QuerySet& where(const storm::Where& where_clause);
        QuerySet& where(storm::Where&& where_clause);

        // Compile-time member pointer version (C++23 NTTP)
        template<auto MemberPtr, typename Value>
        QuerySet& where(Value&& value, storm::Op op = storm::Op::EQ);

        // Special methods for complex operators
        // Compile-time member pointer version (C++23 NTTP)
        template<auto MemberPtr, typename Container>
        QuerySet& where_in(const Container& values);

        // Where between (compile-time member pointer version - C++23 NTTP)
        template<auto MemberPtr, typename T1, typename T2>
        QuerySet& where_between(T1&& value1, T2&& value2);

        // Where null (compile-time member pointer version - C++23 NTTP)
        template<auto MemberPtr>
        QuerySet& where_null();

        // Where not null (compile-time member pointer version - C++23 NTTP)
        template<auto MemberPtr>
        QuerySet& where_not_null();

        // Where like (compile-time member pointer version - C++23 NTTP)
        template<auto MemberPtr, typename Value>
        QuerySet& where_like(Value&& pattern);

        // Logical combination methods for fluent interface
        QuerySet& where_and(const storm::Where& condition);
        QuerySet& where_or(const storm::Where& condition);

        // Support for raw SQL WHERE clauses
        QuerySet& where_raw(const std::string& raw_condition, const std::vector<std::any>& parameters = {});

        // Date range queries (if you have date fields) - C++23 NTTP version
        template<auto MemberPtr>
        QuerySet& where_date_range(const std::string& start_date, 
                                const std::string& end_date);

        [[nodiscard]] storm::QueryResult get_where_query() const;

        // Helper method to clear WHERE conditions
        QuerySet& clear_where();

    private:
        // Where conditions
        template<typename FieldType, typename Value>
        storm::Where create_condition(const FieldType& field_obj, Value&& value, storm::Op op) const;
        
        // Helper to create IN conditions
        template<typename FieldType, typename Container>
        storm::Where create_in_condition(const FieldType& field_obj, const Container& values) const;
    
    public:
        // =============================
        // =============================
        // JOIN
        // =============================
        template<class U>
        QuerySet &join(std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(alias), std::move(addConditions), JoinInfo::JoinType::INNER);
            return *this;
        }

        template<class U>
        QuerySet &left_join(std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(alias), std::move(addConditions), JoinInfo::JoinType::LEFT);
            return *this;
        }

        template<class U>
        QuerySet &left_join(std::string &&cte, std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(cte),
                                        std::move(alias),
                                        std::move(addConditions),
                                        JoinInfo::JoinType::LEFT);
            return *this;
        }

        template<class U>
        QuerySet &right_join(std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(alias), std::move(addConditions), JoinInfo::JoinType::RIGHT);
            return *this;
        }

        template<class U>
        QuerySet &right_join(std::string &&cte, std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(cte),
                                        std::move(alias),
                                        std::move(addConditions),
                                        JoinInfo::JoinType::RIGHT);
            return *this;
        }

        template<class U>
        QuerySet &right_join(QuerySet<U> &&other, std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(other),
                                        std::move(alias),
                                        std::move(addConditions),
                                        JoinInfo::JoinType::RIGHT);
            return *this;
        }

    private:
        template<class U>
        void join_impl_core(std::string &&alias,
                            std::string &&addConditions,
                            JoinInfo::JoinType joinType) {
            std::string condition;
            
            // Get the target table name (what we're joining to)
            std::string targetTable = alias.empty() ? this->get_table_name<U>() : alias;
            
            // Try to automatically determine join condition using reflection
            condition = determine_join_condition<T, U>();
            
            if (condition.empty()) {
                condition = determine_join_condition<U, T>();
            }
            
            // If we couldn't auto-determine the condition and no additional conditions provided
            if (condition.empty() && addConditions.empty()) {
                throw std::runtime_error("Could not determine join condition automatically. Please provide explicit join conditions.");
            }
            
            // If we have additional conditions, append them
            if (!addConditions.empty()) {
                if (!condition.empty()) {
                    condition += " " + addConditions;
                } else {
                    condition = std::move(addConditions);
                }
            }
            
            if (!condition.empty()) {
                // Don't use std::move on condition to prevent it from being invalidated
                // This ensures the condition string remains valid for both the original and copied QuerySet
                joinInfo.joins.emplace_back(targetTable,
                                        std::move(alias),
                                        condition,  // Don't move the condition
                                        joinType);
            }
        }

        template<class U>
        void join_impl(std::string &&alias, std::string &&addConditions, const JoinInfo::JoinType joinType) {
            join_impl_core<U>(std::move(alias), std::move(addConditions), joinType);
        }

        std::string generateJoinSQL() const {
            std::string sql;
            for(const auto &[tableName, alias, condition, type]: joinInfo.joins) {
                if(!tableName.empty()) {
                    sql += fmt::format(R"( {} JOIN "{}" {} ON {})",
                                       joinTypeToString(type),
                                       tableName,
                                       alias.empty() ? "" : fmt::format(R"( AS "{}")", alias),
                                       condition);
                }
            }
            return sql;
        }

        template<typename SourceType, typename TargetType>
        std::string determine_join_condition() {
            auto sourceName = refl::reflect<SourceType>::get_struct_name();
            auto targetName = refl::reflect<TargetType>::get_struct_name();
            auto foreignKeyName = targetName + "_id";
            
            auto field_names = refl::reflect<SourceType>::field_names();
            
            // Find foreign key field
            auto it = std::ranges::find_if(field_names, [&](const auto& name) {
                return utils::to_lower(name) == foreignKeyName;
            });
            
            if (it != field_names.end()) {
                return fmt::format(R"("{}"."id" = "{}"."{}")",
                                  get_table_name<TargetType>(), 
                                  get_table_name<SourceType>(), 
                                  *it);
            }
            
            return "";
        }

    public:
        // =============================
        // =============================
        // ONLY
        // =============================
        // Modern compile-time version using NTTP
        template<auto... Fields>
        QuerySet& only() {
            static_assert((std::is_member_pointer_v<decltype(Fields)> && ...), 
                        "All fields must be member pointers");
            
            // Reserve capacity
            this->onlyFields.reserve(sizeof...(Fields));
            
            // Process each field
            auto addField = [this]<auto Field>() {
                this->onlyFields.emplace_back(std::make_unique<FieldAlias<Field>>());
            };
            
            (addField.template operator()<Fields>(), ...);
            
            return *this;
        }
        
        // Version with alias support
        template<auto Field>
        QuerySet& only(std::string_view alias) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, 
                        "Field must be a member pointer");
            this->onlyFields.emplace_back(std::make_unique<FieldAlias<Field>>(std::string(alias)));
            return *this;
        }

    public:
        // =============================
        // =============================
        // SELECT
        // ===========================
        
        /**
         * @brief Select only specific fields and return them as dictionaries
         * 
         * This method provides a dynamic way to access the data when the exact
         * types are not known at compile time, or when you need to access data by field name.
         * 
         * @return std::vector<std::map<std::string, ValueVariant>> Vector of dictionaries with field name -> value mapping
         * 
         * @example
         *   auto results = QuerySet<Author>(conn)
         *       .only(field(&Author::name))
         *       .only(field(&Author::age))
         *       .select_values();
         *   Access data with: results[0]["name"], results[0]["age"]
         */
        ExpectedValueVectorMap select_values() const {            
            // Get the where query result
            auto where_query_result = this->get_where_query();
            
            // Build SQL query with std::format
            auto sql = std::format("SELECT {} {} FROM \"{}\" {} {} {} {} {}", 
                this->createDistinctClause(),
                this->buildFieldsClause(),
                this->template get_table_name<T>(),
                this->generateJoinSQL(),
                where_query_result.sql,
                this->generateGroupBySQL(),
                this->buildOrderFields(),
                this->limit_impl()
            );
            std::print("SQL: {}\n", sql);
            
            // Execute the query
            Statement stmt(conn, sql);
            bind_query_parameters(stmt, where_query_result);
            // Process results using ranges and views
            return transform_rows_to_value_maps(stmt.execute_all());
        }
        
        ExpectedValueVectorMap select_value() const {            
            // Get the where query result
            auto where_query_result = this->get_where_query();
            
            // Build SQL query with std::format
            auto sql = std::format("SELECT {} {} FROM \"{}\" {} {} {} {} {}", 
                this->createDistinctClause(),
                this->buildFieldsClause(),
                this->template get_table_name<T>(),
                this->generateJoinSQL(),
                where_query_result.sql,
                this->generateGroupBySQL(),
                this->buildOrderFields(),
                this->limit_impl()
            );
            std::print("SQL: {}\n", sql);
            
            // Execute the query
            Statement stmt(conn, sql);
            bind_query_parameters(stmt, where_query_result);
            // Process results using ranges and views
            return transform_rows_to_value_maps(stmt.execute_all()[0]);
        }

        ExpectedT select_one() const {
            // Build the query with limit 1 to ensure we only get one result
            // Get the where query result
            auto where_query_result = this->get_where_query();
            
            // Get field names and clause for SQL generation
            auto fieldsClause = this->buildFieldsClause();
            
            // Build the SQL query with LIMIT 1
            auto sql = fmt::format("SELECT {} {} FROM \"{}\" {} {} {} {} LIMIT 1;", 
                this->createDistinctClause(),
                fieldsClause,
                this->template get_table_name<T>(),
                this->generateJoinSQL(),
                where_query_result.sql,
                this->generateGroupBySQL(),
                this->buildOrderFields());
            
            Statement smt(conn, sql);
            bind_query_parameters(smt, where_query_result);
            auto all_rows = smt.execute_all();

            if (all_rows.empty()) {
                throw std::runtime_error("No results found for select_one query");
            }
            
            // Process the single row
            T obj{};
            const auto& row = all_rows[0];
            int column_idx = 0;
            
            if (this->onlyFields.empty()) {
                // When no specific fields are selected, populate all fields
                Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                    if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                        using FieldType = typename Reflect<T>::template member_value_type<decltype(member)>;
                        
                        if constexpr (std::is_same_v<FieldType, std::string>) {
                            member(obj) = row.get_text(column_idx);
                        } else if constexpr (std::is_same_v<FieldType, bool>) {
                            member(obj) = row.get_int(column_idx) != 0;
                        } else if constexpr (std::is_integral_v<FieldType>) {
                            member(obj) = static_cast<FieldType>(row.get_int(column_idx));
                        } else if constexpr (std::is_floating_point_v<FieldType>) {
                            member(obj) = static_cast<FieldType>(row.get_double(column_idx));
                        } else {
                            static_assert(std::is_pointer<FieldType>::value == false, "Unsupported pointer type for SQLite binding");
                            static_assert(std::is_reference<FieldType>::value == false, "Unsupported reference type for SQLite binding");
                            static_assert(sizeof(FieldType) == 0, "Unsupported type for SQLite binding");
                        }
                        
                        column_idx++;
                    }
                });
            } else {
                // When specific fields are selected, only populate those fields
                for (const auto& fieldAlias : this->onlyFields) {
                    if (!fieldAlias) continue;
                    
                    const std::string fieldName = fieldAlias->getFieldName();
                    
                    // Find and populate the corresponding field
                    Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                        if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                            const std::string memberName = Reflect<T>::get_member_name(member);
                            if (memberName == fieldName) {
                                using FieldType = typename Reflect<T>::template member_value_type<decltype(member)>;
                                
                                if constexpr (std::is_same_v<FieldType, std::string>) {
                                    member(obj) = row.get_text(column_idx);
                                } else if constexpr (std::is_same_v<FieldType, bool>) {
                                    member(obj) = row.get_int(column_idx) != 0;
                                } else if constexpr (std::is_integral_v<FieldType>) {
                                    member(obj) = static_cast<FieldType>(row.get_int(column_idx));
                                } else if constexpr (std::is_floating_point_v<FieldType>) {
                                    member(obj) = static_cast<FieldType>(row.get_double(column_idx));
                                } else {
                                    static_assert(std::is_pointer<FieldType>::value == false, "Unsupported pointer type for SQLite binding");
                                    static_assert(std::is_reference<FieldType>::value == false, "Unsupported reference type for SQLite binding");
                                    static_assert(sizeof(FieldType) == 0, "Unsupported type for SQLite binding");
                                }
                            }
                        }
                    });
                    column_idx++;
                }
            }
            
            return obj;
        }
        
        ExpectedVectorT select_all() const {
            // Get the where query result
            auto where_query_result = this->get_where_query();
            
            // Get field names and clause for SQL generation
            auto fieldsClause = this->buildFieldsClause();
            
            // Build the SQL query
            auto sql = fmt::format("SELECT {} {} FROM \"{}\" {} {} {} {} {}", 
                this->createDistinctClause(),
                fieldsClause,
                this->template get_table_name<T>(),
                this->generateJoinSQL(),
                where_query_result.sql,
                this->generateGroupBySQL(),
                this->buildOrderFields(),
                this->limit_impl());
            std::cout << sql << std::endl;
            
            auto smt_ = Statement(conn, sql);
            bind_query_parameters(smt_, where_query_result);
            auto all_rows = smt_.execute_all();
            
            // Process the results
            std::vector<T> results;
            if (all_rows.empty()) {
                return results;
            }
            
            results.reserve(all_rows.size());
            
            for (const auto& row : all_rows) {
                T obj{};
                int column_idx = 0;
                
                if (!this->distinctFields.empty()) {
                    // When distinct fields are selected, only populate those fields
                    // and make sure we don't exceed the column count
                    for (size_t i = 0; i < this->distinctFields.size() && i < row.get_column_count(); ++i) {
                        const auto& fieldAlias = this->distinctFields[i];
                        if (!fieldAlias) continue;
                        
                        const std::string fieldName = fieldAlias->getFieldName();
                        
                        // Find and populate the corresponding field
                        Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                            if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                                const std::string memberName = Reflect<T>::get_member_name(member);
                                if (memberName == fieldName) {
                                    using FieldType = typename Reflect<T>::template member_value_type<decltype(member)>;
                                    
                                    if constexpr (std::is_same_v<FieldType, std::string>) {
                                        member(obj) = row.get_text(i);
                                    } else if constexpr (std::is_same_v<FieldType, bool>) {
                                        member(obj) = row.get_int(i) != 0;
                                    } else if constexpr (std::is_integral_v<FieldType>) {
                                        member(obj) = static_cast<FieldType>(row.get_int(i));
                                    } else if constexpr (std::is_floating_point_v<FieldType>) {
                                        member(obj) = static_cast<FieldType>(row.get_double(i));
                                    }
                                }
                            }
                        });
                    }
                } else if (this->onlyFields.empty()) {
                    // When no specific fields are selected, populate all fields
                    Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                        if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                            using FieldType = typename Reflect<T>::template member_value_type<decltype(member)>;
                            
                            if constexpr (std::is_same_v<FieldType, std::string>) {
                                member(obj) = row.get_text(column_idx);
                            } else if constexpr (std::is_same_v<FieldType, bool>) {
                                member(obj) = row.get_int(column_idx) != 0;
                            } else if constexpr (std::is_integral_v<FieldType>) {
                                member(obj) = static_cast<FieldType>(row.get_int(column_idx));
                            } else if constexpr (std::is_floating_point_v<FieldType>) {
                                member(obj) = static_cast<FieldType>(row.get_double(column_idx));
                            } else {
                                static_assert(std::is_pointer<FieldType>::value == false, "Unsupported pointer type for SQLite binding");
                                static_assert(std::is_reference<FieldType>::value == false, "Unsupported reference type for SQLite binding");
                                static_assert(sizeof(FieldType) == 0, "Unsupported type for SQLite binding");
                            }
                            
                            column_idx++;
                        }
                    });
                } else {
                    // When specific fields are selected, only populate those fields
                    for (const auto& fieldAlias : this->onlyFields) {
                        if (!fieldAlias) continue;
                        
                        const std::string fieldName = fieldAlias->getFieldName();
                        
                        // Find and populate the corresponding field
                        Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                            if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                                const std::string memberName = Reflect<T>::get_member_name(member);
                                if (memberName == fieldName) {
                                    using FieldType = typename Reflect<T>::template member_value_type<decltype(member)>;
                                    
                                    if constexpr (std::is_same_v<FieldType, std::string>) {
                                        member(obj) = row.get_text(column_idx);
                                    } else if constexpr (std::is_same_v<FieldType, bool>) {
                                        member(obj) = row.get_int(column_idx) != 0;
                                    } else if constexpr (std::is_integral_v<FieldType>) {
                                        member(obj) = static_cast<FieldType>(row.get_int(column_idx));
                                    } else if constexpr (std::is_floating_point_v<FieldType>) {
                                        member(obj) = static_cast<FieldType>(row.get_double(column_idx));
                                    } else {
                                        static_assert(std::is_pointer<FieldType>::value == false, "Unsupported pointer type for SQLite binding");
                                        static_assert(std::is_reference<FieldType>::value == false, "Unsupported reference type for SQLite binding");
                                        static_assert(sizeof(FieldType) == 0, "Unsupported type for SQLite binding");
                                    }
                                }
                            }
                        });
                        column_idx++;
                    }
                }
                
                results.emplace_back(std::move(obj));
            }
            
            return results;
        }

    private:
        /**
         * @brief Transform a vector of rows into a vector of value maps
         * 
         * This helper function processes database rows and converts them into a vector of maps
         * where each map represents a row with column name -> value mapping.
         * 
         * @param rows Vector of Row objects to transform
         * @return std::vector<std::map<std::string, ValueVariant>> Vector of dictionaries with column name -> value mapping
         */
        ValueVectorMap transform_rows_to_value_maps(const std::vector<Row>& rows) const {
            return rows 
                | std::views::transform([](const auto& row) {
                    ValueMap rowDict;
                    
                    // Process each column using the Row's column information
                    for (const auto& i : std::views::iota(0, row.get_column_count())) {
                        // Get column name directly from Row and convert to std::string for map key
                        std::string column_name{row.get_column_name(i)};
                        rowDict[column_name] = to_value<ValueVariant>(row, i);
                    }
                    
                    return rowDict;
                })
                | std::ranges::to<ValueVectorMap>();
        }
    
    public:
        // =============================
        // =============================
        // LIMIT/OFFSET
        // =============================
        QuerySet &limit(int limit) {
            this->_limit = limit;
            return *this;
        }

        QuerySet &offset(int offset) {
            this->_offset = offset;
            return *this;
        }
    private:
        [[nodiscard]] std::string limit_impl() const {
            if(!_limit && !_offset) {
                return "";
            }

            if(_limit && _offset) {
                return fmt::format(" LIMIT {} OFFSET {}", _limit, _offset);
            } else if(_limit) {
                return fmt::format(" LIMIT {}", _limit);
            } else {
                // SQLite requires LIMIT when using OFFSET
                // Use LIMIT -1 to get all rows with an offset
                return fmt::format(" LIMIT -1 OFFSET {}", _offset);
            }
        }

    public:
        // =============================
        // =============================
        // DISTINCT
        // =============================
        template<auto... Fields>
        QuerySet& distinct() {
            static_assert((std::is_member_pointer_v<decltype(Fields)> && ...), 
                        "All fields must be member pointers");
            
            // Reserve capacity
            this->distinctFields.reserve(sizeof...(Fields));
            
            // Process each field
            auto addField = [this]<auto Field>() {
                this->distinctFields.emplace_back(std::make_unique<FieldAlias<Field>>());
            };
            
            (addField.template operator()<Fields>(), ...);
            
            return *this;
        }
        
        // The distinct functionality is now integrated into select_values()
        
    private:
        /**
         * @brief Build the fields clause and field names for SQL queries
         * 
         * This method handles both distinct fields, only fields, and aggregate functions
         * 
         * @return std::string Fields clause
         */
        [[nodiscard]] std::string buildFieldsClause() const {
            // Build fields clause based on the available fields and functions
            std::string fieldsClause;
            
            // First, process any function aliases (like MAX, MIN, etc.)
            std::vector<std::string> functionClauses;
            for (const auto& func : functionsSet) {
                // Extract the alias from the function string (assuming format "FUNCTION(...) AS alias")
                std::string funcStr = func.toStr();
                functionClauses.push_back(funcStr);
            }
            
            if (!this->distinctFields.empty() && this->onlyFields.empty()) {
                // Use distinct fields
                std::vector<std::string> fieldStrings;
                fieldStrings.reserve(distinctFields.size());
                
                for (const auto& fieldAlias : distinctFields) {
                    fieldStrings.emplace_back(fieldAlias->getFullFieldName());
                }
                
                // Build fields string, including function clauses
                if (!functionClauses.empty()) {
                    std::string funcStr = fmt::format("{}", fmt::join(functionClauses, ", "));
                    if (!fieldStrings.empty()) {
                        fieldsClause = fmt::format("{}, {}", fmt::join(fieldStrings, ", "), funcStr);
                    } else {
                        fieldsClause = funcStr;
                    }
                } else {
                    fieldsClause = fmt::format("{}", fmt::join(fieldStrings, ", "));
                }
            } else if (!this->onlyFields.empty()) {
                // Use only fields
                std::vector<std::string> fieldStrings;
                fieldStrings.reserve(onlyFields.size());
                
                for (const auto& fieldAlias : onlyFields) {
                    fieldStrings.emplace_back(fieldAlias->alias.empty() ?
                        fieldAlias->getFullFieldName() :
                        fmt::format("{} AS {}", fieldAlias->getFullFieldName(), fieldAlias->alias));
                }
                
                // Build fields string, including function clauses
                if (!functionClauses.empty()) {
                    std::string funcStr = fmt::format("{}", fmt::join(functionClauses, ", "));
                    if (!fieldStrings.empty()) {
                        fieldsClause = fmt::format("{}, {}", fmt::join(fieldStrings, ", "), funcStr);
                    } else {
                        fieldsClause = funcStr;
                    }
                } else {
                    fieldsClause = fmt::format("{}", fmt::join(fieldStrings, ", "));
                }
            } else if (!functionClauses.empty()) {
                // Only functions are specified
                fieldsClause = fmt::format("{}", fmt::join(functionClauses, ", "));
            } else {
                // When no specific fields requested, explicitly list all fields using reflection
                std::vector<std::string> fieldStrings;
                Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                    if constexpr (refl::descriptor::is_field(member)) {
                        std::string tableName = this->template get_table_name<T>();
                        std::string fieldName = std::string(member.name);
                        std::string formattedField = utils::formatFieldName(tableName, fieldName);
                        fieldStrings.emplace_back(fmt::format(R"({} AS "{}")", formattedField, fieldName));
                    }
                });
                
                fieldsClause = fmt::format("{}", fmt::join(fieldStrings, ", "));
            }
            
            return fieldsClause;
        }
        
        [[nodiscard]] std::string createDistinctClause() const {
            if(distinctFields.empty()) {
                return "";
            }
            
            return "DISTINCT ";
        }

    public:
        // =============================
        // FUNCTIONS
        // =============================
        // MAX aggregate function
        template<auto Field>
        QuerySet &max(std::string_view alias = "") {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            auto field = std::make_unique<FieldAlias<Field>>();
            std::string actual_alias(alias);
            if(actual_alias.empty()) {
                actual_alias = fmt::format("max_{}", field->getFieldName());
            }
            // We want to keep any existing onlyFields to allow selecting both fields and aggregate functions
            functions(Function(fmt::format("MAX({}) AS {}", field->getFullFieldName(), actual_alias)));
            return *this;
        }
        
        // MIN aggregate function
        template<auto Field>
        QuerySet &min(std::string_view alias = "") {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            auto field = std::make_unique<FieldAlias<Field>>();
            std::string actual_alias(alias);
            if(actual_alias.empty()) {
                actual_alias = fmt::format("min_{}", field->getFieldName());
            }
            // We want to keep any existing onlyFields to allow selecting both fields and aggregate functions
            functions(Function(fmt::format("MIN({}) AS {}", field->getFullFieldName(), actual_alias)));
            return *this;
        }

        // AVG aggregate function
        template<auto Field>
        QuerySet &avg(std::string_view alias = "") {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            // Only numeric fields should be used with AVG
            using FieldType = typename member_pointer_traits<decltype(Field)>::type;
            static_assert(std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>, "AVG can only be used with numeric fields");
            auto field = std::make_unique<FieldAlias<Field>>();
            std::string actual_alias(alias);
            if(actual_alias.empty()) {
                actual_alias = fmt::format("avg_{}", field->getFieldName());
            }
            // We want to keep any existing onlyFields to allow selecting both fields and aggregate functions
            functions(Function(fmt::format("AVG({}) AS {}", field->getFullFieldName(), actual_alias)));
            return *this;
        }

        // COUNT aggregate function
        template<auto Field>
        QuerySet &count(std::string_view alias = "") {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            auto field = std::make_unique<FieldAlias<Field>>();
            std::string actual_alias(alias);
            if(actual_alias.empty()) {
                actual_alias = fmt::format("count_{}", field->getFieldName());
            }
            // We want to keep any existing onlyFields to allow selecting both fields and aggregate functions
            functions(Function(fmt::format("COUNT({}) AS {}", field->getFullFieldName(), actual_alias)));
            return *this;
        }
        
        // SUM aggregate function
        template<auto Field>
        QuerySet &sum(std::string_view alias = "") {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "Field must be a member pointer");
            // Only numeric fields should be used with SUM
            using FieldType = typename member_pointer_traits<decltype(Field)>::type;
            static_assert(std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>, "SUM can only be used with numeric fields");
            auto field = std::make_unique<FieldAlias<Field>>();
            std::string actual_alias(alias);
            if(actual_alias.empty()) {
                actual_alias = fmt::format("sum_{}", field->getFieldName());
            }
            functions(Function(fmt::format("SUM({}) AS {}", field->getFullFieldName(), actual_alias)));
            return *this;
        }

        // Helper function to check member pointer at compile time
        template<auto Field>
        static constexpr bool check_member_pointer() {
            static_assert(std::is_member_pointer_v<decltype(Field)>, "All fields must be member pointers");
            return true;
        }

        template<auto FirstField, auto... RestFields>
        QuerySet &group_concat(std::string_view alias = "", 
                              std::string_view separator = ",", 
                              std::string_view fieldSeparator = " ",
                              bool distinct = false) {
            return group_concat_impl<FirstField, RestFields...>(alias, separator, fieldSeparator, distinct);
        }
        
        // Overload with ORDER BY for multiple fields - requires explicit specification
        template<auto OrderField, auto FirstField, auto... RestFields>
        QuerySet &group_concat_order(std::string_view alias = "", 
                              std::string_view separator = ",", 
                              std::string_view fieldSeparator = " ",
                              bool distinct = false) {
            return group_concat_with_order_impl<OrderField, FirstField, RestFields...>(alias, separator, fieldSeparator, distinct);
        }

        // MAX aggregate function that returns the direct value instead of a QuerySet
        template<auto Field>
        auto max_value() -> std::expected<typename member_pointer_traits<decltype(Field)>::type, std::string> {
            using FieldType = typename member_pointer_traits<decltype(Field)>::type;
            
            return execute_aggregate_query<FieldType>(
                [](auto& qs) { qs.template max<Field>(); },
                "find maximum value"
            );
        }

        // MIN aggregate function that returns the direct value instead of a QuerySet
        template<auto Field>
        auto min_value() -> std::expected<typename member_pointer_traits<decltype(Field)>::type, std::string> {
            using FieldType = typename member_pointer_traits<decltype(Field)>::type;
            
            return execute_aggregate_query<FieldType>(
                [](auto& qs) { qs.template min<Field>(); },
                "find minimum value"
            );
        }

        // AVG aggregate function that returns the direct value instead of a QuerySet
        template<auto Field>
        auto avg_value() -> std::expected<double, std::string> {
            return execute_aggregate_query<double>(
                [](auto& qs) { qs.template avg<Field>(); },
                "average"
            );
        }
        
        // COUNT aggregate function that returns the direct value instead of a QuerySet
        template<auto Field>
        auto count_value() -> std::expected<int, std::string> {
            return execute_aggregate_query<int>(
                [](auto& qs) { qs.template count<Field>(); },
                "count"
            );
        }
        
        // SUM aggregate function that returns the direct value instead of a QuerySet
        template<auto Field>
        auto sum_value() -> std::expected<double, std::string> {
            using FieldType = typename member_pointer_traits<decltype(Field)>::type;
            return execute_aggregate_query<double>(
                [](auto& qs) { qs.template sum<Field>(); },
                "sum"
            );
        }
        
        // GROUP_CONCAT that returns the direct string value
        template<auto FirstField, auto... RestFields>
        auto group_concat_value(std::string_view separator = ",", std::string_view fieldSeparator = " ", bool distinct = false) -> std::expected<std::string, std::string> {
            return execute_aggregate_query<std::string>(
                [separator, fieldSeparator, distinct](auto& qs) { qs.template group_concat<FirstField, RestFields...>("", separator, fieldSeparator, distinct); },
                "group_concat"
            );
        }
        
        // GROUP_CONCAT with ORDER BY that returns the direct string value
        template<auto FirstField, auto... RestFields, auto OrderField>
        auto group_concat_value(std::string_view separator = ",", std::string_view fieldSeparator = " ", bool distinct = false) -> std::expected<std::string, std::string> {
            return execute_aggregate_query<std::string>(
                [separator, fieldSeparator, distinct](auto& qs) { 
                    qs.template group_concat<FirstField, RestFields..., OrderField>("", separator, fieldSeparator, distinct); 
                },
                "group_concat"
            );
        }
        
    private:
        template<auto FirstField, auto... RestFields>
        std::pair<std::string, std::string> prepare_group_concat(std::string_view alias, std::string_view fieldSeparator) {
            // Validate all member pointers
            static_assert(std::is_member_pointer_v<decltype(FirstField)>, "FirstField must be a member pointer");
            (check_member_pointer<RestFields>(), ...);
            
            auto firstField = std::make_unique<FieldAlias<FirstField>>();
            
            // Generate alias
            std::string actual_alias = alias.empty() ? 
                fmt::format("group_concat_{}", firstField->getFieldName()) : 
                std::string(alias);
            
            // Build field expression
            std::string field_expr = build_field_expression<FirstField, RestFields...>(fieldSeparator);
            
            return {actual_alias, field_expr};
        }

        template<auto FirstField, auto... RestFields>
        std::string build_field_expression(std::string_view fieldSeparator) const {
            auto firstField = std::make_unique<FieldAlias<FirstField>>();
            std::string field_expr = firstField->getFullFieldName();
            
            if constexpr (sizeof...(RestFields) == 0) {
                return field_expr;
            }

            ([&field_expr, &fieldSeparator]<auto Field>() {
                auto field = std::make_unique<FieldAlias<Field>>();
                field_expr = fmt::format("{}||'{}'||{}", 
                                    field_expr, fieldSeparator,
                                    field->getFullFieldName());
            }.template operator()<RestFields>(), ...);
            return field_expr;
        }

        // Implementation without ORDER BY
        template<auto FirstField, auto... RestFields>
        QuerySet &group_concat_impl(std::string_view alias, 
                                std::string_view separator, 
                                std::string_view fieldSeparator,
                                bool distinct) {
            auto [actual_alias, field_expr] = prepare_group_concat<FirstField, RestFields...>(alias, fieldSeparator);
            
            std::string function_str = "GROUP_CONCAT(";
            
            if (distinct) {
                function_str += "DISTINCT ";
            }
            
            function_str += field_expr;
            
            // SQLite doesn't allow separator with DISTINCT
            if (!distinct) {
                function_str += fmt::format(", '{}'", separator);
            }
            
            function_str += fmt::format(") AS {}", actual_alias);
            
            functions(Function(function_str));
            return *this;
        }

        // Implementation with ORDER BY
        template<auto OrderField, auto FirstField, auto... RestFields>
        QuerySet &group_concat_with_order_impl(std::string_view alias, 
                                            std::string_view separator, 
                                            std::string_view fieldSeparator,
                                            bool distinct) {
            static_assert(std::is_member_pointer_v<decltype(OrderField)>, "OrderField must be a member pointer");
            
            auto [actual_alias, field_expr] = prepare_group_concat<FirstField, RestFields...>(alias, fieldSeparator);
            
            auto orderField = std::make_unique<FieldAlias<OrderField>>();
            
            // Build GROUP_CONCAT function with ORDER BY
            std::string function_str = "GROUP_CONCAT(";
            
            if (distinct) {
                function_str += "DISTINCT ";
            }
            
            function_str += field_expr;
            function_str += fmt::format(" ORDER BY {}", orderField->getFullFieldName());
            function_str += fmt::format(", '{}') AS {}", separator, actual_alias);
            
            functions(Function(function_str));
            return *this;
        }

        // Common helper for executing aggregate queries
        template<typename ReturnType, typename SetupFunction, typename ValueExtractor = std::nullptr_t>
        std::expected<ReturnType, std::string> execute_aggregate_query(
            SetupFunction setup_function,
            std::string_view error_prefix,
            ValueExtractor value_extractor = nullptr
        ) {
            // Create temporary QuerySet and clear existing state
            auto tempQuerySet = *this;
            tempQuerySet.onlyFields.clear();
            tempQuerySet.distinctFields.clear();
            tempQuerySet.functionsSet.clear();
            
            // Apply the specific aggregate function
            setup_function(tempQuerySet);
            
            // Build and execute query
            auto where_query_result = tempQuerySet.get_where_query();
            auto sql = fmt::format(R"(SELECT {} FROM "{}" {} {})", 
                tempQuerySet.buildFieldsClause(),
                tempQuerySet.template get_table_name<T>(),
                this->generateJoinSQL(),
                where_query_result.sql
            );

            Statement stmt(conn, sql);
            bind_query_parameters(stmt, where_query_result);
            auto execResult = stmt.execute_query();
            if (!execResult) {
                return std::unexpected(execResult.error());
            }
            
            const auto& row = execResult.value();
            
            // Verify column exists
            if (row.get_column_count() == 0) [[unlikely]] {
                return std::unexpected(fmt::format("{} column not found", error_prefix));
            }
            
            // For NULL results, return empty value of ReturnType instead of error
            if (row.get_column_type(0) == SQLITE_NULL) [[unlikely]] {
                if constexpr (std::is_same_v<ReturnType, std::string>) {
                    return std::string{};
                } else if constexpr (std::is_same_v<ReturnType, int> || 
                                   std::is_same_v<ReturnType, int64_t> ||
                                   std::is_integral_v<ReturnType>) {
                    return 0;
                } else if constexpr (std::is_same_v<ReturnType, double> ||
                                   std::is_floating_point_v<ReturnType>) {
                    return 0.0;
                } else {
                    // For other types, try to default construct
                    return ReturnType{};
                }
            }
            
            // If a custom value extractor is provided, use it
            if constexpr (!std::is_same_v<decltype(value_extractor), std::nullptr_t>) {
                return value_extractor(row);
            } else {
                // Otherwise use to_value to extract the value based on ReturnType
                try {
                    return to_value<ReturnType>(row, 0);
                } catch(...) {
                    return std::unexpected<std::string>(fmt::format("Unexpected column type for {} result", error_prefix));
                }
            }
        }

        template<typename... Args>
        QuerySet &functions(Args &&...args) {
            // Reserve capacity
            this->functionsSet.reserve(functionsSet.size() + sizeof...(Args));
            
            // Process each function using fold expression
            (functionsSet.emplace_back(std::forward<Args>(args)), ...);
            return *this;
        }
    };
}

#include "QuerySet.where.impl.h" // NOSONAR
