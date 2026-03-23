module;

#include <meta>

export module storm_orm_schema;

import storm_orm_statements_base;
import storm_orm_indexes;
import storm_orm_utilities;
import storm_db_concept;

import <string>;
import <string_view>;
import <expected>;
import <memory>;
import <optional>;
import <vector>;
import <array>;
import <utility>;
import <tuple>;
import <iterator>;
import <type_traits>;
import <chrono>;
import <filesystem>;
import <cstddef>;

export namespace storm::orm::schema {

    // SQL dialect for compile-time schema generation
    enum class Dialect { SQLite, PostgreSQL };

    // Import utilities for compile-time string building
    using storm::orm::utilities::ConstexprString;

    namespace detail {

        // Map a C++ field type to its SQL column definition string for the given dialect.
        // Returns the column type portion (after the column name).
        template <typename FieldType, Dialect D = Dialect::SQLite> consteval auto sql_col_def() -> std::string_view {
            using utilities::is_chrono_duration_v;
            using utilities::is_optional_v;
            using utilities::optional_inner_type_t;
            constexpr bool pg = (D == Dialect::PostgreSQL);

            // === OPTIONAL TYPES (nullable, no NOT NULL) ===
            if constexpr (std::is_same_v<FieldType, std::optional<bool>>) {
                if constexpr (pg) {
                    return "BOOLEAN";
                } else {
                    return "INTEGER";
                }
            } else if constexpr (std::is_same_v<FieldType, std::optional<int>> ||
                                 std::is_same_v<FieldType, std::optional<int64_t>>) {
                if constexpr (pg) {
                    return "BIGINT";
                } else {
                    return "INTEGER";
                }
            } else if constexpr (is_optional_v<FieldType> && std::is_enum_v<optional_inner_type_t<FieldType>>) {
                if constexpr (pg) {
                    return "BIGINT";
                } else {
                    return "INTEGER";
                }
            } else if constexpr (is_optional_v<FieldType> && is_chrono_duration_v<optional_inner_type_t<FieldType>>) {
                if constexpr (pg) {
                    return "BIGINT";
                } else {
                    return "INTEGER";
                }
            } else if constexpr (std::is_same_v<FieldType, std::optional<double>>) {
                if constexpr (pg) {
                    return "DOUBLE PRECISION";
                } else {
                    return "REAL";
                }
            } else if constexpr (std::is_same_v<FieldType, std::optional<float>>) {
                return "REAL";
            } else if constexpr (std::is_same_v<FieldType, std::optional<std::string>> ||
                                 std::is_same_v<FieldType, std::optional<std::string_view>>) {
                return "TEXT";
            } else if constexpr (is_optional_v<FieldType> &&
                                 std::is_same_v<optional_inner_type_t<FieldType>, std::chrono::year_month_day>) {
                if constexpr (pg) {
                    return "DATE";
                } else {
                    return "TEXT";
                }
            } else if constexpr (is_optional_v<FieldType> && std::is_same_v<
                                                                     optional_inner_type_t<FieldType>,
                                                                     std::chrono::system_clock::time_point>) {
                if constexpr (pg) {
                    return "TIMESTAMP";
                } else {
                    return "TEXT";
                }
            } else if constexpr (is_optional_v<FieldType> &&
                                 std::is_same_v<optional_inner_type_t<FieldType>, utilities::UUID>) {
                if constexpr (pg) {
                    return "UUID";
                } else {
                    return "TEXT";
                }
            } else if constexpr (is_optional_v<FieldType> &&
                                 std::is_same_v<optional_inner_type_t<FieldType>, std::filesystem::path>) {
                return "TEXT";
            }
            // === BLOB TYPES ===
            else if constexpr (std::is_same_v<FieldType, std::vector<uint8_t>> ||
                               std::is_same_v<FieldType, std::vector<unsigned char>> ||
                               std::is_same_v<FieldType, std::vector<std::byte>>) {
                if constexpr (pg) {
                    return "BYTEA";
                } else {
                    return "BLOB";
                }
            }
            // === NON-OPTIONAL TYPES (NOT NULL) ===
            else if constexpr (std::is_same_v<FieldType, bool>) {
                if constexpr (pg) {
                    return "BOOLEAN NOT NULL";
                } else {
                    return "INTEGER NOT NULL";
                }
            } else if constexpr (std::is_same_v<FieldType, int> || std::is_same_v<FieldType, int64_t> ||
                                 std::is_same_v<FieldType, short> || std::is_same_v<FieldType, unsigned int> ||
                                 std::is_same_v<FieldType, unsigned short> || std::is_same_v<FieldType, long> ||
                                 std::is_same_v<FieldType, unsigned long> || std::is_same_v<FieldType, long long> ||
                                 std::is_same_v<FieldType, unsigned long long> ||
                                 std::is_same_v<FieldType, signed char> || std::is_same_v<FieldType, unsigned char> ||
                                 std::is_same_v<FieldType, char>) {
                if constexpr (pg) {
                    return "BIGINT NOT NULL";
                } else {
                    return "INTEGER NOT NULL";
                }
            } else if constexpr (std::is_enum_v<FieldType>) {
                if constexpr (pg) {
                    return "BIGINT NOT NULL";
                } else {
                    return "INTEGER NOT NULL";
                }
            } else if constexpr (is_chrono_duration_v<FieldType>) {
                if constexpr (pg) {
                    return "BIGINT NOT NULL";
                } else {
                    return "INTEGER NOT NULL";
                }
            } else if constexpr (std::is_same_v<FieldType, double>) {
                if constexpr (pg) {
                    return "DOUBLE PRECISION NOT NULL";
                } else {
                    return "REAL NOT NULL";
                }
            } else if constexpr (std::is_same_v<FieldType, float>) {
                return "REAL NOT NULL";
            } else if constexpr (std::is_same_v<FieldType, std::string> ||
                                 std::is_same_v<FieldType, std::string_view>) {
                return "TEXT NOT NULL";
            } else if constexpr (std::is_same_v<FieldType, std::chrono::year_month_day>) {
                if constexpr (pg) {
                    return "DATE NOT NULL";
                } else {
                    return "TEXT NOT NULL";
                }
            } else if constexpr (std::is_same_v<FieldType, std::chrono::system_clock::time_point>) {
                if constexpr (pg) {
                    return "TIMESTAMP NOT NULL";
                } else {
                    return "TEXT NOT NULL";
                }
            } else if constexpr (std::is_same_v<FieldType, utilities::UUID>) {
                if constexpr (pg) {
                    return "UUID NOT NULL";
                } else {
                    return "TEXT NOT NULL";
                }
            } else if constexpr (std::is_same_v<FieldType, std::filesystem::path>) {
                return "TEXT NOT NULL";
            } else {
                // Fallback for unknown types — treat as TEXT
                return "TEXT";
            }
        }

    } // namespace detail

