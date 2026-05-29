// Phase-2 probe for issue #326: validates that `import std;` builds and
// links against clang-p2996 + libc++ via CMake's CXX_MODULE_STD support.
//
// This target is intentionally isolated: it does NOT import storm and does
// NOT link any third-party library. If it builds and prints the expected
// line, the std-module plumbing (UUID gate, libc++.modules.json,
// share/libc++/v1 symlink, no-fmodules invocation) is correct.

import std;

auto main() -> int {
    std::println("import std: ok");
    return 0;
}
