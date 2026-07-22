module;

// `duplicate` removed in #277 Phase 3 (shared append_index_sql helper).

#include <meta>

export module storm_orm_schema;

import std;

import storm_orm_statements_base;
import storm_orm_indexes;
import storm_orm_utilities;
import storm_orm_field_attr;
import storm_db_concept;

export namespace storm::orm::schema {

    // SQL dialect for compile-time schema generation
    enum class Dialect : std::uint8_t { SQLite, PostgreSQL };

    // Import utilities for compile-time string building
    using storm::orm::utilities::ConstexprString;

    namespace detail {

        // Storage class — which SQL primitive the C++ inner type maps to. The set is
        // closed; unknown types fall through to `Fallback` (TEXT, untyped).
        enum class StorageClass : std::uint8_t {
            Bool,
            Integer,
            Double,
            Float,
            Text,
            Date,
            DateTime,
            Uuid,
            Blob,
            Fallback,
        };

        // Strip std::optional<T> to T for storage classification. Non-optionals pass
        // through unchanged.
        template <typename T>
        using col_inner_t = std::conditional_t<utilities::is_optional_v<T>, utilities::optional_inner_type_t<T>, T>;

        // Source-type predicates per StorageClass. Combining them as a single named
        // constexpr bool keeps the dispatcher below to one branch per storage class.
        template <typename T>
        constexpr bool is_integer_source_v =
                std::is_same_v<T, int> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, short> ||
                std::is_same_v<T, unsigned int> || std::is_same_v<T, unsigned short> || std::is_same_v<T, long> ||
                std::is_same_v<T, unsigned long> || std::is_same_v<T, long long> ||
                std::is_same_v<T, unsigned long long> || std::is_same_v<T, signed char> ||
                std::is_same_v<T, unsigned char> || std::is_same_v<T, char> || std::is_enum_v<T> ||
                utilities::is_chrono_duration_v<T>;

        template <typename T>
        constexpr bool is_text_source_v = std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> ||
                                          std::is_same_v<T, std::filesystem::path>;

        template <typename T>
        constexpr bool is_blob_source_v =
                std::is_same_v<T, std::vector<std::uint8_t>> || std::is_same_v<T, std::vector<unsigned char>> ||
                std::is_same_v<T, std::vector<std::byte>>;

        // Classify a C++ type (already stripped of optional<>) into one StorageClass.
        // Compile-time only.
        template <typename T> consteval auto storage_class_of() -> StorageClass {
            using enum StorageClass;
            if constexpr (std::is_same_v<T, bool>) {
                return Bool;
            } else if constexpr (is_integer_source_v<T>) {
                return Integer;
            } else if constexpr (std::is_same_v<T, double>) {
                return Double;
            } else if constexpr (std::is_same_v<T, float>) {
                return Float;
            } else if constexpr (is_text_source_v<T>) {
                return Text;
            } else if constexpr (std::is_same_v<T, std::chrono::year_month_day>) {
                return Date;
            } else if constexpr (std::is_same_v<T, std::chrono::system_clock::time_point>) {
                return DateTime;
            } else if constexpr (std::is_same_v<T, utilities::UUID>) {
                return Uuid;
            } else if constexpr (is_blob_source_v<T>) {
                return Blob;
            } else {
                return Fallback;
            }
        }

        // Per-storage-class type-name helpers. Each one returns the bare type string
        // (no NOT NULL) for the given dialect. Splitting these out keeps every helper
        // at one or two if-constexpr branches.
        template <Dialect D> consteval auto bool_type() -> std::string_view {
            return D == Dialect::PostgreSQL ? "BOOLEAN" : "INTEGER";
        }
        template <Dialect D> consteval auto integer_type() -> std::string_view {
            return D == Dialect::PostgreSQL ? "BIGINT" : "INTEGER";
        }
        template <Dialect D> consteval auto double_type() -> std::string_view {
            return D == Dialect::PostgreSQL ? "DOUBLE PRECISION" : "REAL";
        }
        template <Dialect D> consteval auto date_type() -> std::string_view {
            return D == Dialect::PostgreSQL ? "DATE" : "TEXT";
        }
        template <Dialect D> consteval auto datetime_type() -> std::string_view {
            return D == Dialect::PostgreSQL ? "TIMESTAMP" : "TEXT";
        }
        template <Dialect D> consteval auto uuid_type() -> std::string_view {
            return D == Dialect::PostgreSQL ? "UUID" : "TEXT";
        }
        template <Dialect D> consteval auto blob_type() -> std::string_view {
            return D == Dialect::PostgreSQL ? "BYTEA" : "BLOB";
        }

        // Order-preserving full-range unsigned-64 storage (#436): PostgreSQL
        // NUMERIC(20,0) holds the whole 0..2^64-1 range; SQLite TEXT stores a
        // zero-padded 20-char decimal so lexicographic order == numeric order.
        template <Dialect D> consteval auto full_unsigned_type() -> std::string_view {
            return D == Dialect::PostgreSQL ? "NUMERIC(20,0)" : "TEXT";
        }
        template <Dialect D> consteval auto full_unsigned_type_nn() -> std::string_view {
            return D == Dialect::PostgreSQL ? "NUMERIC(20,0) NOT NULL" : "TEXT NOT NULL";
        }

        // NOT NULL counterparts (same dialect dispatch). Splitting them keeps each helper
        // at one ternary, so the outer storage-class dispatcher only counts the one branch
        // per class.
        template <Dialect D> consteval auto bool_type_nn() -> std::string_view {
            return D == Dialect::PostgreSQL ? "BOOLEAN NOT NULL" : "INTEGER NOT NULL";
        }
        template <Dialect D> consteval auto integer_type_nn() -> std::string_view {
            return D == Dialect::PostgreSQL ? "BIGINT NOT NULL" : "INTEGER NOT NULL";
        }
        template <Dialect D> consteval auto double_type_nn() -> std::string_view {
            return D == Dialect::PostgreSQL ? "DOUBLE PRECISION NOT NULL" : "REAL NOT NULL";
        }
        template <Dialect D> consteval auto date_type_nn() -> std::string_view {
            return D == Dialect::PostgreSQL ? "DATE NOT NULL" : "TEXT NOT NULL";
        }
        template <Dialect D> consteval auto datetime_type_nn() -> std::string_view {
            return D == Dialect::PostgreSQL ? "TIMESTAMP NOT NULL" : "TEXT NOT NULL";
        }
        template <Dialect D> consteval auto uuid_type_nn() -> std::string_view {
            return D == Dialect::PostgreSQL ? "UUID NOT NULL" : "TEXT NOT NULL";
        }

