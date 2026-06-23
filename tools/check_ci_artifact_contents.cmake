# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED ARTIFACT_ROOT)
    message(FATAL_ERROR "ARTIFACT_ROOT must be passed to check_ci_artifact_contents.cmake")
endif()

if(NOT DEFINED ARTIFACT_EXECUTABLE_NAME)
    message(FATAL_ERROR "ARTIFACT_EXECUTABLE_NAME must be passed to check_ci_artifact_contents.cmake")
endif()

get_filename_component(artifact_root_absolute "${ARTIFACT_ROOT}" ABSOLUTE)
file(TO_CMAKE_PATH "${artifact_root_absolute}" artifact_root)

if(NOT IS_DIRECTORY "${artifact_root}")
    message(FATAL_ERROR "CI artifact root is not a directory: ${artifact_root}")
endif()

file(TO_CMAKE_PATH "${ARTIFACT_EXECUTABLE_NAME}" expected_executable)
if(expected_executable STREQUAL "" OR expected_executable MATCHES "/")
    message(FATAL_ERROR
        "ARTIFACT_EXECUTABLE_NAME must be a root-level file name, got: "
        "${ARTIFACT_EXECUTABLE_NAME}")
endif()

set(expected_files
    "${expected_executable}"
    "LICENSE"
    "README.md"
    "docs/DEPENDENCIES.md"
    "ARTIFACT.txt"
    "SHA256SUMS.txt")
set(expected_directories
    "docs")

file(GLOB_RECURSE artifact_entries LIST_DIRECTORIES true "${artifact_root}/*")

set(actual_files)
set(actual_directories)
set(symlink_entries)

foreach(artifact_entry IN LISTS artifact_entries)
    file(RELATIVE_PATH relative_entry "${artifact_root}" "${artifact_entry}")
    file(TO_CMAKE_PATH "${relative_entry}" relative_entry)

    if(relative_entry STREQUAL "")
        continue()
    endif()

    if(IS_SYMLINK "${artifact_entry}")
        list(APPEND symlink_entries "${relative_entry}")
    elseif(IS_DIRECTORY "${artifact_entry}")
        list(APPEND actual_directories "${relative_entry}")
    else()
        list(APPEND actual_files "${relative_entry}")
    endif()
endforeach()

list(SORT actual_files)
list(SORT actual_directories)

set(missing_files)
foreach(expected_file IN LISTS expected_files)
    list(FIND actual_files "${expected_file}" expected_file_index)
    if(expected_file_index EQUAL -1)
        list(APPEND missing_files "${expected_file}")
    endif()
endforeach()

set(unexpected_files)
foreach(actual_file IN LISTS actual_files)
    list(FIND expected_files "${actual_file}" actual_file_index)
    if(actual_file_index EQUAL -1)
        list(APPEND unexpected_files "${actual_file}")
    endif()
endforeach()

set(missing_directories)
foreach(expected_directory IN LISTS expected_directories)
    list(FIND actual_directories "${expected_directory}" expected_directory_index)
    if(expected_directory_index EQUAL -1)
        list(APPEND missing_directories "${expected_directory}")
    endif()
endforeach()

set(unexpected_directories)
foreach(actual_directory IN LISTS actual_directories)
    list(FIND expected_directories "${actual_directory}" actual_directory_index)
    if(actual_directory_index EQUAL -1)
        list(APPEND unexpected_directories "${actual_directory}")
    endif()
endforeach()

set(expected_checksum_files ${expected_files})
list(REMOVE_ITEM expected_checksum_files "SHA256SUMS.txt")

set(actual_checksum_files)
set(malformed_checksum_lines)
set(duplicate_checksum_entries)
set(missing_checksum_targets)
set(unexpected_checksum_entries)
set(missing_checksum_entries)
set(hash_mismatches)

