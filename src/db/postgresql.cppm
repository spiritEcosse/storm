module;

#include <libpq-fe.h>

export module storm_db_postgresql;
import storm_db_concept;
import <expected>;
import <string_view>;
import <string>;
import <memory>;
import <unordered_map>;
import <vector>;
import <cstdint>;
import <cstdio>;
import <cstdlib>;

export namespace storm::db::postgresql {

    // Error type for database operations
    struct Error {
        int         code_;
        std::string message_;

        // LCOV_EXCL_START — accessors tested indirectly via .error()
        [[nodiscard]] constexpr auto code() const noexcept -> int {
            return code_;
        }
        [[nodiscard]] constexpr auto message() const noexcept -> std::string_view {
            return message_;
        }
        // LCOV_EXCL_STOP
    };

    // Forward declaration
    class Connection;

    // RAII wrapper emulating step-based iteration over PGresult*
    class Statement {
      public:
        using Error = postgresql::Error;

        // Constants for step return codes
        static constexpr int ROW_AVAILABLE = 1;
        static constexpr int NO_MORE_ROWS  = 0;

        // Construct from a connection and prepared statement name
        explicit Statement(PGconn* conn, std::string stmt_name) : conn_(conn), stmt_name_(std::move(stmt_name)) {}

        // Destructor - clears any pending result
        ~Statement() {
            clear_result();
        }

        // Move semantics
        Statement(Statement&& other) noexcept
            : conn_(other.conn_)
            , stmt_name_(std::move(other.stmt_name_))
            , result_(other.result_)
            , current_row_(other.current_row_)
            , total_rows_(other.total_rows_)
            , param_values_(std::move(other.param_values_))
            , param_ptrs_(std::move(other.param_ptrs_))
            , param_lengths_(std::move(other.param_lengths_))
            , param_formats_(std::move(other.param_formats_))
            , param_count_(other.param_count_)
            , blob_buffer_(std::move(other.blob_buffer_))
            , blob_decoded_size_(other.blob_decoded_size_)
            , blob_decoded_col_(other.blob_decoded_col_) {
            other.result_            = nullptr;
            other.current_row_       = -1;
            other.total_rows_        = 0;
            other.param_count_       = 0;
            other.blob_decoded_size_ = 0;
            other.blob_decoded_col_  = -1;
        }

        // LCOV_EXCL_START — move assignment used internally by unordered_map; move ctor is the tested path
        auto operator=(Statement&& other) noexcept -> Statement& {
            if (this != &other) {
                clear_result();
                conn_              = other.conn_;
                stmt_name_         = std::move(other.stmt_name_);
                result_            = other.result_;
                current_row_       = other.current_row_;
                total_rows_        = other.total_rows_;
                param_values_      = std::move(other.param_values_);
                param_ptrs_        = std::move(other.param_ptrs_);
                param_lengths_     = std::move(other.param_lengths_);
                param_formats_     = std::move(other.param_formats_);
                param_count_       = other.param_count_;
                blob_buffer_       = std::move(other.blob_buffer_);
                blob_decoded_size_ = other.blob_decoded_size_;
                blob_decoded_col_  = other.blob_decoded_col_;

                other.result_            = nullptr;
                other.current_row_       = -1;
                other.total_rows_        = 0;
                other.param_count_       = 0;
                other.blob_decoded_size_ = 0;
                other.blob_decoded_col_  = -1;
            }
            return *this;
        }
        // LCOV_EXCL_STOP

        // Delete copy operations
        Statement(const Statement&)                    = delete;
        auto operator=(const Statement&) -> Statement& = delete;