        // Pre-baked type strings keyed by StorageClass × Dialect. sql_type_for picks
        // one of these (or its NOT NULL counterpart) per call.
        template <StorageClass C, Dialect D> consteval auto bare_type_for() -> std::string_view {
            using enum StorageClass;
            if constexpr (C == Bool) {
                return bool_type<D>();
            } else if constexpr (C == Integer) {
                return integer_type<D>();
            } else if constexpr (C == Double) {
                return double_type<D>();
            } else if constexpr (C == Float) {
                return "REAL";
            } else if constexpr (C == Text) {
                return "TEXT";
            } else if constexpr (C == Date) {
                return date_type<D>();
            } else if constexpr (C == DateTime) {
                return datetime_type<D>();
            } else if constexpr (C == Uuid) {
                return uuid_type<D>();
            } else if constexpr (C == Blob) {
                return blob_type<D>();
            } else {
                static_assert(C == Fallback, "bare_type_for: unhandled StorageClass");
                return "TEXT";
            }
        }

        template <StorageClass C, Dialect D> consteval auto not_null_type_for() -> std::string_view {
            using enum StorageClass;
            if constexpr (C == Bool) {
                return bool_type_nn<D>();
            } else if constexpr (C == Integer) {
                return integer_type_nn<D>();
            } else if constexpr (C == Double) {
                return double_type_nn<D>();
            } else if constexpr (C == Float) {
                return "REAL NOT NULL";
            } else if constexpr (C == Text) {
                return "TEXT NOT NULL";
            } else if constexpr (C == Date) {
                return date_type_nn<D>();
            } else if constexpr (C == DateTime) {
                return datetime_type_nn<D>();
            } else if constexpr (C == Uuid) {
                return uuid_type_nn<D>();
            } else {
                // Blob and Fallback are always nullable-shaped; no NOT NULL variant needed.
                static_assert(
                        C != StorageClass::Blob && C != StorageClass::Fallback,
                        "not_null_type_for: storage class has no NOT NULL variant"
                );
                return {};
            }
        }

        // Pick the bare or NOT-NULL variant for (StorageClass, Dialect, Nullable).
        // Blob and Fallback always use the bare variant (matches the prior catch-all
        // behaviour for std::vector<...> fields).
        template <StorageClass C, Dialect D, bool Nullable> consteval auto sql_type_for() -> std::string_view {
            if constexpr (Nullable) {
                return bare_type_for<C, D>();
            } else {
                return not_null_type_for<C, D>();
            }
        }

        // Append the decimal representation of a signed integral value (handles
        // the leading '-'; the magnitude reuses ConstexprString::append_uint).
        template <typename SqlT> consteval void append_signed(SqlT& col, std::int64_t value) {
            if (value < 0) {
                col.append("-");
                // -(min) overflows in two's complement; widen through unsigned.
                col.append_uint(static_cast<std::uint64_t>(-(value + 1)) + 1U);
            } else {
                col.append_uint(static_cast<std::uint64_t>(value));
            }
        }

        // Append a compile-time decimal rendering of a double. The values are
        // programmer-written literals (e.g. 0.0, 3.14, -2.5), not arbitrary
        // doubles, so a fixed-precision render with trailing-zero trimming (but
        // always keeping one fractional digit) is sufficient and round-trips
        // these literals. std::to_chars is not constexpr for double in this
        // toolchain, so the digits are extracted by hand.
        template <typename SqlT> consteval void append_double(SqlT& col, double value) {
            if (value < 0) {
                col.append("-");
                value = -value;
            }
            constexpr std::uint64_t scale = 1000000U; // 6 fractional digits
            const auto              whole = static_cast<std::uint64_t>(value);
            // Scale the fraction (always >= 0 here, value was made non-negative
            // above) and round half-up: truncate, then bump when the dropped part
            // is >= 0.5. Avoids the cast-of-(x+0.5) pattern, which mis-rounds only
            // for negative inputs — not possible here.
            const double        frac_scaled = (value - static_cast<double>(whole)) * static_cast<double>(scale);
            const auto          frac_trunc  = static_cast<std::uint64_t>(frac_scaled);
            const std::uint64_t scaled_frac =
                    frac_trunc + ((frac_scaled - static_cast<double>(frac_trunc)) >= 0.5 ? 1U : 0U);
            // Carry a rounded-up fraction (e.g. 0.9999995) into the whole part.
            const std::uint64_t carry = scaled_frac / scale;
            std::uint64_t       frac  = scaled_frac % scale;
            col.append_uint(whole + carry);
            col.append(".");
            // Render exactly 6 fractional digits, then trim trailing zeros while
            // keeping at least one digit after the point.
            std::array<char, 6> digits{};
            for (std::size_t i = 6; i > 0; --i) {
                digits[i - 1] = static_cast<char>('0' + (frac % 10));
                frac /= 10;
            }
            std::size_t last = 5;
            while (last > 0 && digits[last] == '0') {
                --last;
            }
            for (std::size_t i = 0; i <= last; ++i) {
                col.append(std::string_view{&digits[i], 1});
            }
        }

        // Append a SQL single-quoted text literal, escaping embedded quotes by
        // doubling them ("O'Brien" -> 'O''Brien'). Same escaping in both dialects.
        template <typename SqlT> consteval void append_sql_text(SqlT& col, std::string_view value) {
            col.append("'");
            for (char c : value) {
                if (c == '\'') {
                    col.append("''");
                } else {
                    col.append(std::string_view{&c, 1});
                }
            }
            col.append("'");
        }

        // Emit the " DEFAULT <value>" clause for a NOT NULL column that carries a
        // C++ default-member-initializer, recovering the value from a
        // default-constructed Owner (P2996 exposes whether a default exists, not
        // its value). Covers bool/int/int64/double/float/string; other storage
        // classes (date, blob, uuid, fallback) emit nothing and keep their prior
        // shape. Generalizes the bool-only #344 clause — see issue #413. Without
        // this, `ALTER TABLE ... ADD COLUMN <x> NOT NULL` is rejected on a
        // populated table ("Cannot add a NOT NULL column with default value NULL").
        // True when Owner can be default-constructed in a constant expression —
        // the prerequisite for recovering a field's default value. Models with a
        // non-literal member (e.g. an m2m `plf::hive<...>`/`std::vector<...>`
        // field) are not literal types; their scalar defaults cannot be read at
        // compile time, so they keep the no-DEFAULT shape (these are the
        // relationship-bearing models, not the plain scalar tables that ALTER ADD
        // COLUMN migrations target).
        template <typename Owner>
        constexpr bool owner_constexpr_constructible = requires { typename std::bool_constant<(Owner{}, true)>; };

