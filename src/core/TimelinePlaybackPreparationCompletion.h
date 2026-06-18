// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "AppSession.h"
#include "BackgroundTimelinePlaybackPreparationJob.h"

#include <string>

namespace projectname
{
enum class TimelinePlaybackPreparationCompletionStatus
{
    scheduledVoiceWindow,
    scheduledImportedClip,
    cancelled,
    staleResult,
    generatedToneFallback,
};

struct TimelinePlaybackPreparationCompletion
{
    TimelinePlaybackPreparationCompletionStatus status =
        TimelinePlaybackPreparationCompletionStatus::generatedToneFallback;
    TimelinePlaybackPreparation playback;
    TimelineVoicePlaybackPreparation voicePlayback;
    std::string message;
};

[[nodiscard]] TimelinePlaybackPreparationCompletion completeBackgroundTimelinePlaybackPreparation(
    AppSession& session,
    BackgroundTimelinePlaybackPreparationResult result,
    double outputSampleRateHz);
} // namespace projectname
