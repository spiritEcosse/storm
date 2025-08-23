module;

// Define the module
export module storm.core_interfaces;

import <memory>;
import <string>;
import <unordered_map>;

// Import required modules
import storm.core_types;
import storm.parameter_binder;

export namespace storm {

// Forward declaration of core classes to break circular dependencies
class Expression;
class Condition;
class LogicalExpression;

} // namespace storm