        // Render the bool DEFAULT literal for the dialect (split out so the
        // dispatcher below stays at a single branch per storage class).
        template <Dialect D, typename SqlT> consteval void append_bool_default(SqlT& col, bool value) {
            if constexpr (D == Dialect::PostgreSQL) {
                col.append(value ? " DEFAULT TRUE" : " DEFAULT FALSE");
            } else {
                col.append(value ? " DEFAULT 1" : " DEFAULT 0");
            }
        }

        template <typename Owner, std::meta::info Member, Dialect D, typename SqlT>
        consteval void append_default_clause(SqlT& col) {
            using enum StorageClass;
            using FieldType            = std::remove_cvref_t<typename[:std::meta::type_of(Member):]>;
            constexpr StorageClass cls = storage_class_of<FieldType>();
            if constexpr (!std::meta::has_default_member_initializer(Member) || !owner_constexpr_constructible<Owner>) {
                return;
            } else {
                constexpr Owner def{};
                if constexpr (cls == Bool) {
                    append_bool_default<D>(col, def.[:Member:]);
                } else if constexpr (cls == Integer) {
                    col.append(" DEFAULT ");
                    append_signed(col, static_cast<std::int64_t>(def.[:Member:]));
                } else if constexpr (cls == Double || cls == Float) {
                    col.append(" DEFAULT ");
                    append_double(col, static_cast<double>(def.[:Member:]));
                } else if constexpr (cls == Text) {
                    col.append(" DEFAULT ");
                    append_sql_text(col, std::string_view{def.[:Member:]});
                }
            }
        }

        // A length-counting sink mirroring the ConstexprString append/append_uint
        // interface used by append_default_clause. Running the same clause builder
        // against it yields the exact rendered length, so the column buffer budget
        // stays in lock-step with the emitted SQL (no hand-maintained constant).
        struct ClauseSizer {
            std::size_t    len = 0;
            consteval void append(const char* str) {
                while (*str != '\0') {
                    ++len;
                    ++str;
                }
            }
            consteval void append(std::string_view str) {
                len += str.size();
            }
            consteval void append_uint(std::uint64_t value) {
                ++len; // at least one digit
                while (value > utilities::numeric::MAX_SINGLE_DIGIT) {
                    value /= 10;
                    ++len;
                }
            }
        };

        // Rendered length of the " DEFAULT <value>" clause for one member (0 when
        // the member carries no default initializer or its type is out of scope).
        template <typename Owner, std::meta::info Member, Dialect D>
        consteval auto default_clause_len() -> std::size_t {
            ClauseSizer sizer;
            append_default_clause<Owner, Member, D>(sizer);
            return sizer.len;
        }

        // Emit the type portion for a max_length<N> text field (#493).
        //   PostgreSQL: "VARCHAR(N)[ NOT NULL]" — a real bounded type.
        //   SQLite:     "TEXT[ NOT NULL]" — the bound is a trailing CHECK, appended
        //               later by append_max_length_check (so the DEFAULT clause can sit
        //               between the type and the CHECK).
        // Nullable fields omit NOT NULL; the SQLite CHECK passes on NULL either way.
        template <Dialect D, bool Nullable, typename SqlT>
        consteval void append_max_length_type(SqlT& col, std::size_t bound) {
            if constexpr (D == Dialect::PostgreSQL) {
                col.append("VARCHAR(");
                col.append_uint(bound);
                col.append(")");
            } else {
                col.append("TEXT");
            }
            if constexpr (!Nullable) {
                col.append(" NOT NULL");
            }
        }

        // Append the SQLite length CHECK for a max_length<N> text field:
        //   " CHECK(length(<name>) <= N)". No-op on PostgreSQL (VARCHAR(N) already bounds
        //   the column). Passes when the value is NULL (standard SQL), so nullable+bounded
        //   works correctly.
        template <Dialect D, typename SqlT>
        consteval void append_max_length_check(SqlT& col, std::string_view name, std::size_t bound) {
            if constexpr (D == Dialect::PostgreSQL) {
                return;
            } else {
                col.append(" CHECK(length(");
                col.append(name);
                col.append(") <= ");
                col.append_uint(bound);
                col.append(")");
            }
        }

        // Rendered length of the max_length clause(s) for one field (0 when the field
        // carries no max_length annotation). Measured by rendering into the counting sink,
        // so the column budget stays byte-exact regardless of dialect or bound magnitude:
        //   PG:     len("VARCHAR(N)") - len("TEXT")   (the type grows by this much)
        //   SQLite: len(" CHECK(length(<name>) <= N)") (a trailing clause)
        template <Dialect D> consteval auto max_length_clause_len(std::meta::info member) -> std::size_t {
            const auto bound = storm::meta::max_length_of(member);
            if (!bound.has_value()) {
                return 0;
            }
            ClauseSizer sizer;
            if constexpr (D == Dialect::PostgreSQL) {
                // "VARCHAR(N)" replaces "TEXT" (4 chars) — count only the net growth.
                sizer.append("VARCHAR(");
                sizer.append_uint(bound.value());
                sizer.append(")");
                return sizer.len >= 4 ? sizer.len - 4 : 0;
            } else {
                append_max_length_check<D>(sizer, std::meta::identifier_of(member), bound.value());
                return sizer.len;
            }
        }

        // Map a C++ field type to its SQL column definition string for the given dialect.
        // Returns the column type portion (after the column name).
        // Two-axis dispatch:
        //   1. col_inner_t<FieldType>   strips std::optional<> to the inner type
        //   2. storage_class_of<…>()    classifies the inner type into one of the
        //                               StorageClass tags
        //   3. sql_type_for<C, D, N>()  returns the SQL fragment for that
        //                               (storage class, dialect, nullable) triple
        // The nullable flag drives the `NOT NULL` suffix; Blob is always nullable-shaped
        // (matches the prior catch-all behaviour for vector<...> fields).
        template <typename FieldType, Dialect D = Dialect::SQLite> consteval auto sql_col_def() -> std::string_view {
            constexpr StorageClass cls = storage_class_of<col_inner_t<FieldType>>();
            constexpr bool         nullable =
                    utilities::is_optional_v<FieldType> || cls == StorageClass::Blob || cls == StorageClass::Fallback;
            return sql_type_for<cls, D, nullable>();
        }

    } // namespace detail