    template <typename T> class SchemaStatement : private storm::orm::statements::BaseStatement<T> {
        using Base = storm::orm::statements::BaseStatement<T>;

        // Column definition buffer size — generous for field name + type + constraints
        static constexpr size_t COL_DEF_BUFFER = 128;

        // Build column definition for field at compile-time index
        template <size_t Index, Dialect D = Dialect::SQLite> static consteval auto build_column_def() {
            ConstexprString<COL_DEF_BUFFER> col;
            constexpr auto                  member = Base::all_members_[Index];

            // Primary key field
            if constexpr (Base::all_members_[Index] == Base::primary_key_) {
                if constexpr (D == Dialect::PostgreSQL) {
                    col.append("id BIGINT PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY");
                } else {
                    col.append("id INTEGER PRIMARY KEY AUTOINCREMENT");
                }
            }
            // FK field — generate "<name>_id INTEGER/BIGINT NOT NULL" (or nullable if optional FK)
            else if constexpr (Base::is_fk_field(member)) {
                using FieldType = std::remove_cvref_t<typename[:std::meta::type_of(member):]>;
                col.append(std::meta::identifier_of(member));
                if constexpr (storm::orm::utilities::is_optional_v<FieldType>) {
                    if constexpr (D == Dialect::PostgreSQL) {
                        col.append("_id BIGINT");
                    } else {
                        col.append("_id INTEGER");
                    }
                } else {
                    if constexpr (D == Dialect::PostgreSQL) {
                        col.append("_id BIGINT NOT NULL");
                    } else {
                        col.append("_id INTEGER NOT NULL");
                    }
                }
            }
            // Unique field — same as regular but with UNIQUE constraint
            else if constexpr (Base::is_unique_field(member)) {
                using FieldType = std::remove_cvref_t<typename[:std::meta::type_of(member):]>;
                col.append(std::meta::identifier_of(member));
                col.append(" ");
                col.append(detail::sql_col_def<FieldType, D>());
                col.append(" UNIQUE");
            }
            // Regular field
            else {
                using FieldType = std::remove_cvref_t<typename[:std::meta::type_of(member):]>;
                col.append(std::meta::identifier_of(member));
                col.append(" ");
                col.append(detail::sql_col_def<FieldType, D>());
            }
            return col;
        }

        // Compile-time SQL size calculation for CREATE TABLE
        template <Dialect D = Dialect::SQLite> static consteval auto calculate_column_defs_size() -> size_t {
            // PG PK: "id BIGINT PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY" = 54
            // SQLite PK: "id INTEGER PRIMARY KEY AUTOINCREMENT" = 36
            constexpr size_t pk_size = (D == Dialect::PostgreSQL) ? 54 : 36;
            // PG max type: "DOUBLE PRECISION NOT NULL" = 25; SQLite max: "INTEGER NOT NULL" = 16
            constexpr size_t max_type_def = (D == Dialect::PostgreSQL) ? 25 : 16;
            // PG FK: "_id BIGINT NOT NULL" = 19; SQLite FK: "_id INTEGER NOT NULL" = 20
            constexpr size_t max_fk_suffix = 3 + ((D == Dialect::PostgreSQL) ? 16 : 20);

            size_t size = 0;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                if (i > 0) {
                    size += 2; // ",\n"
                }
                size += 4; // "    " indent

                if (Base::all_members_[i] == Base::primary_key_) {
                    size += pk_size;
                } else {
                    auto field_attr = std::meta::annotation_of_type<statements::meta::FieldAttr>(Base::all_members_[i]);
                    size += std::meta::identifier_of(Base::all_members_[i]).size();
                    if (field_attr.has_value() && field_attr.value() == statements::meta::FieldAttr::fk) {
                        size += max_fk_suffix;
                    } else {
                        size += 1 + max_type_def + 7; // " " + type def + " UNIQUE"
                    }
                }
            }
            return size;
        }

