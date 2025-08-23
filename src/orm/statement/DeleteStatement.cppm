module;

// Module declaration
export module storm.statement.remove;

// Import standard header units in the global module fragment
import <string>;
import <string_view>;
import <vector>;
import <span>;
import <format>;
import <expected>;
import <ranges>;
import <array>;
import <memory>;

// Import Storm modules
import storm.statement.base;
import storm.expression;
import storm.connection;
import storm.utils;
import storm.reflect;


export namespace storm {

/**
 * Specialized statement class for SQL DELETE operations
 * 
 * @tparam T The model type this statement operates on
 */
template <typename T>
class DeleteStatement : public UnifiedStatementBase<DeleteStatement<T>, T> {
private:
    using Base = UnifiedStatementBase<DeleteStatement<T>, T>;
    
public:
    explicit DeleteStatement(std::shared_ptr<Connection> conn) 
        : Base(conn) {}
    
    /**
     * Execute the DELETE statement with the current configuration
     *
     * @return Number of rows affected or an error message
     */
    [[nodiscard]] std::expected<bool, std::string> execute() noexcept {
        return build_sql()
            .and_then([this](const std::string& sql) -> std::expected<void, std::string> {
                return this->set_sql(sql);
            })
            .and_then([this]() -> std::expected<void, std::string> {
                return bind_where_parameters();
            })
            .and_then([this]() -> std::expected<bool, std::string> {
                return this->Base::execute();
            });
    }
    
    /**
     * Execute DELETE for a batch of objects by their primary keys
     *
     * @param objects Span of objects to delete
     * @return Number of rows affected or an error message
     */
    [[nodiscard]] std::expected<bool, std::string> execute(std::span<const T> objects) noexcept {
        if (objects.empty()) return true;
        
        return build_sql(objects.size())
            .and_then([this](const std::string& sql) -> std::expected<void, std::string> {
                return this->set_sql(sql);
            })
            .and_then([this, objects]() -> std::expected<void, std::string> {
                return bind_primary_keys(objects);
            })
            .and_then([this]() -> std::expected<bool, std::string> {
                return this->Base::execute();
            });
    }
    
private:
    /**
     * Bind parameters from the WHERE clause if present
     * 
     * @return Success or error message
     */
     [[nodiscard]] std::expected<void, std::string> bind_where_parameters() noexcept {
        if (!this->_where_clause) {
            return {}; // No WHERE clause, nothing to bind
        }
        
        auto query_result = this->_where_clause->to_query();
        return this->bind_parameters(*query_result.binder);
    }

    /**
     * Bind primary key values from a collection of objects
     * 
     * @param objects Span of objects to bind primary keys from
     * @return Success or error message
     */
    [[nodiscard]] std::expected<void, std::string> bind_primary_keys(std::span<const T> objects) noexcept {
        // Manual enumeration since std::views::enumerate is not available in libc++
        for (size_t i = 0; i < objects.size(); ++i) {
            const auto& obj = objects[i];
            // Bind the primary key value using base class method
            int param_idx = i + 1;
            
            auto bind_result = this->bind_primary_key_field(obj, param_idx);
            if (!bind_result) {
                return bind_result;
            }
        }
        
        return {};
    }
    
    /**
     * Get the RETURNING COUNT(*) clause for affected rows
     *
     * @return RETURNING COUNT(*) clause as a string_view
     */
    [[nodiscard]] consteval std::string_view get_returning_count_clause() const noexcept {
        return ""; // TODO: implement
    }
    
    /**
     * Generate the DELETE SQL string at compile time
     */
    [[nodiscard]] static consteval std::string_view get_base_delete_sql() noexcept {
        constexpr auto table_name = refl::reflect<T>::get_struct_name();
        constexpr std::string_view prefix = "DELETE FROM ";
        
        // Create compile-time concatenated string
        constexpr std::size_t total_size = prefix.size() + table_name.size() + 1;
        constexpr auto sql_string = [prefix]() constexpr {
            utils::fixed_string<total_size> result{""};
            
            // Append prefix
            for (char c : prefix) {
                result.data.push_back(c);
            }
            
            // Append table name
            for (char c : table_name) {
                result.data.push_back(c);
            }
            return result;
        }();
        
        return sql_string.view();
    }
    
    /**
     * Build SQL for batch delete operation with IN clause
     *
     * @param count Number of items to include in the IN clause
     * @return SQL string or error message
     */
    [[nodiscard]] std::expected<std::string, std::string> build_sql(size_t count) const noexcept {
        return std::format("{} WHERE {} IN ({}){}",
            get_base_delete_sql(),
            this->get_primary_key_field(),
            storm::utils::join(std::vector<std::string>(count, "?"), ", "),
            get_returning_count_clause());
    }
    
    /**
     * Build the SQL DELETE statement
     *
     * @return SQL string or error message
     */
    [[nodiscard]] std::expected<std::string, std::string> build_sql() const noexcept {
        return std::format("{}{}{}", 
            get_base_delete_sql(),
            this->_where_clause ? std::format(" WHERE {}", this->_where_clause->to_query().sql) : "",
            get_returning_count_clause());
    }
};

} // namespace storm