module;

export module storm_statement_remove;
import storm_db_concept;
import storm_meta;

import <string_view>;
import <string>;
import <format>;
import <span>;
import <expected>;
import <meta>;

export namespace storm {

    template <typename T, storm::db::DatabaseConnection ConnType> class RemoveStatement {
        using Connection = ConnType;
        using Error      = typename ConnType::Error;
        using Statement  = typename ConnType::Statement;

        // Compile-time reflection attributes - computed once per template instantiation
        static constexpr auto primary_key_ = storm::meta::find_primary_key(^^T);
        static constexpr auto pk_name_     = std::meta::identifier_of(primary_key_);
        static constexpr auto table_name_  = std::meta::identifier_of(^^T);

        // Generate DELETE SQL string at runtime (std::format is not constexpr)
        static std::string get_delete_sql() {
            return std::format("DELETE FROM {} WHERE {} = ?", table_name_, pk_name_);
        }

      public:
        explicit RemoveStatement(Connection& conn) : conn_(conn) {}

        // Convenience constructor for when T can be deduced
        template <storm::db::DatabaseConnection C>
        explicit RemoveStatement(C& conn)
            requires std::same_as<C, Connection>
            : conn_(conn) {}

        [[nodiscard]] auto execute(std::span<const T> objects) noexcept -> std::expected<bool, Error> {
            if (objects.empty()) {
                return true;
            }

            // Use generated SQL string
            return conn_.prepare(get_delete_sql())
                    .and_then([this, objects](Statement stmt) -> std::expected<bool, Error> {
                        return bind_and_execute(std::move(stmt), objects);
                    });
        }

      private:
        // Bind primary keys and execute for all objects
        [[nodiscard]] auto bind_and_execute(Statement stmt, std::span<const T> objects) noexcept
                -> std::expected<bool, Error> {
            for (const auto& obj : objects) {
                // Reset statement for reuse
                stmt.reset();

                // Bind primary key value using meta reflection
                if (auto result = bind_primary_key(stmt, obj); !result) {
                    return std::unexpected(result.error());
                }

                // Execute statement
                if (auto result = stmt.execute(); !result) {
                    return std::unexpected(result.error());
                }
            }

            return true;
        }

        // Bind primary key value using pre-computed reflection data
        [[nodiscard]] auto bind_primary_key(Statement& stmt, const T& obj) noexcept -> std::expected<void, Error> {
            // Get primary key value using pre-computed reflection
            auto pk_value = obj.[:primary_key_:];

            // Bind based on type - database-agnostic binding
            if constexpr (std::is_same_v<decltype(pk_value), int>) {
                return stmt.bind_int(1, pk_value);
            } else if constexpr (std::is_convertible_v<decltype(pk_value), std::string_view>) {
                return stmt.bind_text(1, std::string_view{pk_value});
            } else {
                static_assert(
                        std::is_same_v<decltype(pk_value), int> ||
                                std::is_convertible_v<decltype(pk_value), std::string_view>,
                        "Unsupported primary key type"
                );
            }
        }

        Connection& conn_;
    };

} // namespace storm