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
# github-cli + jq + python-numpy/scipy are for the bench.yml regression gate
# (#241): gh fetches the develop-baseline-latest artifact and posts the PR
# comment, jq renders the comparison table, numpy/scipy back compare.py's
# Mann-Whitney U-test in benchmarks/scripts/compare_against_baseline.sh.
RUN pacman -Syu --noconfirm \
    && pacman -S --noconfirm --needed \
        cmake ninja gcc sqlite postgresql-libs \
        python python-yaml python-numpy python-scipy git valgrind lcov \
        github-cli jq \
    && pacman -Scc --noconfirm

# Prebuilt clang-p2996 from the sibling data-only image.
COPY --from=ghcr.io/spiritecosse/storm-clang:latest \
     /opt/clang-p2996 /opt/clang-p2996

# clang/clang++ symlinks (storm-clang ships clang-21 only).
RUN ln -s clang-21 /opt/clang-p2996/build/bin/clang \
    && ln -s clang-21 /opt/clang-p2996/build/bin/clang++

ENV LIBCXX_ROOT=/opt/clang-p2996 \
    PATH=/opt/clang-p2996/build/bin:$PATH

# Storm's CMakePresets.json references `${sourceDir}/../clang-p2996`. The
# entrypoint creates that symlink at $(dirname $PWD) on every container start,
# so the image works regardless of where the workspace is mounted. GitHub
# Actions `container:` jobs override ENTRYPOINT and need a per-job shim step
# (`ln -s /opt/clang-p2996 ../clang-p2996`) — see .github/workflows/ci.yml.
COPY docker/ci/entrypoint.sh /usr/local/bin/storm-ci-entrypoint
RUN chmod +x /usr/local/bin/storm-ci-entrypoint

ENTRYPOINT ["/usr/local/bin/storm-ci-entrypoint"]
CMD ["bash"]
