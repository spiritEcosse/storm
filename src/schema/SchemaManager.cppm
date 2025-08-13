module;

// Module global fragment - standard library (imports used below)

export module storm.schema_manager;

// Import standard header units and storm utils
import <format>;
import <string_view>;
import <string>;
import <vector>;
import <unordered_set>;
import <memory>;
import <algorithm>;
import <iostream>;
import <expected>;
import storm.utils;

// Import Storm modules
import storm.connection;
import storm.statement;
import storm.reflect;
import storm.utils;

export namespace storm {

class SchemaManager {
private:
    std::shared_ptr<Connection> conn;
    std::vector<std::string> registered_models;
    std::vector<std::string> create_table_sqls;  // Store SQL strings instead of functions
    std::unordered_set<std::string> created_tables;

    template<typename T>
    static std::string get_sql_type() {
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
            return "TEXT"; // Default for custom types; users can handle serialization
        }
    }

    template<typename T>
    std::string get_table_name() const {
        return std::string(refl::reflect<T>::get_struct_name());
    }

    template<typename T>
    std::string generate_create_table_sql() {
        std::string table_name_str = get_table_name<T>();
        std::vector<std::string> field_definitions;

        refl::reflect<T>::for_each_member([&]<size_t I>(auto member) {
            std::string field_name = utils::to_lower(std::string(member.get_name()));
            std::string field_type_sql = get_sql_type<typename decltype(member)::member_type>();
            if (field_name == "id") {
                field_type_sql = "INTEGER PRIMARY KEY AUTOINCREMENT";
            }
            field_definitions.push_back(std::format("    {} {}", field_name, field_type_sql));
        });

        if (field_definitions.empty()) {
            throw std::runtime_error(std::format("No fields found for type {}", table_name_str));
        }

        return std::format(
            "CREATE TABLE IF NOT EXISTS {} (\n{}\n);",
            table_name_str,
            utils::join(field_definitions, ",\n")
        );
    }

public:
    explicit SchemaManager(std::shared_ptr<Connection> connection)
        : conn(std::move(connection)) {}

    template<typename T>
    void register_model() {
        std::string model_name = get_table_name<T>();

        if (std::find(registered_models.begin(), registered_models.end(), model_name) != registered_models.end()) {
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
};

// Implementation in SchemaManager.cpp

inline std::expected<bool, std::string> SchemaManager::create_all_tables() {
    if (create_table_sqls.empty()) {
        std::cout << "No models registered for table creation" << std::endl;
        return std::unexpected("No models registered for table creation");
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
        return std::unexpected("No new tables to create");
    }

    // Create single batch SQL without explicit transaction; the caller can manage transactions
    std::string batch_sql = utils::join(sql_to_execute, "\n");

    std::cout << "Executing single database transaction..." << std::endl;

    try {
        auto success = Statement(conn, batch_sql).execute();
        if (!success.has_value()) {
            std::cerr << "Error during batch table creation: " << success.error() << std::endl;
            return std::unexpected(success.error());
        }
        if (success.value()) {
            // Mark all tables as created
            for (const auto& table_name : tables_to_create) {
                created_tables.insert(table_name);
            }
            std::cout << "\u2713 All " << tables_to_create.size()
                      << " tables created successfully in single database hit!" << std::endl;
        } else {
            std::cerr << "Failed to execute batch table creation" << std::endl;
            return std::unexpected(success.error());
        }

        return success;
    } catch (const std::exception& e) {
        std::cerr << "Error during batch table creation: " << e.what() << std::endl;
        return std::unexpected(e.what());
    }
}

} // namespace storm
