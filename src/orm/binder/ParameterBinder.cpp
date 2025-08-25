module;

// Module implementation unit
module storm.parameter_binder;

import <format>;
import <string>;
import <unordered_map>;
import <string_view>;
import <functional>;
import <variant>;

// Import required modules
import storm.core_types; // For SqlValue

namespace storm {

std::string ParameterBinder::add_parameter(const SqlValue &value) {
  std::string param_name = std::format("p{}", param_counter_);
  ++param_counter_;
  parameters_[param_name] = value;
  return param_name;
}

const std::unordered_map<std::string, SqlValue, TransparentStringHash,
                         std::equal_to<>> &
ParameterBinder::get_parameters() const {
  return parameters_;
}

void ParameterBinder::clear() {
  parameters_.clear();
  param_counter_ = 0;
}

SqlValue
ParameterBinder::get_parameter_value(const std::string &param_name) const {
  if (auto it = parameters_.find(param_name); it != parameters_.end()) {
    return it->second;
  }
  throw ParameterNotFoundException(param_name);
}

} // namespace storm
