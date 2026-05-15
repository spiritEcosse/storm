module;

// LINT-EXCLUDE-FILE: duplicate
// bind_int/bind_int64/bind_text share a 4-line shape; refactor deferred to
// storm issue #264 Phase 3 (extract bind_typed<T> helper). Move-op duplication
// already eliminated via the swap idiom above.

#include <libpq-fe.h>

export module storm_db_postgresql_statement;
import storm_db_postgresql_error;
import <array>;
import <expected>;
import <string_view>;
import <string>;
import <vector>;
import <cstdint>;
import <cstdio>;
import <cstdlib>;

export namespace storm::db::postgresql {

    // RAII wrapper emulating step-based iteration over PGresult*
    class Statement { // NOSONAR(cpp:S1448) - monolithic statement class required for RAII + step iteration over C
                      // PGresult API
      public:
        using Error = postgresql::Error;

        // Constants for step return codes
        static constexpr int  ROW_AVAILABLE      = 1;
        static constexpr int  NO_MORE_ROWS       = 0;
        static constexpr bool preserves_bindings = false; // reset() clears params

        // Construct from a connection and prepared statement name
        explicit Statement(PGconn* conn, std::string stmt_name) : conn_(conn), stmt_name_(std::move(stmt_name)) {}

        // Destructor - clears any pending result
        ~Statement() {
            clear_result();
        }

        // Move semantics — swap-based to keep the field list in one place.
        // `other` ends up in a default-constructed-equivalent state; its destructor
        // sees result_==nullptr and does nothing.
        Statement(Statement&& other) noexcept {
            swap(*this, other);
        }

        auto operator=(Statement&& other) noexcept -> Statement& {
            if (this != &other) {
                clear_result();
                swap(*this, other);
            }
            return *this;
        }

        friend auto swap(Statement& first, Statement& second) noexcept -> void {
            using std::swap;
            swap(first.conn_, second.conn_);
            swap(first.stmt_name_, second.stmt_name_);
            swap(first.original_sql_, second.original_sql_);
            swap(first.result_, second.result_);
            swap(first.current_row_, second.current_row_);
            swap(first.total_rows_, second.total_rows_);
            swap(first.param_values_, second.param_values_);
            swap(first.param_ptrs_, second.param_ptrs_);
            swap(first.param_lengths_, second.param_lengths_);
            swap(first.param_formats_, second.param_formats_);
            swap(first.param_count_, second.param_count_);
            swap(first.blob_buffer_, second.blob_buffer_);
            swap(first.blob_decoded_size_, second.blob_decoded_size_);
            swap(first.blob_decoded_col_, second.blob_decoded_col_);
        }

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

        // ensure_param_slot may throw bad_alloc via vector::resize — in hot bind paths we accept
        // terminate-on-OOM (issue #262 audit).
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto
        bind_double(int index, double value) noexcept // NOLINT(bugprone-exception-escape)
                -> std::expected<void, Error> {
            ensure_param_slot(index);
            // std::to_string uses %f (6 decimal places) — insufficient for double precision.
            // Use snprintf with %.17g for full double precision (17 significant digits).
            std::array<char, 32> buf{};
            std::snprintf(buf.data(), buf.size(), "%.17g", value); // NOLINT(cppcoreguidelines-pro-type-vararg)
            param_values_[index - 1] = buf.data();
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

        // Execute the prepared statement with accumulated parameters.
        // rebuild_param_ptrs uses vector ops that can throw bad_alloc — accepted as
        // terminate-on-OOM in hot paths (issue #262 audit).
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto execute() noexcept // NOLINT(bugprone-exception-escape)
                -> std::expected<void, Error> {
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
            if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) [[unlikely]] {
                const std::string msg = PQerrorMessage(conn_);
                clear_result();
                return std::unexpected(Error{static_cast<int>(status), msg});
            }

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
                if (!exec_result.has_value()) {
                    return std::unexpected(exec_result.error());
                }
            }

            ++current_row_;
            if (current_row_ < total_rows_) [[likely]] {
                return true; // Row available
            }
            return false;
        }