        template <Dialect D = Dialect::SQLite> static consteval auto calculate_create_table_sql_size() -> size_t {
            return 13 + Base::table_name_.size() + 3 + calculate_column_defs_size<D>() + 2;
        }

        // Build the full CREATE TABLE SQL at compile-time using index sequence fold
        template <Dialect D = Dialect::SQLite, size_t... Is>
        static consteval auto build_sql_impl(std::index_sequence<Is...> /*unused*/) {
            // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - constexpr IS initialized
            constexpr size_t          sql_size = calculate_create_table_sql_size<D>() + utilities::sql_len::XL_BUFFER;
            ConstexprString<sql_size> sql;

            sql.append("CREATE TABLE ");
            sql.append(Base::table_name_);
            sql.append(" (\n");

            bool first = true;
            (([&] {
                 if (!first) {
                     sql.append(",\n");
                 }
                 sql.append("    ");
                 sql.append(build_column_def<Is, D>());
                 first = false;
             }()),
             ...);

            sql.append("\n)");
            return sql;
        }

        template <Dialect D = Dialect::SQLite> static consteval auto build_create_table_sql() {
            return build_sql_impl<D>(std::make_index_sequence<Base::field_count_>{});
        }

        // Pre-computed CREATE TABLE SQL generated at compile-time (one per dialect)
        static constexpr auto           sqlite_create_table_sql_array_  = build_create_table_sql<Dialect::SQLite>();
        static inline const std::string sqlite_create_table_sql_string_ = std::string(sqlite_create_table_sql_array_);

