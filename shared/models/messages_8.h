#pragma once

/**
 * @file messages_8.h
 * @brief MESSAGES_8 — the 8-row Message seed dataset.
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include "message.h"

#include <array>

namespace storm::test {

// 8 Messages — sender IDs are placeholders (1-4); for PostgreSQL, re-query after insert.
inline constexpr std::array<Message, 8> MESSAGES_8 = {
    Message{.content = "Hello", .value = 10, .sender = {.id = 1}},
    Message{.content = "World", .value = 20, .sender = {.id = 1}},
    Message{.content = "Hi there", .value = 30, .sender = {.id = 1}},
    Message{.content = "Goodbye", .value = 40, .sender = {.id = 2}},
    Message{.content = "Testing", .value = 50, .sender = {.id = 2}},
    Message{.content = "Greetings", .value = 60, .sender = {.id = 3}},
    Message{.content = "Reply", .value = 70, .sender = {.id = 3}},
    Message{.content = "Quick note", .value = 80, .sender = {.id = 4}},
};

} // namespace storm::test
