// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace projectname
{
struct TimelinePlaybackPlanOptions
{
    double sampleRateHz = 44100.0;
};

struct TimelinePlaybackClipPlan
{
    std::string trackId;
    std::string clipId;
    std::string clipName;
    std::string relativePath;
    double startBeats = 0.0;
    double lengthBeats = 0.0;
    std::int64_t timelineStartSample = 0;
    std::int64_t timelineLengthSamples = 0;
    std::int64_t timelineEndSample = 0;

    [[nodiscard]] bool containsTimelineSample(std::int64_t timelineSample) const noexcept;
};

struct TimelinePlaybackPlan
{
    double tempoBpm = 120.0;
    double sampleRateHz = 44100.0;
    std::vector<TimelinePlaybackClipPlan> clips;
};

struct TimelinePlaybackActivation
{
    std::size_t clipIndex = 0;
    std::int64_t timelinePlaybackStartSample = 0;
    std::int64_t clipLocalStartOffsetSamples = 0;
    std::int64_t clipLengthSamples = 0;
};

[[nodiscard]] std::optional<std::int64_t> beatsToTimelineSamples(double beats,
                                                                 double tempoBpm,
                                                                 double sampleRateHz) noexcept;

[[nodiscard]] TimelinePlaybackPlan buildImportedAudioTimelinePlaybackPlan(
    const ProjectModel& project,
    TimelinePlaybackPlanOptions options = {});

[[nodiscard]] std::optional<TimelinePlaybackActivation> makeImportedAudioClipPlaybackActivation(
    const TimelinePlaybackPlan& plan,
    std::size_t clipIndex,
    std::int64_t transportTimelineSample) noexcept;

[[nodiscard]] std::optional<TimelinePlaybackActivation> findActiveImportedAudioClipPlaybackActivation(
    const TimelinePlaybackPlan& plan,
    std::int64_t transportTimelineSample) noexcept;

[[nodiscard]] std::optional<TimelinePlaybackActivation> findNextImportedAudioClipPlaybackActivation(
    const TimelinePlaybackPlan& plan,
    std::int64_t transportTimelineSample) noexcept;
} // namespace projectname
