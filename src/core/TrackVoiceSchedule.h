// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "TimelinePlaybackPlan.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace projectname
{
struct PreparedTrackVoiceBuffer
{
    std::string clipId;
    std::shared_ptr<const std::vector<float>> samples;
};

struct TrackMixState
{
    std::string trackId;
    float gain = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
};

struct TrackVoiceScheduleOptions
{
    std::size_t maxVoices = 32;
};

struct TrackVoice
{
    std::size_t clipIndex = 0;
    std::string trackId;
    std::string clipId;
    std::int64_t renderStartOffsetSamples = 0;
    std::int64_t timelineStartSample = 0;
    std::int64_t clipLocalStartOffsetSamples = 0;
    std::int64_t frameCount = 0;
    float gainLeft = 1.0f;
    float gainRight = 1.0f;
};

struct TrackVoiceSchedule
{
    std::int64_t renderTimelineStartSample = 0;
    std::int64_t frameCount = 0;
    std::vector<TrackVoice> voices;
    bool voiceLimitReached = false;
};

[[nodiscard]] TrackVoiceSchedule buildTrackVoiceSchedule(
    const TimelinePlaybackPlan& plan,
    std::int64_t renderTimelineStartSample,
    std::int64_t frameCount,
    const std::vector<TrackMixState>& mixStates = {},
    TrackVoiceScheduleOptions options = {});
} // namespace projectname