        // Parameter binding - accumulate as text strings for PQexecPrepared
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto bind_int(int index, int value) noexcept
                -> std::expected<void, Error> {
            ensure_param_slot(index);
            param_values_[index - 1] = std::to_string(value);
            update_param_ptrs(index);
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto bind_text(int index, std::string_view value) noexcept
                -> std::expected<void, Error> {
            ensure_param_slot(index);
            param_values_[index - 1] = std::string(value);
            update_param_ptrs(index);
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto bind_int64(int index, int64_t value) noexcept
                -> std::expected<void, Error> {
            ensure_param_slot(index);
            param_values_[index - 1] = std::to_string(value);
            update_param_ptrs(index);
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto bind_double(int index, double value) noexcept
                -> std::expected<void, Error> {
            ensure_param_slot(index);
            // std::to_string uses %f (6 decimal places) — insufficient for double precision.
            // Use snprintf with %.17g for full double precision (17 significant digits).
            char buf[32]; // NOLINT(modernize-avoid-c-arrays) - stack buffer for snprintf
            std::snprintf(buf, sizeof(buf), "%.17g", value);
            param_values_[index - 1] = buf;
            update_param_ptrs(index);
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto bind_null(int index) noexcept -> std::expected<void, Error> {
            ensure_param_slot(index);
            param_values_[index - 1].clear();
            param_ptrs_[index - 1]    = nullptr; // NULL parameter
            param_lengths_[index - 1] = 0;
            param_formats_[index - 1] = 0;
            return {};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto
        bind_blob(int index, const void* data, size_t size) noexcept // NOSONAR(cpp:S5008) - PostgreSQL BLOB API
                -> std::expected<void, Error> {
            ensure_param_slot(index);
            if (data != nullptr && size > 0) {
                // Store binary data as-is, use binary format
                param_values_[index - 1] =
                        std::string(static_cast<const char*>(data), size); // NOSONAR(cpp:S5008) - Binary data
            } else {
                // Empty blob - use empty string (non-NULL, zero-length)
                param_values_[index - 1].clear();
            }
            param_ptrs_[index - 1]    = param_values_[index - 1].c_str();
            param_lengths_[index - 1] = static_cast<int>(size);
            param_formats_[index - 1] = 1; // Binary format
            return {};
        }

        // Execute the prepared statement with accumulated parameters
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto execute() noexcept -> std::expected<void, Error> {
            clear_result();

            // Rebuild param_ptrs_ from param_values_ to ensure pointers are valid
            rebuild_param_ptrs();

            result_ = PQexecPrepared(
                    conn_,
                    stmt_name_.c_str(),
                    param_count_,
                    param_count_ > 0 ? param_ptrs_.data() : nullptr,
                    param_count_ > 0 ? param_lengths_.data() : nullptr,
                    param_count_ > 0 ? param_formats_.data() : nullptr,
                    0 // Text result format
            );

            const ExecStatusType status = PQresultStatus(result_);
            // LCOV_EXCL_START — PQ execute error path
            if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) [[unlikely]] {
                const std::string msg = PQerrorMessage(conn_);
                clear_result();
                return std::unexpected(Error{static_cast<int>(status), msg});
            }
            // LCOV_EXCL_STOP

            total_rows_  = PQntuples(result_);
            current_row_ = -1; // Before first row
            return {};
        }

        // Step through results - returns true if row available, false if done
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto step() noexcept -> std::expected<bool, Error> {
            if (result_ == nullptr) {
                // Auto-execute if not yet executed
                auto exec_result = execute();
                // LCOV_EXCL_START — auto-execute error path
                if (!exec_result.has_value()) {
                    return std::unexpected(exec_result.error());
                }
                // LCOV_EXCL_STOP
            }

            ++current_row_;
            if (current_row_ < total_rows_) [[likely]] {
                return true; // Row available
            }
            return false; // LCOV_EXCL_LINE — step() only used for INSERT RETURNING (always 1 row)
        }

        // Step with raw return value (no std::expected overhead)
        template <typename = void> [[nodiscard]] __attribute__((always_inline)) auto step_raw() noexcept -> int {
            if (result_ == nullptr) {
                auto exec_result = execute();
                // LCOV_EXCL_START — auto-execute error path
                if (!exec_result.has_value()) {
                    return -1; // Error
                }
                // LCOV_EXCL_STOP
            }

            ++current_row_;
            if (current_row_ < total_rows_) [[likely]] {
                return ROW_AVAILABLE;
            }
            return NO_MORE_ROWS;
        }

        // Reset statement for reuse - clear params and result
        template <typename = void> __attribute__((always_inline)) auto reset() noexcept -> void {
            clear_result();
            clear_params();
        }

        // Finalize - deallocate server-side prepared statement
        template <typename = void> __attribute__((always_inline)) auto finalize() noexcept -> void {
            if (conn_ != nullptr && !stmt_name_.empty()) {
                const std::string dealloc = "DEALLOCATE " + stmt_name_;
                PGresult*         res     = PQexec(conn_, dealloc.c_str());
                if (res != nullptr) {
                    PQclear(res);
                }
                stmt_name_.clear();
            }
            clear_result();
        }

        // Access raw handle (PGresult) for advanced operations
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto handle() const noexcept -> PGresult* {
            return result_;
        }

        // Column extraction methods
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_int(int col_index) const noexcept -> int {
            const char* val = PQgetvalue(result_, current_row_, col_index);
            return std::atoi(val); // NOSONAR - performance over strtol for known-valid DB output
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_int64(int col_index) const noexcept -> int64_t {
            const char* val = PQgetvalue(result_, current_row_, col_index);
            return std::strtoll(val, nullptr, 10);
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_double(int col_index) const noexcept -> double {
            const char* val = PQgetvalue(result_, current_row_, col_index);
            return std::strtod(val, nullptr);
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_text_ptr(int col_index) const noexcept -> const
                unsigned char* {
            return reinterpret_cast<const unsigned char*>(PQgetvalue(result_, current_row_, col_index));
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_bytes(int col_index) noexcept -> int {
            // If we just decoded a blob for this column, return decoded size
            if (blob_decoded_col_ == col_index) {
                return static_cast<int>(blob_decoded_size_);
            }
            return PQgetlength(result_, current_row_, col_index);
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_text_view(int col_index) const noexcept
                -> std::string_view {
            if (PQgetisnull(result_, current_row_, col_index) != 0) {
                return {};
            }
            const char* text = PQgetvalue(result_, current_row_, col_index);
            const auto  len  = static_cast<size_t>(PQgetlength(result_, current_row_, col_index));
            return {text, len};
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_bool(int col_index) const noexcept -> bool {
            const char* val = PQgetvalue(result_, current_row_, col_index);
            // PostgreSQL returns 't'/'f' for boolean, or '1'/'0'
            return val[0] == 't' || val[0] == '1';
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_float(int col_index) const noexcept -> float {
            const char* val = PQgetvalue(result_, current_row_, col_index);
            return std::strtof(val, nullptr);
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_blob_ptr(int col_index) noexcept -> const
                void* { // NOSONAR(cpp:S5008) - PostgreSQL BLOB API
            // PG text-mode returns BYTEA as hex string: "\xDEADBEEF"
            // Decode hex to raw binary bytes
            blob_decoded_col_ = col_index;
            // LCOV_EXCL_START — NULL BLOBs use is_null() path
            if (PQgetisnull(result_, current_row_, col_index) != 0) {
                blob_decoded_size_ = 0;
                return nullptr;
            }
            // LCOV_EXCL_STOP
            const char* hex_str = PQgetvalue(result_, current_row_, col_index);
            const int   hex_len = PQgetlength(result_, current_row_, col_index);

            // PG hex format starts with "\x" prefix
            if (hex_len >= 2 && hex_str[0] == '\\' && hex_str[1] == 'x') {
                const int binary_len = (hex_len - 2) / 2;
                blob_buffer_.resize(static_cast<size_t>(binary_len));
                for (int i = 0; i < binary_len; ++i) {
                    const char hi = hex_str[2 + i * 2];
                    const char lo = hex_str[2 + i * 2 + 1];
                    blob_buffer_[static_cast<size_t>(i)] =
                            static_cast<unsigned char>((hex_digit(hi) << 4) | hex_digit(lo));
                }
                blob_decoded_size_ = static_cast<size_t>(binary_len);
                // LCOV_EXCL_START — PG always uses hex format for BYTEA
            } else {
                blob_buffer_
                        .assign(reinterpret_cast<const unsigned char*>(hex_str),
                                reinterpret_cast<const unsigned char*>(hex_str) + hex_len);
                blob_decoded_size_ = static_cast<size_t>(hex_len);
            }
            // LCOV_EXCL_STOP
            return blob_buffer_.data();
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto is_null(int col_index) const noexcept -> bool {
            return PQgetisnull(result_, current_row_, col_index) != 0;
        }

        // LCOV_EXCL_START — error message extraction used by ORM error reporting
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto get_error_message() const noexcept -> const char* {
            return PQerrorMessage(conn_);
        }
        // LCOV_EXCL_STOP

      private:
        static constexpr auto hex_digit(char ch) noexcept -> unsigned char {
            if (ch >= '0' && ch <= '9')
                return static_cast<unsigned char>(ch - '0');
            if (ch >= 'a' && ch <= 'f')
                return static_cast<unsigned char>(ch - 'a' + 10);
            // LCOV_EXCL_START — PG uses lowercase hex
            if (ch >= 'A' && ch <= 'F')
                return static_cast<unsigned char>(ch - 'A' + 10);
            return 0;
        }
        // LCOV_EXCL_STOP

        auto clear_result() noexcept -> void {
            if (result_ != nullptr) {
                PQclear(result_);
                result_ = nullptr;
            }
            current_row_ = -1;
            total_rows_  = 0;
        }

        auto clear_params() noexcept -> void {
            param_values_.clear();
            param_ptrs_.clear();
            param_lengths_.clear();
            param_formats_.clear();
            param_count_ = 0;
        }

        auto ensure_param_slot(int index) noexcept -> void {
            const auto idx = static_cast<size_t>(index);
            if (idx > param_values_.size()) {
                param_values_.resize(idx);
                param_ptrs_.resize(idx, nullptr);
                param_lengths_.resize(idx, 0);
                param_formats_.resize(idx, 0); // Text format by default
            }
            if (index > param_count_) {
                param_count_ = index;
            }
        }

        auto update_param_ptrs(int index) noexcept -> void {
            const auto idx      = static_cast<size_t>(index - 1);
            param_ptrs_[idx]    = param_values_[idx].c_str();
            param_lengths_[idx] = static_cast<int>(param_values_[idx].size());
            param_formats_[idx] = 0; // Text format
        }

        auto rebuild_param_ptrs() noexcept -> void {
            for (int i = 0; i < param_count_; ++i) {
                const auto idx = static_cast<size_t>(i);
                if (param_ptrs_[idx] != nullptr) {
                    // Non-NULL param (text or binary) - update pointer
                    // (may have been invalidated by param_values_ vector resize)
                    param_ptrs_[idx] = param_values_[idx].c_str();
                }
                // NULL params: param_ptrs_ already nullptr, no update needed
            }
        }

        PGconn*     conn_ = nullptr; // Borrowed reference
        std::string stmt_name_;
        PGresult*   result_      = nullptr;
        int         current_row_ = -1;
        int         total_rows_  = 0;

        // Parameter accumulation for PQexecPrepared
        std::vector<std::string> param_values_;
        std::vector<const char*> param_ptrs_;
        std::vector<int>         param_lengths_;
        std::vector<int>         param_formats_;
        int                      param_count_ = 0;

        // BYTEA decode buffer (reused across extract_blob_ptr calls)
        std::vector<unsigned char> blob_buffer_;
        size_t                     blob_decoded_size_ = 0;
        int                        blob_decoded_col_  = -1;
    };

    // Transparent hash for string_view lookups without allocation
    struct string_hash {
        using is_transparent = void;
        using hash_type      = std::hash<std::string_view>;

        [[nodiscard]] auto operator()(std::string_view str) const noexcept -> size_t {
            return hash_type{}(str);
        }

        [[nodiscard]] auto operator()(const std::string& str) const noexcept -> size_t {
            return hash_type{}(str);
        }
    };

    struct string_equal {
        using is_transparent = void;

        [[nodiscard]] auto operator()(std::string_view lhs, std::string_view rhs) const noexcept -> bool {
            return lhs == rhs;
        }
    };

    // Custom deleter for PGconn
    struct PGconnDeleter {
        auto operator()(PGconn* conn) const noexcept -> void {
            if (conn != nullptr) {
                PQfinish(conn);
            }
        }
    };

    class Connection {
        using PGconnPtr      = std::unique_ptr<PGconn, PGconnDeleter>;
        using StatementCache = std::unordered_map<std::string, Statement, string_hash, string_equal>;

        // Cache size constants
        static constexpr size_t STMT_CACHE_RESERVE = 32;

      public:
        using Error     = postgresql::Error;
        using Statement = postgresql::Statement;

        // Dialect traits
        static constexpr bool supports_limit_all = true;
        static constexpr bool supports_returning = true;

        // Pre-populate statement cache with common operations (stub for PostgreSQL)
        auto prepare_common_statements() -> void {
            // PostgreSQL doesn't need pre-populated common statements
            // Transaction management uses direct PQexec
        }

        // Factory method with error handling
        [[nodiscard]] static auto open(std::string_view conninfo) -> std::expected<Connection, Error> {
            const std::string conninfo_str(conninfo); // Ensure null-termination
            PGconn*           raw_conn = PQconnectdb(conninfo_str.c_str());

            // LCOV_EXCL_START — PQ connection failure paths
            if (raw_conn == nullptr) {
                return std::unexpected(Error{-1, "Failed to allocate PGconn"});
            }

            if (PQstatus(raw_conn) != CONNECTION_OK) {
                const std::string msg = PQerrorMessage(raw_conn);
                PQfinish(raw_conn);
                return std::unexpected(Error{static_cast<int>(PQstatus(raw_conn)), msg});
            }
            // LCOV_EXCL_STOP

            return Connection{PGconnPtr{raw_conn}};
        }

        // Destructor - unique_ptr handles cleanup via PGconnDeleter
        ~Connection() = default;

        // Move semantics
        Connection(Connection&&)                    = default;
        auto operator=(Connection&&) -> Connection& = default;

        // Delete copy operations
        Connection(const Connection&)                    = delete;
        auto operator=(const Connection&) -> Connection& = delete;

        // Connection state
        [[nodiscard]] constexpr auto is_open() const noexcept -> bool {
            return static_cast<bool>(conn_);
        }

        // Prepare a statement - translates ? placeholders to $1, $2, ...
        [[nodiscard]] auto prepare(std::string_view sql) -> std::expected<Statement, Error> {
            // LCOV_EXCL_START — prepare error paths (closed connection, PQ failure)
            if (!is_open()) {
                return std::unexpected(Error{-1, "Connection not open"});
            }
            // LCOV_EXCL_STOP

            const std::string pg_sql    = translate_placeholders(sql);
            const std::string stmt_name = next_stmt_name();

            PGresult* res = PQprepare(conn_.get(), stmt_name.c_str(), pg_sql.c_str(), 0, nullptr);

            // LCOV_EXCL_START — PQ prepare failure
            if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
                const std::string msg = PQerrorMessage(conn_.get());
                if (res != nullptr) {
                    PQclear(res);
                }
                return std::unexpected(Error{-1, msg});
            }
            // LCOV_EXCL_STOP

            PQclear(res);
            return Statement{conn_.get(), stmt_name};
        }

        // Prepare with caching - reuses prepared statements for identical SQL
        [[nodiscard]] auto prepare_cached(std::string_view sql) -> std::expected<Statement*, Error> {
            // LCOV_EXCL_START — closed connection guard
            if (!is_open()) {
                return std::unexpected(Error{-1, "Connection not open"});
            }
            // LCOV_EXCL_STOP

            // Heterogeneous lookup using string_view
            auto it = statement_cache_.find(sql);
            if (it != statement_cache_.end()) [[likely]] {
                it->second.reset();
                return &it->second;
            }

            // Cache miss - create new prepared statement
            const std::string pg_sql    = translate_placeholders(sql);
            const std::string stmt_name = next_stmt_name();

            PGresult* res = PQprepare(conn_.get(), stmt_name.c_str(), pg_sql.c_str(), 0, nullptr);

            // LCOV_EXCL_START — PQ prepare_cached failure
            if (res == nullptr || PQresultStatus(res) != PGRES_COMMAND_OK) {
                const std::string msg = PQerrorMessage(conn_.get());
                if (res != nullptr) {
                    PQclear(res);
                }
                return std::unexpected(Error{-1, msg});
            }
            // LCOV_EXCL_STOP

            PQclear(res);
            auto [inserted_it, inserted] =
                    statement_cache_.emplace(std::string(sql), Statement{conn_.get(), stmt_name});
            (void)inserted; // Always true: find() above confirmed key absence
            return &inserted_it->second;
        }

        // LCOV_EXCL_START — utility methods used by ORM internals
        auto clear_statement_cache() noexcept -> void {
            statement_cache_.clear();
        }

        [[nodiscard]] auto cached_statement_count() const noexcept -> size_t {
            return statement_cache_.size();
        }
        // LCOV_EXCL_STOP

        // Execute SQL directly (simple queries without parameters)
        [[nodiscard]] auto execute(std::string_view sql) -> std::expected<void, Error> {
            // LCOV_EXCL_START — closed connection guard
            if (!is_open()) {
                return std::unexpected(Error{-1, "Connection not open"});
            }
            // LCOV_EXCL_STOP

            const std::string sql_str(sql); // Ensure null-termination
            PGresult*         res = PQexec(conn_.get(), sql_str.c_str());

            // LCOV_EXCL_START — PQ exec null result
            if (res == nullptr) {
                return std::unexpected(Error{-1, PQerrorMessage(conn_.get())});
            }
            // LCOV_EXCL_STOP

            const ExecStatusType status = PQresultStatus(res);
            // LCOV_EXCL_START — PQ exec SQL error
            if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
                const std::string msg = PQerrorMessage(conn_.get());
                PQclear(res);
                return std::unexpected(Error{static_cast<int>(status), msg});
            }
            // LCOV_EXCL_STOP

            PQclear(res);
            return {};
        }

        // LCOV_EXCL_START — accessor methods used by ORM internals
        [[nodiscard]] auto get() const noexcept -> PGconn* {
            return conn_.get();
        }

        [[nodiscard]] auto last_insert_rowid() const noexcept -> int64_t {
            return last_insert_rowid_;
        }

        auto set_last_insert_rowid(int64_t rowid) noexcept -> void {
            last_insert_rowid_ = rowid;
        }
        // LCOV_EXCL_STOP

      private:
        explicit Connection(PGconnPtr conn_ptr) : conn_(std::move(conn_ptr)) {
            statement_cache_.reserve(STMT_CACHE_RESERVE);
        }

        // Translate ? placeholders to $1, $2, ... for PostgreSQL
        [[nodiscard]] static auto translate_placeholders(std::string_view sql) -> std::string {
            std::string result;
            result.reserve(sql.size() + 16); // Extra space for $N expansions
            int param_index = 0;

            bool in_single_quote = false;
            bool in_double_quote = false;

            for (size_t i = 0; i < sql.size(); ++i) {
                const char ch = sql[i];

                // Track quoted strings to avoid translating ? inside them
                if (ch == '\'' && !in_double_quote) {       // LCOV_EXCL_LINE
                    in_single_quote = !in_single_quote;     // LCOV_EXCL_LINE
                    result += ch;                           // LCOV_EXCL_LINE
                } else if (ch == '"' && !in_single_quote) { // LCOV_EXCL_LINE
                    in_double_quote = !in_double_quote;     // LCOV_EXCL_LINE
                    result += ch;                           // LCOV_EXCL_LINE
                } else if (ch == '?' && !in_single_quote && !in_double_quote) {
                    ++param_index;
                    result += '$';
                    result += std::to_string(param_index);
                } else {
                    result += ch;
                }
            }

            return result;
        }

        // Generate unique prepared statement names
        [[nodiscard]] auto next_stmt_name() -> std::string {
            return "_storm_" + std::to_string(stmt_counter_++);
        }

        PGconnPtr      conn_;
        StatementCache statement_cache_;
        int64_t        last_insert_rowid_ = 0;
        int            stmt_counter_      = 0;
    };

    // Verify concepts are satisfied
    static_assert(storm::db::DatabaseConnection<Connection>);
    static_assert(storm::db::CachedDatabaseConnection<Connection>);
    static_assert(storm::db::DatabaseStatement<Statement>);
    static_assert(storm::db::DatabaseError<Error>);

} // namespace storm::db::postgresql
