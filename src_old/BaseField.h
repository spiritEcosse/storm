#pragma once

#include "BaseClass.h"
#include <string>

/**
 * @brief Base class for database field representations
 *
 * This abstract class provides the interface for all database field types,
 * allowing them to be used in queries and expressions.
 */
class BaseField : public BaseClass {
  public:
    /**
     * @brief Construct a new Base Field object
     */
    BaseField() = default;

    /**
     * @brief Virtual destructor
     */
    ~BaseField() override = default;

    /**
     * @brief Get the full field name including table name/alias
     * @return Full field name as string
     */
    [[nodiscard]] virtual std::string getFullFieldName() const = 0;

    /**
     * @brief Convert field to string representation
     * @return String representation of the field
     */
    [[nodiscard]] virtual std::string toStr() const = 0;
};
