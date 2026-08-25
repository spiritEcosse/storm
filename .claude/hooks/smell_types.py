"""Thresholds and fix messages for code smell detection."""

MAX_COMPLEXITY = 10
MAX_FUNCTION_LINES = 60      # C++ functions are naturally longer than Python
MAX_NESTING_DEPTH = 4        # C++ has more nesting (namespaces, classes, templates)
MAX_PARAMETERS = 6           # C++ often needs more params (no kwargs)
MAX_FILE_LINES = 600         # C++ modules/headers are larger than Python files
DUPLICATE_MIN_LINES = 6
DUPLICATE_MIN_OCCURRENCES = 2

FIXES = {
    "complexity": (
        "Cyclomatic complexity too high. "
        "Extract conditional branches into named helper functions. "
        "Replace nested if/else chains with early returns or a lookup table."
    ),
    "length": (
        "Function is too long. "
        "Split into smaller functions, each doing exactly one thing. "
        "A function name should describe its single responsibility."
    ),
    "nesting": (
        "Nesting too deep. "
        "Use early returns/guard clauses to flatten control flow. "
        "Extract inner blocks into named helper functions."
    ),
    "parameters": (
        "Too many parameters. "
        "Group related params into a struct or pass a config/options object. "
        "Consider if this function is doing too much."
    ),
    "file_size": (
        "File is too large. "
        "Split into focused modules, each with a single responsibility. "
        "Move related types/functions into their own header or source file."
    ),
    "duplicates": (
        "Duplicate code blocks detected. "
        "Extract the repeated logic into a shared helper function. "
        "DRY: Don't Repeat Yourself."
    ),
}

SKIP_DIRS = {
    "node_modules", "__pycache__", ".git", "dist", "build",
    ".next", "vendor", "third_party", "external", "_deps",
}
