#pragma once

/**
 * @file test_seed_helpers.h
 * @brief Batch insert/update/remove helpers for test fixture setup.
 *
 * Centralises the ORM batch API calls so that if the API changes,
 * only this file needs updating. All test fixtures should use these
 * instead of per-row loops or inline batch calls.
 *
 * Must be included AFTER `import storm;` (uses ORM types).
 * Usage: ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(people)));
 */

#include <gtest/gtest.h>
#include <span>
#include <vector>

namespace storm::test {

/// Batch-insert a vector of Model objects.  Returns true on success.
/// On failure, records the error via ADD_FAILURE() and returns false.
/// An empty vector is a no-op that always succeeds.
template <typename Model, typename ConnType> auto batch_insert(const std::vector<Model> &objects) -> bool {
    if (objects.empty())
        return true;

    storm::QuerySet<Model, ConnType> qs;
    auto result = qs.insert(std::span<const Model>(objects)).execute();
    if (!result.has_value()) {
        ADD_FAILURE() << "batch_insert failed: " << result.error().message();
        return false;
    }
    return true;
}

/// Batch-update a vector of Model objects.  Returns true on success.
/// On failure, records the error via ADD_FAILURE() and returns false.
/// An empty vector is a no-op that always succeeds.
template <typename Model, typename ConnType> auto batch_update(const std::vector<Model> &objects) -> bool {
    if (objects.empty())
        return true;

    storm::QuerySet<Model, ConnType> qs;
    auto result = qs.update(std::span<const Model>(objects)).execute();
    if (!result.has_value()) {
        ADD_FAILURE() << "batch_update failed: " << result.error().message();
        return false;
    }
    return true;
}

/// Batch-remove a vector of Model objects.  Returns true on success.
/// On failure, records the error via ADD_FAILURE() and returns false.
/// An empty vector is a no-op that always succeeds.
template <typename Model, typename ConnType> auto batch_remove(const std::vector<Model> &objects) -> bool {
    if (objects.empty())
        return true;

    storm::QuerySet<Model, ConnType> qs;
    auto result = qs.remove(std::span<const Model>(objects)).execute();
    if (!result.has_value()) {
        ADD_FAILURE() << "batch_remove failed: " << result.error().message();
        return false;
    }
    return true;
}

} // namespace storm::test

/// Fixture that creates a Person QuerySet and seeds PEOPLE_25.
/// Reuse to avoid duplicating the same on_after_setup / TearDown / qs boilerplate.
template <typename ConnType> class PersonSeedFixture : public StormTestFixture<Person, ConnType> {
  public:
    auto on_after_setup(const std::shared_ptr<ConnType> &) -> void override {
        qs = std::make_unique<storm::QuerySet<Person, ConnType>>();

        ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(
            std::vector<Person>(storm::test::PEOPLE_25.begin(), storm::test::PEOPLE_25.end()))));
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<Person, ConnType>::TearDown();
    }

    std::unique_ptr<storm::QuerySet<Person, ConnType>> qs;
};