    template <typename T> class SchemaStatement : private storm::orm::statements::BaseStatement<T> {
        using Base = storm::orm::statements::BaseStatement<T>;

        // Per-column buffer size — derived from the same data the table-level
        // estimator uses (max reflected identifier length + the largest fixed
        // suffix budget) so a long field name can never truncate (#361). The
        // table estimator (calculate_column_defs_size) already grows with the
        // identifier length; this keeps the per-column buffer in lockstep.
        // " REFERENCES <Related>(id)" clause length for one FK field at compile-time index,
        // or 0 if the field is not an FK. The related table is the FK field's C++ type
        // identifier (optional-unwrapped); 12 = len(" REFERENCES "), 4 = len("(id)").
        // Index-templated (not a runtime loop) so the FK check and the type splice both run
        // in a constant context — annotation_of_type on a runtime-loop member info reads
        // empty across a BMI boundary (#262), which would silently under-size the buffer.
        template <std::size_t Index> static consteval auto fk_references_len() -> std::size_t {
            constexpr auto member = Base::all_members_[Index];
            if constexpr (Base::is_fk_field(member)) {
                using FieldType   = std::remove_cvref_t<typename[:std::meta::type_of(member):]>;
                using RelatedType = detail::col_inner_t<FieldType>;
                std::size_t len   = 12 + std::meta::identifier_of(std::meta::dealias(^^RelatedType)).size() + 4;
                // " ON DELETE <action>" when the FK carries an on_delete policy (#431):
                // 11 = len(" ON DELETE ") + the action keyword (NO ACTION = 9 is longest).
                if constexpr (constexpr auto action = statements::meta::fk_on_delete_action_of(member);
                              action.has_value()) {
                    len += 11 + statements::meta::ref_action_sql(action.value()).size();
                }
                return len;
            } else {
                return 0;
            }
        }

        // Longest " REFERENCES <Related>(id)" clause across this model's FK fields (#412).
        // Zero when the model has no FK fields, so non-FK models keep their prior buffer size.
        static consteval auto max_fk_references_len() -> std::size_t {
            return [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                std::size_t longest = 0;
                ((longest = std::max(longest, fk_references_len<Is>())), ...);
                return longest;
            }(std::make_index_sequence<Base::field_count_>{});
        }

        // Longest " DEFAULT <value>" clause across the model's fields, measured by
        // rendering each field's clause into the counting sink — keeps the column
        // budget exact for non-bool defaults (#413), where the clause length is
        // value-driven (e.g. a long string default), not a fixed 14 chars.
        template <Dialect D> static consteval auto max_default_clause_len() -> std::size_t {
            return [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                std::size_t longest = 0;
                ((longest = std::max(longest, detail::default_clause_len<T, Base::all_members_[Is], D>())), ...);
                return longest;
            }(std::make_index_sequence<Base::field_count_>{});
        }

        // Longest max_length<N> clause across the model's fields (#493), measured by
        // rendering — PG's VARCHAR(N) net growth over TEXT, or SQLite's trailing
        // CHECK(length(<name>) <= N). Zero for models with no max_length field, so their
        // column budget is unchanged. Added to regular_suffix below.
        template <Dialect D> static consteval auto max_max_length_clause_len() -> std::size_t {
            return [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                std::size_t longest = 0;
                ((longest = std::max(longest, detail::max_length_clause_len<D>(Base::all_members_[Is]))), ...);
                return longest;
            }(std::make_index_sequence<Base::field_count_>{});
        }

        // Per-column byte budgets shared by the buffer sizer (col_def_buffer) and the
        // CREATE TABLE size estimator (calculate_column_defs_size). Both must agree on
        // every constant or the buffer drifts from the emitted SQL — keep them in one
        // place (DRY). `pk_size`/`fk_suffix`/`regular_suffix` are the three column shapes;
        // `regular_suffix` already folds in the per-model widest DEFAULT clause (#413).
        struct ColumnSizeBudget {
            std::size_t pk_size;
            std::size_t fk_suffix;
            std::size_t regular_suffix;
        };
        template <Dialect D = Dialect::SQLite> static consteval auto column_size_budget() -> ColumnSizeBudget {
            using enum Dialect;
            // PG PK: "id BIGINT PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY" = 54
            // SQLite PK: "id INTEGER PRIMARY KEY AUTOINCREMENT" = 36
            constexpr std::size_t pk_size = (D == PostgreSQL) ? 54 : 36;
            // " " + max type def ("DOUBLE PRECISION NOT NULL"=25 PG / "INTEGER NOT NULL"=16
            // SQLite) + " DEFAULT <value>" (measured, #413) + max_length clause (measured,
            // #493) + " UNIQUE" (7)
            constexpr std::size_t max_type_def = (D == PostgreSQL) ? 25 : 16;
            constexpr std::size_t regular_suffix =
                    1 + max_type_def + max_default_clause_len<D>() + max_max_length_clause_len<D>() + 7;
            // FK suffix: "_id INTEGER NOT NULL" (20) / "_id BIGINT NOT NULL" (19),
            // plus the longest " REFERENCES <Related>(id)" clause across this model (#412).
            constexpr std::size_t fk_suffix = 3 + ((D == PostgreSQL) ? 16 : 20) + max_fk_references_len();
            return {.pk_size = pk_size, .fk_suffix = fk_suffix, .regular_suffix = regular_suffix};
        }

        template <Dialect D = Dialect::SQLite> static consteval auto col_def_buffer() -> std::size_t {
            constexpr auto        budget = column_size_budget<D>();
            constexpr std::size_t max_fixed_suffix =
                    std::max({budget.pk_size, budget.regular_suffix, budget.fk_suffix});

            std::size_t max_name_len = 0;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                max_name_len = std::max(max_name_len, std::meta::identifier_of(Base::all_members_[i]).size());
            }
            // +2 for the trailing '\0' guard in ConstexprString (len < N - 1): a column
            // whose name IS the longest field and is itself an FK (e.g. CascadeChild::owner
            // with an ON DELETE clause, #431) makes max_name_len + max_fixed_suffix an exact
            // fit, so N must exceed len by 2, not 1.
            return max_name_len + max_fixed_suffix + 2;
        }

