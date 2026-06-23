# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED CI_WORKFLOW_PATH)
    message(FATAL_ERROR
        "CI_WORKFLOW_PATH must be passed to check_ci_macos_artifact_upload_guard.cmake")
endif()

if("${CI_WORKFLOW_PATH}" STREQUAL "")
    message(FATAL_ERROR
        "CI_WORKFLOW_PATH must not be empty. When running this check manually, "
        "pass an absolute path or use -DCI_WORKFLOW_PATH:FILEPATH=...")
endif()

get_filename_component(ci_workflow_path_absolute "${CI_WORKFLOW_PATH}" ABSOLUTE)
file(TO_CMAKE_PATH "${ci_workflow_path_absolute}" ci_workflow_path)

if(NOT EXISTS "${ci_workflow_path}")
    message(FATAL_ERROR "CI workflow file does not exist: ${ci_workflow_path}")
endif()

file(STRINGS "${ci_workflow_path}" workflow_lines)

set(in_jobs_section false)
set(found_jobs_section false)
set(current_job_id "")
set(current_job_text "")
set(found_windows_app_job false)
set(found_linux_app_job false)
set(found_macos_app_job false)
set(windows_app_job_has_upload false)
set(linux_app_job_has_upload false)
set(macos_app_job_has_upload false)

macro(projectname_finish_ci_job)
    if(NOT current_job_id STREQUAL "")
        set(job_name "")
        if(current_job_text MATCHES "(^|\n)[ \t]+name:[ \t]*([^\n#]+)")
            set(job_name "${CMAKE_MATCH_2}")
            string(STRIP "${job_name}" job_name)

            if(job_name MATCHES "^\"(.*)\"$")
                set(job_name "${CMAKE_MATCH_1}")
            elseif(job_name MATCHES "^'(.*)'$")
                set(job_name "${CMAKE_MATCH_1}")
            endif()
        endif()

        string(TOLOWER "${current_job_text}" current_job_text_lower)
        set(job_uses_upload_artifact false)
        if(current_job_text_lower MATCHES "(^|\n)[ \t]*uses:[ \t]*actions/upload-artifact@")
            set(job_uses_upload_artifact true)
        endif()

        if(job_name STREQUAL "Windows MSVC App")
            set(found_windows_app_job true)
            set(windows_app_job_has_upload "${job_uses_upload_artifact}")
        elseif(job_name STREQUAL "Linux JUCE App")
            set(found_linux_app_job true)
            set(linux_app_job_has_upload "${job_uses_upload_artifact}")
        elseif(job_name STREQUAL "macOS JUCE App")
            set(found_macos_app_job true)
            set(macos_app_job_has_upload "${job_uses_upload_artifact}")
        endif()
    endif()
endmacro()

foreach(workflow_line IN LISTS workflow_lines)
    string(REGEX REPLACE "\r$" "" workflow_line "${workflow_line}")

    if(workflow_line MATCHES "^jobs:[ \t]*$")
        set(in_jobs_section true)
        set(found_jobs_section true)
        continue()
    endif()

    if(NOT in_jobs_section)
        continue()
    endif()

    if(workflow_line MATCHES "^  ([-A-Za-z0-9_]+):[ \t]*$")
        set(next_job_id "${CMAKE_MATCH_1}")
        projectname_finish_ci_job()
        set(current_job_id "${next_job_id}")
        set(current_job_text "${workflow_line}\n")
    elseif(NOT current_job_id STREQUAL "")
        string(APPEND current_job_text "${workflow_line}\n")
    endif()
endforeach()

projectname_finish_ci_job()

set(error_message "")

if(NOT found_jobs_section)
    string(APPEND error_message "\nMissing top-level jobs section.")
endif()

if(NOT found_windows_app_job)
    string(APPEND error_message "\nMissing required CI job name: Windows MSVC App.")
elseif(NOT windows_app_job_has_upload)
    string(APPEND error_message
        "\nWindows MSVC App must keep its existing actions/upload-artifact step.")
endif()

if(NOT found_linux_app_job)
    string(APPEND error_message "\nMissing required CI job name: Linux JUCE App.")
elseif(NOT linux_app_job_has_upload)
    string(APPEND error_message
        "\nLinux JUCE App must keep its existing actions/upload-artifact step.")
endif()

if(NOT found_macos_app_job)
    string(APPEND error_message "\nMissing required CI job name: macOS JUCE App.")
elseif(macos_app_job_has_upload)
    string(APPEND error_message
        "\nmacOS JUCE App must remain build/test-only and must not use "
        "actions/upload-artifact until ADR-0106 artifact rules are implemented.")
endif()

if(NOT error_message STREQUAL "")
    message(FATAL_ERROR "macOS CI artifact upload guard failed:${error_message}")
endif()

message(STATUS
    "macOS CI artifact upload guard passed: Windows/Linux uploads are allowed "
    "and macOS JUCE App remains build/test-only.")
