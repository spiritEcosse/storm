# Storm CI/dev image: Arch base + system deps + prebuilt clang-p2996.
#
# Used as the `container:` for GitHub Actions CI jobs and as the recommended
# build environment for outside contributors who don't want to build clang-p2996
# from source.
#
#   docker build -t storm-ci:dev -f docker/ci/Dockerfile .
#   docker run --rm -v "$(pwd):/storm" -w /storm storm-ci:dev \
#       cmake --preset ninja-release && cmake --build --preset ninja-release
#
FROM manjarolinux/base:20260315

# System dependencies. valgrind is included for future CodSpeed Macro /
# cachegrind work; lcov so the same image works for coverage builds.
RUN pacman -Syu --noconfirm \
    && pacman -S --noconfirm --needed \
        cmake ninja gcc sqlite postgresql-libs \
        python python-yaml git valgrind lcov \
    && pacman -Scc --noconfirm

# Prebuilt clang-p2996 from the sibling data-only image.
COPY --from=ghcr.io/spiritecosse/storm-clang:latest \
     /opt/clang-p2996 /opt/clang-p2996

# clang/clang++ symlinks (storm-clang ships clang-21 only).
RUN ln -s clang-21 /opt/clang-p2996/build/bin/clang \
    && ln -s clang-21 /opt/clang-p2996/build/bin/clang++

ENV LIBCXX_ROOT=/opt/clang-p2996 \
    PATH=/opt/clang-p2996/build/bin:$PATH

# Storm's CMakePresets.json references `${sourceDir}/../clang-p2996`. We resolve
# that symlink two ways:
#
#   1. Baked-in for GitHub Actions: `container:` jobs mount the workspace at
#      /__w/<repo>/<repo>, so /__w/clang-p2996 is the ${sourceDir}/../clang-p2996
#      Storm expects. GHA overrides ENTRYPOINT, so the symlink must already exist.
#
#   2. Runtime entrypoint for local `docker run`: the entrypoint creates the
#      symlink at $(dirname $PWD)/clang-p2996 wherever the workspace is mounted.
#
RUN mkdir -p /__w \
    && ln -s /opt/clang-p2996 /__w/clang-p2996

COPY docker/ci/entrypoint.sh /usr/local/bin/storm-ci-entrypoint
RUN chmod +x /usr/local/bin/storm-ci-entrypoint

ENTRYPOINT ["/usr/local/bin/storm-ci-entrypoint"]
CMD ["bash"]