        // True when the model's PK opted into the SQLite never-reuse guarantee
        // via storm::primary_autoincrement (#379). Drives the AUTOINCREMENT keyword.
        static consteval auto pk_wants_autoincrement() -> bool {
            return storm::meta::is_primary_autoincrement(Base::primary_key_);
        }

        // Append an FK column definition: "<name>_id INTEGER/BIGINT [NOT NULL] REFERENCES
        // <Related>(id) [ON DELETE <action>]". The REFERENCES clause enforces referential
        // integrity (#412): the related table is the FK field's C++ type identifier
        // (optional-unwrapped) and the related PK is always "id". An unannotated FK emits
        // plain REFERENCES (= ON DELETE NO ACTION / RESTRICT, the SQL default); an
        // fk<RefAction::...> policy (#431) appends the chosen ON DELETE clause.
        // Extracted from build_column_def to keep it single-purpose.
        template <std::meta::info Member, Dialect D, typename SqlT>
        static consteval void append_fk_column_def(SqlT& col) {
            using FieldType   = std::remove_cvref_t<typename[:std::meta::type_of(Member):]>;
            using RelatedType = detail::col_inner_t<FieldType>;
            col.append(std::meta::identifier_of(Member));
            // [nullable][pg] → fk column suffix; avoids branch-clone and nested ternary
            constexpr std::array<std::array<std::string_view, 2>, 2> fk_suffixes = {{
                    {"_id INTEGER NOT NULL", "_id BIGINT NOT NULL"},
                    {"_id INTEGER", "_id BIGINT"},
            }};
            constexpr auto fk_nullable = static_cast<int>(storm::orm::utilities::is_optional_v<FieldType>);
            constexpr auto fk_pg       = static_cast<int>(D == Dialect::PostgreSQL);
            col.append(fk_suffixes[fk_nullable][fk_pg]);
            col.append(" REFERENCES ");
            col.append(std::meta::identifier_of(std::meta::dealias(^^RelatedType)));
            col.append("(id)");
            // Per-FK ON DELETE policy (#431): emitted only when the field's fk<RefAction>
            // is a non-default action, so bare fk<>/fk<Restrict> stay byte-identical.
            if constexpr (constexpr auto action = statements::meta::fk_on_delete_action_of(Member);
                          action.has_value()) {
                col.append(" ON DELETE ");
                col.append(statements::meta::ref_action_sql(action.value()));
            }
        }

        // Append a regular (non-PK, non-FK, non-full_unsigned) column definition, with the
        // optional UNIQUE constraint. Handles the max_length<N> text bound (#493): PG swaps
        // TEXT for VARCHAR(N); SQLite keeps TEXT and appends a trailing
        // CHECK(length(<name>) <= N). SQLite emission order is
        //   <name> TEXT [NOT NULL] [DEFAULT v] [CHECK(...)] [UNIQUE]
        // — the DEFAULT sits between the type and the CHECK, matching #413. Extracted from
        // build_column_def to keep the regular and unique branches from duplicating.
        template <std::meta::info Member, Dialect D, bool Unique, typename SqlT>
        static consteval void append_regular_column_def(SqlT& col) {
            using FieldType = std::remove_cvref_t<typename[:std::meta::type_of(Member):]>;
            col.append(std::meta::identifier_of(Member));
            col.append(" ");
            if constexpr (constexpr auto bound = storm::meta::max_length_of(Member); bound.has_value()) {
                constexpr bool nullable = storm::orm::utilities::is_optional_v<FieldType>;
                detail::append_max_length_type<D, nullable>(col, bound.value());
                detail::append_default_clause<T, Member, D>(col);
                detail::append_max_length_check<D>(col, std::meta::identifier_of(Member), bound.value());
            } else {
                col.append(detail::sql_col_def<FieldType, D>());
                detail::append_default_clause<T, Member, D>(col);
            }
            if constexpr (Unique) {
                col.append(" UNIQUE");
            }
        }

        // Append the column-level primary-key definition of a SINGLE-PK model. PG ignores
        // the SQLite-only AUTOINCREMENT opt-in (#379). SQLite default: plain INTEGER
        // PRIMARY KEY auto-assigns ids (aliases rowid) without the per-insert
        // sqlite_sequence cost; AUTOINCREMENT (never-reuse) is opt-in via
        // storm::primary_autoincrement.
        //
        // The column name is hardcoded "id" rather than read from the member — a latent
        // bug for a single PK named anything else, filed separately as #506. Extracted
        // here unchanged so the composite path (#500) sits beside it rather than layered
        // on top of it; fixing #506 is a one-line change to this helper.
        template <Dialect D, typename SqlT> static consteval void append_single_pk_column_def(SqlT& col) {
            if constexpr (D == Dialect::PostgreSQL) {
                col.append("id BIGINT PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY");
            } else if constexpr (pk_wants_autoincrement()) {
                col.append("id INTEGER PRIMARY KEY AUTOINCREMENT");
            } else {
                col.append("id INTEGER PRIMARY KEY");
            }
        }

