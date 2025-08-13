module;

// Module global fragment
// (no legacy standard includes)

// Define the module
export module storm.core_interfaces;

// Import required modules
import storm.core_types;
import storm.parameter_binder;
// Import standard header units
import <memory>;
import <string>;
import <unordered_map>;

export namespace storm {

// Forward declaration of core classes to break circular dependencies
class Expression;
class Condition;
class LogicalExpression;

} // namespace storm
