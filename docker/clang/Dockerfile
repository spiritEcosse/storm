# Minimal image containing only pre-built clang-p2996 (bloomberg/clang-p2996) artifacts.
# Used as a source for COPY --from in CI images.
#
# Build context: the clang-p2996 repository root (where build/ lives).
#
#   docker build --load -t ghcr.io/spiritecosse/storm-clang:latest \
#       -f storm/docker/clang/Dockerfile ../clang-p2996
#
FROM scratch

# Compiler binaries
COPY build/bin/clang-21        /opt/clang-p2996/build/bin/clang-21
COPY build/bin/clang-scan-deps /opt/clang-p2996/build/bin/clang-scan-deps
COPY build/bin/clang-format    /opt/clang-p2996/build/bin/clang-format
COPY build/bin/clang-tidy      /opt/clang-p2996/build/bin/clang-tidy

# Compiler runtime (builtins, sanitizer runtimes, etc.)
COPY build/lib/clang/ /opt/clang-p2996/build/lib/clang/

# Custom libc++ / libc++abi / libunwind
COPY build/lib/x86_64-unknown-linux-gnu/ /opt/clang-p2996/build/lib/x86_64-unknown-linux-gnu/

# C++26 standard library headers
COPY build/include/ /opt/clang-p2996/build/include/
