// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ImportedClipInspector.h"

#include "PackagePath.h"
#include "WaveformSummary.h"

#include <cmath>
#include <filesystem>
#include <system_error>

namespace projectname
{
namespace
{
struct ImportedClipMatch
{
    const ProjectTrack* track = nullptr;
    const ProjectClip* clip = nullptr;
};

[[nodiscard]] bool isImportedAudioClip(const ProjectClip& clip) noexcept
{
    return clip.type == "audio-file" && !clip.relativePath.empty();
}

[[nodiscard]] ImportedClipMatch findFirstImportedAudioClip(const ProjectModel& project) noexcept
{
    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (isImportedAudioClip(clip))
                return { &track, &clip };
        }
    }

    return {};
}

[[nodiscard]] ImportedClipMatch findImportedAudioClipById(const ProjectModel& project,
                                                          const std::string& clipId) noexcept
{
    if (clipId.empty())
        return {};

    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (clip.id == clipId && isImportedAudioClip(clip))
                return { &track, &clip };
        }
    }

    return {};
}

[[nodiscard]] double normalizeOutputSampleRate(double outputSampleRateHz) noexcept
{
    return std::isfinite(outputSampleRateHz) && outputSampleRateHz > 0.0
        ? outputSampleRateHz
        : 0.0;
}

[[nodiscard]] bool sampleRatesDiffer(double sourceSampleRateHz, double outputSampleRateHz) noexcept
{
    return std::isfinite(sourceSampleRateHz)
        && std::isfinite(outputSampleRateHz)
        && sourceSampleRateHz > 0.0
        && outputSampleRateHz > 0.0
        && std::abs(sourceSampleRateHz - outputSampleRateHz) > 1.0;
}

void copyClipIdentity(ImportedClipInspectorState& state,
                      const ProjectTrack& track,
                      const ProjectClip& clip)
{
    state.trackId = track.id;
    state.trackName = track.name;
    state.clipId = clip.id;
    state.clipName = clip.name;
    state.relativePath = clip.relativePath;
    state.analysisPath = clip.analysisPath;
    state.startBeats = clip.startBeats;
    state.lengthBeats = clip.lengthBeats;
}
} // namespace

ImportedClipInspectorState buildFirstImportedAudioClipInspector(
    const ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    double outputSampleRateHz)
{
    ImportedClipInspectorState state;
    state.outputSampleRateHz = normalizeOutputSampleRate(outputSampleRateHz);
    state.selectedClipId = project.getSelectedClipId();

    auto match = findImportedAudioClipById(project, state.selectedClipId);
    state.usingSelectedClip = match.track != nullptr && match.clip != nullptr;
    if (!state.usingSelectedClip)
        match = findFirstImportedAudioClip(project);

    if (match.track == nullptr || match.clip == nullptr)
    {
        state.message = "No imported audio clip is available.";
        return state;
    }

    copyClipIdentity(state, *match.track, *match.clip);

    const auto analysisPath = std::filesystem::path(match.clip->analysisPath);
    if (!isSafePackageRelativePath(analysisPath))
    {
        state.status = ImportedClipInspectorStatus::unsafeAnalysisPath;
        state.message = "Imported clip analysis path is not package-relative.";
        return state;
    }

    const auto resolvedAnalysisPath = resolvePackagePath(packageDirectory, analysisPath);
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(resolvedAnalysisPath, filesystemError))
    {
        state.status = ImportedClipInspectorStatus::missingAnalysis;
        state.message = "Imported clip waveform analysis is missing.";
        return state;
    }

    std::string error;
    const auto summary = loadWaveformSummary(resolvedAnalysisPath, error);
    if (!summary.has_value() || summary->sampleRateHz <= 0.0 || summary->frameCount <= 0)
    {
        state.status = ImportedClipInspectorStatus::invalidAnalysis;
        state.message = error.empty()
            ? "Imported clip waveform analysis is invalid."
            : error;
        return state;
    }

    state.status = ImportedClipInspectorStatus::ready;
    state.sourceSampleRateHz = summary->sampleRateHz;
    state.sourceFrameCount = summary->frameCount;
    state.durationSeconds = static_cast<double>(summary->frameCount) / summary->sampleRateHz;
    state.sampleRateMismatch = sampleRatesDiffer(state.sourceSampleRateHz, state.outputSampleRateHz);
    state.message = "Imported clip metadata is ready.";

    if (state.sampleRateMismatch)
        state.warning = "Source sample rate differs from the current output device; resampling is not implemented yet.";

    return state;
}
} // namespace projectname
