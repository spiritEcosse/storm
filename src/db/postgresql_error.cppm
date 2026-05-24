module;

export module storm_db_postgresql_error;
import <array>;
import <cstddef>;
import <cstring>;
import <string_view>;

export namespace storm::db::postgresql {

    // Error type for database operations.
    // Non-allocating: stores the message inline so construction is noexcept and Error can
    // be returned from noexcept catch blocks without risking bad_alloc (issue #316).
    struct Error {
        static constexpr std::size_t kMaxMessageLen = 255;

        int                                  code_{0};
        std::array<char, kMaxMessageLen + 1> message_buf_{};
        std::size_t                          message_len_{0};

        Error() noexcept = default;

        Error(int code, std::string_view msg) noexcept : code_{code} {
            const std::size_t n = msg.size() < kMaxMessageLen ? msg.size() : kMaxMessageLen;
            if (n > 0) {
                std::memcpy(message_buf_.data(), msg.data(), n);
            }
            message_buf_[n] = '\0';
            message_len_    = n;
        }

        [[nodiscard]] constexpr auto code() const noexcept -> int {
            return code_;
        }
        [[nodiscard]] constexpr auto message() const noexcept -> std::string_view {
            return std::string_view{message_buf_.data(), message_len_};
        }
    };

} // namespace storm::db::postgresql
