#pragma once

#include <string>
#include <utility>
#include <vector>
#include <variant>
#include "Function.h"
#include "Where.h"
#include <ranges>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <cstdint>
#include "Connection.h"
#include "Statement.h"
#include "Reflect.h"
#include "MemberPointerUtils.h"
#include "StringUtils.h"
#include <sstream>

namespace storm {
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

    // DistinctInfo has been removed and replaced with direct field aliases

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
    };

    template<auto MemberPtr>
    struct FieldAlias : public FieldAliasBase {
        // Constructor for string literals and std::string
        explicit FieldAlias(std::string alias = "") 
            : FieldAliasBase(std::move(alias)) {}
        
        ~FieldAlias() = default;
        
        std::string getFullFieldName() const override {
            using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
            std::string fieldName = getFieldNameFromMemberPtr<MemberPtr>();
            std::string tableName = Reflect<ClassType>::get_struct_name();
            return fmt::format("\"{}\".\"{}\"" , tableName, fieldName);
        }
        
        std::string getFieldName() const override {
            return getFieldNameFromMemberPtr<MemberPtr>();
        }
        
        std::string getTableName() const override {
            using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
            return Reflect<ClassType>::get_struct_name();
        }
        
        std::string getAlias() const override {
            return alias;
        }
        
        bool isStringField() const override {
            using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::type;
            return std::is_same_v<FieldType, std::string>;
        }
        
        bool isBoolField() const override {
            using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::type;
            return std::is_same_v<FieldType, bool>;
        }
        
        bool isNumericField() const override {
            using FieldType = typename member_pointer_traits<decltype(MemberPtr)>::type;
            return std::is_arithmetic_v<FieldType> && !std::is_same_v<FieldType, bool>;
        }
    };
    
    // String-based field alias for runtime field references
    class StringFieldAlias : public FieldAliasBase {
    private:
        std::string tableName;
        std::string fieldName;
        
    public:
        // Constructor for string-based field references
        explicit StringFieldAlias(std::string tableName, std::string fieldName, std::string alias = "") 
            : FieldAliasBase(std::move(alias)), tableName(std::move(tableName)), fieldName(std::move(fieldName)) {}
        
        ~StringFieldAlias() = default;
        
        std::string getFullFieldName() const override {
            return utils::formatFieldName(tableName, fieldName);
        }
        
        std::string getFieldName() const override {
            return fieldName;
        }
        
        std::string getTableName() const override {
            return tableName;
        }
        
        // For string-based fields, we assume it could be any type
        bool isStringField() const override {
            return true;  // Conservative assumption
        }
        
        bool isBoolField() const override {
            return false;  // Conservative assumption
        }
        
        bool isNumericField() const override {
            return false;  // Conservative assumption
        }
    };

    template<class T>
    class QuerySet : public BaseClass {
    private:
        std::shared_ptr<Connection> conn;
        std::optional<storm::Where> _whereExpression;
        JoinInfo joinInfo;
        std::vector<std::pair<std::function<std::string()>, bool>> orderFields;
        std::vector<std::unique_ptr<FieldAliasBase>> distinctFields;
        std::string _jsonFields;
        std::vector<std::unique_ptr<FieldAliasBase>> onlyFields;
        std::vector<Function> functionsSet;
        std::vector<std::string> groupByFieldNames; // For compile-time field names
        int _limit{};
        int _offset{};
        std::string _alias;
        bool _one{};
        bool _doAndCheck{};
        bool _returnInMain{};

        template<class U>
        static constexpr auto get_reflected_type() {
            return Reflect<U>::get_reflected_type();
        }

        // Function to get the reflected members of the type
        template<class U>
        static constexpr auto get_reflected_members() {
            return Reflect<U>::get_reflected_type().members;
        }

        template<class U>
        static constexpr std::string get_table_name() {
            return Reflect<U>::get_struct_name();
        }
    
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
                    int param_index = sqlite3_bind_parameter_index(stmt.get_stmt(), (":" + param_name).c_str()); // TODO : move to Statement
                    if (param_index > 0) { // SQLite parameter indices are 1-based
                        bind_sql_value(stmt, param_index, value);
                    }
                }
            }
        }
        
        void bind_object_values(Statement& stmt, const T& obj, int& param_index) const {
            Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                    std::string field_name = Reflect<T>::get_member_name(member);
                    if (field_name == "id") return; // Skip auto-generated id
                    
                    auto value = member(obj);
                    bind_sql_value(stmt, param_index, value);
                    param_index++;
                }
            });
        }

        // Helper function to bind a specific field value by name
        void bind_field_value(Statement& stmt, const T& obj, const std::string& field_name, int& param_index) const {
            bool found = false;
            Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                if (found) return; // Skip if we already found the field
                
                if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                    std::string member_name = Reflect<T>::get_member_name(member);
                    if (member_name == field_name) {
                        found = true;
                        
                        auto value = member(obj);
                        bind_sql_value(stmt, param_index, value);
                        param_index++;
                    }
                }
            });
        }

        // Field name mapping using your reflection system
        template<typename ClassType, typename FieldType>
        std::string get_field_name(FieldType ClassType::* memberPtr) const {
            std::string tableName = Reflect<ClassType>::get_struct_name();
            std::string fieldName = getFieldNameFromMemberPtr(memberPtr);
            return storm::utils::formatFieldName(tableName, fieldName);
        }

        std::string addExtraQuotes(const std::string& query) const {
            // Your existing implementation
            return query;
        }
    
    public:
        // =============================
        // =============================
        // Constructor
        // =============================
        explicit QuerySet(std::shared_ptr<Connection> conn, const std::string& alias = "") 
            : conn(std::move(conn)) {
        }

        // =============================
        // =============================
        // INSERT
        // =============================
        std::vector<int> insert(const std::vector<T>& objs) {
            if (objs.empty()) return {};
            
            try {
                auto field_names = get_insert_field_names();
                if (field_names.empty()) return {};
                
                // Convert to const pointer vector
                std::vector<const T*> obj_ptrs;
                obj_ptrs.reserve(objs.size());
                for (const auto& obj : objs) {
                    obj_ptrs.push_back(&obj);
                }
                
                return execute_insert(obj_ptrs, field_names);
                
            } catch (const std::exception& e) {
                std::cerr << "Exception in insert: " << e.what() << "\n";
                return {};
            }
        }
        
        // Single insert method
        int insert(const T& obj) {
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
        
    private:
        std::vector<int> execute_insert(const std::vector<const T*>& obj_ptrs, const std::vector<std::string>& field_names) {
            std::string sql = build_insert_sql(field_names, obj_ptrs.size(), true); // Use RETURNING id
            if (sql.empty()) return {};
            
            auto stmt = Statement(conn, sql);
            
            // Bind all values for all objects
            int param_index = 1;
            for (const T* obj_ptr : obj_ptrs) {
                bind_object_values(stmt, *obj_ptr, param_index);
            }
            
            std::vector<int> generated_ids;
            
            // Execute and collect all returned IDs
            auto rows = stmt.execute_all();
            
            // Extract IDs from the returned rows
            for (const auto& row : rows) {
                generated_ids.push_back(row.get_int(0)); // ID is in the first column
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
            std::vector<std::string> field_names;
            
            Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                    std::string field_name = Reflect<T>::get_member_name(member);
                    if (field_name != "id") { // Skip auto-generated id
                        field_names.push_back(field_name);
                    }
                }
            });
            
            return field_names;
        }
        
    public:
        // =============================
        // =============================
        // UPDATE
        // =============================
        bool update(const T& obj) {
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
        bool update(const std::vector<T>& objs) {
            if (objs.empty()) return true;
            
            try {
                auto field_names = get_update_field_names();
                if (field_names.empty()) return false;
                
                // Convert to pointer vector
                std::vector<const T*> obj_ptrs;
                obj_ptrs.reserve(objs.size());
                for (const auto& obj : objs) {
                    obj_ptrs.push_back(&obj);
                }
                
                return execute_update(obj_ptrs, field_names);
                
            } catch (const std::exception& e) {
                std::cerr << "Exception in update: " << e.what() << "\n";
                return false;
            }
        }

    private:
        std::string build_update_sql(const std::vector<std::string>& field_names, size_t num_objects) {
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
                set_clauses.push_back(case_expr);
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

        bool execute_update(const std::vector<const T*>& obj_ptrs, const std::vector<std::string>& field_names) {
            if (obj_ptrs.empty()) return true;
            
            std::string sql = build_update_sql(field_names, obj_ptrs.size());
            if (sql.empty()) return false;
            
            auto stmt = Statement(conn, sql);
            
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
        bool remove(const T& obj) {
            try {
                return execute_delete({&obj});
            } catch (const std::exception& e) {
                std::cerr << "Exception in delete: " << e.what() << "\n";
                return false;
            }
        }
        
        bool remove(const std::vector<T>& objs) {
            if (objs.empty()) return true;
            
            try {
                // Convert to pointer vector
                std::vector<const T*> obj_ptrs;
                obj_ptrs.reserve(objs.size());
                for (const auto& obj : objs) {
                    obj_ptrs.push_back(&obj);
                }
                
                return execute_delete(obj_ptrs);
                
            } catch (const std::exception& e) {
                std::cerr << "Exception in delete: " << e.what() << "\n";
                return false;
            }
        }

    private:
        std::string build_delete_sql() {
            return fmt::format("DELETE FROM {} WHERE id = ?;", this->template get_table_name<T>());
        }
        
        std::string build_batch_delete_sql(size_t count) {
            std::vector<std::string> placeholders(count, "?");
            return fmt::format(
                "DELETE FROM {} WHERE id IN ({});",
                this->template get_table_name<T>(),
                fmt::join(placeholders, ", ")
            );
        }

        bool execute_delete(const std::vector<const T*>& obj_ptrs) {
            if (obj_ptrs.empty()) return true;
            
            if (obj_ptrs.size() == 1) {
                // Single delete
                std::string sql = build_delete_sql();
                auto stmt = Statement(conn, sql);
                stmt.bind(1, obj_ptrs[0]->id);
                return stmt.execute();
            } else {
                // Batch delete using IN clause
                std::string sql = build_batch_delete_sql(obj_ptrs.size());
                auto stmt = Statement(conn, sql);
                
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
        // Helper to process a single member pointer and push its fully qualified name.
        // template<auto MemberPtr>
        // void add_group_by_field() {
        //     using ClassType = typename detail::member_pointer_class<decltype(MemberPtr)>::type;
        //     std::string fieldName = detail::getFieldNameFromMemberPtr<ClassType, MemberPtr>();
        //     std::string tableName = this->template get_table_name<ClassType>();
        //     std::string fullFieldName = tableName.empty() ? fmt::format("\"{}\"", fieldName)
        //                                                   : fmt::format("\"{}\".\"{}\"", tableName, fieldName);
        //     this->groupByFieldNames.push_back(fullFieldName);
        // }

        // Compile-time version for multiple member pointers
        template<auto MemberPtr, auto... RestMemberPtrs>
        QuerySet &group_by() {
            // Add the first (or only) member pointer field
            // add_group_by_field<MemberPtr>();
            
            // Recursively add the rest of the fields
            if constexpr (sizeof...(RestMemberPtrs) > 0) {
                group_by<RestMemberPtrs...>();
            }
            
            return *this;
        }

    private:
        [[nodiscard]] std::string generateGroupBySQL() const {
            if(groupByFieldNames.empty()) {
                return "";
            }
        
            return fmt::format("GROUP BY {}", fmt::join(groupByFieldNames, ", "));
        }

        template<typename U>
        void group_by_impl(U &&u) {
            // Extract field name and table name
            std::string fieldName = u->getFieldName();
            std::string tableName = u->getTableName();
            
            // Format as table.field
            std::string fullFieldName;
            if (!tableName.empty()) {
                fullFieldName = format("\"{}\".\"{}\"", tableName, fieldName);
            } else {
                fullFieldName = format("\"{}\"", fieldName);
            }
            
            groupByFieldNames.push_back(fullFieldName);
        }

        template<typename U, typename... Args>
        void group_by_impl(U &&u, Args &&...args) {
            group_by_impl(std::forward<U>(u));
            group_by_impl(std::forward<Args>(args)...);
        }
    
    public:
        // =============================
        // =============================
        // ORDER BY
        // =============================
        // Compile-time version for multiple member pointers
        template<auto Field, bool Ascending = true, auto... Rest>
        QuerySet& order_by() {
            static_assert(std::is_member_pointer_v<decltype(Field)>, 
                        "Field must be a member pointer");
            static_assert(sizeof...(Rest) % 2 == 0, 
                        "Must provide field-direction pairs (field, bool, field, bool, ...)");
            
            // Add current field to order criteria
            this->orderFields.emplace_back(createFieldNameGenerator<Field>(), Ascending);
            
            // Recursively process remaining field-direction pairs
            if constexpr (sizeof...(Rest) > 0) {
                return order_by<Rest...>();
            }
            
            return *this;
        }

        // Helper function to create field name generator (compile-time)
        template<auto Field>
        auto createFieldNameGenerator() const {
            return [this]() -> std::string {
                static_assert(std::is_member_pointer_v<decltype(Field)>, 
                            "Field must be a member pointer");
                using ClassType = typename member_pointer_class<decltype(Field)>::type;
                std::string fieldName = getFieldNameFromMemberPtr<Field>();
                std::string tableName = this->template get_table_name<ClassType>();
                return utils::formatFieldName(tableName, fieldName);
            };
        }
    private:
        [[nodiscard]] std::string buildOrderFields() const {
            if(orderFields.empty()) {
                return "";
            }
            
            std::vector<std::string> fieldStrings;
            fieldStrings.reserve(orderFields.size());
            
            for(const auto &pair : orderFields) {
                fieldStrings.push_back(fmt::format("{} {}", 
                                                    pair.first(), // Call the function to get field name
                                                    pair.second ? "ASC" : "DESC"));
            }
            
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

        // Method 2: Convenience method using Storm field helper
        template<typename ClassType, typename FieldType, typename Value>
        QuerySet& where(FieldType ClassType::* memberPtr, Value&& value, storm::Op op = storm::Op::EQ);

        // Special methods for complex operators
        template<typename ClassType, typename FieldType, typename Container>
        QuerySet& where_in(FieldType ClassType::* memberPtr, const Container& values);

        // Where between
        template<typename ClassType, typename FieldType, typename T1, typename T2>
        QuerySet& where_between(FieldType ClassType::* memberPtr, T1&& value1, T2&& value2);

        template<typename ClassType, typename FieldType>
        QuerySet& where_null(FieldType ClassType::* memberPtr);

        template<typename ClassType, typename FieldType>
        QuerySet& where_not_null(FieldType ClassType::* memberPtr);

        template<typename ClassType, typename FieldType, typename Value>
        QuerySet& where_like(FieldType ClassType::* memberPtr, Value&& pattern);

        // Method 3: NTTP version (compile-time field specification)
        template<auto MemberPtr, typename Value>
        QuerySet& where(Value&& value, storm::Op op = storm::Op::EQ);

        // NTTP versions for complex operators
        template<auto MemberPtr, typename Container>
        QuerySet& where_in(const Container& values);

        template<auto MemberPtr, typename T1, typename T2>
        QuerySet& where_between(T1&& value1, T2&& value2);

        template<auto MemberPtr>
        QuerySet& where_null();

        template<auto MemberPtr, typename Value>
        QuerySet& where_like(Value&& pattern);

        template<auto MemberPtr>
        QuerySet& where_not_null();

        // Logical combination methods for fluent interface
        QuerySet& where_and(const storm::Where& condition);
        QuerySet& where_or(const storm::Where& condition);

        // Support for raw SQL WHERE clauses
        QuerySet& where_raw(const std::string& raw_condition, const std::vector<std::any>& parameters = {});
        //     // In SQLite, you might use UPPER() functions or COLLATE NOCASE
        //     auto field_name = get_field_name<ClassType>(memberPtr);
        //     auto custom_condition = /* create custom condition with UPPER() */;
        //     return where(custom_condition);
        // }

        // Date range queries (if you have date fields)
        template<typename ClassType, typename FieldType>
        QuerySet& where_date_range(FieldType ClassType::* memberPtr, 
                                const std::string& start_date, 
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
                joinInfo.joins.emplace_back(targetTable,
                                        std::move(alias),
                                        std::move(condition),
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
            std::string condition;
            
            // Get lowercase table names for foreign key naming convention
            std::string sourceName = utils::to_lower(get_table_name<SourceType>());
            std::string targetName = utils::to_lower(get_table_name<TargetType>());
            
            // Strategy 1: Look for foreign key in source pointing to target
            // e.g., author_id in Post pointing to Author.id
            std::string foreignKeyName = targetName + "_id";
            bool foundForeignKeyInSource = false;
            
            // Check each field in the source type
            Reflect<SourceType>::for_each_member(get_reflected_members<SourceType>(), [&](auto member) {
                if constexpr (refl::trait::is_field_v<decltype(member)>) {
                    std::string fieldName = std::string(member.name.str());
                    if (utils::to_lower(fieldName) == foreignKeyName) {
                        condition = fmt::format(R"("{}"."id" = "{}"."{}")",
                                              get_table_name<TargetType>(), get_table_name<SourceType>(), fieldName);
                        foundForeignKeyInSource = true;
                    }
                }
            });
            
            return foundForeignKeyInSource ? condition : "";
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
                this->onlyFields.push_back(std::make_unique<FieldAlias<Field>>());
            };
            
            (addField.template operator()<Fields>(), ...);
            
            return *this;
        }
        
        // Version with alias support
        template<auto Field>
        QuerySet& only(const std::string& alias) {
            static_assert(std::is_member_pointer_v<decltype(Field)>, 
                        "Field must be a member pointer");
            this->onlyFields.push_back(std::make_unique<FieldAlias<Field>>(alias));
            return *this;
        }

    private:
        [[nodiscard]] std::string buildOnlyFields() const {
            // Start with onlyFields
            std::vector<std::string> fieldStrings;
            
            // If onlyFields is empty, use reflection to get all fields from the primary model
            if (onlyFields.empty() && distinctFields.empty()) {
                // When no specific fields requested, explicitly list all fields using reflection
                Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                    if constexpr (refl::descriptor::is_field(member)) {
                        std::string tableName = this->template get_table_name<T>();
                        std::string fieldName = std::string(member.name);
                        std::string formattedField = utils::formatFieldName(tableName, fieldName);
                        fieldStrings.push_back(fmt::format(R"({} AS "{}")", formattedField, fieldName));
                    }
                });
            } else {
                // Use only the specified fields
                fieldStrings.reserve(onlyFields.size());
                std::transform(onlyFields.begin(), onlyFields.end(), std::back_inserter(fieldStrings),
                    [](const auto& fieldAlias) -> std::string {
                        return fieldAlias->alias.empty() ?
                            fieldAlias->getFullFieldName() :
                            fmt::format("{} AS {}", fieldAlias->getFullFieldName(), fieldAlias->alias);
                    });
            }
            
            return fmt::format("{}", fmt::join(fieldStrings, ", "));
        }

    public:
        // =============================
        // =============================
        // SELECT
        // =============================
        /**
         * @brief Select only specific fields and return them as tuples
         * 
         * This method is more efficient than select_all() when you only need specific fields,
         * as it doesn't construct complete model objects.
         * 
         * @tparam Fields Types of the fields to select (should match the types in the model)
         * @return std::vector<std::tuple<Fields...>> Vector of tuples containing only selected fields
         * 
         * @example
         *   auto results = QuerySet<Author>(conn)
         *       .only(field(&Author::name))
         *       .only(field(&Author::age))
         *       .select_tuple<std::string, int>();
         *   // returns: std::vector<std::tuple<std::string, int>>
         */
        template<typename... Fields>
        std::vector<std::tuple<Fields...>> select_tuple() const {
            static_assert(sizeof...(Fields) > 0, "You must specify at least one field type");
            
            // Make sure we have the right number of fields selected
            if (this->onlyFields.size() != sizeof...(Fields)) {
                throw std::runtime_error(fmt::format("Number of field types ({}) doesn't match number of selected fields ({})", 
                                                   sizeof...(Fields), this->onlyFields.size()));
            }
            
            // Get the where query result
            auto where_query_result = this->get_where_query();
            
            auto sql = fmt::format("SELECT {} {} {} FROM \"{}\" {} {} {} {} {}", 
                    this->createDistinctClause(),
                    this->buildOnlyFields(), // Use the fields specified with only()
                    this->buildFunctions(),
                    this->template get_table_name<T>(),
                    this->generateJoinSQL(),
                    where_query_result.sql,
                    this->generateGroupBySQL(),
                    this->buildOrderFields(),
                    this->limit_impl());
            std::cout << sql << std::endl;
            
            auto stmt = Statement(conn, sql);
            bind_query_parameters(stmt, where_query_result);
            auto all_rows = stmt.execute_all();
            
            std::vector<std::tuple<Fields...>> results;
            if (all_rows.empty()) {
                return results;
            }
            
            results.reserve(all_rows.size());
            
            for (const auto& row : all_rows) {
                results.push_back(create_tuple<Fields...>(row));
            }
            
            return results;
        }
        
        // Helper function to create tuples from rows
        template<typename... Fields>
        std::tuple<Fields...> create_tuple(const Row& row) const {
            return create_tuple_impl<Fields...>(row, std::index_sequence_for<Fields...>{});
        }
        
        template<typename... Fields, size_t... I>
        std::tuple<Fields...> create_tuple_impl(const Row& row, std::index_sequence<I...>) const {
            return std::make_tuple(get_value<Fields>(row, I)...);
        }
        
        template<typename ValueType>
        ValueType get_value(const Row& row, size_t idx) const {
            if constexpr (std::is_same_v<ValueType, std::string>) {
                return row.get_text(idx);
            } else if constexpr (std::is_same_v<ValueType, bool>) {
                return row.get_int(idx) != 0;
            } else if constexpr (std::is_integral_v<ValueType>) {
                return static_cast<ValueType>(row.get_int(idx));
            } else if constexpr (std::is_floating_point_v<ValueType>) {
                return static_cast<ValueType>(row.get_double(idx));
            } else {
                static_assert(std::is_pointer<ValueType>::value == false, "Unsupported pointer type for SQLite binding");
                static_assert(std::is_reference<ValueType>::value == false, "Unsupported reference type for SQLite binding");
                static_assert(sizeof(ValueType) == 0, "Unsupported type for SQLite binding");
                return ValueType{}; // This will never be reached due to the static_assert
            }
        }
        
        /**
         * @brief Value variant type used in the select_values result
         * 
         * This variant can store any of the common DB value types
         */
        using ValueVariant = std::variant<
            std::monostate,  // For null values
            int,             // For integer types
            double,          // For floating point types
            bool,            // For boolean values
            std::string      // For text values
        >;
        
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
         *   // Access data with: results[0]["name"], results[0]["age"]
         */
        /**
         * @brief Projection structure template for strongly-typed partial model access
         * 
         * @tparam Fields The field types included in this projection
         */
        template <typename... Fields>
        struct Projection {
            std::tuple<Fields...> values;
            
            // Access helpers by index
            template <size_t I>
            auto& get() { return std::get<I>(values); }
            
            template <size_t I>
            const auto& get() const { return std::get<I>(values); }
        };
        
        /**
         * @brief Select only specific fields and return them as strongly-typed projection objects
         * 
         * This provides a balance between efficiency and type safety. The returned objects
         * contain only the specified fields but with proper typing.
         * 
         * @tparam Fields Types of the fields to include in the projection
         * @return std::vector<Projection<Fields...>> Vector of projection objects
         * 
         * @example
         *   auto results = QuerySet<Author>(conn)
         *       .only(field(&Author::name))
         *       .only(field(&Author::age))
         *       .select_projection<std::string, int>();
         *   // Access with: results[0].get<0>() for name, results[0].get<1>() for age
         */
        template <typename... Fields>
        std::vector<Projection<Fields...>> select_projection() const {
            static_assert(sizeof...(Fields) > 0, "You must specify at least one field type");
            
            // Make sure we have the right number of fields selected
            if (this->onlyFields.size() != sizeof...(Fields)) {
                throw std::runtime_error(fmt::format("Number of field types ({}) doesn't match number of selected fields ({})", 
                                                   sizeof...(Fields), this->onlyFields.size()));
            }
            
            // Get tuples first using select_tuple
            auto tuples = select_tuple<Fields...>();
            
            // Convert tuples to projections
            std::vector<Projection<Fields...>> results;
            results.reserve(tuples.size());
            
            for (auto& tuple : tuples) {
                Projection<Fields...> proj{std::move(tuple)};
                results.push_back(std::move(proj));
            }
            
            return results;
        }
        
        std::vector<std::map<std::string, ValueVariant>> select_values() const {
            if (this->onlyFields.empty()) {
                throw std::runtime_error("You must specify at least one field with only() before calling select_values()");
            }
            
            // Get the where query result
            auto where_query_result = this->get_where_query();
            auto sql = fmt::format("SELECT {} {} {} FROM \"{}\" {} {} {} {} {}", 
                    this->createDistinctClause(),
                    this->buildOnlyFields(),
                    this->buildFunctions(),
                    this->template get_table_name<T>(),
                    this->generateJoinSQL(),
                    where_query_result.sql,
                    this->generateGroupBySQL(),
                    this->buildOrderFields(),
                    this->limit_impl());
            std::cout << sql << std::endl;
            
            auto stmt = Statement(conn, sql);
            bind_query_parameters(stmt, where_query_result);
            auto all_rows = stmt.execute_all();
            
            std::vector<std::map<std::string, ValueVariant>> results;
            if (all_rows.empty()) {
                return results;
            }
            
            results.reserve(all_rows.size());
            
            // Build a list of field names from the onlyFields list
            std::vector<std::string> fieldNames;
            fieldNames.reserve(this->onlyFields.size());
            
            for (const auto& fieldAlias : this->onlyFields) {
                // Use just the field name without table qualifier when no alias is provided
                fieldNames.push_back(fieldAlias->alias.empty() ? 
                                    fieldAlias->getFieldName() : 
                                    fieldAlias->alias);
            }
            
            for (const auto& row : all_rows) {
                std::map<std::string, ValueVariant> rowDict;
                
                for (size_t i = 0; i < fieldNames.size(); ++i) {
                    // Get the field type information from the field alias
                    const auto& fieldAlias = this->onlyFields[i];
                    
                    // Use the field's type information to determine how to retrieve the value
                    if (fieldAlias->isStringField()) {
                        // String fields should be retrieved as text
                        rowDict[fieldNames[i]] = row.get_text(i);
                    } else if (fieldAlias->isBoolField()) {
                        // Boolean fields
                        rowDict[fieldNames[i]] = static_cast<bool>(row.get_int(i));
                    } else {
                        // For numeric fields, try integer first, then double
                        try {
                            rowDict[fieldNames[i]] = row.get_int(i);
                        } catch (...) {
                            try {
                                rowDict[fieldNames[i]] = row.get_double(i);
                            } catch (...) {
                                // Default to text representation as fallback
                                rowDict[fieldNames[i]] = row.get_text(i);
                            }
                        }
                    }
                }
                
                results.push_back(std::move(rowDict));
            }
            
            return results;
        }

        std::vector<T> select_all() const {
            // Get the where query result
            auto where_query_result = this->get_where_query();
            
            // Build the SQL query based on whether we have distinct fields
            std::string sql;
            
            sql = fmt::format("SELECT {} {} {} FROM \"{}\" {} {} {} {} {}", 
                this->createDistinctClause(),
                this->buildOnlyFields(),
                this->buildFunctions(),
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

            std::vector<T> results;
            if (all_rows.empty()) {
                return results;
            }
            
            results.reserve(all_rows.size());
            
            for (const auto& row : all_rows) {
                T obj{};
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
                
                results.push_back(std::move(obj));
            }
            
            return results;
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
            if(!_limit) {
                return "";
            }

            return _offset ? fmt::format(" LIMIT {} OFFSET {}", _limit, _offset)
                        : fmt::format(" LIMIT {}", _limit);
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
                this->distinctFields.push_back(std::make_unique<FieldAlias<Field>>());
            };
            
            (addField.template operator()<Fields>(), ...);
            
            return *this;
        }
        
    private:
        [[nodiscard]] std::string createDistinctClause() const {
            if(distinctFields.empty()) {
                return "";
            }
            
            std::vector<std::string> fieldStrings;
            fieldStrings.reserve(distinctFields.size());
            std::transform(distinctFields.begin(), distinctFields.end(), std::back_inserter(fieldStrings),
                [](const auto& fieldAlias) -> std::string {
                    return fieldAlias->getFullFieldName();
                });
            return fmt::format("DISTINCT {}", fmt::join(fieldStrings, ", "));
        }

    public:
        // =============================
        // =============================
        // FUNCTIONS
        // =============================
        // QuerySet &count(const BaseField *field, std::string_view alias) {
        //     functions(Function(fmt::format("count({})::integer as {}", field->getFullFieldName(), alias)));
        //     return *this;
        // }

        template<typename... Args>
        QuerySet &functions(Args &&...args) {
            this->functions_impl(std::forward<Args>(args)...);
            return *this;
        }
    
    private:
        template<typename U>
        void functions_impl(U &&u) {
            functionsSet.emplace_back(std::forward<U>(u));
        }

        template<typename U, typename... Args>
        void functions_impl(U &&u, Args &&...args) {
            functionsSet.emplace_back(std::forward<U>(u));
            functions_impl(std::forward<Args>(args)...);
        }

        [[nodiscard]] std::string buildFunctions() const {
            if(functionsSet.empty()) {
                return "";
            }

            return format("{}{}",
                            onlyFields.empty() && _jsonFields.empty() ? "" : ", ",
                            fmt::join(functionsSet | std::views::transform([](const auto &function) {
                                        return function.toStr();
                                    }),
                                    ", "));
        }
    };
}

#include "QuerySet.where.impl.h"
