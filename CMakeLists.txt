cmake_minimum_required(VERSION 3.24)

project(RabbingtonStudio
    VERSION 0.1.0
    DESCRIPTION "Rabbington Studio native desktop music production app prototype"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PROJECTNAME_FETCH_JUCE "Fetch JUCE from the official GitHub repository" ON)
option(PROJECTNAME_BUILD_APP "Build the JUCE desktop application target" ON)
option(PROJECTNAME_BUILD_WIN32_FALLBACK "Build the dependency-light Win32 verification app" OFF)

if(PROJECTNAME_BUILD_APP AND MINGW)
    message(FATAL_ERROR
        "JUCE does not support MinGW. Use a JUCE-supported compiler such as MSVC "
        "for the desktop app, or configure with -DPROJECTNAME_BUILD_APP=OFF to "
        "build and test the plain C++ core.")
endif()

include(CTest)

include(FetchContent)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "")
FetchContent_MakeAvailable(nlohmann_json)

if(PROJECTNAME_BUILD_APP AND PROJECTNAME_FETCH_JUCE)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.13
        GIT_SHALLOW TRUE
        GIT_SUBMODULES "")
    FetchContent_MakeAvailable(juce)
elseif(PROJECTNAME_BUILD_APP)
    find_package(JUCE CONFIG REQUIRED)
endif()

add_library(projectname_core
    src/core/AppCommandRegistry.cpp
    src/core/AppCommandRegistry.h
    src/core/AppSession.cpp
    src/core/AppSession.h
    src/core/AudioEngineStub.cpp
    src/core/AudioEngineStub.h
    src/core/BackgroundAudioImportJob.cpp
    src/core/BackgroundAudioImportJob.h
    src/core/BackgroundMediaRelinkPreparationJob.cpp
    src/core/BackgroundMediaRelinkPreparationJob.h
    src/core/BackgroundTimelinePlaybackPreparationJob.cpp
    src/core/BackgroundTimelinePlaybackPreparationJob.h
    src/core/BackgroundWaveformAnalysisJob.cpp
    src/core/BackgroundWaveformAnalysisJob.h
    src/core/ImportedClipInspectorEditDraft.cpp
    src/core/ImportedClipInspectorEditDraft.h
    src/core/ImportedClipMediaRelink.cpp
    src/core/ImportedClipMediaRelink.h
    src/core/ImportedMediaPackageInventory.cpp
    src/core/ImportedMediaPackageInventory.h
    src/core/ImportedClipInspector.cpp
    src/core/ImportedClipInspector.h
    src/core/PackagePath.cpp
    src/core/PackagePath.h
    src/core/PackageMediaQuarantineCommand.cpp
    src/core/PackageMediaQuarantineCommand.h
    src/core/PackageMediaQuarantinePreflightPlan.cpp
    src/core/PackageMediaQuarantinePreflightPlan.h
    src/core/PackageMediaQuarantineRestoreManifest.cpp
    src/core/PackageMediaQuarantineRestoreManifest.h
    src/core/ProductIdentity.h
    src/core/ProjectAudioImport.cpp
    src/core/ProjectAudioImport.h
    src/core/ProjectModel.cpp
    src/core/ProjectModel.h
    src/core/TimelineClipLane.cpp
    src/core/TimelineClipLane.h
    src/core/TimelinePlaybackPlan.cpp
    src/core/TimelinePlaybackPlan.h
    src/core/TimelinePlaybackPreparationCompletion.cpp
    src/core/TimelinePlaybackPreparationCompletion.h
    src/core/ToneRenderer.cpp
    src/core/ToneRenderer.h
    src/core/TrackVoiceSchedule.cpp
    src/core/TrackVoiceSchedule.h
    src/core/TransportState.cpp
    src/core/TransportState.h
    src/core/WaveformAnalysisRegenerator.cpp
    src/core/WaveformAnalysisRegenerator.h
    src/core/WaveformSummary.cpp
    src/core/WaveformSummary.h
    src/core/WaveformThumbnail.cpp
    src/core/WaveformThumbnail.h
    src/core/WorkspaceCommandRouter.cpp
    src/core/WorkspaceCommandRouter.h
    src/core/WavAudioImporter.cpp
    src/core/WavAudioImporter.h)

target_include_directories(projectname_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_features(projectname_core PUBLIC cxx_std_20)

target_link_libraries(projectname_core
    PUBLIC
        nlohmann_json::nlohmann_json)

if(PROJECTNAME_BUILD_APP)
    juce_add_gui_app(projectname
        VERSION ${PROJECT_VERSION}
        COMPANY_NAME "Rabbington Studio Contributors"
        PRODUCT_NAME "Rabbington Studio"
        BUNDLE_ID "org.rabbingtonstudio.daw")

    target_sources(projectname
        PRIVATE
            src/app/Main.cpp
            src/app/MainComponent.cpp
            src/app/MainComponent.h
            src/core/AudioDeviceService.cpp
            src/core/AudioDeviceService.h)

    target_compile_definitions(projectname
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)

    target_link_libraries(projectname
        PRIVATE
            projectname_core
            juce::juce_audio_devices
            juce::juce_audio_utils
            juce::juce_gui_extra
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(BUILD_TESTING)
        add_test(NAME projectname_app_smoke COMMAND projectname --smoke-test)
        set_tests_properties(projectname_app_smoke PROPERTIES TIMEOUT 20)
    endif()
endif()

if(PROJECTNAME_BUILD_WIN32_FALLBACK)
    if(NOT WIN32)
        message(FATAL_ERROR "PROJECTNAME_BUILD_WIN32_FALLBACK is only available on Windows.")
    endif()

    add_executable(projectname_win32 WIN32
        src/platform/win32/Win32FallbackApp.cpp)

    target_link_libraries(projectname_win32
        PRIVATE
            projectname_core
            gdi32
            winmm)

    if(BUILD_TESTING)
        add_test(NAME projectname_win32_launch_smoke COMMAND projectname_win32 --smoke-test)
        add_test(NAME projectname_win32_audio_smoke COMMAND projectname_win32 --smoke-audio)
        add_test(NAME projectname_win32_project_smoke COMMAND projectname_win32 --smoke-project)
        add_test(NAME projectname_win32_import_smoke COMMAND projectname_win32 --smoke-import)
        set_tests_properties(projectname_win32_launch_smoke
                             projectname_win32_audio_smoke
                             projectname_win32_project_smoke
                             projectname_win32_import_smoke
                             PROPERTIES TIMEOUT 20)
    endif()
endif()

if(BUILD_TESTING)
    add_test(NAME projectname_spdx_check
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_spdx.cmake)
    set_tests_properties(projectname_spdx_check PROPERTIES TIMEOUT 20)

    add_executable(projectname_tests
        tests/ProjectNameTests.cpp)

    target_compile_features(projectname_tests PRIVATE cxx_std_20)

    target_link_libraries(projectname_tests
        PRIVATE
            projectname_core)

    add_test(NAME projectname_tests COMMAND projectname_tests)
    set_tests_properties(projectname_tests PROPERTIES TIMEOUT 20)
endif()