        // Build column definition for field at compile-time index
        template <std::size_t Index, Dialect D = Dialect::SQLite> static consteval auto build_column_def() {
            ConstexprString<col_def_buffer<D>()> col;
            constexpr auto                       member = Base::all_members_[Index];

            // Column-level primary key — SINGLE-PK models only. A composite key (#500) is
            // emitted once as a table-level PRIMARY KEY (a, b) clause after all columns
            // (see append_composite_pk_clause), so each of its parts falls through to the
            // regular-column branch below: a part genuinely IS an ordinary column here.
            // The has_composite_pk_ guard also keeps composite models away from the
            // hardcoded "id" name this branch uses. Autoincrement cannot reach a composite
            // model at all: ModelPrimaryKeyValid rejects primary_autoincrement combined
            // with primary_part, since SQL cannot spell an autoincrementing column inside
            // a multi-column key on either backend.
            if constexpr (!Base::has_composite_pk_ && Base::all_members_[Index] == Base::primary_key_) {
                append_single_pk_column_def<D>(col);
            }
            // FK field — "<name>_id INTEGER/BIGINT [NOT NULL] REFERENCES <Related>(id)" (#412).
            else if constexpr (Base::is_fk_field(member)) {
                append_fk_column_def<member, D>(col);
            }
            // full_unsigned field (#436) — order-preserving NUMERIC(20,0)/TEXT, regardless
            // of the C++ uint64 type. Checked before the type-driven branches because the
            // storage class comes from the annotation, not storage_class_of<FieldType>.
            else if constexpr (storm::meta::has_full_unsigned_attr(member)) {
                using FieldType = std::remove_cvref_t<typename[:std::meta::type_of(member):]>;
                col.append(std::meta::identifier_of(member));
                col.append(" ");
                if constexpr (storm::orm::utilities::is_optional_v<FieldType>) {
                    col.append(detail::full_unsigned_type<D>());
                } else {
                    col.append(detail::full_unsigned_type_nn<D>());
                }
            }
            // Unique field — same as regular but with UNIQUE constraint
            else if constexpr (Base::is_unique_field(member)) {
                append_regular_column_def<member, D, /*Unique=*/true>(col);
            }
            // Regular field
            else {
                append_regular_column_def<member, D, /*Unique=*/false>(col);
            }
            // Backstop (#361): col_def_buffer<D>() is sized to fit every column,
            // so this can only fire if the suffix budget above drifts. In a
            // consteval context a hit makes compilation fail (no silent
            // truncation), which is exactly the guarantee we want.
            if (col.len >= col_def_buffer<D>() - 1) {
                std::unreachable();
            }
            return col;
        }

