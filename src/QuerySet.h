#pragma once

#include <string>
#include <utility>
#include <vector>
#include <variant>
#include "BaseField.h"
#include "Function.h"
#include "WhereClause.h"
#include <ranges>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <cstdint>
#include "Connection.h"
#include "Statement.h"
#include "Reflect.h"
#include "MemberPointerUtils.h"
#include <sstream>

namespace storm {
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

    using FieldOrFunction = std::variant<const BaseField *, Function, std::string>;

    namespace detail {
        template<typename T>
        std::string getFullFieldName(const T &obj) {
            return obj.getFullFieldName();
        }

        template<>
        inline std::string getFullFieldName(const std::string &obj) {
            return obj;
        }

        template<>
        inline std::string getFullFieldName(const Function &obj) {
            return obj.getFullFieldName();
        }

        template<>
        inline std::string getFullFieldName(const BaseField *const &obj) {
            return obj->getFullFieldName();
        }

        template<>
        inline std::string getFullFieldName(const std::variant<const BaseField *, Function, std::string> &obj) {
            return std::visit(
                [](const auto &value) {
                    return getFullFieldName(value);
                },
                obj);
        }
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

    struct DistinctInfo {
        std::vector<const BaseField *> distinctFields;
        std::string distinctOn;
    };

    class BaseQuerySetVirtual : public BaseClass {
    public:
        virtual std::string buildSelect() const = 0;
        virtual std::string getAlias() const = 0;
    };

    struct FieldAlias final : BaseClass {
        const BaseField *field;
        std::string alias;

        explicit FieldAlias(const BaseField *f, const std::string_view a = "") : BaseClass(), field(f), alias(a) {}
    };

    template<class T>
    class BaseQuerySet : public BaseQuerySetVirtual {
    public:
        explicit BaseQuerySet(int limit, std::string alias, bool returnInMain = true) :
            BaseQuerySetVirtual(), _limit(limit), _alias(std::move(alias)), _returnInMain(returnInMain) {}

        explicit BaseQuerySet(const std::string_view &alias, bool doAndCheck = false, bool returnInMain = true) :
            BaseQuerySetVirtual(), _alias(alias), _one(true), _doAndCheck(doAndCheck), _returnInMain(returnInMain) {}

        std::vector<WhereClause> filters;
        JoinInfo joinInfo;
        std::vector<std::pair<std::function<std::string()>, bool>> orderFields;
        DistinctInfo distinctInfo;
        std::string _jsonFields;
        std::vector<FieldAlias> onlyFields;
        std::vector<Function> functionsSet;
        std::vector<std::unique_ptr<BaseQuerySetVirtual>> otherQueries;
        std::vector<std::string> groupByFieldNames; // For compile-time field names
        int _limit{};
        int _offset{};
        std::string _alias;
        bool _one{};
        bool _doAndCheck{};
        bool _returnInMain{};
        // TODO : fix private
    
        // Helper to get reflected type information for T
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
    