        static constexpr auto           pg_create_table_sql_array_  = build_create_table_sql<Dialect::PostgreSQL>();
        static inline const std::string pg_create_table_sql_string_ = std::string(pg_create_table_sql_array_);

        // =====================================================================
        // INDEX SQL GENERATION
        // =====================================================================

        // Index SQL buffer — generous for "CREATE UNIQUE INDEX IF NOT EXISTS idx_<table>_<field> ON <table>(<field>)"
        static constexpr size_t INDEX_SQL_BUFFER = 256;

        // Build CREATE INDEX SQL for a single field at compile-time
        template <size_t Index> static consteval auto build_create_index_sql() {
            ConstexprString<INDEX_SQL_BUFFER> sql;
            constexpr auto                    member = Base::all_members_[Index];

            if constexpr (!Base::needs_index(member)) {
                return sql;
            } else if constexpr (Base::is_unique_field(member)) {
                sql.append("CREATE UNIQUE INDEX IF NOT EXISTS idx_");
                sql.append(Base::table_name_);
                sql.append("_");
                sql.append(std::meta::identifier_of(member));
                sql.append(" ON ");
                sql.append(Base::table_name_);
                sql.append("(");
                sql.append(std::meta::identifier_of(member));
                sql.append(")");
            } else if constexpr (Base::is_fk_field(member)) {
                sql.append("CREATE INDEX IF NOT EXISTS idx_");
                sql.append(Base::table_name_);
                sql.append("_");
                sql.append(std::meta::identifier_of(member));
                sql.append("_id ON ");
                sql.append(Base::table_name_);
                sql.append("(");
                sql.append(std::meta::identifier_of(member));
                sql.append("_id)");
            } else {
                sql.append("CREATE INDEX IF NOT EXISTS idx_");
                sql.append(Base::table_name_);
                sql.append("_");
                sql.append(std::meta::identifier_of(member));
                sql.append(" ON ");
                sql.append(Base::table_name_);
                sql.append("(");
                sql.append(std::meta::identifier_of(member));
                sql.append(")");
            }
            return sql;
        }

        // Count how many fields need indexes at compile-time
        static consteval auto count_indexes() -> size_t {
            size_t count = 0;
            for (size_t i = 0; i < Base::field_count_; ++i) {
                if (Base::needs_index(Base::all_members_[i])) {
                    ++count;
                }
            }
            return count;
        }

        // Collect all index SQL strings into a vector at runtime (from constexpr data)
        template <size_t... Is>
        static auto build_index_sql_vector(std::index_sequence<Is...> /*unused*/) -> std::vector<std::string> {
            std::vector<std::string> result;
            result.reserve(count_indexes());
            (([&] {
                 constexpr auto sql = build_create_index_sql<Is>();
                 if constexpr (sql.len > 0) {
                     result.emplace_back(std::string(sql));
                 }
             }()),
             ...);
            return result;
        }

