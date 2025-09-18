module;

// Define the module
export module storm.expression;

import <string>;
import <memory>;
import <expected>;

// Import required modules
import storm.core_types;       // For shared types
import storm.parameter_binder; // For ParameterBinder
import storm.reflect;          // For bind_variant function

export namespace storm {

    // Base expression interface
    class Expression {
      public:
        virtual ~Expression()                                                     = default;
        virtual std::string                 to_sql(ParameterBinder& binder) const = 0;
        virtual std::unique_ptr<Expression> clone() const                         = 0;
    };

} // namespace storm
