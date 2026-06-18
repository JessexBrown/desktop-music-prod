// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TimelinePlaybackPlan.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] bool isValidTimelineClip(const ProjectClip& clip) noexcept
{
    return clip.type == "audio-file"
        && !clip.relativePath.empty()
        && std::isfinite(clip.startBeats)
        && std::isfinite(clip.lengthBeats)
        && clip.startBeats >= 0.0
        && clip.lengthBeats > 0.0
        && std::isfinite(clip.startBeats + clip.lengthBeats);
}

[[nodiscard]] double normalizeSampleRate(double sampleRateHz) noexcept
{
    if (!std::isfinite(sampleRateHz) || sampleRateHz <= 0.0)
        return 44100.0;

    return sampleRateHz;
}

[[nodiscard]] bool canAddWithoutOverflow(std::int64_t left, std::int64_t right) noexcept
{
    return right <= std::numeric_limits<std::int64_t>::max() - left;
}
} // namespace

bool TimelinePlaybackClipPlan::containsTimelineSample(std::int64_t timelineSample) const noexcept
{
    return timelineSample >= timelineStartSample && timelineSample < timelineEndSample;
}

std::optional<std::int64_t> beatsToTimelineSamples(double beats,
                                                  double tempoBpm,
                                                  double sampleRateHz) noexcept
{
    if (!std::isfinite(beats)
        || !std::isfinite(tempoBpm)
        || !std::isfinite(sampleRateHz)
        || beats < 0.0
        || tempoBpm <= 0.0
        || sampleRateHz <= 0.0)
    {
        return std::nullopt;
    }

    const auto samples = beats * 60.0 / tempoBpm * sampleRateHz;
    if (!std::isfinite(samples)
        || samples > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::nullopt;
    }

    return static_cast<std::int64_t>(std::llround(samples));
}

TimelinePlaybackPlan buildImportedAudioTimelinePlaybackPlan(const ProjectModel& project,
                                                            TimelinePlaybackPlanOptions options)
{
    TimelinePlaybackPlan plan;
    plan.tempoBpm = project.getTransport().getTempoBpm();
    plan.sampleRateHz = normalizeSampleRate(options.sampleRateHz);

    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (!isValidTimelineClip(clip))
                continue;

            const auto timelineStartSample = beatsToTimelineSamples(clip.startBeats,
                                                                    plan.tempoBpm,
                                                                    plan.sampleRateHz);
            const auto timelineLengthSamples = beatsToTimelineSamples(clip.lengthBeats,
                                                                     plan.tempoBpm,
                                                                     plan.sampleRateHz);
            if (!timelineStartSample.has_value() || !timelineLengthSamples.has_value())
                continue;

            const auto safeLength = std::max<std::int64_t>(1, *timelineLengthSamples);
            if (!canAddWithoutOverflow(*timelineStartSample, safeLength))
                continue;

            TimelinePlaybackClipPlan item;
            item.trackId = track.id;
            item.clipId = clip.id;
            item.clipName = clip.name;
            item.relativePath = clip.relativePath;
            item.startBeats = clip.startBeats;
            item.lengthBeats = clip.lengthBeats;
            item.timelineStartSample = *timelineStartSample;
            item.timelineLengthSamples = safeLength;
            item.timelineEndSample = item.timelineStartSample + item.timelineLengthSamples;
            plan.clips.push_back(std::move(item));
        }
    }

    std::stable_sort(plan.clips.begin(),
                     plan.clips.end(),
                     [](const TimelinePlaybackClipPlan& left, const TimelinePlaybackClipPlan& right)
                     {
                         if (left.timelineStartSample != right.timelineStartSample)
                             return left.timelineStartSample < right.timelineStartSample;

                         if (left.timelineEndSample != right.timelineEndSample)
                             return left.timelineEndSample < right.timelineEndSample;

                         return left.clipId < right.clipId;
                     });

    return plan;
}

std::optional<TimelinePlaybackActivation> makeImportedAudioClipPlaybackActivation(
    const TimelinePlaybackPlan& plan,
    std::size_t clipIndex,
    std::int64_t transportTimelineSample) noexcept
{
    if (clipIndex >= plan.clips.size())
        return std::nullopt;

    const auto& clip = plan.clips[clipIndex];
    if (transportTimelineSample >= clip.timelineEndSample)
        return std::nullopt;

    TimelinePlaybackActivation activation;
    activation.clipIndex = clipIndex;
    activation.clipLengthSamples = clip.timelineLengthSamples;

    if (transportTimelineSample <= clip.timelineStartSample)
    {
        activation.timelinePlaybackStartSample = clip.timelineStartSample;
        activation.clipLocalStartOffsetSamples = 0;
        return activation;
    }

    activation.timelinePlaybackStartSample = transportTimelineSample;
    activation.clipLocalStartOffsetSamples = transportTimelineSample - clip.timelineStartSample;
    return activation;
}

std::optional<TimelinePlaybackActivation> findActiveImportedAudioClipPlaybackActivation(
    const TimelinePlaybackPlan& plan,
    std::int64_t transportTimelineSample) noexcept
{
    for (std::size_t index = 0; index < plan.clips.size(); ++index)
    {
        if (plan.clips[index].containsTimelineSample(transportTimelineSample))
            return makeImportedAudioClipPlaybackActivation(plan, index, transportTimelineSample);
    }

    return std::nullopt;
}

std::optional<TimelinePlaybackActivation> findNextImportedAudioClipPlaybackActivation(
    const TimelinePlaybackPlan& plan,
    std::int64_t transportTimelineSample) noexcept
{
    for (std::size_t index = 0; index < plan.clips.size(); ++index)
    {
        if (transportTimelineSample < plan.clips[index].timelineEndSample)
            return makeImportedAudioClipPlaybackActivation(plan, index, transportTimelineSample);
    }

    return std::nullopt;
}
} // namespace projectname