        // Build CREATE INDEX SQL for a single composite index type at compile-time
        template <typename IdxType> static consteval auto build_composite_index_sql() {
            ConstexprString<INDEX_SQL_BUFFER> sql;
            if constexpr (IdxType::unique) {
                sql.append("CREATE UNIQUE INDEX IF NOT EXISTS idx_");
            } else {
                sql.append("CREATE INDEX IF NOT EXISTS idx_");
            }
            sql.append(Base::table_name_);
            for (size_t i = 0; i < IdxType::fields.size(); ++i) {
                sql.append("_");
                sql.append(std::meta::identifier_of(IdxType::fields[i]));
            }
            sql.append(" ON ");
            sql.append(Base::table_name_);
            sql.append("(");
            for (size_t i = 0; i < IdxType::fields.size(); ++i) {
                if (i > 0) {
                    sql.append(", ");
                }
                if (Base::is_fk_field(IdxType::fields[i])) {
                    sql.append(std::meta::identifier_of(IdxType::fields[i]));
                    sql.append("_id");
                } else {
                    sql.append(std::meta::identifier_of(IdxType::fields[i]));
                }
            }
            sql.append(")");
            return sql;
        }

        // Collect composite index SQL strings into a vector
        template <typename Tuple, size_t... Is>
        static auto build_composite_index_sql_vector(std::index_sequence<Is...> /*unused*/)
                -> std::vector<std::string> {
            std::vector<std::string> result;
            ((result.emplace_back(std::string(build_composite_index_sql<std::tuple_element_t<Is, Tuple>>()))), ...);
            return result;
        }

        static auto build_all_index_sql() -> std::vector<std::string> {
            auto result         = build_index_sql_vector(std::make_index_sequence<Base::field_count_>{});
            using CompositeIdxs = storm::indexes_t<T>;
            if constexpr (std::tuple_size_v<CompositeIdxs> > 0) {
                auto composite = build_composite_index_sql_vector<CompositeIdxs>(
                        std::make_index_sequence<std::tuple_size_v<CompositeIdxs>>{}
                );
                result
                        .insert(result.end(),
                                std::make_move_iterator(composite.begin()),
                                std::make_move_iterator(composite.end()));
            }
            return result;
        }

        // Pre-computed index SQL strings
        static inline const std::vector<std::string> index_sql_strings_ = build_all_index_sql();

      public:
        // Return the pre-computed CREATE TABLE SQL for the given dialect.
        template <Dialect D = Dialect::SQLite> static auto create_table_sql() -> const std::string& {
            if constexpr (D == Dialect::PostgreSQL) {
                return pg_create_table_sql_string_;
            } else {
                return sqlite_create_table_sql_string_;
            }
        }

        // Return pre-computed CREATE INDEX SQL statements for all indexed/unique/FK fields.
        static auto create_index_sql() -> const std::vector<std::string>& {
            return index_sql_strings_;
        }

        // Execute all CREATE INDEX IF NOT EXISTS statements on the given connection.
        template <db::DatabaseConnection ConnType>
        static auto create_indexes_if_not_exist(std::shared_ptr<ConnType> conn)
                -> std::expected<void, typename ConnType::Error> {
            for (const auto& sql : index_sql_strings_) {
                auto result = conn->execute(sql);
                if (!result) {
                    return result; // LCOV_EXCL_LINE
                } // LCOV_EXCL_LINE
            }
            return {};
        }

        // Execute CREATE TABLE IF NOT EXISTS on the given connection.
        // Selects the appropriate dialect at compile time.
        template <db::DatabaseConnection ConnType>
        static auto create_table_if_not_exists(std::shared_ptr<ConnType> conn)
                -> std::expected<void, typename ConnType::Error> {
            std::string sql;
            if constexpr (requires { ConnType::uses_pg_dialect; }) {
                sql = create_table_sql<Dialect::PostgreSQL>();
            } else {
                sql = create_table_sql<Dialect::SQLite>();
            }

            // Inject IF NOT EXISTS
            const std::string create_prefix = "CREATE TABLE ";
            const std::string if_not_exists = "CREATE TABLE IF NOT EXISTS ";
            if (sql.substr(0, create_prefix.size()) == create_prefix) {
                sql = if_not_exists + sql.substr(create_prefix.size());
            }

            return conn->execute(sql);
        }
    };

} // namespace storm::orm::schema
