# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED SPDX_FIXTURE_ROOT)
    message(FATAL_ERROR "SPDX_FIXTURE_ROOT must be passed to check_spdx_fixtures.cmake")
endif()

set(check_spdx_script "${CMAKE_CURRENT_LIST_DIR}/check_spdx.cmake")
get_filename_component(spdx_fixture_root_absolute "${SPDX_FIXTURE_ROOT}" ABSOLUTE)
file(TO_CMAKE_PATH "${spdx_fixture_root_absolute}" spdx_fixture_root)

file(REMOVE_RECURSE "${spdx_fixture_root}")
file(MAKE_DIRECTORY "${spdx_fixture_root}")

function(write_fixture_file fixture_root relative_path contents)
    set(file_path "${fixture_root}/${relative_path}")
    get_filename_component(parent_path "${file_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent_path}")
    file(WRITE "${file_path}" "${contents}")
endfunction()

function(run_spdx_fixture fixture_name expected_result expected_pattern)
    set(fixture_dir "${spdx_fixture_root}/${fixture_name}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DPROJECT_SOURCE_DIR=${fixture_dir}
            -DPROJECTNAME_EXPECT_SPDX_EXCEPTION_COUNT=0
            -P "${check_spdx_script}"
        RESULT_VARIABLE fixture_result
        OUTPUT_VARIABLE fixture_output
        ERROR_VARIABLE fixture_error)

    set(combined_output "${fixture_output}${fixture_error}")

    if(expected_result STREQUAL "PASS")
        if(NOT fixture_result EQUAL 0)
            message(FATAL_ERROR
                "SPDX fixture '${fixture_name}' was expected to pass but failed:\n"
                "${combined_output}")
        endif()
    elseif(expected_result STREQUAL "FAIL")
        if(fixture_result EQUAL 0)
            message(FATAL_ERROR
                "SPDX fixture '${fixture_name}' was expected to fail but passed.")
        endif()
    else()
        message(FATAL_ERROR "Unknown SPDX fixture expectation: ${expected_result}")
    endif()

    if(NOT combined_output MATCHES "${expected_pattern}")
        message(FATAL_ERROR
            "SPDX fixture '${fixture_name}' output did not match '${expected_pattern}':\n"
            "${combined_output}")
    endif()
endfunction()

set(valid_fixture "${spdx_fixture_root}/valid-vendor-json")
write_fixture_file(
    "${valid_fixture}"
    "docs/SPDX_EXCEPTIONS.txt"
    "# SPDX-License-Identifier: AGPL-3.0-or-later\n")
write_fixture_file(
    "${valid_fixture}"
    "CMakePresets.json"
    "{\n  \"version\": 6,\n  \"vendor\": {\n    \"rabbington.studio/license\": {\n      \"SPDX-License-Identifier\": \"AGPL-3.0-or-later\"\n    }\n  }\n}\n")
run_spdx_fixture("valid-vendor-json" "PASS" "0 baseline exceptions")

set(missing_header_fixture "${spdx_fixture_root}/missing-header")
write_fixture_file(
    "${missing_header_fixture}"
    "docs/SPDX_EXCEPTIONS.txt"
    "# SPDX-License-Identifier: AGPL-3.0-or-later\n")
write_fixture_file(
    "${missing_header_fixture}"
    "README.md"
    "# Missing Header\n\nThis first-party file intentionally lacks an SPDX header.\n")
run_spdx_fixture("missing-header" "FAIL" "README\\.md")

set(stale_exception_fixture "${spdx_fixture_root}/stale-exception")
write_fixture_file(
    "${stale_exception_fixture}"
    "docs/SPDX_EXCEPTIONS.txt"
    "# SPDX-License-Identifier: AGPL-3.0-or-later\nmissing-file.md\n")
write_fixture_file(
    "${stale_exception_fixture}"
    "README.md"
    "<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->\n\n# Fixture\n")
run_spdx_fixture("stale-exception" "FAIL" "missing-file\\.md")

set(exception_count_fixture "${spdx_fixture_root}/exception-count")
write_fixture_file(
    "${exception_count_fixture}"
    "docs/SPDX_EXCEPTIONS.txt"
    "# SPDX-License-Identifier: AGPL-3.0-or-later\nREADME.md\n")
write_fixture_file(
    "${exception_count_fixture}"
    "README.md"
    "# Accepted Baseline Exception\n\nThis fixture file is listed as a baseline exception.\n")
run_spdx_fixture("exception-count" "FAIL" "expected 0")

message(STATUS "SPDX checker fixtures passed.")
