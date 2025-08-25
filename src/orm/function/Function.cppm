module;

export module storm.function;

import <string>;

import storm.base_class;

export namespace storm {

class Function final : public BaseClass {
public:
  using BaseClass::BaseClass;

  explicit Function(std::string function) : function(std::move(function)) {}

  // Virtual destructor to properly handle resource management
  ~Function() override = default;

  // Custom copy constructor that doesn't call the base class copy constructor
  Function(const Function &other) : function(other.function) {}

  // Custom copy assignment operator that doesn't call the base class assignment
  // operator
  Function &operator=(const Function &other) {
    if (this != &other) {
      function = other.function;
    }
    return *this;
  }

  [[nodiscard]] std::string toStr() const { return function; }

  [[nodiscard]] std::string getFullFieldName() const { return function; }

  std::string function;
};
} // namespace storm
