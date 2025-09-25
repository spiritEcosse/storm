module;

export module storm_db_manager;

import storm_db_sqlite_adapter;
import storm_db_sqlite;
import <expected>;
import <memory>;
import <string>;
import <string_view>;
import <mutex>;

export namespace storm::db {

    // Simplified connection manager - just for default connection
    class ConnectionManager {
      public:
        using Error = sqlite::Error;

        // Set the default connection by opening a database file
        [[nodiscard]] static auto set_default_connection(std::string_view db_path) noexcept -> std::expected<void, Error> {
            // Open the SQLite connection
            auto conn_result = sqlite::Connection::open(db_path);
            if (!conn_result) {
                return std::unexpected(conn_result.error());
            }

            // Store the connection in a managed pointer
            get_default_connection_ptr() = std::make_unique<sqlite::Connection>(std::move(conn_result.value()));

            // Create the adapter
            get_default_adapter_ptr() = std::make_unique<sqlite::ConnectionAdapter>(*get_default_connection_ptr());

            return {};
        }

        // Get the default connection (throws if not set)
        [[nodiscard]] static auto get_default_connection() -> sqlite::ConnectionAdapter& {
            if (!get_default_adapter_ptr()) {
                throw std::runtime_error("Default database connection not set. Call ConnectionManager::set_default_connection() first.");
            }
            return *get_default_adapter_ptr();
        }

        // Check if default connection exists
        [[nodiscard]] static bool has_default_connection() noexcept {
            return static_cast<bool>(get_default_adapter_ptr());
        }

        // Clear all connections
        static void clear_all() noexcept {
            get_default_adapter_ptr().reset();
            get_default_connection_ptr().reset();
        }

      private:
        // Static storage for default connection
        static auto get_default_connection_ptr() -> std::unique_ptr<sqlite::Connection>& {
            static std::unique_ptr<sqlite::Connection> conn_;
            return conn_;
        }

        static auto get_default_adapter_ptr() -> std::unique_ptr<sqlite::ConnectionAdapter>& {
            static std::unique_ptr<sqlite::ConnectionAdapter> adapter_;
            return adapter_;
        }
    };

} // namespace storm::db