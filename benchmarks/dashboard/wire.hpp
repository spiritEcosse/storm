#pragma once

// Wire format for the storm_bench → storm_bench_dashboard link (Issue #247,
// Phase 2). NDJSON over AF_UNIX SOCK_DGRAM: one datagram = one JSON line. The
// datagram boundary preserves message framing, so neither side glues partial
// lines.
//
// Three message kinds:
//   {"type":"run_start","filter":"<regex>","is_full_run":<bool>}
//   {"name":"<test>","category":"<cat>","dataset_size":<int>,
//    "real_ns":<double>,"cpu_ns":<double>,"iterations":<int>,
//    "items_per_second":<double>}
//   {"type":"run_complete"}
//
// Pure stdlib — no `import storm`, no `<benchmark/benchmark.h>`. Both sides
// (textual #include in reporter.cpp and dashboard main.cpp) consume this. The
// JSON shape is deliberately tiny: backslash and double-quote are the only
// recognised escapes; benchmark names are ASCII-only by gbench convention.

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace bench_dashboard::wire {

    enum class MessageKind : std::uint8_t { Result, RunStart, RunComplete };

    // row_kind values for ResultMsg (Phase 7).
    inline constexpr std::string_view kRowKindMeasurement = "measurement";
    inline constexpr std::string_view kRowKindBigO        = "bigo";
    inline constexpr std::string_view kRowKindRms         = "rms";

    struct ResultMsg {
        MessageKind kind{MessageKind::Result};

        std::string filter{};
        bool        is_full_run{false};

        std::string  test_name{};
        std::string  category{};
        std::int64_t dataset_size{0};
        std::string  row_kind{kRowKindMeasurement}; // Phase 7: "measurement" | "bigo" | "rms"
        double       real_ns{0.0};
        double       cpu_ns{0.0};
        std::int64_t iterations{0};
        double       items_per_second{0.0};

        // Phase 7: complexity fields (bigo/rms rows only).
        std::string complexity_class{}; // "N", "NlgN", "N^2", ...
        double      complexity_coef{0.0};
        double      rms_pct{0.0};

        // Set by the dashboard when a baseline run is active (Phase 6).
        // nullopt = no matching baseline row; baseline_looked_up distinguishes
        // "no baseline active" (false) from "active but no match" (true+nullopt).
        std::optional<double> delta_pct{};
        bool                  baseline_looked_up{false};

        // Phase 7: baseline complexity data (set by dashboard when baseline is active).
        bool        shape_regression{false}; // complexity_class differs from baseline
        std::string baseline_class{};        // baseline complexity_class
        double      baseline_coef{0.0};      // baseline complexity_coef
    };

    // Single source of truth for the default socket path; both reporter.cpp
    // (gbench side) and dashboard/main.cpp (storm side) call this. Meyers
    // singleton — thread-safe per C++11 magic-statics, evaluated once.
    inline auto default_socket_path() -> std::string_view {
        static const std::string p = []() -> std::string {
            if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg != nullptr && *xdg != '\0')
                return std::format("{}/storm-bench.sock", xdg);
            const char* user = std::getenv("USER");
            if (user == nullptr || *user == '\0')
                user = "unknown";
            return std::format("/tmp/storm-bench-{}.sock", user);
        }();
        return p;
    }

    inline auto append_escaped(std::string& out, std::string_view s) -> void {
        out.reserve(out.size() + s.size() + 2);
        for (char c : s) {
            if (c == '"' || c == '\\') {
                out.push_back('\\');
                out.push_back(c);
            } else {
                out.push_back(c);
            }
        }
    }

    inline auto append_double(std::string& out, double v) -> void {
        std::array<char, 32> buf{};
        const auto           r = std::to_chars(buf.data(), buf.data() + buf.size(), v);
        out.append(buf.data(), r.ptr);
    }

    inline auto append_int(std::string& out, long long v) -> void {
        std::array<char, 32> buf{};
        const auto           r = std::to_chars(buf.data(), buf.data() + buf.size(), v);
        out.append(buf.data(), r.ptr);
    }

    inline auto build_run_start(std::string_view filter, bool is_full_run) -> std::string {
        std::string s;
        s.reserve(64 + filter.size());
        s.append(R"({"type":"run_start","filter":")");
        append_escaped(s, filter);
        s.append(R"(","is_full_run":)");
        s.append(is_full_run ? "true" : "false");
        s.push_back('}');
        return s;
    }

    inline auto build_run_complete() -> std::string {
        return std::string{R"({"type":"run_complete"})"};
    }

    inline auto build_result(ResultMsg const& m) -> std::string {
        std::string s;
        s.reserve(256 + m.test_name.size() + m.category.size() + m.row_kind.size() + m.complexity_class.size());
        s.append(R"({"name":")");
        append_escaped(s, m.test_name);
        s.append(R"(","category":")");
        append_escaped(s, m.category);
        s.append(R"(","dataset_size":)");
        append_int(s, m.dataset_size);
        s.append(R"(,"row_kind":")");
        append_escaped(s, m.row_kind);
        s.push_back('"');
        s.append(R"(,"real_ns":)");
        append_double(s, m.real_ns);
        s.append(R"(,"cpu_ns":)");
        append_double(s, m.cpu_ns);
        s.append(R"(,"iterations":)");
        append_int(s, m.iterations);
        s.append(R"(,"items_per_second":)");
        append_double(s, m.items_per_second);
        if (!m.complexity_class.empty()) {
            s.append(R"(,"complexity_class":")");
            append_escaped(s, m.complexity_class);
            s.push_back('"');
            s.append(R"(,"complexity_coef":)");
            append_double(s, m.complexity_coef);
        }
        if (m.rms_pct != 0.0) {
            s.append(R"(,"rms_pct":)");
            append_double(s, m.rms_pct);
        }
        s.push_back('}');
        return s;
    }

    namespace detail {

        // Walk top-level (depth-1) keys, tracking whether we're inside a
        // string, so a key character that happens to appear inside a string
        // value can never match. Returns position of the value (after the
        // ':') or npos.
        //
        // The wire format is closed (only this header builds + parses it),
        // but datagrams arrive over AF_UNIX from a process whose name a user
        // could influence — keep parsing defensive.
        inline auto find_top_level_key(std::string_view json, std::string_view key) -> std::size_t {
            bool        in_string = false;
            int         depth     = 0;
            std::size_t i         = 0;
            while (i < json.size()) {
                const char c = json[i];
                if (in_string) {
                    if (c == '\\' && i + 1 < json.size()) {
                        i += 2;
                        continue;
                    }
                    if (c == '"')
                        in_string = false;
                    ++i;
                    continue;
                }
                if (c == '"') {
                    if (depth == 1) {
                        // Could be the start of a top-level key.
                        const std::size_t key_start = i + 1;
                        const std::size_t key_end   = key_start + key.size();
                        if (key_end < json.size() && json[key_end] == '"' && json[key_end + 1] == ':' &&
                            json.substr(key_start, key.size()) == key) {
                            return key_end + 2; // position right after ':'
                        }
                    }
                    in_string = true;
                    ++i;
                    continue;
                }
                if (c == '{' || c == '[')
                    ++depth;
                else if (c == '}' || c == ']')
                    --depth;
                ++i;
            }
            return std::string_view::npos;
        }

        // Read a JSON-string value starting at `pos` (which points at the
        // opening "). Recognises only \\ and \" escapes — any other escape
        // is rejected (returns false), matching the closed wire-format
        // contract documented at the top of this file.
        inline auto read_string(std::string_view json, std::size_t pos, std::string& out) -> bool {
            if (pos >= json.size() || json[pos] != '"')
                return false;
            ++pos;
            out.clear();
            while (pos < json.size()) {
                const char c = json[pos];
                if (c == '\\') {
                    if (pos + 1 >= json.size())
                        return false;
                    const char esc = json[pos + 1];
                    if (esc != '\\' && esc != '"')
                        return false;
                    out.push_back(esc);
                    pos += 2;
                    continue;
                }
                if (c == '"')
                    return true;
                out.push_back(c);
                ++pos;
            }
            return false;
        }

        inline auto read_number_view(std::string_view json, std::size_t pos) -> std::string_view {
            const std::size_t start = pos;
            while (pos < json.size()) {
                const char c = json[pos];
                if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
                    break;
                ++pos;
            }
            return json.substr(start, pos - start);
        }

        inline auto read_double(std::string_view json, std::size_t pos, double& out) -> bool {
            const auto v = read_number_view(json, pos);
            if (v.empty())
                return false;
            const auto r = std::from_chars(v.data(), v.data() + v.size(), out);
            return r.ec == std::errc{};
        }

        inline auto read_int64(std::string_view json, std::size_t pos, std::int64_t& out) -> bool {
            const auto v = read_number_view(json, pos);
            if (v.empty())
                return false;
            const auto r = std::from_chars(v.data(), v.data() + v.size(), out);
            return r.ec == std::errc{};
        }

        inline auto read_bool(std::string_view json, std::size_t pos, bool& out) -> bool {
            if (json.substr(pos, 4) == "true") {
                out = true;
                return true;
            }
            if (json.substr(pos, 5) == "false") {
                out = false;
                return true;
            }
            return false;
        }

        // Tiny field-binding helper: keeps parse() flat and below SonarCloud
        // S3776's cognitive-complexity threshold.
        struct Reader {
            std::string_view json;

            template <typename T> auto read_field(std::string_view key, T& out) const -> void {
                const auto p = find_top_level_key(json, key);
                if (p == std::string_view::npos)
                    return;
                if constexpr (std::is_same_v<T, std::string>)
                    read_string(json, p, out);
                else if constexpr (std::is_same_v<T, double>)
                    read_double(json, p, out);
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    read_int64(json, p, out);
                else if constexpr (std::is_same_v<T, bool>)
                    read_bool(json, p, out);
            }
        };

    } // namespace detail

    inline auto parse(std::string_view json) -> std::optional<ResultMsg> {
        ResultMsg            m;
        const detail::Reader rdr{json};

        if (const auto p = detail::find_top_level_key(json, "type"); p != std::string_view::npos) {
            std::string type_v;
            if (!detail::read_string(json, p, type_v))
                return std::nullopt;
            if (type_v == "run_complete") {
                m.kind = MessageKind::RunComplete;
                return m;
            }
            if (type_v == "run_start") {
                m.kind = MessageKind::RunStart;
                rdr.read_field("filter", m.filter);
                rdr.read_field("is_full_run", m.is_full_run);
                return m;
            }
            return std::nullopt;
        }

        m.kind = MessageKind::Result;
        rdr.read_field("name", m.test_name);
        rdr.read_field("category", m.category);
        rdr.read_field("dataset_size", m.dataset_size);
        rdr.read_field("row_kind", m.row_kind);
        if (m.row_kind.empty())
            m.row_kind = std::string{kRowKindMeasurement};
        rdr.read_field("real_ns", m.real_ns);
        rdr.read_field("cpu_ns", m.cpu_ns);
        rdr.read_field("iterations", m.iterations);
        rdr.read_field("items_per_second", m.items_per_second);
        rdr.read_field("complexity_class", m.complexity_class);
        rdr.read_field("complexity_coef", m.complexity_coef);
        rdr.read_field("rms_pct", m.rms_pct);
        return m;
    }

} // namespace bench_dashboard::wire
