# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED CI_MACOS_ARTIFACT_GUARD_FIXTURE_ROOT)
    message(FATAL_ERROR
        "CI_MACOS_ARTIFACT_GUARD_FIXTURE_ROOT must be passed to "
        "check_ci_macos_artifact_upload_guard_fixtures.cmake")
endif()

set(check_guard_script "${CMAKE_CURRENT_LIST_DIR}/check_ci_macos_artifact_upload_guard.cmake")
get_filename_component(
    ci_macos_artifact_guard_fixture_root_absolute
    "${CI_MACOS_ARTIFACT_GUARD_FIXTURE_ROOT}"
    ABSOLUTE)
file(TO_CMAKE_PATH
    "${ci_macos_artifact_guard_fixture_root_absolute}"
    ci_macos_artifact_guard_fixture_root)

file(REMOVE_RECURSE "${ci_macos_artifact_guard_fixture_root}")
file(MAKE_DIRECTORY "${ci_macos_artifact_guard_fixture_root}")

function(write_guard_fixture fixture_name workflow_text)
    set(workflow_path "${ci_macos_artifact_guard_fixture_root}/${fixture_name}/ci.yml")
    get_filename_component(workflow_parent "${workflow_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${workflow_parent}")
    file(WRITE "${workflow_path}" "${workflow_text}")
endfunction()

function(run_guard_fixture fixture_name expected_result expected_pattern)
    set(workflow_path "${ci_macos_artifact_guard_fixture_root}/${fixture_name}/ci.yml")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DCI_WORKFLOW_PATH=${workflow_path}
            -P "${check_guard_script}"
        RESULT_VARIABLE fixture_result
        OUTPUT_VARIABLE fixture_output
        ERROR_VARIABLE fixture_error)

    set(combined_output "${fixture_output}${fixture_error}")

    if(expected_result STREQUAL "PASS")
        if(NOT fixture_result EQUAL 0)
            message(FATAL_ERROR
                "macOS artifact guard fixture '${fixture_name}' was expected to pass but failed:\n"
                "${combined_output}")
        endif()
    elseif(expected_result STREQUAL "FAIL")
        if(fixture_result EQUAL 0)
            message(FATAL_ERROR
                "macOS artifact guard fixture '${fixture_name}' was expected to fail but passed.")
        endif()
    else()
        message(FATAL_ERROR "Unknown macOS artifact guard fixture expectation: ${expected_result}")
    endif()

    if(NOT combined_output MATCHES "${expected_pattern}")
        message(FATAL_ERROR
            "macOS artifact guard fixture '${fixture_name}' output did not match "
            "'${expected_pattern}':\n${combined_output}")
    endif()
endfunction()

set(valid_workflow [=[
name: CI

on:
  push:

jobs:
  windows:
    name: Windows MSVC App
    runs-on: windows-latest
    steps:
      - name: Upload Windows MSVC app artifact
        uses: actions/upload-artifact@v7

  linux-juce-app:
    name: Linux JUCE App
    runs-on: ubuntu-latest
    steps:
      - name: Upload Linux JUCE app artifact
        uses: actions/upload-artifact@v7

  macos-juce-app:
    name: macOS JUCE App
    runs-on: macos-15
    steps:
      - name: Configure app
        run: cmake --preset dev-host
      - name: Build app
        run: cmake --build --preset dev-host
      - name: Test app
        run: ctest --preset dev-host --output-on-failure
]=])

write_guard_fixture("valid-current-app-artifact-shape" "${valid_workflow}")
run_guard_fixture(
    "valid-current-app-artifact-shape"
    "PASS"
    "macOS CI artifact upload guard passed")

set(macos_upload_workflow [=[
name: CI

on:
  push:

jobs:
  windows:
    name: Windows MSVC App
    runs-on: windows-latest
    steps:
      - name: Upload Windows MSVC app artifact
        uses: actions/upload-artifact@v7

  linux-juce-app:
    name: Linux JUCE App
    runs-on: ubuntu-latest
    steps:
      - name: Upload Linux JUCE app artifact
        uses: actions/upload-artifact@v7

  macos-juce-app:
    name: macOS JUCE App
    runs-on: macos-15
    steps:
      - name: Configure app
        run: cmake --preset dev-host
      - name: Upload macOS app artifact
        uses: actions/upload-artifact@v7
]=])

write_guard_fixture("macos-upload-artifact-rejected" "${macos_upload_workflow}")
run_guard_fixture(
    "macos-upload-artifact-rejected"
    "FAIL"
    "macOS JUCE App must remain build/test-only")

set(missing_windows_upload_workflow [=[
name: CI

on:
  push:

jobs:
  windows:
    name: Windows MSVC App
    runs-on: windows-latest
    steps:
      - name: Test
        run: ctest --preset dev --output-on-failure

  linux-juce-app:
    name: Linux JUCE App
    runs-on: ubuntu-latest
    steps:
      - name: Upload Linux JUCE app artifact
        uses: actions/upload-artifact@v7

  macos-juce-app:
    name: macOS JUCE App
    runs-on: macos-15
    steps:
      - name: Test app
        run: ctest --preset dev-host --output-on-failure
]=])

write_guard_fixture("missing-windows-upload-rejected" "${missing_windows_upload_workflow}")
run_guard_fixture(
    "missing-windows-upload-rejected"
    "FAIL"
    "Windows MSVC App must keep its existing actions/upload-artifact step")

set(missing_macos_job_workflow [=[
name: CI

on:
  push:

jobs:
  windows:
    name: Windows MSVC App
    runs-on: windows-latest
    steps:
      - name: Upload Windows MSVC app artifact
        uses: actions/upload-artifact@v7

  linux-juce-app:
    name: Linux JUCE App
    runs-on: ubuntu-latest
    steps:
      - name: Upload Linux JUCE app artifact
        uses: actions/upload-artifact@v7
]=])

write_guard_fixture("missing-macos-job-rejected" "${missing_macos_job_workflow}")
run_guard_fixture(
    "missing-macos-job-rejected"
    "FAIL"
    "Missing required CI job name: macOS JUCE App")

message(STATUS "macOS CI artifact upload guard fixtures passed.")
