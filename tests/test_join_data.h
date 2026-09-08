#pragma once

/**
 * @file test_join_data.h
 * @brief Person + Message join fixture data.
 *
 * Split out of test_models.h (issue #634) — only the DISTINCT and VALUES
 * fixtures seed this shape.
 *
 * IMPORTANT: Include AFTER `import storm;` — the model headers below require it.
 */

#include "../shared/models/message.h"
#include "../shared/models/person.h"

#include <gtest/gtest.h>
#include <span>
#include <vector>

namespace storm::test {

// Populates join test data: 3 Persons + 5 Messages with sender FKs.
// Used by DistinctTest, ValuesTest, and AggregateTest fixtures.
template <typename ConnType> inline auto populate_join_test_data() -> void {
    storm::QuerySet<Person, ConnType> person_qs;
    std::vector<Person> const people = {
        {.name = "Alice", .age = 30},
        {.name = "Bob", .age = 25},
        {.name = "Charlie", .age = 35},
    };
    auto person_result = person_qs.insert(std::span<const Person>(people)).execute();
    if (!person_result.has_value()) {
        ADD_FAILURE() << "populate_join_test_data: person insert failed: " << person_result.error().message();
        return;
    }

    storm::QuerySet<Message, ConnType> msg_qs;
    std::vector<Message> const messages = {
        {.content = "Hello", .sender = {.id = 1}},    {.content = "World", .sender = {.id = 1}},
        {.content = "Hi there", .sender = {.id = 2}}, {.content = "Goodbye", .sender = {.id = 2}},
        {.content = "Test", .sender = {.id = 3}},
    };
    auto msg_result = msg_qs.insert(std::span<const Message>(messages)).execute();
    if (!msg_result.has_value()) {
        ADD_FAILURE() << "populate_join_test_data: message insert failed: " << msg_result.error().message();
    }
}

} // namespace storm::test
