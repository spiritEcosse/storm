#pragma once

#include <string>
#include <optional>
#include <array>
#include <memory>
#include <stdexcept>

namespace storm::benchmark {

/**
 * Git integration utilities for benchmark tracking
 */
class GitInfo {
public:
    /**
     * Execute a shell command and capture output
     */
    static std::optional<std::string> exec_command(const char* cmd) {
        std::array<char, 128> buffer;
        std::string result;

        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
        if (!pipe) {
            return std::nullopt;
        }

        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }

        // Remove trailing newline
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }

        return result.empty() ? std::nullopt : std::optional{result};
    }

    /**
     * Get current git commit hash (short)
     */
    static std::optional<std::string> get_commit_hash() {
        return exec_command("git rev-parse --short HEAD");
    }

    /**
     * Get current git commit hash (full)
     */
    static std::optional<std::string> get_commit_hash_full() {
        return exec_command("git rev-parse HEAD");
    }

    /**
     * Get current git branch name
     */
    static std::optional<std::string> get_branch_name() {
        return exec_command("git rev-parse --abbrev-ref HEAD");
    }

    /**
     * Check if working directory has uncommitted changes
     */
    static bool has_uncommitted_changes() {
        auto result = exec_command("git status --porcelain");
        return result.has_value() && !result->empty();
    }

    /**
     * Get commit hash with dirty flag if needed
     */
    static std::string get_commit_hash_with_status() {
        auto hash = get_commit_hash();
        if (!hash.has_value()) {
            return "unknown";
        }

        if (has_uncommitted_changes()) {
            return *hash + "-dirty";
        }

        return *hash;
    }

    /**
     * Get full git context for benchmark run
     */
    struct Context {
        std::string commit_hash;
        std::string branch_name;
        bool has_uncommitted;
    };

    static Context get_context() {
        Context ctx;

        auto hash = get_commit_hash();
        ctx.commit_hash = hash.value_or("unknown");

        auto branch = get_branch_name();
        ctx.branch_name = branch.value_or("unknown");

        ctx.has_uncommitted = has_uncommitted_changes();

        return ctx;
    }
};

}  // namespace storm::benchmark
