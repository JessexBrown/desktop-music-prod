// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TrackVoiceSchedule.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] std::int64_t safeWindowEnd(std::int64_t start, std::int64_t frameCount) noexcept
{
    if (frameCount > std::numeric_limits<std::int64_t>::max() - start)
        return std::numeric_limits<std::int64_t>::max();

    return start + frameCount;
}

[[nodiscard]] const TrackMixState* findTrackMixState(const std::vector<TrackMixState>& mixStates,
                                                     const std::string& trackId) noexcept
{
    const auto match = std::find_if(mixStates.begin(),
                                    mixStates.end(),
                                    [&trackId](const TrackMixState& state)
                                    {
                                        return state.trackId == trackId;
                                    });
    return match == mixStates.end() ? nullptr : &*match;
}

[[nodiscard]] bool anySoloedTrack(const std::vector<TrackMixState>& mixStates) noexcept
{
    return std::any_of(mixStates.begin(),
                       mixStates.end(),
                       [](const TrackMixState& state)
                       {
                           return state.solo;
                       });
}

[[nodiscard]] float sanitizeGain(float gain) noexcept
{
    if (!std::isfinite(gain) || gain <= 0.0f)
        return 0.0f;

    return std::min(gain, 4.0f);
}

[[nodiscard]] float sanitizePan(float pan) noexcept
{
    if (!std::isfinite(pan))
        return 0.0f;

    return std::clamp(pan, -1.0f, 1.0f);
}

[[nodiscard]] bool isAudible(const TrackMixState* state, bool hasSolo) noexcept
{
    if (state == nullptr)
        return !hasSolo;

    if (state->muted)
        return false;

    return !hasSolo || state->solo;
}

[[nodiscard]] std::pair<float, float> calculateLinearPanGains(const TrackMixState* state) noexcept
{
    const auto gain = sanitizeGain(state == nullptr ? 1.0f : state->gain);
    const auto pan = sanitizePan(state == nullptr ? 0.0f : state->pan);

    const auto left = pan <= 0.0f ? gain : gain * (1.0f - pan);
    const auto right = pan >= 0.0f ? gain : gain * (1.0f + pan);
    return { left, right };
}
} // namespace

TrackVoiceSchedule buildTrackVoiceSchedule(const TimelinePlaybackPlan& plan,
                                           std::int64_t renderTimelineStartSample,
                                           std::int64_t frameCount,
                                           const std::vector<TrackMixState>& mixStates,
                                           TrackVoiceScheduleOptions options)
{
    TrackVoiceSchedule schedule;
    schedule.renderTimelineStartSample = std::max<std::int64_t>(0, renderTimelineStartSample);
    schedule.frameCount = std::max<std::int64_t>(0, frameCount);

    if (schedule.frameCount <= 0 || plan.clips.empty())
        return schedule;

    const auto windowStart = schedule.renderTimelineStartSample;
    const auto windowEnd = safeWindowEnd(windowStart, schedule.frameCount);
    const auto hasSolo = anySoloedTrack(mixStates);

    for (std::size_t clipIndex = 0; clipIndex < plan.clips.size(); ++clipIndex)
    {
        const auto& clip = plan.clips[clipIndex];
        if (clip.timelineEndSample <= windowStart || clip.timelineStartSample >= windowEnd)
            continue;

        const auto* mixState = findTrackMixState(mixStates, clip.trackId);
        if (!isAudible(mixState, hasSolo))
            continue;

        if (schedule.voices.size() >= options.maxVoices)
        {
            schedule.voiceLimitReached = true;
            break;
        }

        const auto voiceStart = std::max(windowStart, clip.timelineStartSample);
        const auto voiceEnd = std::min(windowEnd, clip.timelineEndSample);
        if (voiceEnd <= voiceStart)
            continue;

        const auto [gainLeft, gainRight] = calculateLinearPanGains(mixState);

        TrackVoice voice;
        voice.clipIndex = clipIndex;
        voice.trackId = clip.trackId;
        voice.clipId = clip.clipId;
        voice.renderStartOffsetSamples = voiceStart - windowStart;
        voice.timelineStartSample = voiceStart;
        voice.clipLocalStartOffsetSamples = voiceStart - clip.timelineStartSample;
        voice.frameCount = voiceEnd - voiceStart;
        voice.gainLeft = gainLeft;
        voice.gainRight = gainRight;
        schedule.voices.push_back(std::move(voice));
    }

    return schedule;
}
} // namespace projectname
