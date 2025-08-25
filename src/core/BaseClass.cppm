module;

// Define the module
export module storm.base_class;

/**
 * @brief Base class for all database-related classes
 *
 * This class serves as a common base for all database-related classes
 * in the system, providing shared functionality and type information.
 */
export class BaseClass {
public:
  BaseClass() = default;
  virtual ~BaseClass() = default;

  // Prevent copying
  BaseClass(const BaseClass &) = delete;
  BaseClass &operator=(const BaseClass &) = delete;

  // Allow moving
  BaseClass(BaseClass &&) noexcept = default;
  BaseClass &operator=(BaseClass &&) noexcept = default;
};