        // Compile-time SQL size calculation for CREATE TABLE
        template <Dialect D = Dialect::SQLite> static consteval auto calculate_column_defs_size() -> std::size_t {
            constexpr auto budget = column_size_budget<D>();

            std::size_t size = 0;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                if (i > 0) {
                    size += 2; // ",\n"
                }
                size += 4; // "    " indent

                // The composite guard replaces only the PK case (#500): a composite model
                // has no column-level PK column, so every column is sized by its own kind.
                // It must NOT collapse the FK case — an FK column emits the far longer
                // "... REFERENCES <R>(id) [ON DELETE ...]", and crediting it the regular
                // suffix under-counts, which ConstexprString would silently truncate.
                if (!Base::has_composite_pk_ && Base::all_members_[i] == Base::primary_key_) {
                    size += budget.pk_size;
                } else {
                    size += std::meta::identifier_of(Base::all_members_[i]).size();
                    if (statements::meta::is_fk_field(Base::all_members_[i])) {
                        size += budget.fk_suffix;
                    } else {
                        size += budget.regular_suffix;
                    }
                }
            }
            return size;
        }

        // Bytes the trailing table-level PRIMARY KEY (a, b) clause of a composite-PK
        // model adds (#500), 0 for a single-PK model: ",\n    PRIMARY KEY (" + parts +
        // ", " separators + ")". Same shape as the junction-table clause at
        // build_junction_sql, which is the pattern this reuses.
        static consteval auto composite_pk_clause_size() -> std::size_t {
            if (!Base::has_composite_pk_) {
                return 0;
            }
            std::size_t size = 20; // ",\n    PRIMARY KEY (" (19) + ")" (1)
            for (const auto member : Base::primary_key_members_) {
                // column_name_size, not identifier_of: an FK part's COLUMN is "<name>_id"
                // (#422). Over-counts the ", " separators by one (N vs N-1), which is
                // deliberate slack.
                size += statements::meta::column_name_size(member) + 2;
            }
            return size;
        }

        // Append the table-level PRIMARY KEY (a, b) of a composite-PK model (#500), in
        // declaration order — column order is semantically significant for the index the
        // key creates. Emitted once, after every column, exactly like the junction-table
        // DDL. No-op for a single-PK model, whose key is column-level.
        //
        // Parts are written with the canonical column-name writer (#422), not
        // identifier_of: an FK part's column is "<name>_id", and naming the bare member
        // would emit a PRIMARY KEY over a column that does not exist. A composite key
        // whose parts are FKs is the canonical reason composite keys exist, so this path
        // is load-bearing, not defensive.
        template <typename SqlT> static consteval void append_composite_pk_clause(SqlT& sql) {
            if constexpr (Base::has_composite_pk_) {
                sql.append(",\n    PRIMARY KEY (");
                bool first_part = true;
                for (const auto member : Base::primary_key_members_) {
                    if (!first_part) {
                        sql.append(", ");
                    }
                    statements::meta::append_column_name(sql, member);
                    first_part = false;
                }
                sql.append(")");
            }
        }

        template <Dialect D = Dialect::SQLite> static consteval auto calculate_create_table_sql_size() -> std::size_t {
            return 13 + Base::table_name_.size() + 3 + calculate_column_defs_size<D>() + composite_pk_clause_size() + 2;
        }

        // Build the full CREATE TABLE SQL at compile-time using index sequence fold
        template <Dialect D = Dialect::SQLite, std::size_t... Is>
        static consteval auto build_sql_impl(std::index_sequence<Is...> /*unused*/) {
            constexpr std::size_t     sql_size = calculate_create_table_sql_size<D>() + utilities::sql_len::XL_BUFFER;
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

            append_composite_pk_clause(sql);
            sql.append("\n)");
            // Whole-SQL backstop, mirroring the per-column one in build_column_def (#361).
            // The per-column check cannot catch a shortfall in calculate_column_defs_size
            // itself: ConstexprString::append silently drops overflow, so an under-count
            // there would truncate the CREATE TABLE with no diagnostic. In a consteval
            // context a hit fails the build instead.
            if (sql.len >= sql_size - 1) {
                std::unreachable();
            }
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

        // Index SQL buffer sized from reflected lengths (#355, mirrors col_def_buffer)
        // so a long table/field name can never silently truncate. Sums EVERY field
        // name x2 (+"_id"+", " worst case), strictly bounding both the single-field
        // and composite builders; 64 covers table*2 + "CREATE UNIQUE INDEX ... ON
        // (...)" scaffolding + '\0'. Correct-by-construction — no truncation possible.
        static consteval auto index_sql_buffer() -> std::size_t {
            std::size_t names = 0;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                names += (std::meta::identifier_of(Base::all_members_[i]).size() + 3 + 2) * 2;
            }
            return (Base::table_name_.size() * 2) + names + 64;
        }
        static constexpr std::size_t INDEX_SQL_BUFFER = index_sql_buffer();

        // Append the shared tail of a CREATE INDEX statement:
        //   <prefix>idx_<table>_<field><suffix> ON <table>(<field><column_suffix>)
        // The three callers differ only in the leading keyword ("CREATE INDEX" vs
        // "CREATE UNIQUE INDEX") and in whether FK fields get the "_id" decoration.
        template <typename SqlT>
        static consteval void append_index_sql(
                SqlT& sql, std::string_view prefix, std::string_view field_name, std::string_view column_suffix
        ) {
            sql.append(prefix);
            sql.append(Base::table_name_);
            sql.append("_");
            sql.append(field_name);
            sql.append(column_suffix);
            sql.append(" ON ");
            sql.append(Base::table_name_);
            sql.append("(");
            sql.append(field_name);
            sql.append(column_suffix);
            sql.append(")");
        }

        // Build CREATE INDEX SQL for a single field at compile-time
        template <std::size_t Index> static consteval auto build_create_index_sql() {
            ConstexprString<INDEX_SQL_BUFFER> sql;
            constexpr auto                    member = Base::all_members_[Index];

            if constexpr (!Base::needs_index(member)) {
                return sql;
            } else if constexpr (Base::is_unique_field(member)) {
                append_index_sql(sql, "CREATE UNIQUE INDEX IF NOT EXISTS idx_", std::meta::identifier_of(member), "");
            } else if constexpr (Base::is_fk_field(member)) {
                append_index_sql(sql, "CREATE INDEX IF NOT EXISTS idx_", std::meta::identifier_of(member), "_id");
            } else {
                append_index_sql(sql, "CREATE INDEX IF NOT EXISTS idx_", std::meta::identifier_of(member), "");
            }
            return sql;
        }

        // Count how many fields need indexes at compile-time
        static consteval auto count_indexes() -> std::size_t {
            std::size_t count = 0;
            for (std::size_t i = 0; i < Base::field_count_; ++i) {
                if (Base::needs_index(Base::all_members_[i])) {
                    ++count;
                }
            }
            return count;
        }

        // Collect all index SQL strings into a vector at runtime (from constexpr data)
        template <std::size_t... Is>
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
            for (std::size_t i = 0; i < IdxType::fields.size(); ++i) {
                sql.append("_");
                sql.append(std::meta::identifier_of(IdxType::fields[i]));
            }
            sql.append(" ON ");
            sql.append(Base::table_name_);
            sql.append("(");
            for (std::size_t i = 0; i < IdxType::fields.size(); ++i) {
                if (i > 0) {
                    sql.append(", ");
                }
                storm::meta::append_column_name(sql, IdxType::fields[i]); // #422 — emits FK "_id"
            }
            sql.append(")");
            return sql;
        }

        // Collect composite index SQL strings into a vector
        template <typename Tuple, std::size_t... Is>
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

        // =====================================================================
        // AUTO-JUNCTION TABLE for many_to_many fields without a through model (#203)
        // =====================================================================

        // Scans RAW members — all_members_ excludes m2m fields by design.
        // A model may carry several auto-junction m2m fields (#392), each one
        // producing its own junction table.
        static constexpr std::size_t m2m_auto_count_ = []() consteval {
            std::size_t count = 0;
            for (auto member : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                count += statements::meta::is_m2m_auto(member) ? 1U : 0U;
            }
            return count;
        }();

        static constexpr auto m2m_auto_members_ = []() consteval {
            std::array<std::meta::info, m2m_auto_count_> members{};
            std::size_t                                  idx = 0;
            for (auto member : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                if (statements::meta::is_m2m_auto(member)) {
                    members[idx++] = member;
                }
            }
            return members;
        }();

        // Append "FOREIGN KEY (<name>_id) REFERENCES <name>(id) ON DELETE <action>" for one
        // junction side (#412). The default action is CASCADE — an orphaned junction row is
        // meaningless once its owner/related entity is gone — but it is overridable per m2m
        // field via many_to_many<RefAction::...> (#431), applied to both sides.
        template <typename SqlT>
        static consteval void append_junction_fk(SqlT& sql, std::string_view name, std::string_view action) {
            sql.append("FOREIGN KEY (");
            sql.append(name);
            sql.append("_id) REFERENCES ");
            sql.append(name);
            sql.append("(id) ON DELETE ");
            sql.append(action);
        }

        // Junction DDL: <T>_<Related> (<T>_id, <Related>_id, PRIMARY KEY (both),
        // FOREIGN KEY each side ON DELETE <action>). The composite PK rejects duplicate
        // relation pairs and doubles as the index; the FKs enforce referential integrity (#412).
        // The ON DELETE action is CASCADE unless the m2m field carries
        // many_to_many<RefAction::...> (#431), which overrides BOTH sides.
        template <Dialect D, std::meta::info Member> static consteval auto build_junction_sql() {
            constexpr auto related      = statements::meta::related_type_from_container(std::meta::type_of(Member));
            constexpr auto owner_name   = std::meta::identifier_of(^^T);
            constexpr auto related_name = std::meta::identifier_of(related);
            constexpr std::string_view id_type =
                    (D == Dialect::PostgreSQL) ? "_id BIGINT NOT NULL" : "_id INTEGER NOT NULL";
            constexpr std::string_view action =
                    statements::meta::ref_action_sql(statements::meta::m2m_junction_on_delete_of(Member));
            // owner/related names each appear 5× (column ×2, PK ×2, FK clause once more);
            // 256 fixed covers the CREATE/FOREIGN KEY/REFERENCES/ON DELETE <action> scaffolding.
            ConstexprString<((owner_name.size() + related_name.size()) * 5) + 256> sql;
            sql.append("CREATE TABLE ");
            sql.append(owner_name);
            sql.append("_");
            sql.append(related_name);
            sql.append(" (\n    ");
            sql.append(owner_name);
            sql.append(id_type);
            sql.append(",\n    ");
            sql.append(related_name);
            sql.append(id_type);
            sql.append(",\n    PRIMARY KEY (");
            sql.append(owner_name);
            sql.append("_id, ");
            sql.append(related_name);
            sql.append("_id),\n    ");
            append_junction_fk(sql, owner_name, action);
            sql.append(",\n    ");
            append_junction_fk(sql, related_name, action);
            sql.append("\n)");
            return sql;
        }

        // Rewrites "CREATE TABLE " → "CREATE TABLE IF NOT EXISTS " (idempotent DDL).
        static auto with_if_not_exists(std::string sql) -> std::string {
            const std::string create_prefix = "CREATE TABLE ";
            if (sql.starts_with(create_prefix)) {
                sql.insert(create_prefix.size(), "IF NOT EXISTS ");
            }
            return sql;
        }

      public:
        // True when T has a many_to_many field with an auto-generated junction table.
        static constexpr bool has_m2m_junction_ = m2m_auto_count_ > 0;

        // Pre-computed junction CREATE TABLE SQL statements for the given
        // dialect — one per auto-junction m2m field, in declaration order
        // (#203 single relation; #392 several).
        template <Dialect D = Dialect::SQLite>
        static auto junction_table_sqls() -> const std::vector<std::string>&
            requires has_m2m_junction_
        {
            static const std::vector<std::string> sqls = []() {
                std::vector<std::string> out;
                out.reserve(m2m_auto_count_);
                [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                    (out.emplace_back(std::string(build_junction_sql<D, m2m_auto_members_[Is]>())), ...);
                }(std::make_index_sequence<m2m_auto_count_>{});
                return out;
            }();
            return sqls;
        }

        // Pre-computed junction CREATE TABLE SQL for the first (often only)
        // auto-junction m2m field (#203).
        template <Dialect D = Dialect::SQLite>
        static auto junction_table_sql() -> const std::string&
            requires has_m2m_junction_
        {
            return junction_table_sqls<D>()[0];
        }
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
        [[nodiscard]] static auto create_indexes_if_not_exist(std::shared_ptr<ConnType> conn)
                -> std::expected<void, typename ConnType::Error> {
            for (const auto& sql : index_sql_strings_) {
                auto result = conn->execute(sql);
                if (!result) {
                    return result;
                }
            }
            return {};
        }

        // Create each FK-referenced parent table before T's own table (#412). With
        // always-on REFERENCES a table cannot be created before its FK target exists
        // (PG rejects it; SQLite tolerated it). `done` is the set of already-created
        // table names — it makes the recursion idempotent and cycle-safe (self-FK,
        // mutual FK). Index fold so each FK target type is recursed at compile time.
        template <db::DatabaseConnection ConnType>
        [[nodiscard]] static auto
        create_fk_parents(std::shared_ptr<ConnType> conn, std::set<std::string, std::less<>>& done)
                -> std::expected<void, typename ConnType::Error> {
            std::expected<void, typename ConnType::Error> result{};
            [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                (
                        [&] {
                            if constexpr (Base::is_fk_field(Base::all_members_[Is])) {
                                using FieldType =
                                        std::remove_cvref_t<typename[:std::meta::type_of(Base::all_members_[Is]):]>;
                                using RelatedType = detail::col_inner_t<FieldType>;
                                result            = result.and_then([&] {
                                    return SchemaStatement<RelatedType>::create_table_if_not_exists(conn, done);
                                });
                            }
                        }(),
                        ...
                );
            }(std::make_index_sequence<Base::field_count_>{});
            return result;
        }

        // Create the related model's table for each auto-junction m2m field (#412). The
        // junction FK-references both T (already created) and the related model, so the
        // related table must exist before the junction DDL runs. Recurses through the same
        // `done` cycle guard. Only instantiated when has_m2m_junction_.
        template <db::DatabaseConnection ConnType>
        [[nodiscard]] static auto
        create_m2m_related_tables(std::shared_ptr<ConnType> conn, std::set<std::string, std::less<>>& done)
                -> std::expected<void, typename ConnType::Error>
            requires has_m2m_junction_
        {
            std::expected<void, typename ConnType::Error> result{};
            [&]<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
                (
                        [&] {
                            using Container =
                                    std::remove_cvref_t<typename[:std::meta::type_of(m2m_auto_members_[Is]):]>;
                            using RelatedType = statements::meta::m2m_related_t<Container>;
                            result            = result.and_then([&] {
                                return SchemaStatement<RelatedType>::create_table_if_not_exists(conn, done);
                            });
                        }(),
                        ...
                );
            }(std::make_index_sequence<m2m_auto_count_>{});
            return result;
        }

        // Execute CREATE TABLE IF NOT EXISTS on the given connection (recursion overload).
        // `done` tracks tables already created in this call tree (cycle/idempotency guard).
        template <db::DatabaseConnection ConnType>
        [[nodiscard]] static auto
        create_table_if_not_exists(std::shared_ptr<ConnType> conn, std::set<std::string, std::less<>>& done)
                -> std::expected<void, typename ConnType::Error> {
            constexpr Dialect dialect = db::SupportsPgDialect<ConnType> ? Dialect::PostgreSQL : Dialect::SQLite;

            // Already created in this call tree — break FK cycles and avoid redundant DDL.
            if (!done.insert(std::string(Base::table_name_)).second) {
                return {};
            }

            // FK-referenced parent tables first, then T's own table (#412).
            auto result = create_fk_parents(conn, done).and_then([&conn] {
                return conn->execute(with_if_not_exists(create_table_sql<dialect>()));
            });
            result      = result.and_then([&conn] { return create_indexes_if_not_exist(conn); });

            // Auto-generated junction tables for many_to_many fields — one per
            // auto-junction field (#203 Phase 1; #392 several relations). Each junction
            // FK-references both T and the related model, so the related tables must
            // exist first (#412).
            if constexpr (has_m2m_junction_) {
                result = result.and_then([&conn, &done] { return create_m2m_related_tables(conn, done); });
                for (const auto& junction_sql : junction_table_sqls<dialect>()) {
                    result = result.and_then([&conn, &junction_sql] {
                        return conn->execute(with_if_not_exists(junction_sql));
                    });
                }
            }
            return result;
        }

        // Execute CREATE TABLE IF NOT EXISTS on the given connection.
        // Selects the appropriate dialect at compile time. FK-referenced parent tables
        // are created first so always-on referential integrity (#412) is satisfied.
        template <db::DatabaseConnection ConnType>
        [[nodiscard]] static auto create_table_if_not_exists(std::shared_ptr<ConnType> conn)
                -> std::expected<void, typename ConnType::Error> {
            std::set<std::string, std::less<>> done;
            return create_table_if_not_exists(conn, done);
        }
    };

} // namespace storm::orm::schema
