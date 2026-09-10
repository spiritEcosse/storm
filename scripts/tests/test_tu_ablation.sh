#!/bin/bash
# Tests for scripts/tu_ablation.py's source transforms.
#
# Those transforms rewrite a real test file and the result is COMPILED and
# TIMED, so a mis-parse does not crash — it produces a plausible number that
# lands in docs/internals/performance/COMPILE_TIME.md. Three such bugs shipped
# and were caught in review rather than by running anything: an apostrophe in a
# prose comment read as a character literal (swallowing the rest of the file), a
# semicolon inside a string literal ending an assertion's replacement span, and
# a fixture-narrowing regex hardcoded to one model name that silently no-ops on
# 11 of the tree's 13 multi-model fixtures — reporting "an extra fixture model
# is free" when the measured answer is 0.58 s.
#
# So every scenario below pins a case where the transform must be exactly right
# or fail LOUDLY. None of them needs a compiler.

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ABLATE="$REPO_ROOT/scripts/tu_ablation.py"

PASS=0
FAIL=0
FAILED_TESTS=()
CURRENT_TAG=""

fail() {
    echo "  FAIL: $1"
    FAIL=$((FAIL+1))
    FAILED_TESTS+=("$CURRENT_TAG")
    return 0
}

pass() {
    echo "  ok: $1"
    PASS=$((PASS+1))
    return 0
}

# Run the python snippet on stdin against the module under test. The module is
# loaded by path rather than imported, so the test does not depend on cwd or
# PYTHONPATH, and each scenario feeds its snippet from a QUOTED heredoc so the
# shell cannot mangle the quotes inside it.
run_py() {
    {
        cat <<'PRELUDE'
import importlib.util, pathlib, sys
spec = importlib.util.spec_from_file_location("tu_ablation", sys.argv[1])
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
PRELUDE
        cat
    } | python3 - "$ABLATE" 2>&1
}

# Compare a snippet's output (read from stdin) against an expected string.
check() {
    local what="$1" expected="$2" actual
    actual="$(run_py)"
    if [[ "$actual" == "$expected" ]]; then
        pass "$what"
    else
        fail "$what: expected '$expected', got '$actual'"
    fi
}

# Assert a snippet (read from stdin) fails with a message containing a needle.
check_fails() {
    local what="$1" needle="$2" actual
    actual="$(run_py)"
    if [[ "$actual" == *"$needle"* ]]; then
        pass "$what"
    else
        fail "$what: expected a failure mentioning '$needle', got '$actual'"
    fi
}

# --- the apostrophe that started it ----------------------------------------
scenario_mask_comment_apostrophe() {
    check "an apostrophe in a comment does not open a literal" "OK" <<'SNIPPET'
text = "// don't do this\nint a = 1;\nvoid f() { int b = 2; }\n"
m = mod.mask_literals(text)
print("OK" if m.count("{") == 1 and m.count("}") == 1 else "MASKED AWAY THE CODE")
SNIPPET
}

# --- the semicolon inside a streamed message --------------------------------
scenario_semicolon_in_string() {
    check "a ';' inside a streamed message does not end the span" "OK" <<'SNIPPET'
text = ('#include <gtest/gtest.h>\nimport std;\nTEST(S, T) {\n'
        '    EXPECT_TRUE(found) << "truncated; here";\n}\n')
out = mod.sink_asserts(text)
print("OK" if '"truncated; here"' not in out and out.count("storm_probe_sink") == 2
      else "SPAN ENDED INSIDE THE LITERAL")
SNIPPET
}

# --- the fixture narrowing that silently did nothing ------------------------
scenario_fixture_narrowing_is_generic() {
    check "narrowing works on a fixture that is not <Person, ConnType, ...>" "OK" <<'SNIPPET'
text = ('import std;\ntemplate <typename ConnType>\n'
        'class T : public StormTestFixture<Student, ConnType, Course> {};\n'
        'TYPED_TEST(T, A) { EXPECT_EQ(1, 1); }\n')
out = mod.trivial_first(text, one_model=True)
print("OK" if "StormTestFixture<Student, ConnType>" in out else "SILENTLY UNCHANGED")
SNIPPET
}

scenario_fixture_narrowing_fails_loudly() {
    check_fails "narrowing a file with no fixture exits with an explanation" \
        "silently measure the same thing" <<'SNIPPET'
mod.trivial_first("import std;\nTYPED_TEST(T, A) { EXPECT_EQ(1, 1); }\n", one_model=True)
SNIPPET
}

# --- merging across macros changes what is instantiated ---------------------
scenario_merge_refuses_mixed_macros() {
    check_fails "merging a file that mixes macros/suites exits with an explanation" \
        "cannot merge" <<'SNIPPET'
text = ("import std;\nTYPED_TEST(S, A) { EXPECT_EQ(1, 1); }\n"
        "TEST(Other, B) { EXPECT_EQ(2, 2); }\n")
mod.merge_blocks(text)
SNIPPET
}

# --- the scanner must not run off the end silently --------------------------
scenario_unterminated_block_comment_raises() {
    check "an unterminated /* raises instead of masking to EOF" "ValueError" <<'SNIPPET'
try:
    mod.mask_literals("int a = 1; /* never closed\n")
    print("NO ERROR")
except ValueError:
    print("ValueError")
SNIPPET
}

scenario_unterminated_raw_string_raises() {
    check "an unterminated raw string raises instead of masking to EOF" "ValueError" <<'SNIPPET'
try:
    mod.mask_literals('auto s = R"delim(never closed\n')
    print("NO ERROR")
except ValueError:
    print("ValueError")
SNIPPET
}

# --- block discovery against the real tree ----------------------------------
scenario_block_discovery() {
    check "TYPED_TEST_SUITE is not counted as a block" "1" <<'SNIPPET'
text = ("import std;\nTYPED_TEST_SUITE(S, DatabaseTypes);\n"
        "TYPED_TEST(S, A) { EXPECT_EQ(1, 1); }\n")
print(len(mod.blocks(text)))
SNIPPET
}

scenario_every_test_file_parses() {
    check "every tests/**/*.cpp parses without a scanner error" "OK" <<'SNIPPET'
bad = []
for f in sorted(pathlib.Path(sys.argv[1]).parent.parent.joinpath("tests").rglob("*.cpp")):
    try:
        mod.blocks(f.read_text(encoding="utf-8"))
    except Exception as exc:
        bad.append(f"{f}: {exc}")
print("OK" if not bad else "\n".join(bad))
SNIPPET
}

for tag in mask_comment_apostrophe semicolon_in_string \
           fixture_narrowing_is_generic fixture_narrowing_fails_loudly \
           merge_refuses_mixed_macros unterminated_block_comment_raises \
           unterminated_raw_string_raises block_discovery every_test_file_parses; do
    CURRENT_TAG="$tag"
    echo ""
    echo "Scenario: $tag"
    "scenario_$tag"
done

echo ""
echo "================================================"
echo "Passed: $PASS, Failed: $FAIL"

if [[ $FAIL -gt 0 ]]; then
    echo "Failed scenarios: ${FAILED_TESTS[*]}"
    exit 1
fi
exit 0