        [[nodiscard]] std::string getAlias() const override {
            return _alias;
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

        [[nodiscard]] std::string aliasQuery() const {
            return fmt::format(" {} AS ( {} ),", _alias, buildSelect());
        }

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

        [[nodiscard]] std::string createDistinctClause() const {
            if(distinctInfo.distinctFields.empty()) {
                return "";
            }
            return format("DISTINCT ON ({}) ",
                          fmt::join(distinctInfo.distinctFields | std::views::transform([](const auto &field) {
                                        return field->getFullFieldName();
                                    }),
                                    ", "));
        }

        [[nodiscard]] std::string buildOnlyFields() const {
            return format(
                "{}",
                fmt::join(onlyFields | std::views::transform([](const auto &fieldAlias) {
                              if(fieldAlias.alias.empty()) {
                                  return fieldAlias.field->getFullFieldName();
                              }
                              return fmt::format("{} AS {}", fieldAlias.field->getFullFieldName(), fieldAlias.alias);
                          }),
                          ", "));
        }

        template<typename U>
        void only_impl(const U &u) {
            onlyFields.emplace_back(u);
        }

        template<typename U, typename... Args>
        void only_impl(const U &u, Args... args) {
            onlyFields.emplace_back(u);
            only_impl(args...);
        }

        template<typename U>
        void distinct_impl(const U &u) {
            distinctInfo.distinctFields.emplace_back(u);
        }

        template<typename U, typename... Args>
        void distinct_impl(const U &u, const Args &...args) {
            distinctInfo.distinctFields.emplace_back(u);
            distinct_impl(args...);
        }

        template<typename U>
        void functions_impl(U &&u) {
            functionsSet.emplace_back(std::forward<U>(u));
        }

        template<typename U, typename... Args>
        void functions_impl(U &&u, Args &&...args) {
            functionsSet.emplace_back(std::forward<U>(u));
            functions_impl(std::forward<Args>(args)...);
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

        // template<class T, class U>
        // void join_impl(std::string &&cte,
        //                std::string &&alias,
        //                std::string &&addConditions,
        //                const JoinInfo::JoinType joinType) {
        //     join_impl_core<T, U>(std::move(cte), std::move(alias), std::move(addConditions), joinType);
        // }

        // void join_impl(BaseQuerySet &&querySet,
        //                std::string &&alias,
        //                std::string &&addConditions,
        //                const JoinInfo::JoinType joinType) {
        //     std::string aliasCTE = querySet.getAlias();
        //     this->addCTE(std::move(querySet));
        //     join_impl_core<T, U>(std::move(aliasCTE), std::move(alias), std::move(addConditions), joinType);
        // }

        // void addCTE(BaseQuerySet &&cte) {
        //     this->otherQueries.push_back(std::make_unique<BaseQuerySet>(std::move(cte)));
        // }

        [[nodiscard]] std::string buildSelectOne() const {
        //     return fmt::format("{}{} FROM \"{}\" {}{} LIMIT 1{}",
        //                        _doAndCheck ? "SELECT do_and_check('SELECT " : "SELECT ",
        //                        _jsonFields.empty()
        //                            ? fmt::format("{}{}", buildOnlyFields(), buildFunctions())
        //                            : fmt::format("json_build_object({} {})", _jsonFields, buildFunctions()),
        //                        get_table_name(),
        //                        generateJoinSQL(),
        //                        filter_impl(),
        //                        _doAndCheck ? "')" : "");
            return "";
        }

        [[nodiscard]] std::string buildSelect() const override {
        //     if(_one)
        //         return buildSelectOne();

        //     return fmt::format("{} SELECT {} {} {} FROM \"{}\" {} {} {} {} {}",
        //                        buildOtherQueries(),
        //                        createDistinctClause(),
        //                        buildOnlyFields(),
        //                        buildFunctions(),
        //                        get_table_name(),
        //                        generateJoinSQL(),
        //                        filter_impl(),
        //                        generateGroupBySQL(),
        //                        buildOrderFields(),
        //                        limit_impl());
            return "";
        }

        [[nodiscard]] std::string generateGroupBySQL() const {
            if(groupByFieldNames.empty()) {
                return "";
            }
     
            return fmt::format("GROUP BY {}", fmt::join(groupByFieldNames, ", "));
        }

        [[nodiscard]] std::string buildOtherQueries() const {
            if(this->otherQueries.empty()) {
                return "";
            }

            return format("WITH {}",
                          fmt::join(otherQueries | std::views::transform([](const auto &query) {
                                        return fmt::format("{} AS ({})", query->getAlias(), query->buildSelect());
                                    }),
                                    ", "));
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

        [[nodiscard]] std::string limit_impl() const {
            if(!_limit) {
                return "";
            }

            return _offset ? fmt::format(" LIMIT {} OFFSET {}", _limit, _offset)
                           : fmt::format(" LIMIT {}", _limit);
        }

        [[nodiscard]] std::string filter_impl() const {
            if(filters.empty()) {
                return "";
            }

            auto transformed = filters | std::views::transform([](const WhereClause &filter) {
                                   return filter.serialize();
                               });

            std::vector serialized(transformed.begin(), transformed.end());
            std::string query = fmt::format(" WHERE {} ", fmt::join(serialized, " AND "));
            return _doAndCheck ? addExtraQuotes(query) : query;
        }
    };

    template<class T>
    class QuerySet : public BaseQuerySet<T> {
    private:
        std::shared_ptr<Connection> conn;

        QuerySet &addFilter(WhereClause &&whereClause) {
            this->filters.push_back(std::move(whereClause));
            return *this;
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

        std::vector<std::string> get_update_field_names() const {
            return get_insert_field_names();
        }
        
        // Helper function to bind object values to statement
        void bind_object_values(Statement& stmt, const T& obj, int& param_index) const {
            Reflect<T>::for_each_member(this->template get_reflected_members<T>(), [&](auto member) {
                if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                    std::string field_name = Reflect<T>::get_member_name(member);
                    if (field_name == "id") return; // Skip auto-generated id
                    
                    auto value = member(obj);
                    using ValueType = std::decay_t<decltype(value)>;
                    
                    if constexpr (std::is_same_v<ValueType, std::string>) {
                        stmt.bind(param_index, value);
                    } else if constexpr (std::is_same_v<ValueType, bool>) {
                        stmt.bind(param_index, value ? 1 : 0);
                    } else if constexpr (std::is_integral_v<ValueType> && !std::is_same_v<ValueType, bool>) {
                        stmt.bind(param_index, static_cast<long long>(value));
                    } else if constexpr (std::is_floating_point_v<ValueType>) {
                        stmt.bind(param_index, static_cast<double>(value));
                    } else if constexpr (std::is_null_pointer_v<ValueType>) {
                        stmt.bind_null(param_index);
                    } else {
                        static_assert(sizeof(ValueType) == 0, "Unsupported type for SQLite binding");
                    }
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
                        using ValueType = std::decay_t<decltype(value)>;
                        
                        if constexpr (std::is_same_v<ValueType, std::string>) {
                            stmt.bind(param_index, value);
                        } else if constexpr (std::is_same_v<ValueType, bool>) {
                            stmt.bind(param_index, value ? 1 : 0);
                        } else if constexpr (std::is_integral_v<ValueType> && !std::is_same_v<ValueType, bool>) {
                            stmt.bind(param_index, static_cast<long long>(value));
                        } else if constexpr (std::is_floating_point_v<ValueType>) {
                            stmt.bind(param_index, static_cast<double>(value));
                        } else if constexpr (std::is_null_pointer_v<ValueType>) {
                            stmt.bind_null(param_index);
                        } else {
                            static_assert(sizeof(ValueType) == 0, "Unsupported type for SQLite binding");
                        }
                        param_index++;
                    }
                }
            });
        }
        
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
        // Constructor
        explicit QuerySet(std::shared_ptr<Connection> conn, const std::string& alias = "") 
            : BaseQuerySet<T>(alias, false, true), conn(std::move(conn)) {
        }

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

        std::vector<T> select_all() {
            auto sql = fmt::format("SELECT {} {} {} * FROM \"{}\" {} {} {} {} {}", // TODO : wise *
                    this->createDistinctClause(),
                    this->buildOnlyFields(),
                    this->buildFunctions(),
                    this->template get_table_name<T>(),
                    this->generateJoinSQL(),
                    this->filter_impl(),
                    this->generateGroupBySQL(),
                    this->buildOrderFields(),
                    this->limit_impl());
            std::cout << sql << std::endl;
            auto smt_ = Statement(conn, sql);
            auto all_rows = smt_.execute_all();
            
            std::vector<T> results;
            if (all_rows.empty()) {
                return results;
            }
            
            results.reserve(all_rows.size());
            
            for (const auto& row : all_rows) {
                T obj{};
                int column_idx = 0;
                
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
                
                results.push_back(std::move(obj));
            }
            
            return results;
        }

        [[nodiscard]] std::string alias() const {
            return this->_alias;
        }

        // Single-member overload removed to avoid ambiguity; variadic handles all cases.
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
        

        // =============================
        // =============================
        // Compile-time ORDER_BY overload using member pointers
        // =============================
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
        
        // =============================
        // Compile-time WHERE overloads using member pointers as NTTPs
        // =============================
        // Non-arithmetic values WHERE overload using member pointers
        template<auto MemberPtr, typename Value,
                 std::enable_if_t<!std::is_arithmetic_v<Value> || std::is_same_v<std::decay_t<Value>, bool>, int> = 0>
        QuerySet &where(Value &&value, const Operator op = Operator::EQUALS) {
            using ClassType = typename member_pointer_class<decltype(MemberPtr)>::type;
            using FieldType = typename member_pointer_type<decltype(MemberPtr)>::type;
            
            // Use the WhereClause constructor directly
            return addFilter(WhereClause(MemberPtr, std::forward<Value>(value), op));
        }

        // Compile-time WHERE overloads for arithmetic values using member pointers
        // =============================
        
        // Backward compatibility overload for arithmetic values
        // This allows using .where(&Post::author_id, author_id) syntax
        template<typename ClassType, typename FieldType, typename Value>
        QuerySet &where(FieldType ClassType::* memberPtr, Value&& value, const Operator op = Operator::EQUALS) {
            return addFilter(WhereClause(memberPtr, std::forward<Value>(value), op));
        }

        // FUNCTIONS

        template<typename... Args>
        QuerySet &functions(Args &&...args) {
            this->functions_impl(std::forward<Args>(args)...);
            return *this;
        }

        template<class U>
        QuerySet &join(std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(alias), std::move(addConditions), JoinInfo::JoinType::INNER);
            return *this;
        }

        // template<class T, class U>
        // QuerySet &join(QuerySet<U> &&other, std::string &&alias = "", std::string &&addConditions = "") {
        //     this->template join_impl<T, U>(std::move(other),
        //                                 std::move(alias),
        //                                 std::move(addConditions),
        //                                 JoinInfo::JoinType::INNER);
        //     return *this;
        // }

        // template<class T, class U>
        // QuerySet &join(std::string &&cte, std::string &&alias = "", std::string &&addConditions = "") {
        //     this->template join_impl<T, U>(std::move(cte),
        //                                 std::move(alias),
        //                                 std::move(addConditions),
        //                                 JoinInfo::JoinType::INNER);
        //     return *this;
        // }

        template<class U>
        QuerySet &left_join(std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(alias), std::move(addConditions), JoinInfo::JoinType::LEFT);
            return *this;
        }

