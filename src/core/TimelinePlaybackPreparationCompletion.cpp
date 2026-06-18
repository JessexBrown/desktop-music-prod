// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TimelinePlaybackPreparationCompletion.h"

#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] std::string fallbackMessage(const BackgroundTimelinePlaybackPreparationResult& result)
{
    if (!result.error.empty())
        return result.error;

    if (!result.voicePreparation.message.empty())
        return result.voicePreparation.message;

    if (!result.preparation.message.empty())
        return result.preparation.message;

    return "Timeline playback preparation did not produce an imported clip.";
}

[[nodiscard]] ProjectClip makeProjectClip(const TimelinePlaybackClipPlan& clipPlan)
{
    ProjectClip clip;
    clip.id = clipPlan.clipId;
    clip.name = clipPlan.clipName;
    clip.type = "audio-file";
    clip.relativePath = clipPlan.relativePath;
    clip.startBeats = clipPlan.startBeats;
    clip.lengthBeats = clipPlan.lengthBeats;
    return clip;
}
} // namespace

TimelinePlaybackPreparationCompletion completeBackgroundTimelinePlaybackPreparation(
    AppSession& session,
    BackgroundTimelinePlaybackPreparationResult result,
    double outputSampleRateHz)
{
    TimelinePlaybackPreparationCompletion completion;

    if (result.cancelled)
    {
        completion.status = TimelinePlaybackPreparationCompletionStatus::cancelled;
        completion.message = result.error.empty()
            ? "Timeline playback preparation was cancelled."
            : std::move(result.error);
        return completion;
    }

    if (!result.preparedClips.empty())
    {
        for (auto& preparedClip : result.preparedClips)
        {
            auto cachedSamples = session.cacheImportedTimelineClip(makeProjectClip(preparedClip.clip),
                                                                   std::move(preparedClip.preparedClip),
                                                                   std::move(preparedClip.preparedSamples));
            if (cachedSamples == nullptr)
            {
                completion.status = TimelinePlaybackPreparationCompletionStatus::staleResult;
                completion.message = "Timeline playback preparation no longer matches the current project.";
                return completion;
            }
        }

        std::string error;
        auto voicePlayback = session.playCachedTimelineVoiceWindow(outputSampleRateHz,
                                                                   result.minimumRenderFrameCount,
                                                                   error);
        if (voicePlayback.status == TimelineVoicePlaybackPreparationStatus::voiceWindowReady)
        {
            completion.status = TimelinePlaybackPreparationCompletionStatus::scheduledVoiceWindow;
            completion.message = voicePlayback.message;
            completion.voicePlayback = std::move(voicePlayback);
            return completion;
        }

        if (voicePlayback.status == TimelineVoicePlaybackPreparationStatus::backgroundPreparationRequired)
        {
            completion.status = TimelinePlaybackPreparationCompletionStatus::staleResult;
            completion.message = "Timeline playback preparation did not cache every required voice-window clip.";
            return completion;
        }

        session.play();
        completion.status = TimelinePlaybackPreparationCompletionStatus::generatedToneFallback;
        completion.message = error.empty()
            ? "Timeline playback preparation could not be scheduled."
            : std::move(error);
        return completion;
    }

    if (result.preparation.status != TimelinePlaybackPreparationStatus::importedClipReady
        || !result.preparation.clip.has_value()
        || result.preparation.preparedSamples == nullptr)
    {
        session.play();
        completion.status = TimelinePlaybackPreparationCompletionStatus::generatedToneFallback;
        completion.message = fallbackMessage(result);
        return completion;
    }

    const auto cachedSamples = session.cacheImportedTimelineClip(makeProjectClip(*result.preparation.clip),
                                                                 std::move(result.preparation.preparedClip),
                                                                 std::move(result.preparation.preparedSamples));
    if (cachedSamples == nullptr)
    {
        completion.status = TimelinePlaybackPreparationCompletionStatus::staleResult;
        completion.message = "Timeline playback preparation no longer matches the current project.";
        return completion;
    }

    std::string error;
    auto playback = session.playFromCachedTimeline(outputSampleRateHz, error);
    if (playback.status == TimelinePlaybackPreparationStatus::importedClipReady)
    {
        completion.status = TimelinePlaybackPreparationCompletionStatus::scheduledImportedClip;
        completion.message = playback.message;
        completion.playback = std::move(playback);
        return completion;
    }

    session.play();
    completion.status = TimelinePlaybackPreparationCompletionStatus::generatedToneFallback;
    completion.message = error.empty()
        ? "Timeline playback preparation could not be scheduled."
        : std::move(error);
    return completion;
}
} // namespace projectname
