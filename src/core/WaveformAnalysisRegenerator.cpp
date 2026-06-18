// SPDX-License-Identifier: AGPL-3.0-or-later

#include "WaveformAnalysisRegenerator.h"

#include "PackagePath.h"

#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] bool isImportedAudioClip(const ProjectClip& clip)
{
    return clip.type == "audio-file";
}

[[nodiscard]] bool isCancelRequested(const WaveformRegenerationOptions& options) noexcept
{
    if (options.cancelRequested != nullptr && options.cancelRequested->load(std::memory_order_acquire))
        return true;

    return options.decodeOptions.cancelRequested != nullptr
        && options.decodeOptions.cancelRequested->load(std::memory_order_acquire);
}

[[nodiscard]] WaveformRegenerationResult makeResult(WaveformRegenerationStatus status,
                                                    ProjectClip clip,
                                                    std::filesystem::path audioPath,
                                                    std::filesystem::path analysisPath,
                                                    std::string error)
{
    WaveformRegenerationResult result;
    result.status = status;
    result.clip = std::move(clip);
    result.audioPath = std::move(audioPath);
    result.analysisPath = std::move(analysisPath);
    result.error = std::move(error);
    return result;
}
} // namespace

WaveformRegenerationResult regenerateFirstImportedAudioWaveformAnalysis(
    const ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    const WaveformRegenerationOptions& options)
{
    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (!isImportedAudioClip(clip))
                continue;

            const auto relativeAudioPath = std::filesystem::path(clip.relativePath);
            const auto relativeAnalysisPath = std::filesystem::path(clip.analysisPath);

            if (clip.analysisPath.empty())
                return makeResult(WaveformRegenerationStatus::missingAnalysisPath,
                                  clip,
                                  {},
                                  {},
                                  "Imported clip does not reference waveform analysis.");

            if (!isSafePackageRelativePath(relativeAudioPath) || !isSafePackageRelativePath(relativeAnalysisPath))
                return makeResult(WaveformRegenerationStatus::invalidPackagePath,
                                  clip,
                                  {},
                                  {},
                                  "Imported clip audio or analysis path is not package-relative.");

            const auto audioPath = resolvePackagePath(packageDirectory, relativeAudioPath);
            const auto analysisPath = resolvePackagePath(packageDirectory, relativeAnalysisPath);

            std::string error;
            auto existingSummary = loadWaveformSummary(analysisPath, error);
            if (existingSummary.has_value())
            {
                auto result = makeResult(WaveformRegenerationStatus::notNeeded, clip, audioPath, analysisPath, {});
                result.summary = std::move(*existingSummary);
                return result;
            }

            if (!std::filesystem::is_regular_file(audioPath))
                return makeResult(WaveformRegenerationStatus::missingAudio,
                                  clip,
                                  audioPath,
                                  analysisPath,
                                  "Imported clip audio file is missing.");

            if (isCancelRequested(options))
                return makeResult(WaveformRegenerationStatus::cancelled,
                                  clip,
                                  audioPath,
                                  analysisPath,
                                  "Waveform analysis regeneration was cancelled.");

            WavDecodeOptions decodeOptions = options.decodeOptions;
            if (decodeOptions.cancelRequested == nullptr)
                decodeOptions.cancelRequested = options.cancelRequested;

            auto preparedClip = loadPcm16WavAsPreparedMonoClip(audioPath, decodeOptions, error);
            if (!preparedClip.has_value())
            {
                const auto cancelled = isCancelRequested(options);
                return makeResult(cancelled ? WaveformRegenerationStatus::cancelled : WaveformRegenerationStatus::decodeFailed,
                                  clip,
                                  audioPath,
                                  analysisPath,
                                  error);
            }

            auto summary = buildWaveformSummary(*preparedClip);
            if (isCancelRequested(options))
                return makeResult(WaveformRegenerationStatus::cancelled,
                                  clip,
                                  audioPath,
                                  analysisPath,
                                  "Waveform analysis regeneration was cancelled.");

            if (!saveWaveformSummary(summary, analysisPath, error))
                return makeResult(WaveformRegenerationStatus::saveFailed,
                                  clip,
                                  audioPath,
                                  analysisPath,
                                  error);

            auto result = makeResult(WaveformRegenerationStatus::regenerated, clip, audioPath, analysisPath, {});
            result.summary = std::move(summary);
            return result;
        }
    }

    return {};
}
} // namespace projectname