set(checksum_file "${artifact_root}/SHA256SUMS.txt")
if(EXISTS "${checksum_file}")
    file(STRINGS "${checksum_file}" checksum_lines)
    set(checksum_line_number 0)

    foreach(checksum_line IN LISTS checksum_lines)
        math(EXPR checksum_line_number "${checksum_line_number} + 1")

        if(NOT checksum_line MATCHES "^([0-9A-Fa-f]+)[ \t]+(.+)$")
            list(APPEND malformed_checksum_lines "line ${checksum_line_number}: ${checksum_line}")
            continue()
        endif()

        set(expected_hash "${CMAKE_MATCH_1}")
        string(TOLOWER "${expected_hash}" expected_hash)
        string(LENGTH "${expected_hash}" expected_hash_length)
        if(NOT expected_hash_length EQUAL 64)
            list(APPEND malformed_checksum_lines "line ${checksum_line_number}: ${checksum_line}")
            continue()
        endif()

        set(checksum_entry_path "${CMAKE_MATCH_2}")
        string(STRIP "${checksum_entry_path}" checksum_entry_path)
        file(TO_CMAKE_PATH "${checksum_entry_path}" checksum_entry_path)

        while(checksum_entry_path MATCHES "^\\./")
            string(REGEX REPLACE "^\\./" "" checksum_entry_path "${checksum_entry_path}")
        endwhile()

        if(checksum_entry_path STREQUAL ""
            OR checksum_entry_path MATCHES "^/"
            OR checksum_entry_path MATCHES "(^|/)\\.\\.(/|$)")
            list(APPEND malformed_checksum_lines "line ${checksum_line_number}: ${checksum_line}")
            continue()
        endif()

        list(FIND actual_checksum_files "${checksum_entry_path}" duplicate_checksum_index)
        if(NOT duplicate_checksum_index EQUAL -1)
            list(APPEND duplicate_checksum_entries "${checksum_entry_path}")
        endif()

        list(APPEND actual_checksum_files "${checksum_entry_path}")

        list(FIND expected_checksum_files "${checksum_entry_path}" expected_checksum_index)
        if(expected_checksum_index EQUAL -1)
            list(APPEND unexpected_checksum_entries "${checksum_entry_path}")
            continue()
        endif()

        set(checksum_target "${artifact_root}/${checksum_entry_path}")
        if(NOT EXISTS "${checksum_target}" OR IS_DIRECTORY "${checksum_target}")
            list(APPEND missing_checksum_targets "${checksum_entry_path}")
            continue()
        endif()

        file(SHA256 "${checksum_target}" actual_hash)
        string(TOLOWER "${actual_hash}" actual_hash)
        if(NOT actual_hash STREQUAL expected_hash)
            list(APPEND hash_mismatches "${checksum_entry_path}")
        endif()
    endforeach()

    foreach(expected_checksum_file IN LISTS expected_checksum_files)
        list(FIND actual_checksum_files "${expected_checksum_file}" actual_checksum_index)
        if(actual_checksum_index EQUAL -1)
            list(APPEND missing_checksum_entries "${expected_checksum_file}")
        endif()
    endforeach()
endif()

set(error_message "")

if(missing_files)
    list(SORT missing_files)
    string(REPLACE ";" "\n  " missing_file_text "${missing_files}")
    string(APPEND error_message "\nMissing expected files:\n  ${missing_file_text}")
endif()

if(unexpected_files)
    list(SORT unexpected_files)
    string(REPLACE ";" "\n  " unexpected_file_text "${unexpected_files}")
    string(APPEND error_message "\nUnexpected files:\n  ${unexpected_file_text}")
endif()

if(missing_directories)
    list(SORT missing_directories)
    string(REPLACE ";" "\n  " missing_directory_text "${missing_directories}")
    string(APPEND error_message "\nMissing expected directories:\n  ${missing_directory_text}")
endif()

if(unexpected_directories)
    list(SORT unexpected_directories)
    string(REPLACE ";" "\n  " unexpected_directory_text "${unexpected_directories}")
    string(APPEND error_message "\nUnexpected directories:\n  ${unexpected_directory_text}")
endif()

if(symlink_entries)
    list(SORT symlink_entries)
    string(REPLACE ";" "\n  " symlink_entry_text "${symlink_entries}")
    string(APPEND error_message "\nSymlinks are not allowed in CI app artifacts:\n  ${symlink_entry_text}")
endif()

if(malformed_checksum_lines)
    string(REPLACE ";" "\n  " malformed_checksum_text "${malformed_checksum_lines}")
    string(APPEND error_message "\nMalformed checksum lines:\n  ${malformed_checksum_text}")
endif()

if(duplicate_checksum_entries)
    list(SORT duplicate_checksum_entries)
    string(REPLACE ";" "\n  " duplicate_checksum_text "${duplicate_checksum_entries}")
    string(APPEND error_message "\nDuplicate checksum entries:\n  ${duplicate_checksum_text}")
endif()

if(unexpected_checksum_entries)
    list(SORT unexpected_checksum_entries)
    string(REPLACE ";" "\n  " unexpected_checksum_text "${unexpected_checksum_entries}")
    string(APPEND error_message "\nUnexpected checksum entries:\n  ${unexpected_checksum_text}")
endif()

if(missing_checksum_targets)
    list(SORT missing_checksum_targets)
    string(REPLACE ";" "\n  " missing_checksum_target_text "${missing_checksum_targets}")
    string(APPEND error_message "\nChecksum entries point to missing files:\n  ${missing_checksum_target_text}")
endif()

if(missing_checksum_entries)
    list(SORT missing_checksum_entries)
    string(REPLACE ";" "\n  " missing_checksum_entry_text "${missing_checksum_entries}")
    string(APPEND error_message "\nMissing checksum entries:\n  ${missing_checksum_entry_text}")
endif()

if(hash_mismatches)
    list(SORT hash_mismatches)
    string(REPLACE ";" "\n  " hash_mismatch_text "${hash_mismatches}")
    string(APPEND error_message "\nChecksum mismatches:\n  ${hash_mismatch_text}")
endif()

if(NOT error_message STREQUAL "")
    message(FATAL_ERROR "CI artifact contents check failed:${error_message}")
endif()

list(LENGTH actual_files actual_file_count)
message(STATUS
    "CI artifact contents check passed for ${actual_file_count} files in ${artifact_root}.")
