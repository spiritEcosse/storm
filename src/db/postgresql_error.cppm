module;

export module storm_db_postgresql_error;
import <string>;
import <string_view>;

export namespace storm::db::postgresql {

    // Error type for database operations
    struct Error {
        int         code_;
        std::string message_;

        [[nodiscard]] constexpr auto code() const noexcept -> int {
            return code_;
        }
        [[nodiscard]] constexpr auto message() const noexcept -> std::string_view {
            return message_;
        }
    };

} // namespace storm::db::postgresql
