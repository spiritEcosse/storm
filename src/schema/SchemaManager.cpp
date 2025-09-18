module;

// Module implementation unit
module storm.schema_manager;

import <iostream>;
import <string>;

// Import required modules
import storm.utils;

namespace storm {
    void SchemaManager::show_schemas() {
        if (create_table_sqls.empty()) {
            std::cout << "No models registered yet." << std::endl;
            return;
        }

        std::cout << "-- Generated SQL Schema for all registered models:\n" << std::endl;

        for (const auto& sql : create_table_sqls) {
            std::cout << sql << std::endl;
        }
    }

    // create_all_tables() is implemented inline in the module interface file
} // namespace storm
