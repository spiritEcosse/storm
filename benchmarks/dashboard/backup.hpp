#pragma once

// Backup / restore subcommands for storm_bench_dashboard (Issue #247).
//
// Textual header — `#include`d from the anonymous namespace of main.cpp so
// every helper here lives at internal linkage in that TU. Split out of
// main.cpp purely to keep each TU under the 600-line code-quality cap.
// Do NOT include this from any other TU; depends on `Options` from
// args.hpp being in scope.
//
// import std; migration (issue #326): no std #includes here — std types come
// from main.cpp's `import std;` (this header is pulled in after it). A textual
// std #include after the module import breaks the build (Finding B).

namespace {

    // -----------------------------------------------------------------------
    // Backup / restore helpers
    //
    // Both operations shell out to `gh` because that's the path of least
    // resistance: gh handles auth, retries, and rate limits. Pre-existing
    // `gh auth login` is required — we surface a clear error otherwise.
    // -----------------------------------------------------------------------

    // Wrap a string for safe inclusion as a single-quoted shell argument.
    // GNU shells allow any byte except `'` inside single quotes, so an
    // embedded apostrophe must close the quote, append an escaped `\'`, and
    // reopen the quote. Used for db paths, repo, and tag values that flow
    // unmodified into a popen() command line.
    auto shell_single_quote(std::string_view s) -> std::string {
        std::string out;
        out.reserve(s.size() + 2);
        out += '\'';
        for (char c : s) {
            if (c == '\'') {
                out += "'\\''";
            } else {
                out += c;
            }
        }
        out += '\'';
        return out;
    }

    // Run `cmd`, capture combined stdout+stderr, return exit_status + output.
    // popen()'s default mode "r" forwards stderr through the parent shell
    // unless the caller redirects — we pass `2>&1` in the command string so
    // gh's friendly error messages reach us here.
    struct ShellResult {
        int         exit_status{};
        std::string output{};
    };

    auto run_shell(std::string const& cmd) -> ShellResult {
        ShellResult                                   result;
        std::unique_ptr<std::FILE, decltype(&pclose)> pipe{::popen(cmd.c_str(), "r"), &::pclose};
        if (!pipe) {
            result.exit_status = -1;
            result.output      = std::format("popen() failed: {}", std::strerror(errno));
            return result;
        }
        char buf[1024]{};
        while (std::fgets(buf, sizeof(buf), pipe.get()) != nullptr)
            result.output.append(buf);
        // Reclaim ownership so we can read pclose()'s return code; the
        // unique_ptr destructor would otherwise discard it.
        std::FILE* raw     = pipe.release();
        const int  rc      = ::pclose(raw);
        result.exit_status = rc;
        return result;
    }

    auto check_gh_auth() -> std::expected<void, std::string> {
        const auto rc = run_shell("gh auth status >/dev/null 2>&1");
        if (rc.exit_status != 0)
            return std::unexpected(std::string{"gh CLI is not authenticated — run `gh auth login` first"});
        return {};
    }

    // Verify the backup repo exists and is reachable. Returns the gh stderr
    // on failure so the caller can surface the actual reason (missing,
    // private without access, etc.).
    auto check_backup_repo_exists(std::string_view repo) -> std::expected<void, std::string> {
        const auto cmd = std::format("gh repo view {} >/dev/null 2>&1", shell_single_quote(repo));
        const auto rc  = run_shell(cmd);
        if (rc.exit_status != 0)
            return std::unexpected(
                    std::format(
                            "backup repo '{}' is not reachable — create it first: gh repo create {} --private",
                            repo,
                            repo
                    )
            );
        return {};
    }

    // Print a "uploaded/restored <db_path> {to,from} <repo>@<tag>" line on
    // stdout. Single source of truth for the success message shape.
    auto log_backup_action(char const* verb, char const* preposition, Options const& opts) -> void {
        std::fprintf(
                stdout,
                "storm_bench_dashboard: %s %s %s %s@%s\n",
                verb,
                opts.db_path.c_str(),
                preposition,
                opts.backup_repo.c_str(),
                opts.backup_tag.c_str()
        );
    }

