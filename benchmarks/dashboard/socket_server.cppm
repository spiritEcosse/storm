// AF_UNIX SOCK_DGRAM listener for the storm_bench_dashboard (Issue #247,
// Phase 2). One datagram = one NDJSON line, so we don't need stream parsing.
//
// Module-attached helper for the dashboard binary only — never imported by
// reporter.cpp (gbench-side code stays out of any module purview to keep its
// PCM-cache hash stable; see project_pcm_hash_divergence,
// feedback_bench_main_register_split). The reporter does its own raw `socket
// + sendto` calls — they're trivial and don't justify a shared abstraction
// across the gbench/storm boundary.

module;

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>

export module storm.bench_dashboard.socket_server;

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg,concurrency-mt-unsafe)
export namespace bench_dashboard {

    // RAII wrapper around an AF_UNIX SOCK_DGRAM listener. The socket file is
    // unlinked on shutdown — stale-file recovery on open() removes a previous
    // file at the same path before binding, so a crash that left the file
    // behind doesn't break the next run.
    class SocketServer {
      public:
        SocketServer() = default;

        SocketServer(SocketServer const&)                    = delete;
        auto operator=(SocketServer const&) -> SocketServer& = delete;

        SocketServer(SocketServer&& other) noexcept : fd_{other.fd_}, path_{std::move(other.path_)} {
            other.fd_ = -1;
        }

        auto operator=(SocketServer&& other) noexcept -> SocketServer& {
            if (this != &other) {
                shutdown();
                fd_       = other.fd_;
                path_     = std::move(other.path_);
                other.fd_ = -1;
            }
            return *this;
        }

        ~SocketServer() {
            shutdown();
        }

        // Open the listener. Returns empty string on success, an error
        // message otherwise. Unlinks any pre-existing file at `path` first
        // (stale socket recovery; the dashboard owns the file on this run).
        auto open(std::string_view path) -> std::string {
            shutdown();
            if (path.size() >= sizeof(sockaddr_un{}.sun_path)) {
                return "socket path too long";
            }

            fd_ = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            if (fd_ < 0) {
                return std::string{"socket(): "} + std::strerror(errno); // NOLINT(concurrency-mt-unsafe)
            }

            ::unlink(std::string{path}.c_str()); // best-effort stale cleanup

            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            std::memcpy(addr.sun_path, path.data(), path.size());
            addr.sun_path[path.size()] = '\0';

            // Portable addrlen: only the bytes actually used in sun_path
            // plus its trailing NUL. Using sizeof(addr) over-counts on Linux
            // and trips lints elsewhere.
            const auto addr_len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
            if (::bind(fd_, reinterpret_cast<sockaddr const*>(&addr), addr_len) != 0) {
                std::string err = std::string{"bind("} + std::string{path} +
                                  "): " + std::strerror(errno); // NOLINT(concurrency-mt-unsafe)
                ::close(fd_);
                fd_ = -1;
                return err;
            }

            // Bigger receive buffer — bench can fire results faster than the
            // dashboard renders. 1 MiB is well under the system default cap
            // and well above what gbench's 60 s/run can produce.
            const int rcvbuf = 1 << 20;
            if (::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) != 0) {
                // Non-fatal: kernel default rcvbuf is enough for normal
                // bench loads. Surface the error so a sandboxed env doesn't
                // hide it silently.
                std::
                        fprintf( // NOLINT(cppcoreguidelines-pro-type-vararg)
                                stderr,
                                "storm_bench_dashboard: setsockopt(SO_RCVBUF) failed: %s\n",
                                std::strerror(errno) // NOLINT(concurrency-mt-unsafe)
                        );
            }

            path_.assign(path);
            return {};
        }

        // Block up to `timeout_ms` for a datagram (-1 = forever). Retries on
        // EINTR transparently — caller never sees a spurious wakeup. Returns
        // the payload on success, std::nullopt on timeout or socket error.
        auto recv_one(int timeout_ms) const -> std::optional<
                std::string> { // NOLINT(readability-make-member-function-const) — reads from fd_ which is mutable state
            if (fd_ < 0) {
                return std::nullopt;
            }

            for (;;) {
                pollfd pfd{};
                pfd.fd     = fd_;
                pfd.events = POLLIN;

                const int prc = ::poll(&pfd, 1, timeout_ms);
                if (prc < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    return std::nullopt;
                }
                if (prc == 0) {
                    return std::nullopt; // timeout
                }

                // 64 KiB — well above any realistic gbench result line.
                std::string buf;
                buf.resize(
                        static_cast<std::size_t>(64) * 1024
                ); // NOLINT(bugprone-implicit-widening-of-multiplication-result)
                const ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);
                if (n < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    return std::nullopt;
                }
                if (n == 0) {
                    return std::nullopt;
                }
                buf.resize(static_cast<std::size_t>(n));
                return buf;
            }
        }

        auto shutdown() -> void {
            if (fd_ >= 0) {
                ::close(fd_);
                fd_ = -1;
            }
            if (!path_.empty()) {
                ::unlink(path_.c_str());
                path_.clear();
            }
        }

        [[nodiscard]] auto is_open() const noexcept -> bool {
            return fd_ >= 0;
        }
        [[nodiscard]] auto path() const noexcept -> std::string_view {
            return path_;
        }

      private:
        int         fd_{-1};
        std::string path_; // NOLINT(readability-redundant-member-init) — explicit default is intentional style
    };

} // namespace bench_dashboard
// NOLINTEND(cppcoreguidelines-pro-type-vararg,concurrency-mt-unsafe)
