// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TimelineClipLane.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace projectname
{
namespace
{
[[nodiscard]] TimelineClipLaneOptions normalizeOptions(TimelineClipLaneOptions options)
{
    if (!std::isfinite(options.viewStartBeats))
        options.viewStartBeats = 0.0;

    if (!std::isfinite(options.beatsPerPixel) || options.beatsPerPixel <= 0.0)
        options.beatsPerPixel = 0.125;

    options.viewportWidthPixels = std::max(0, options.viewportWidthPixels);
    options.clipHeightPixels = std::max(1, options.clipHeightPixels);
    options.rowGapPixels = std::max(0, options.rowGapPixels);
    return options;
}

[[nodiscard]] int beatToPixel(double beat, const TimelineClipLaneOptions& options)
{
    const auto pixels = (beat - options.viewStartBeats) / options.beatsPerPixel;
    if (!std::isfinite(pixels))
        return 0;

    const auto rounded = std::round(pixels);
    if (rounded <= static_cast<double>(std::numeric_limits<int>::min()))
        return std::numeric_limits<int>::min();

    if (rounded >= static_cast<double>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();

    return static_cast<int>(rounded);
}

[[nodiscard]] int beatLengthToPixelWidth(double lengthBeats, const TimelineClipLaneOptions& options)
{
    const auto pixels = lengthBeats / options.beatsPerPixel;
    if (!std::isfinite(pixels) || pixels <= 0.0)
        return 1;

    return std::max(1, static_cast<int>(std::ceil(pixels)));
}

[[nodiscard]] bool isUsableImportedClip(const ProjectClip& clip)
{
    return clip.type == "audio-file"
        && std::isfinite(clip.startBeats)
        && std::isfinite(clip.lengthBeats)
        && clip.lengthBeats > 0.0;
}

[[nodiscard]] std::optional<TimelineLoopRange> makeLoopRange(const ProjectModel& project,
                                                            const TimelineClipLaneOptions& options)
{
    const auto& loop = project.getLoopRegion();
    if (!loop.enabled || loop.lengthBeats <= 0.0)
        return std::nullopt;

    const auto endBeats = loop.startBeats + loop.lengthBeats;
    if (!std::isfinite(loop.startBeats)
        || !std::isfinite(loop.lengthBeats)
        || !std::isfinite(endBeats))
    {
        return std::nullopt;
    }

    TimelineLoopRange range;
    range.startBeats = loop.startBeats;
    range.lengthBeats = loop.lengthBeats;
    range.endBeats = endBeats;
    range.x = beatToPixel(range.startBeats, options);
    range.width = beatLengthToPixelWidth(range.lengthBeats, options);
    range.visible = range.x < options.viewportWidthPixels && range.x + range.width > 0;
    return range;
}

[[nodiscard]] std::vector<TimelineClipLaneItem> collectImportedAudioClips(
    const ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    const TimelineClipLaneOptions& options)
{
    std::vector<TimelineClipLaneItem> items;
    auto thumbnails = loadImportedAudioWaveforms(project, packageDirectory);
    auto thumbnailIndex = std::size_t { 0 };
    auto sourceOrder = std::size_t { 0 };

    const auto& tracks = project.getTracks();
    for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex)
    {
        for (std::size_t clipIndex = 0; clipIndex < tracks[trackIndex].clips.size(); ++clipIndex)
        {
            const auto& clip = tracks[trackIndex].clips[clipIndex];
            if (clip.type != "audio-file")
                continue;

            auto waveform = thumbnailIndex < thumbnails.size()
                ? std::move(thumbnails[thumbnailIndex])
                : WaveformThumbnail {};
            ++thumbnailIndex;

            if (!isUsableImportedClip(clip))
            {
                ++sourceOrder;
                continue;
            }

            TimelineClipLaneItem item;
            item.waveform = std::move(waveform);
            item.sourceOrder = sourceOrder++;
            item.trackIndex = trackIndex;
            item.clipIndex = clipIndex;
            item.startBeats = clip.startBeats;
            item.lengthBeats = clip.lengthBeats;
            item.endBeats = clip.startBeats + clip.lengthBeats;
            item.x = beatToPixel(item.startBeats, options);
            item.width = beatLengthToPixelWidth(item.lengthBeats, options);
            item.height = options.clipHeightPixels;
            item.visible = item.x < options.viewportWidthPixels && item.x + item.width > 0;
            item.selected = clip.id == project.getSelectedClipId();
            items.push_back(std::move(item));
        }
    }

    return items;
}
} // namespace

TimelineClipLaneLayout buildImportedAudioTimelineClipLane(const ProjectModel& project,
                                                         const std::filesystem::path& packageDirectory,
                                                         TimelineClipLaneOptions options)
{
    options = normalizeOptions(options);

    TimelineClipLaneLayout layout;
    layout.options = options;
    layout.viewEndBeats = options.viewStartBeats
        + static_cast<double>(options.viewportWidthPixels) * options.beatsPerPixel;
    layout.loopRange = makeLoopRange(project, options);

    auto items = collectImportedAudioClips(project, packageDirectory, options);
    std::stable_sort(items.begin(),
                     items.end(),
                     [](const TimelineClipLaneItem& left, const TimelineClipLaneItem& right)
                     {
                         if (left.startBeats != right.startBeats)
                             return left.startBeats < right.startBeats;

                         return left.sourceOrder < right.sourceOrder;
                     });

    std::vector<double> rowEndBeats;
    for (auto& item : items)
    {
        auto rowIndex = 0;
        for (; rowIndex < static_cast<int>(rowEndBeats.size()); ++rowIndex)
        {
            if (item.startBeats >= rowEndBeats[static_cast<std::size_t>(rowIndex)])
                break;
        }

        if (rowIndex == static_cast<int>(rowEndBeats.size()))
            rowEndBeats.push_back(item.endBeats);
        else
            rowEndBeats[static_cast<std::size_t>(rowIndex)] = item.endBeats;

        item.rowIndex = rowIndex;
        item.y = rowIndex * (options.clipHeightPixels + options.rowGapPixels);
    }

    if (!rowEndBeats.empty())
    {
        layout.contentHeightPixels = static_cast<int>(rowEndBeats.size()) * options.clipHeightPixels
            + (static_cast<int>(rowEndBeats.size()) - 1) * options.rowGapPixels;
    }

    layout.clips = std::move(items);
    return layout;
}

std::optional<TimelineClipLaneHit> hitTestTimelineClipLane(const TimelineClipLaneLayout& layout,
                                                          int x,
                                                          int y)
{
    for (auto iterator = layout.clips.rbegin(); iterator != layout.clips.rend(); ++iterator)
    {
        const auto& item = *iterator;
        if (!item.visible)
            continue;

        const auto left = static_cast<long long>(item.x);
        const auto top = static_cast<long long>(item.y);
        const auto right = left + static_cast<long long>(item.width);
        const auto bottom = top + static_cast<long long>(item.height);
        const auto hitX = static_cast<long long>(x);
        const auto hitY = static_cast<long long>(y);
        if (hitX < left || hitX >= right || hitY < top || hitY >= bottom)
            continue;

        TimelineClipLaneHit hit;
        hit.clipId = item.waveform.clip.id;
        hit.sourceOrder = item.sourceOrder;
        hit.trackIndex = item.trackIndex;
        hit.clipIndex = item.clipIndex;
        return hit;
    }

    return std::nullopt;
}
} // namespace projectname
