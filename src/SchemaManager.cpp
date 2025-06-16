#include "SchemaManager.h"
#include "Statement.h"

void SchemaManager::show_schemas() {
    if (create_table_sqls.empty()) {
        std::cout << "No models registered yet." << std::endl;
        return;
    }
    
    std::cout << "-- Generated SQL Schema for all registered models:\n" << std::endl;
    
    for (const auto& sql : create_table_sqls) {
        std::cout << sql << std::endl;
    }
    
    std::cout << "\n-- Summary:" << std::endl;
    std::cout << "-- Total registered models: " << registered_models.size() << std::endl;
    std::cout << "-- Tables created: " << created_tables.size() << std::endl;
}

bool SchemaManager::create_all_tables() {
    if (create_table_sqls.empty()) {
        std::cout << "No models registered for table creation" << std::endl;
        return true;
    }
    
    std::cout << "Creating " << create_table_sqls.size() << " tables in single batch..." << std::endl;
    
    // Filter out already created tables
    std::vector<std::string> sql_to_execute;
    std::vector<std::string> tables_to_create;
    
    for (size_t i = 0; i < registered_models.size(); ++i) {
        const std::string& model_name = registered_models[i];
        std::string table_name = utils::to_lower(model_name);
        
        if (created_tables.find(table_name) != created_tables.end()) {
            std::cout << "Table " << table_name << " already created, skipping..." << std::endl;
            continue;
        }
        
        sql_to_execute.push_back(create_table_sqls[i]);
        tables_to_create.push_back(table_name);
    }
    
    if (sql_to_execute.empty()) {
        std::cout << "No new tables to create" << std::endl;
        return true;
    }
    
    // Create single batch SQL with transaction
    std::string batch_sql = fmt::format("{}", fmt::join(sql_to_execute, "\n"));
    
    std::cout << "Executing single database transaction..." << std::endl;
    
    try {
        bool success = Statement(conn, batch_sql).execute();
        
        if (success) {
            // Mark all tables as created
            for (const auto& table_name : tables_to_create) {
                created_tables.insert(table_name);
            }
            std::cout << "✓ All " << tables_to_create.size() 
                     << " tables created successfully in single database hit!" << std::endl;
        } else {
            std::cerr << "Failed to execute batch table creation" << std::endl;
        }
        
        return success;
        
    } catch (const std::exception& e) {
        std::cerr << "Error during batch table creation: " << e.what() << std::endl;
        return false;
    }
}
