find_program(CLANG_FORMAT_EXE NAMES clang-format)

if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE CLANG_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.ixx"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    )

    set(CLANG_FORMAT_STYLE "--style=file")
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.clang-format" AND
       NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/_clang-format")
        set(CLANG_FORMAT_STYLE "--style=LLVM")
    endif()

    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXE}" -i ${CLANG_FORMAT_STYLE} ${CLANG_FORMAT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running clang-format on source files"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND "${CLANG_FORMAT_EXE}" ${CLANG_FORMAT_STYLE} --dry-run --Werror ${CLANG_FORMAT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking code style with clang-format (fails on diffs)"
        VERBATIM
    )
else()
    message(STATUS "clang-format not found; 'format' and 'format-check' targets are disabled")
endif()
