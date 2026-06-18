// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectModel.h"
#include "WaveformSummary.h"

#include <filesystem>
#include <string>
#include <vector>

namespace projectname
{
enum class WaveformThumbnailState
{
    noImportedAudio,
    ready,
    missingAnalysis,
    invalidAnalysis
};

struct WaveformThumbnail
{
    WaveformThumbnailState state = WaveformThumbnailState::noImportedAudio;
    ProjectClip clip;
    WaveformSummary summary;
    std::filesystem::path resolvedAnalysisPath;
    std::string error;
};

[[nodiscard]] WaveformThumbnail loadFirstImportedAudioWaveform(const ProjectModel& project,
                                                              const std::filesystem::path& packageDirectory);

[[nodiscard]] std::vector<WaveformThumbnail> loadImportedAudioWaveforms(
    const ProjectModel& project,
    const std::filesystem::path& packageDirectory);

[[nodiscard]] std::vector<float> makeWaveformPeakColumns(const WaveformSummary& summary,
                                                         std::size_t maxColumnCount);
} // namespace projectname
