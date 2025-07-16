#pragma once
#include <string>
#include <vector>
#include <typeinfo>
#include <functional>
#include <iostream>
#include <memory>
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include "Connection.h"
#include "Reflect.h"
#include <expected>

namespace storm {
class SchemaManager {
private:
    std::shared_ptr<Connection> conn;
    std::vector<std::string> registered_models;
    std::vector<std::string> create_table_sqls;  // Store SQL strings instead of functions
    std::unordered_set<std::string> created_tables;

public:
    explicit SchemaManager(std::shared_ptr<Connection> connection) 
        : conn(std::move(connection)) {}
    
    template<typename T>
    void register_model() {
        std::string model_name = get_table_name<T>();
        
        if (std::find(registered_models.begin(), registered_models.end(), model_name) 
            != registered_models.end()) {
            std::cout << "Model " << model_name << " already registered, skipping..." << std::endl;
            return;
        }
        
        // Generate and store SQL during registration
        std::string sql = generate_create_table_sql<T>();
        
        registered_models.push_back(model_name);
        create_table_sqls.push_back(sql);
        
        std::cout << "Registered model: " << model_name << std::endl;
    }

    std::expected<bool, std::string> create_all_tables();
    void show_schemas();

private:
    template<typename T>
    std::string get_table_name() const {
        return Reflect<T>::get_struct_name();
    }

    template<typename T>
    std::string generate_create_table_sql() {
        std::string table_name_str = get_table_name<T>();
        std::vector<std::string> field_definitions;
        
        Reflect<T>::for_each_member(Reflect<T>::get_reflected_members(), [&](auto member) {
            if constexpr (Reflect<T>::template is_field<decltype(member)>::value) {
                std::string field_name = utils::to_lower(std::string(Reflect<T>::get_member_name(member)));
                std::string field_type_sql = get_sql_type<typename decltype(member)::value_type>();
                if (field_name == "id") {
                    field_type_sql = "INTEGER PRIMARY KEY AUTOINCREMENT";
                }
                
                field_definitions.push_back(fmt::format("    {} {}", field_name, field_type_sql));
            }
        });
        
        if (field_definitions.empty()) {
            throw std::runtime_error(fmt::format("No fields found for type {}", get_table_name<T>()));
        }
        
        return fmt::format("CREATE TABLE IF NOT EXISTS {} (\n{}\n);", 
                          table_name_str, 
                          fmt::join(field_definitions, ",\n"));
    }
    
    template<typename T>
    std::string get_sql_type() {
        using BaseType = std::decay_t<T>;
        
        if constexpr (std::is_same_v<BaseType, int> || 
                      std::is_same_v<BaseType, int32_t> || 
                      std::is_same_v<BaseType, int64_t> || 
                      std::is_same_v<BaseType, long> ||
                      std::is_same_v<BaseType, long long> ||
                      std::is_same_v<BaseType, size_t>) {
            return "INTEGER";
        } else if constexpr (std::is_same_v<BaseType, double> || 
                            std::is_same_v<BaseType, float> ||
                            std::is_same_v<BaseType, long double>) {
            return "REAL";
        } else if constexpr (std::is_same_v<BaseType, bool>) {
            return "INTEGER"; // SQLite uses 0 and 1 for booleans
        } else if constexpr (std::is_same_v<BaseType, std::string>) {
            return "TEXT";
        } else if constexpr (std::is_same_v<BaseType, const char*> ||
                            std::is_same_v<BaseType, char*>) {
            return "TEXT";
        } else {
            // For custom types, default to TEXT and let user handle serialization
            static_assert(sizeof(BaseType) > 0, "Unsupported type for SQL mapping");
            return "TEXT";
        }
    }
};

}
