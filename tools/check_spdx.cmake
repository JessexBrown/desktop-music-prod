# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR must be passed to check_spdx.cmake")
endif()

file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}" project_source_dir)
set(spdx_identifier "SPDX-License-Identifier: AGPL-3.0-or-later")
set(exception_file "${project_source_dir}/docs/SPDX_EXCEPTIONS.txt")

if(NOT EXISTS "${exception_file}")
    message(FATAL_ERROR "Missing SPDX exception file: ${exception_file}")
endif()

file(STRINGS "${exception_file}" exception_lines)
set(spdx_exceptions)

foreach(exception_line IN LISTS exception_lines)
    string(STRIP "${exception_line}" exception_line)

    if(exception_line STREQUAL "" OR exception_line MATCHES "^#")
        continue()
    endif()

    file(TO_CMAKE_PATH "${exception_line}" normalized_exception)
    list(APPEND spdx_exceptions "${normalized_exception}")

    if(NOT EXISTS "${project_source_dir}/${normalized_exception}")
        list(APPEND stale_exceptions "${normalized_exception}")
    endif()
endforeach()

set(first_party_globs
    "CMakeLists.txt"
    "CMakePresets.json"
    "AGENTS.md"
    "CODEX_STARTING_PROMPT.md"
    "GOAL.md"
    "LICENSE"
    "README.md"
    ".github/workflows/*.yml"
    "docs/*.md"
    "docs/*.txt"
    "docs/adr/*.md"
    "docs/issues/*.md"
    "src/*.cpp"
    "src/*.h"
    "src/*/*.cpp"
    "src/*/*.h"
    "src/*/*/*.cpp"
    "src/*/*/*.h"
    "tests/*.cpp"
    "tests/*.h"
    "tools/*.cmake")

set(first_party_files)

foreach(first_party_glob IN LISTS first_party_globs)
    file(GLOB_RECURSE matched_files LIST_DIRECTORIES false "${project_source_dir}/${first_party_glob}")
    list(APPEND first_party_files ${matched_files})
endforeach()

list(REMOVE_DUPLICATES first_party_files)

set(checked_file_count 0)

foreach(first_party_file IN LISTS first_party_files)
    file(RELATIVE_PATH relative_file "${project_source_dir}" "${first_party_file}")
    file(TO_CMAKE_PATH "${relative_file}" relative_file)

    if(relative_file MATCHES "^out/" OR relative_file MATCHES "^foss_daw_codex_starter_pack/")
        continue()
    endif()

    math(EXPR checked_file_count "${checked_file_count} + 1")

    file(STRINGS "${first_party_file}" file_header LIMIT_COUNT 12)
    set(has_spdx_identifier FALSE)

    foreach(header_line IN LISTS file_header)
        if(header_line MATCHES "SPDX-License-Identifier:[ \t]*AGPL-3\\.0-or-later")
            set(has_spdx_identifier TRUE)
            break()
        endif()
    endforeach()

    if(NOT has_spdx_identifier)
        list(FIND spdx_exceptions "${relative_file}" exception_index)
        if(exception_index EQUAL -1)
            list(APPEND missing_spdx_files "${relative_file}")
        endif()
    endif()
endforeach()

if(stale_exceptions)
    list(SORT stale_exceptions)
    string(REPLACE ";" "\n  " stale_exception_text "${stale_exceptions}")
    message(FATAL_ERROR "SPDX exception file contains missing paths:\n  ${stale_exception_text}")
endif()

if(missing_spdx_files)
    list(SORT missing_spdx_files)
    string(REPLACE ";" "\n  " missing_file_text "${missing_spdx_files}")
    message(FATAL_ERROR
        "First-party files are missing '${spdx_identifier}' and are not listed in docs/SPDX_EXCEPTIONS.txt:\n"
        "  ${missing_file_text}")
endif()

list(LENGTH spdx_exceptions exception_count)
message(STATUS "SPDX check passed for ${checked_file_count} first-party files with ${exception_count} baseline exceptions.")
