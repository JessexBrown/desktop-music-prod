// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectModel.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace projectname
{
enum class ImportedClipInspectorStatus
{
    noImportedAudio,
    ready,
    missingAnalysis,
    invalidAnalysis,
    unsafeAnalysisPath,
};

struct ImportedClipInspectorState
{
    ImportedClipInspectorStatus status = ImportedClipInspectorStatus::noImportedAudio;
    std::string trackId;
    std::string trackName;
    std::string clipId;
    std::string clipName;
    std::string relativePath;
    std::string analysisPath;
    std::string selectedClipId;
    double startBeats = 0.0;
    double lengthBeats = 0.0;
    double sourceSampleRateHz = 0.0;
    double outputSampleRateHz = 0.0;
    double durationSeconds = 0.0;
    std::int64_t sourceFrameCount = 0;
    bool usingSelectedClip = false;
    bool sampleRateMismatch = false;
    std::string message;
    std::string warning;
};

[[nodiscard]] ImportedClipInspectorState buildFirstImportedAudioClipInspector(
    const ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    double outputSampleRateHz);
} // namespace projectname
