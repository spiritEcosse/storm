module;

// Module global fragment - include necessary headers
// (no legacy standard includes)

// Define the module
export module storm.parameter_binder;

// Import required modules
import storm.core_types; // For SqlValue
// Import standard header units
import <string>;
import <unordered_map>;
import <string_view>;
import <functional>;
import <stdexcept>;
import <utility>;
import <variant>; // For std::variant used in SqlValue

export namespace storm {

// Custom transparent string hasher that works with any string-like type
struct TransparentStringHash {
    using is_transparent = void; // Mark as transparent

    // Hash for std::string
    size_t operator()(const std::string& str) const {
        return std::hash<std::string_view>{}(str);
    }

    // Hash for string_view
    size_t operator()(std::string_view str) const {
        return std::hash<std::string_view>{}(str);
    }

    // Hash for const char*
    size_t operator()(const char* str) const {
        return std::hash<std::string_view>{}(str);
    }
};

// Custom exception for parameter binding errors
class ParameterNotFoundException : public std::runtime_error {
public:
    explicit ParameterNotFoundException(const std::string& param_name) 
        : std::runtime_error("Parameter not found: " + param_name) {}
};

// Parameter binding system to prevent SQL injection
class ParameterBinder {
private:
    // Use transparent hasher and comparator for heterogeneous lookup
    std::unordered_map<std::string, SqlValue, TransparentStringHash, std::equal_to<>> parameters_;
    mutable size_t param_counter_ = 0;
    
public:
    std::string add_parameter(const SqlValue& value);
    
    const std::unordered_map<std::string, SqlValue, TransparentStringHash, std::equal_to<>>& get_parameters() const;
    
    void clear();
    
    // Get parameter value for prepared statement binding
    SqlValue get_parameter_value(const std::string& param_name) const;
};

} // namespace storm
