# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED CI_ARTIFACT_FIXTURE_ROOT)
    message(FATAL_ERROR
        "CI_ARTIFACT_FIXTURE_ROOT must be passed to check_ci_artifact_contents_fixtures.cmake")
endif()

set(check_artifact_script "${CMAKE_CURRENT_LIST_DIR}/check_ci_artifact_contents.cmake")
get_filename_component(ci_artifact_fixture_root_absolute "${CI_ARTIFACT_FIXTURE_ROOT}" ABSOLUTE)
file(TO_CMAKE_PATH "${ci_artifact_fixture_root_absolute}" ci_artifact_fixture_root)

file(REMOVE_RECURSE "${ci_artifact_fixture_root}")
file(MAKE_DIRECTORY "${ci_artifact_fixture_root}")

function(write_fixture_file fixture_root relative_path contents)
    set(file_path "${fixture_root}/${relative_path}")
    get_filename_component(parent_path "${file_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent_path}")
    file(WRITE "${file_path}" "${contents}")
endfunction()

function(write_fixture_checksums fixture_root)
    set(checksum_paths
        "${ARGN}"
        "LICENSE"
        "README.md"
        "docs/DEPENDENCIES.md"
        "ARTIFACT.txt")

    set(checksum_text "")
    foreach(checksum_path IN LISTS checksum_paths)
        file(SHA256 "${fixture_root}/${checksum_path}" checksum_hash)
        string(APPEND checksum_text "${checksum_hash}  ${checksum_path}\n")
    endforeach()

    file(WRITE "${fixture_root}/SHA256SUMS.txt" "${checksum_text}")
endfunction()

function(write_valid_artifact fixture_root executable_name)
    write_fixture_file("${fixture_root}" "${executable_name}" "test executable\n")
    write_fixture_file("${fixture_root}" "LICENSE" "test license\n")
    write_fixture_file("${fixture_root}" "README.md" "test readme\n")
    write_fixture_file("${fixture_root}" "docs/DEPENDENCIES.md" "test dependencies\n")
    write_fixture_file("${fixture_root}" "ARTIFACT.txt" "test artifact note\n")
    write_fixture_checksums("${fixture_root}" "${executable_name}")
endfunction()

function(run_artifact_fixture fixture_name executable_name expected_result expected_pattern)
    set(fixture_dir "${ci_artifact_fixture_root}/${fixture_name}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DARTIFACT_ROOT=${fixture_dir}
            "-DARTIFACT_EXECUTABLE_NAME=${executable_name}"
            -P "${check_artifact_script}"
        RESULT_VARIABLE fixture_result
        OUTPUT_VARIABLE fixture_output
        ERROR_VARIABLE fixture_error)

    set(combined_output "${fixture_output}${fixture_error}")

    if(expected_result STREQUAL "PASS")
        if(NOT fixture_result EQUAL 0)
            message(FATAL_ERROR
                "CI artifact fixture '${fixture_name}' was expected to pass but failed:\n"
                "${combined_output}")
        endif()
    elseif(expected_result STREQUAL "FAIL")
        if(fixture_result EQUAL 0)
            message(FATAL_ERROR
                "CI artifact fixture '${fixture_name}' was expected to fail but passed.")
        endif()
    else()
        message(FATAL_ERROR "Unknown CI artifact fixture expectation: ${expected_result}")
    endif()

    if(NOT combined_output MATCHES "${expected_pattern}")
        message(FATAL_ERROR
            "CI artifact fixture '${fixture_name}' output did not match '${expected_pattern}':\n"
            "${combined_output}")
    endif()
endfunction()

set(valid_windows_fixture "${ci_artifact_fixture_root}/valid-windows")
write_valid_artifact("${valid_windows_fixture}" "Rabbington Studio.exe")
run_artifact_fixture("valid-windows" "Rabbington Studio.exe" "PASS" "CI artifact contents check passed")

set(valid_linux_fixture "${ci_artifact_fixture_root}/valid-linux")
write_valid_artifact("${valid_linux_fixture}" "Rabbington Studio")
run_artifact_fixture("valid-linux" "Rabbington Studio" "PASS" "CI artifact contents check passed")

set(unexpected_cache_fixture "${ci_artifact_fixture_root}/unexpected-cache")
write_valid_artifact("${unexpected_cache_fixture}" "Rabbington Studio")
write_fixture_file("${unexpected_cache_fixture}" ".cache/fetchcontent/juce-src/README.md" "cache leak\n")
run_artifact_fixture("unexpected-cache" "Rabbington Studio" "FAIL" "\\.cache/fetchcontent")

set(unexpected_plugin_fixture "${ci_artifact_fixture_root}/unexpected-plugin")
write_valid_artifact("${unexpected_plugin_fixture}" "Rabbington Studio")
write_fixture_file("${unexpected_plugin_fixture}" "plugins/unknown-device.vst3/Contents/info.txt" "plugin leak\n")
run_artifact_fixture("unexpected-plugin" "Rabbington Studio" "FAIL" "plugins/unknown-device\\.vst3")

set(unexpected_media_fixture "${ci_artifact_fixture_root}/unexpected-media")
write_valid_artifact("${unexpected_media_fixture}" "Rabbington Studio")
write_fixture_file("${unexpected_media_fixture}" "samples/one-shot.wav" "sample leak\n")
write_fixture_file("${unexpected_media_fixture}" "presets/default-device.json" "preset leak\n")
run_artifact_fixture("unexpected-media" "Rabbington Studio" "FAIL" "presets/default-device\\.json")

set(checksum_mismatch_fixture "${ci_artifact_fixture_root}/checksum-mismatch")
write_valid_artifact("${checksum_mismatch_fixture}" "Rabbington Studio")
write_fixture_file("${checksum_mismatch_fixture}" "ARTIFACT.txt" "mutated artifact note\n")
run_artifact_fixture("checksum-mismatch" "Rabbington Studio" "FAIL" "Checksum mismatches")

set(missing_checksum_entry_fixture "${ci_artifact_fixture_root}/missing-checksum-entry")
write_valid_artifact("${missing_checksum_entry_fixture}" "Rabbington Studio")
file(READ "${missing_checksum_entry_fixture}/SHA256SUMS.txt" checksum_text)
string(REGEX REPLACE "[0-9a-fA-F]+[ \t]+docs/DEPENDENCIES\\.md\n" "" checksum_text "${checksum_text}")
file(WRITE "${missing_checksum_entry_fixture}/SHA256SUMS.txt" "${checksum_text}")
run_artifact_fixture("missing-checksum-entry" "Rabbington Studio" "FAIL" "Missing checksum entries")

message(STATUS "CI artifact contents checker fixtures passed.")
