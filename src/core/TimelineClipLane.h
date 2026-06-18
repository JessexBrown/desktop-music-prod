// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "WaveformThumbnail.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace projectname
{
struct TimelineClipLaneOptions
{
    double viewStartBeats = 0.0;
    double beatsPerPixel = 0.125;
    int viewportWidthPixels = 640;
    int clipHeightPixels = 54;
    int rowGapPixels = 6;
};

struct TimelineClipLaneItem
{
    WaveformThumbnail waveform;
    std::size_t sourceOrder = 0;
    std::size_t trackIndex = 0;
    std::size_t clipIndex = 0;
    int rowIndex = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    double startBeats = 0.0;
    double lengthBeats = 0.0;
    double endBeats = 0.0;
    bool visible = false;
    bool selected = false;
};

struct TimelineClipLaneHit
{
    std::string clipId;
    std::size_t sourceOrder = 0;
    std::size_t trackIndex = 0;
    std::size_t clipIndex = 0;
};

struct TimelineLoopRange
{
    int x = 0;
    int width = 0;
    double startBeats = 0.0;
    double lengthBeats = 0.0;
    double endBeats = 0.0;
    bool visible = false;
};

struct TimelineClipLaneLayout
{
    TimelineClipLaneOptions options;
    double viewEndBeats = 0.0;
    int contentHeightPixels = 0;
    std::optional<TimelineLoopRange> loopRange;
    std::vector<TimelineClipLaneItem> clips;
};

[[nodiscard]] TimelineClipLaneLayout buildImportedAudioTimelineClipLane(
    const ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    TimelineClipLaneOptions options = {});

[[nodiscard]] std::optional<TimelineClipLaneHit> hitTestTimelineClipLane(
    const TimelineClipLaneLayout& layout,
    int x,
    int y);
} // namespace projectname
