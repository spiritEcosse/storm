#pragma once

/**
 * @brief Base class for all database-related classes
 *
 * This class serves as a common base for all database-related classes
 * in the system, providing shared functionality and type information.
 */
class BaseClass {
  public:
    BaseClass()          = default;
    virtual ~BaseClass() = default;

    // Prevent copying
    BaseClass(const BaseClass&)            = delete;
    BaseClass& operator=(const BaseClass&) = delete;

    // Allow moving
    BaseClass(BaseClass&&) noexcept            = default;
    BaseClass& operator=(BaseClass&&) noexcept = default;
};