        // Step with raw return value (no std::expected overhead)
        template <typename = void> [[nodiscard]] __attribute__((always_inline)) auto step_raw() noexcept -> int {
            if (result_ == nullptr) {
                auto exec_result = execute();
                if (!exec_result.has_value()) {
                    return -1; // Error
                }
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

        // Finalize - deallocate server-side prepared statement.
        // String concat (DEALLOCATE + stmt_name_) can throw bad_alloc; accepted as
        // terminate-on-OOM during cleanup (issue #262 audit).
        template <typename = void>
        __attribute__((always_inline)) auto finalize() noexcept -> void { // NOLINT(bugprone-exception-escape)
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

        // Append a quoted parameter value to result, escaping single quotes
        auto append_quoted_param(std::string& result, size_t idx) const -> void {
            if (param_ptrs_[idx] == nullptr) {
                result += "NULL";
                return;
            }
            result += '\'';
            for (const char c : param_values_[idx]) {
                if (c == '\'') {
                    result += "''";
                } else {
                    result += c;
                }
            }
            result += '\'';
        }

        // Returns SQL string with all bound parameters inlined (for debugging / SQL inspection)
        // Substitutes ? placeholders with quoted param_values_ strings
        template <typename = void> [[nodiscard]] auto expanded_sql() const -> std::string {
            std::string result;
            result.reserve(original_sql_.size() + (static_cast<size_t>(param_count_) * 8));
            int  param_idx       = 0;
            bool in_single_quote = false;
            bool in_double_quote = false;
            for (size_t i = 0; i < original_sql_.size(); ++i) {
                const char ch = original_sql_[i];
                if (ch == '\'' && !in_double_quote) {
                    in_single_quote = !in_single_quote;
                    result += ch;
                } else if (ch == '"' && !in_single_quote) {
                    in_double_quote = !in_double_quote;
                    result += ch;
                } else if (ch == '?' && !in_single_quote && !in_double_quote) {
                    if (param_idx < param_count_) {
                        append_quoted_param(result, static_cast<size_t>(param_idx));
                        ++param_idx;
                    } else {
                        result += ch;
                    }
                } else {
                    result += ch;
                }
            }
            return result;
        }

        // Store original SQL (with ? placeholders) for expanded_sql()
        auto set_original_sql(std::string sql) -> void {
            original_sql_ = std::move(sql);
        }

        // Column extraction methods
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_int(int col_index) const noexcept -> int {
            const char* val = PQgetvalue(result_, current_row_, col_index);
            return std::atoi(val); // NOLINT(cert-err34-c) NOSONAR - performance over strtol for known-valid DB output
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

        // blob_buffer_ resize can throw bad_alloc; accepted as terminate-on-OOM
        // during BYTEA hex decode (issue #262 audit). PostgreSQL BLOB API uses void*.
        // NOLINTBEGIN(bugprone-exception-escape)
        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto extract_blob_ptr(int col_index) noexcept -> const
                void* { // NOSONAR(cpp:S5008) - PostgreSQL BLOB API uses void*
            // PG text-mode returns BYTEA as hex string: "\xDEADBEEF"
            // Decode hex to raw binary bytes
            blob_decoded_col_ = col_index;
            if (PQgetisnull(result_, current_row_, col_index) != 0) {
                blob_decoded_size_ = 0;
                return nullptr;
            }
            const char* hex_str = PQgetvalue(result_, current_row_, col_index);
            const int   hex_len = PQgetlength(result_, current_row_, col_index);

            // PG hex format starts with "\x" prefix
            if (hex_len >= 2 && hex_str[0] == '\\' && hex_str[1] == 'x') {
                const int binary_len = (hex_len - 2) / 2;
                blob_buffer_.resize(static_cast<size_t>(binary_len));
                for (int i = 0; i < binary_len;
                     ++i) { // NOSONAR(cpp:S6022) - unsigned char required for uint8_t blob API compatibility
                    const char hi = hex_str[2 + (i * 2)];
                    const char lo = hex_str[2 + (i * 2) + 1];
                    blob_buffer_[static_cast<size_t>(i)] =
                            static_cast<unsigned char>((hex_digit(hi) << 4) | hex_digit(lo)); // NOSONAR(cpp:S6022)
                }
                blob_decoded_size_ = static_cast<size_t>(binary_len);
            } else {
                blob_buffer_
                        .assign(reinterpret_cast<const unsigned char*>(hex_str),
                                reinterpret_cast<const unsigned char*>(hex_str) + hex_len);
                blob_decoded_size_ = static_cast<size_t>(hex_len);
            }
            return blob_buffer_.data();
        }
        // NOLINTEND(bugprone-exception-escape)

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto is_null(int col_index) const noexcept -> bool {
            return PQgetisnull(result_, current_row_, col_index) != 0;
        }

        template <typename = void>
        [[nodiscard]] __attribute__((always_inline)) auto get_error_message() const noexcept -> const char* {
            return PQerrorMessage(conn_);
        }

      private:
        static constexpr auto hex_digit(char ch) noexcept -> unsigned char {
            if (ch >= '0' && ch <= '9') {
                return static_cast<unsigned char>(ch - '0');
            }
            if (ch >= 'a' && ch <= 'f') {
                return static_cast<unsigned char>(ch - 'a' + 10);
            }
            if (ch >= 'A' && ch <= 'F') {
                return static_cast<unsigned char>(ch - 'A' + 10);
            }
            return 0;
        }

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

        // vector::resize can throw bad_alloc; accepted as terminate-on-OOM (issue #262 audit).
        auto ensure_param_slot(int index) noexcept -> void { // NOLINT(bugprone-exception-escape)
            const auto idx = static_cast<size_t>(index);
            if (idx > param_values_.size()) {
                param_values_.resize(idx);
                param_ptrs_.resize(idx, nullptr);
                param_lengths_.resize(idx, 0);
                param_formats_.resize(idx, 0); // Text format by default
            }
            // NOLINTBEGIN(readability-use-std-min-max) — avoid <algorithm> import; clang-scan-deps
            // SIGSEGVs on atomic_ref.h via <algorithm> under ninja-release. See issue #262.
            if (index > param_count_) {
                param_count_ = index;
            }
            // NOLINTEND(readability-use-std-min-max)
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
        std::string original_sql_; // Original SQL with ? placeholders (for expanded_sql())
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

} // namespace storm::db::postgresql