        // template<class T, class U>
        // QuerySet &left_join(QuerySet<U> &&other, std::string &&alias = "", std::string &&addConditions = "") {
        //     this->template join_impl<T, U>(std::move(other),
        //                                 std::move(alias),
        //                                 std::move(addConditions),
        //                                 JoinInfo::JoinType::LEFT);
        //     return *this;
        // }

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

        template<typename... Args>
        QuerySet &distinct(const Args &...args) {
            this->distinct_impl(args...);
            return *this;
        }

        QuerySet &only(const std::vector<const BaseField *> &fields) {
            for(const auto &field: fields) {
                only(field);
            }
            return *this;
        }

        QuerySet &only(const BaseField *field, std::string alias = "") {
            this->onlyFields.emplace_back(field, std::move(alias));
            return *this;
        }

        template<typename... Args>
        QuerySet &only(const Args &...args) {
            this->only_impl(args...);
            return *this;
        }

        QuerySet &limit(int limit) {
            this->_limit = limit;
            return *this;
        }

        QuerySet &offset(int offset) {
            this->_offset = offset;
            return *this;
        }

        QuerySet &jsonFields(std::string &&jsonFields) {
            this->_jsonFields = std::move(jsonFields);
            return *this;
        }

        QuerySet &count(const BaseField *field, std::string_view alias) {
            functions(Function(fmt::format("count({})::integer as {}", field->getFullFieldName(), alias)));
            return *this;
        }
    };

    class BuildComplexQueries final : public BaseClass {
    public:
        [[nodiscard]] static std::string removeLastComma(const std::string_view &query) {
            return std::string(query.substr(0, query.size() - 2));
        }

        static std::string addQuery_impl() {
            return "";
        }

        template<class U, typename... Args>
        static std::string addQuery_impl(U &&u, Args &&...args) {
            return fmt::format("{} {}", std::forward<U>(u).aliasQuery(), addQuery_impl(std::forward<Args>(args)...));
        }

        static std::string addQueryMain_impl() {
            return "";
        }

        template<class U, typename... Args>
        static std::string addQueryMain_impl(U &&u, Args &&...args) {
            return fmt::format("{} {}",
                               std::forward<U>(u).aliasQueryMain(),
                               addQueryMain_impl(std::forward<Args>(args)...));
        }

        template<typename... Args>
        static std::string buildQuery(Args &&...args) {
            std::string query = removeLastComma(fmt::format("WITH {}", addQuery_impl(std::forward<Args>(args)...)));
            return fmt::format(" {} SELECT json_build_object({}) as result",
                               query,
                               removeLastComma(addQueryMain_impl(std::forward<Args>(args)...)));
        }
    };
}
