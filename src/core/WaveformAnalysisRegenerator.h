// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectModel.h"
#include "WaveformSummary.h"
#include "WavAudioImporter.h"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>

namespace projectname
{
enum class WaveformRegenerationStatus
{
    noImportedAudio,
    notNeeded,
    regenerated,
    missingAnalysisPath,
    invalidPackagePath,
    missingAudio,
    decodeFailed,
    cancelled,
    saveFailed
};

struct WaveformRegenerationOptions
{
    std::atomic_bool* cancelRequested = nullptr;
    WavDecodeOptions decodeOptions;
};

struct WaveformRegenerationResult
{
    WaveformRegenerationStatus status = WaveformRegenerationStatus::noImportedAudio;
    ProjectClip clip;
    std::filesystem::path audioPath;
    std::filesystem::path analysisPath;
    std::optional<WaveformSummary> summary;
    std::string error;
};

[[nodiscard]] WaveformRegenerationResult regenerateFirstImportedAudioWaveformAnalysis(
    const ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    const WaveformRegenerationOptions& options = {});
} // namespace projectname
