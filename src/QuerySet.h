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

namespace orm {

    using FieldOrFunction = std::variant<const BaseField *, Function>;

    namespace detail {
        template<typename T>
        std::string getFullFieldName(const T &obj) {
            return obj.getFullFieldName();
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
        inline std::string getFullFieldName(const std::variant<const BaseField *, Function> &obj) {
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
        std::vector<std::pair<std::variant<const BaseField *, Function>, bool>> orderFields;
        DistinctInfo distinctInfo;
        std::string _jsonFields;
        std::vector<FieldAlias> onlyFields;
        std::vector<Function> functionsSet;
        std::vector<std::unique_ptr<BaseQuerySetVirtual>> otherQueries;
        std::vector<const BaseField *> groupByFields;
        int _limit{};
        std::string _offset;
        std::string _alias;
        bool _one{};
        bool _doAndCheck{};
        bool _returnInMain{};
    private:
        [[nodiscard]] std::string getAlias() const override {
            return _alias;
        }

        template<typename U>
        void group_by_impl(U &&u) {
            groupByFields.emplace_back(std::forward<U>(u));
        }

        template<typename U, typename... Args>
        void group_by_impl(U &&u, Args &&...args) {
            groupByFields.emplace_back(std::forward<U>(u));
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
            return format(" ORDER BY {}",
                          fmt::join(orderFields | std::views::transform([](const auto &pair) {
                                        return fmt::format("{} {}",
                                                           detail::getFullFieldName(pair.first),
                                                           pair.second ? "ASC" : "DESC");
                                    }),
                                    ", "));
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
                              return format("{} AS {}", fieldAlias.field->getFullFieldName(), fieldAlias.alias);
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

        template<class U>
        void join_impl_core(const std::string &tableReference,
                            std::string &&alias,
                            std::string &&addConditions,
                            JoinInfo::JoinType joinType) {
            std::string lastJoinTable = joinInfo.joins.empty() ? T::tableName : joinInfo.joins.back().tableName;

            auto mapFields = U::joinMap();
            auto it = mapFields.find(lastJoinTable);
            if(it == mapFields.end()) {
                it = mapFields.find(T::tableName);
            }

            if(it == mapFields.end()) {
                mapFields = T::joinMap();
                it = mapFields.find(U::tableName);
            }

            if(it != mapFields.end()) {
                const auto &[joinFieldFirstTable, joinFieldSecondField] = it->second;

                const std::string &tableNameStr = joinFieldFirstTable->getTableName();
                const std::string &effectiveTableName = alias.empty() ? tableNameStr : alias;

                std::string condition = fmt::format(R"("{}"."{}" = {} {})",
                                                    effectiveTableName,
                                                    joinFieldFirstTable->getFieldName(),
                                                    joinFieldSecondField->getFullFieldName(),
                                                    std::move(addConditions));

                joinInfo.joins.emplace_back(std::string(tableReference),
                                            std::move(alias),
                                            std::move(condition),
                                            joinType);
            }
        }

        template<class U>
        void join_impl(std::string &&alias, std::string &&addConditions, const JoinInfo::JoinType joinType) {
            join_impl_core<U>(U::tableName, std::move(alias), std::move(addConditions), joinType);
        }

        template<class U>
        void join_impl(std::string &&cte,
                       std::string &&alias,
                       std::string &&addConditions,
                       const JoinInfo::JoinType joinType) {
            join_impl_core<U>(std::move(cte), std::move(alias), std::move(addConditions), joinType);
        }

        template<class U>
        void join_impl(BaseQuerySet<U> &&querySet,
                       std::string &&alias,
                       std::string &&addConditions,
                       const JoinInfo::JoinType joinType) {
            std::string aliasCTE = querySet.getAlias();
            this->addCTE(std::move(querySet));
            join_impl_core<U>(std::move(aliasCTE), std::move(alias), std::move(addConditions), joinType);
        }

        template<class U>
        void addCTE(BaseQuerySet<U> &&cte) {
            this->otherQueries.push_back(std::make_unique<BaseQuerySet<U>>(std::move(cte)));
        }

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
            if(groupByFields.empty()) {
                return "";
            }

            return format("GROUP BY {}",
                          fmt::join(groupByFields | std::views::transform([](const auto &field) {
                                        return field->getFullFieldName();
                                    }),
                                    ", "));
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

            return !_offset.empty() ? fmt::format(" LIMIT {} OFFSET {}", _limit, _offset)
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

    template <typename T>
    class QuerySet : public BaseQuerySet<T> {
    private:
        std::shared_ptr<Connection> conn;

        QuerySet &addFilter(WhereClause &&whereClause) {
            this->filters.push_back(std::move(whereClause));
            return *this;
        }

        // Helper to get reflected type information for T
        static constexpr auto get_reflected_type() {
            return Reflect<T>::get_reflected_type();
        }

        // Function to get the reflected members of the type
        static constexpr auto get_reflected_members() {
            return get_reflected_type().members;
        }
        
        static std::string get_table_name() {
            return Reflect<T>::get_struct_name();
        }

        // Helper function to get field names (excluding 'id')
        std::vector<std::string> get_insert_field_names() const {
            constexpr auto type = get_reflected_type();
            std::vector<std::string> field_names;
            
            Reflect<T>::for_each_member(type.members, [&](auto member) {
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
            constexpr auto type = get_reflected_type();
            
            Reflect<T>::for_each_member(type.members, [&](auto member) {
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
        
        // Helper function to set auto-generated ID
        void set_generated_id(T& obj, std::int64_t id) const {
            constexpr auto type = get_reflected_type();
            
            Reflect<T>::for_each_member(type.members, [&](auto member) {
                if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                    std::string field_name = Reflect<T>::get_member_name(member);
                    if (field_name == "id") {
                        using IdType = std::decay_t<decltype(member(obj))>;
                        if constexpr (std::is_integral_v<IdType>) {
                            member(obj) = static_cast<IdType>(id);
                        }
                    }
                }
            });
        }

    public:
        // Constructor
        explicit QuerySet(std::shared_ptr<Connection> conn, const std::string& alias = "") 
            : BaseQuerySet<T>(alias.empty() ? get_table_name() : alias, false, true), conn(std::move(conn)) {
        }

        bool insert(T& obj) {
            try {
                auto field_names = get_insert_field_names();
                if (field_names.empty()) return false;
                
                std::vector<std::string> placeholders(field_names.size(), "?");
                std::string sql = fmt::format(
                    "INSERT INTO {} ({}) VALUES ({});",
                    get_table_name(),
                    fmt::join(field_names, ", "),
                    fmt::join(placeholders, ", ")
                );
                
                auto stmt = Statement(conn, sql);
                int param_index = 1;
                bind_object_values(stmt, obj, param_index);
                
                bool success = stmt.execute();
                if (success) {
                    set_generated_id(obj, conn->last_insert_id());
                }
                
                return success;
            } catch (const std::exception& e) {
                // Consider logging e.what()
                std::cerr << "Exception in insert: " << e.what() << "\n";
                return false;
            }
        }
        
        // Insert multiple objects (batch insert)
        bool insert(std::vector<T>& objs) {
            if (objs.empty()) return true;
            
            try {
                auto field_names = get_insert_field_names();
                if (field_names.empty()) return false;
                
                // Create VALUES clause: (?,?), (?,?), ...
                std::vector<std::string> value_groups;
                std::vector<std::string> placeholders(field_names.size(), "?");
                std::string single_group = fmt::format("({})", fmt::join(placeholders, ", "));
                
                for (size_t i = 0; i < objs.size(); ++i) {
                    value_groups.push_back(single_group);
                }
                
                std::string sql = fmt::format(
                    "INSERT INTO {} ({}) VALUES {};",
                    get_table_name(),
                    fmt::join(field_names, ", "),
                    fmt::join(value_groups, ", ")
                );
                
                auto stmt = Statement(conn, sql);
                
                // Bind all values for all objects
                int param_index = 1;
                for (auto& obj : objs) {
                    bind_object_values(stmt, obj, param_index);
                }
                
                bool success = stmt.execute();
                
                // Set generated IDs for all objects
                if (success && !objs.empty()) {
                    auto first_id = conn->last_insert_id();
                    for (size_t i = 0; i < objs.size(); ++i) {
                        set_generated_id(objs[i], first_id - (objs.size() - 1) + i);
                    }
                }
                
                return success;
                
            } catch (const std::exception& e) {
                std::cerr << "Exception in insert: " << e.what() << "\n";
                return false;
            }
        }
        
        bool update(const T& obj) {   
            try {
                auto field_names = get_update_field_names();
                if (field_names.empty()) return false;
                
                // Build assignments: field1 = ?, field2 = ?, ...
                std::vector<std::string> assignments;
                for (const auto& name : field_names) {
                    assignments.push_back(fmt::format("{} = ?", name));
                }
                std::string sql = fmt::format(
                    "UPDATE {} SET {} WHERE id = ?;",
                    get_table_name(),
                    fmt::join(assignments, ", ")
                );
                
                auto stmt = Statement(conn, sql);
                int param_index = 1;
                bind_object_values(stmt, obj, param_index);
                stmt.bind(param_index, obj.id);
                return stmt.execute();
            } catch (const std::exception& e) {
                std::cerr << "Exception in update: " << e.what() << "\n";
                return false;
            }
        }

        bool remove(const T& obj) {
            try {
                std::string sql = fmt::format(
                    "DELETE FROM {} WHERE id = ?;",
                    get_table_name()
                );
                
                auto stmt = Statement(conn, sql);
                stmt.bind(1, obj.id);
                return stmt.execute();
            } catch (const std::exception& e) {
                std::cerr << "Exception in delete: " << e.what() << "\n";
                return false;
            }
        }

        std::vector<T> select_all() {
            std::vector<T> results;
            auto smt_ = Statement(conn, fmt::format("SELECT * FROM {};", get_table_name()) );    
            // Execute query and get all results in a single database operation
            auto all_rows = smt_.execute_all(); // or handle error appropriately
            
            if (all_rows.empty()) {
                return results;
            }
            
            // Pre-allocate memory for efficiency
            results.reserve(all_rows.size());
            
            // Process each row into objects
            constexpr auto type = get_reflected_type();
            
            for (const auto& row : all_rows) {
                T obj{};
                int column_idx = 0;
                
                Reflect<T>::for_each_member(type.members, [&](auto member) {
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

        template<typename... Args>
        QuerySet &group_by(Args &&...args) {
            this->group_by_impl(std::forward<Args>(args)...);
            return *this;
        }

        QuerySet &filter(const BaseField *field, const std::string &value, const Operator op = Operator::EQUALS) {
            return addFilter(WhereClause(field, value, op));
        }

        QuerySet &filter(const BaseField *field, std::string &&value, const Operator op = Operator::EQUALS) {
            return addFilter(WhereClause(field, std::move(value), op));
        }

        QuerySet &
        filter(const BaseField *field, const std::optional<bool> value, const Operator op = Operator::EQUALS) {
            return addFilter(WhereClause(field, value, op));
        }

        QuerySet &filter(const BaseField *field1, const BaseField *field2) {
            return addFilter(WhereClause(field1, field2));
        }

        QuerySet &filter(WhereClause &&whereClause) {
            return addFilter(std::move(whereClause));
        }

        QuerySet &order_by(const BaseField *field, bool asc = true) {
            this->orderFields.emplace_back(field, asc);
            return *this;
        }

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

        template<class U>
        QuerySet &join(QuerySet<U> &&other, std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(other),
                                        std::move(alias),
                                        std::move(addConditions),
                                        JoinInfo::JoinType::INNER);
            return *this;
        }

        template<class U>
        QuerySet &join(std::string &&cte, std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(cte),
                                        std::move(alias),
                                        std::move(addConditions),
                                        JoinInfo::JoinType::INNER);
            return *this;
        }

        template<class U>
        QuerySet &left_join(std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(alias), std::move(addConditions), JoinInfo::JoinType::LEFT);
            return *this;
        }

        template<class U>
        QuerySet &left_join(QuerySet<U> &&other, std::string &&alias = "", std::string &&addConditions = "") {
            this->template join_impl<U>(std::move(other),
                                        std::move(alias),
                                        std::move(addConditions),
                                        JoinInfo::JoinType::LEFT);
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

        QuerySet &offset(std::string &&offset) {
            this->_offset = std::move(offset);
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