    // Run `gh auth status` + `gh repo view` and surface errors with the
    // dashboard prefix. Returns 0 on success, 1 (already-printed) on any
    // failure — caller should propagate the code.
    auto preflight_gh(std::string_view repo) -> int {
        if (auto rc = check_gh_auth(); !rc) {
            std::fprintf(stderr, "storm_bench_dashboard: %s\n", rc.error().c_str());
            return 1;
        }
        if (auto rc = check_backup_repo_exists(repo); !rc) {
            std::fprintf(stderr, "storm_bench_dashboard: %s\n", rc.error().c_str());
            return 1;
        }
        return 0;
    }

    // One-shot upload. Uses `gh release upload --clobber` so re-running
    // overwrites the existing asset for that tag. Tag is created on first
    // upload via `gh release create`; subsequent uploads attach to it.
    auto upload_backup(Options const& opts) -> int {
        if (const int rc = preflight_gh(opts.backup_repo); rc != 0)
            return rc;
        if (std::error_code ec; !std::filesystem::exists(opts.db_path, ec) || ec) {
            std::fprintf(stderr, "storm_bench_dashboard: db file not found: %s\n", opts.db_path.c_str());
            return 1;
        }

        const auto repo_q = shell_single_quote(opts.backup_repo);
        const auto tag_q  = shell_single_quote(opts.backup_tag);
        const auto file_q = shell_single_quote(opts.db_path);

        // gh release upload fails if the release does not yet exist for the
        // tag. Try `release view` first; create the release on miss.
        const auto view_cmd = std::format("gh release view {} --repo {} >/dev/null 2>&1", tag_q, repo_q);
        if (run_shell(view_cmd).exit_status != 0) {
            const auto create_cmd = std::format(
                    "gh release create {} --repo {} --notes 'storm_bench dashboard backup' 2>&1", tag_q, repo_q
            );
            const auto create_rc = run_shell(create_cmd);
            if (create_rc.exit_status != 0) {
                std::fprintf(stderr, "storm_bench_dashboard: gh release create failed: %s\n", create_rc.output.c_str());
                return 1;
            }
        }

        const auto upload_cmd = std::format("gh release upload --clobber {} {} --repo {} 2>&1", tag_q, file_q, repo_q);
        const auto upload_rc  = run_shell(upload_cmd);
        if (upload_rc.exit_status != 0) {
            std::fprintf(stderr, "storm_bench_dashboard: gh release upload failed: %s\n", upload_rc.output.c_str());
            return 1;
        }
        log_backup_action("uploaded", "to", opts);
        return 0;
    }

    // One-shot restore. Downloads the asset matching the db filename into
    // the parent directory of --db. We deliberately do not auto-rename the
    // existing local file; the user is warned to move it themselves.
    auto restore_backup(Options const& opts) -> int {
        if (const int rc = preflight_gh(opts.backup_repo); rc != 0)
            return rc;

        const std::filesystem::path db{opts.db_path};
        const std::filesystem::path dir      = db.parent_path().empty() ? std::filesystem::path{"."} : db.parent_path();
        const std::string           filename = db.filename().string();

        if (std::error_code ec; std::filesystem::exists(opts.db_path, ec) && !ec) {
            std::fprintf(
                    stderr,
                    "storm_bench_dashboard: warning — '%s' already exists; "
                    "rename or move it first if you want to keep the current copy. "
                    "Restore will overwrite it.\n",
                    opts.db_path.c_str()
            );
        }

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::fprintf(stderr, "storm_bench_dashboard: cannot create '%s': %s\n", dir.c_str(), ec.message().c_str());
            return 1;
        }

        const auto repo_q    = shell_single_quote(opts.backup_repo);
        const auto tag_q     = shell_single_quote(opts.backup_tag);
        const auto dir_q     = shell_single_quote(dir.string());
        const auto pattern_q = shell_single_quote(filename);
        const auto cmd       = std::format(
                "gh release download {} --repo {} --dir {} --pattern {} --clobber 2>&1", tag_q, repo_q, dir_q, pattern_q
        );
        const auto rc = run_shell(cmd);
        if (rc.exit_status != 0) {
            std::fprintf(stderr, "storm_bench_dashboard: gh release download failed: %s\n", rc.output.c_str());
            return 1;
        }
        log_backup_action("restored", "from", opts);
        return 0;
    }

} // namespace
