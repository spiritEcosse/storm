# Getting Started

Storm builds against a custom Clang with C++26 reflection (`clang-p2996`). Two
ways to get a working build environment:

## Option 1 (recommended) — Build via Docker

Use the prebuilt `storm-ci` image. It bundles clang-p2996, libc++, and all
system dependencies. No toolchain build from source.

```bash
git clone https://github.com/spiritEcosse/storm.git
cd storm

# One-shot release build inside the image.
docker run --rm -v "$(pwd):/storm" -w /storm \
    ghcr.io/spiritecosse/storm-ci:latest \
    bash -c "cmake --preset ninja-release && cmake --build --preset ninja-release"

# Or drop into an interactive shell for iterative work.
docker run --rm -it -v "$(pwd):/storm" -w /storm \
    ghcr.io/spiritecosse/storm-ci:latest
```

The image's entrypoint creates the `${sourceDir}/../clang-p2996` symlink Storm's
`CMakePresets.json` expects, regardless of where you mount the workspace.

The same image runs in CI, so a green local build is a strong predictor of
green CI.

## Option 2 — Build clang-p2996 from source

If you can't use Docker (e.g. host doesn't support it, or you're working on the
toolchain itself), follow the upstream build instructions for
[bloomberg/clang-p2996](https://github.com/bloomberg/clang-p2996) and place the
result at `../clang-p2996` (sibling to the Storm source tree). Expect a
multi-hour build.

Once `../clang-p2996/build/bin/clang` exists, the standard preset commands work:

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release
```

## Next steps

- Common workflows: [COMMON_TASKS.md](COMMON_TASKS.md)
- Running tests: [TESTING.md](TESTING.md)
- Coverage: [CODE_COVERAGE.md](CODE_COVERAGE.md)
- Adding features: [ADDING_FEATURES.md](ADDING_FEATURES.md)
