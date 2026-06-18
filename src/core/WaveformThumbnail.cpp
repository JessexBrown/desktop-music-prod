// SPDX-License-Identifier: AGPL-3.0-or-later

#include "WaveformThumbnail.h"

#include "PackagePath.h"

#include <algorithm>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] bool isImportedAudioClip(const ProjectClip& clip)
{
    return clip.type == "audio-file";
}

[[nodiscard]] WaveformThumbnail makeState(WaveformThumbnailState state,
                                          ProjectClip clip,
                                          std::filesystem::path resolvedPath,
                                          std::string error)
{
    WaveformThumbnail thumbnail;
    thumbnail.state = state;
    thumbnail.clip = std::move(clip);
    thumbnail.resolvedAnalysisPath = std::move(resolvedPath);
    thumbnail.error = std::move(error);
    return thumbnail;
}
[[nodiscard]] WaveformThumbnail loadImportedAudioClipWaveform(const ProjectClip& clip,
                                                             const std::filesystem::path& packageDirectory)
{
    if (clip.analysisPath.empty())
        return makeState(WaveformThumbnailState::missingAnalysis,
                         clip,
                         {},
                         "Imported clip does not reference waveform analysis.");

    const auto relativePath = std::filesystem::path(clip.analysisPath);
    if (!isSafePackageRelativePath(relativePath))
        return makeState(WaveformThumbnailState::invalidAnalysis,
                         clip,
                         {},
                         "Waveform analysis path is not package-relative.");

    const auto resolvedPath = resolvePackagePath(packageDirectory, relativePath);
    if (!std::filesystem::is_regular_file(resolvedPath))
        return makeState(WaveformThumbnailState::missingAnalysis,
                         clip,
                         resolvedPath,
                         "Waveform analysis file is missing.");

    std::string error;
    auto summary = loadWaveformSummary(resolvedPath, error);
    if (!summary.has_value())
        return makeState(WaveformThumbnailState::invalidAnalysis,
                         clip,
                         resolvedPath,
                         error);

    auto thumbnail = makeState(WaveformThumbnailState::ready, clip, resolvedPath, {});
    thumbnail.summary = std::move(*summary);
    return thumbnail;
}
} // namespace

WaveformThumbnail loadFirstImportedAudioWaveform(const ProjectModel& project,
                                                const std::filesystem::path& packageDirectory)
{
    const auto thumbnails = loadImportedAudioWaveforms(project, packageDirectory);
    if (thumbnails.empty())
        return {};

    return thumbnails.front();
}

std::vector<WaveformThumbnail> loadImportedAudioWaveforms(const ProjectModel& project,
                                                         const std::filesystem::path& packageDirectory)
{
    std::vector<WaveformThumbnail> thumbnails;

    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (!isImportedAudioClip(clip))
                continue;

            thumbnails.push_back(loadImportedAudioClipWaveform(clip, packageDirectory));
        }
    }

    return thumbnails;
}

std::vector<float> makeWaveformPeakColumns(const WaveformSummary& summary, std::size_t maxColumnCount)
{
    std::vector<float> columns;
    if (summary.buckets.empty() || maxColumnCount == 0)
        return columns;

    const auto columnCount = std::min(maxColumnCount, summary.buckets.size());
    columns.reserve(columnCount);

    for (std::size_t columnIndex = 0; columnIndex < columnCount; ++columnIndex)
    {
        const auto bucketIndex = (columnIndex * summary.buckets.size()) / columnCount;
        columns.push_back(std::clamp(summary.buckets[bucketIndex].peak, 0.0f, 1.0f));
    }

    return columns;
}
} // namespace projectname
