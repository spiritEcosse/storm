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
#include "Connection.h"
#include "ReflectionUtils.h"

class SchemaManager {
private:
    std::shared_ptr<Connection> conn;
    std::vector<std::function<bool()>> table_creators;
    std::vector<std::string> registered_models; // Track model names for debugging
    std::unordered_set<std::string> created_tables; // Avoid duplicate creation
    
public:
    explicit SchemaManager(std::shared_ptr<Connection> connection) 
        : conn(std::move(connection)) {}
    
    // Template method to register a model
    template<typename T>
    void register_model() {
        std::string model_name = get_table_name<T>();
        
        // Avoid duplicate registrations
        if (std::find(registered_models.begin(), registered_models.end(), model_name) 
            != registered_models.end()) {
            std::cout << "Model " << model_name << " already registered, skipping..." << std::endl;
            return;
        }
        
        registered_models.push_back(model_name);
        table_creators.push_back([this]() {
            return create_table_for_model<T>();
        });
        
        std::cout << "Registered model: " << model_name << std::endl;
    }
    
    // Create all registered tables
    bool create_all_tables() {
        if (table_creators.empty()) {
            std::cout << "No models registered for table creation" << std::endl;
            return true;
        }
        
        std::cout << "Creating tables for " << table_creators.size() << " models..." << std::endl;
        
        for (size_t i = 0; i < table_creators.size(); ++i) {
            const std::string& model_name = registered_models[i];
            try {
                if (!table_creators[i]()) {
                    std::cerr << "Failed to create table for model: " << model_name << std::endl;
                    return false;
                }
                std::cout << "✓ Table for " << model_name << " created successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Error creating table for " << model_name << ": " << e.what() << std::endl;
                return false;
            }
        }
        
        std::cout << "All tables created successfully!" << std::endl;
        return true;
    }
    
    // Get table creation SQL for a model (useful for debugging)
    template<typename T>
    std::string get_create_table_sql() {
        return generate_create_table_sql<T>();
    }
    
    // Show all table schemas that would be created
    void show_schemas() {
        std::cout << "Registered models and their schemas:\n" << std::endl;
        
        for (size_t i = 0; i < registered_models.size(); ++i) {
            std::cout << "Model: " << registered_models[i] << std::endl;
            // We can't easily get the SQL without the type, but we can show the count
            std::cout << "  (Use get_create_table_sql<T>() to see the actual schema)\n" << std::endl;
        }
        
        if (registered_models.empty()) {
            std::cout << "No models registered yet." << std::endl;
        }
    }
    
    // Drop all registered tables (useful for testing)
    bool drop_all_tables() {
        std::cout << "Dropping tables for " << registered_models.size() << " models..." << std::endl;
        
        for (const auto& model_name : registered_models) {
            try {
                std::string table_name = to_lower(model_name) + "s";
                std::string sql = fmt::format("DROP TABLE IF EXISTS {}", table_name);
                std::cout << "Executing: " << sql << std::endl;
                
                if (!Statement(conn, sql).execute()) {
                    std::cerr << "Failed to drop table: " << table_name << std::endl;
                    return false;
                }
                std::cout << "✓ Table " << table_name << " dropped successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Error dropping table for " << model_name << ": " << e.what() << std::endl;
                return false;
            }
        }
        
        created_tables.clear();
        std::cout << "All tables dropped successfully!" << std::endl;
        return true;
    }

private:
    template<typename T>
    std::string get_table_name() const {
        // Use static method directly - no need for instance
        return storm::utils::to_lower(std::string(get_reflected_type<T>().name));
    }

    // Create table for a specific model type
    template<typename T>
    bool create_table_for_model() {
        std::string table_name = get_table_name<T>();
        
        // Check if already created in this session
        if (created_tables.find(table_name) != created_tables.end()) {
            std::cout << "Table " << table_name << " already created in this session, skipping..." << std::endl;
            return true;
        }
        
        std::string sql = generate_create_table_sql<T>();
        std::cout << "Executing: " << sql << std::endl;
        
        bool success = Statement(conn, sql).execute();
        if (success) {
            created_tables.insert(table_name);
        }
        
        return success;
    }
    
    // Helper to get reflected type information for T
    template<typename T>
    static constexpr auto get_reflected_type() {
        return storm::reflect::getType<T>();
    }
    
    // Generate the CREATE TABLE SQL for a model
    template<typename T>
    std::string generate_create_table_sql() {
        std::string table_name_str = get_table_name<T>();
        std::vector<std::string> field_definitions;
        constexpr auto type = get_reflected_type<T>();

        storm::reflect::for_each_member(type.members, [&](auto member) {
            if constexpr (storm::reflect::is_field<decltype(member)>::value) {
                std::string field_name = storm::utils::to_lower(storm::reflect::get_member_name(member));
                std::string field_type_sql = get_sql_type<typename decltype(member)::value_type>();
                // Special handling for 'id' field
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
    
    // Map C++ types to SQL types - expanded version
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
    
    // Utility functions
    std::string to_lower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), 
                      [](unsigned char c) { return std::tolower(c); });
        return result;
    }
    
    // Simple pluralization - you might want a more sophisticated version
    std::string pluralize(const std::string& word) {
        if (word.empty()) return word;
        
        // Handle some common cases
        if (word.back() == 'y') {
            return word.substr(0, word.length() - 1) + "ies";
        } else if (word.back() == 's' || 
                   word.ends_with("sh") || 
                   word.ends_with("ch") ||
                   word.ends_with("x") ||
                   word.ends_with("z")) {
            return word + "es";
        } else {
            return word + "s";
        }
    }
};