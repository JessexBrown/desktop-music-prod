// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectAudioImport.h"
#include "ProjectModel.h"
#include "TimelinePlaybackPlan.h"
#include "TrackVoiceSchedule.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace projectname
{
enum class TimelinePlaybackPreparationStatus
{
    importedClipReady,
    backgroundPreparationRequired,
    generatedToneFallback,
};

struct TimelinePlaybackPreparation
{
    TimelinePlaybackPreparationStatus status = TimelinePlaybackPreparationStatus::generatedToneFallback;
    std::optional<TimelinePlaybackClipPlan> clip;
    std::optional<TimelinePlaybackActivation> activation;
    PreparedMonoAudioClip preparedClip;
    std::shared_ptr<const std::vector<float>> preparedSamples;
    std::filesystem::path audioPath;
    std::int64_t transportTimelineSample = 0;
    bool usedCachedBuffer = false;
    std::string message;
};

struct TimelinePlaybackSampleRateMismatch
{
    std::string clipId;
    std::string clipName;
    std::string relativePath;
    double sourceSampleRateHz = 0.0;
    double outputSampleRateHz = 0.0;
};

enum class TimelineVoicePlaybackPreparationStatus
{
    voiceWindowReady,
    backgroundPreparationRequired,
    generatedToneFallback,
};

struct TimelineVoicePlaybackPreparation
{
    TimelineVoicePlaybackPreparationStatus status =
        TimelineVoicePlaybackPreparationStatus::generatedToneFallback;
    TrackVoiceSchedule schedule;
    std::vector<PreparedTrackVoiceBuffer> preparedBuffers;
    std::vector<TimelinePlaybackClipPlan> missingClips;
    std::vector<TimelinePlaybackSampleRateMismatch> sampleRateMismatches;
    std::int64_t transportTimelineSample = 0;
    std::int64_t requestedFrameCount = 0;
    bool usedCachedBuffers = false;
    std::string message;
};

struct ImportedTimelineClipCacheLimits
{
    std::size_t maxEntries = 4;
    std::size_t maxSampleBytes = 32U * 1024U * 1024U;
};

struct TimelineViewportState
{
    double viewStartBeats = 0.0;
    double beatsPerPixel = 0.125;
};

enum class ImportedClipEditKind
{
    none,
    placement,
    mediaReplacement,
};

[[nodiscard]] std::string formatTimelineViewportIndicator(const TimelineViewportState& viewport);
[[nodiscard]] std::optional<TimelineViewportState> fitTimelineViewportToImportedAudioClips(
    const ProjectModel& project,
    int viewportWidthPixels,
    double paddingBeats = 1.0);
[[nodiscard]] std::optional<TimelineViewportState> centerTimelineViewportOnSelectedImportedAudioClip(
    const ProjectModel& project,
    TimelineViewportState currentViewport,
    int viewportWidthPixels);

class AppSession
{
public:
    AppSession();
    explicit AppSession(ProjectModel project);

    [[nodiscard]] ProjectModel& getProject() noexcept;
    [[nodiscard]] const ProjectModel& getProject() const noexcept;
    void replaceProject(ProjectModel project);

    [[nodiscard]] TransportState& getTransport() noexcept;
    [[nodiscard]] const TransportState& getTransport() const noexcept;

    void play() noexcept;
    [[nodiscard]] std::shared_ptr<const std::vector<float>> cacheImportedTimelineClip(
        const ProjectClip& clip,
        PreparedMonoAudioClip preparedClip);
    [[nodiscard]] std::shared_ptr<const std::vector<float>> cacheImportedTimelineClip(
        const ProjectClip& clip,
        PreparedMonoAudioClip preparedClip,
        std::shared_ptr<const std::vector<float>> preparedSamples);
    void clearImportedTimelineClipCache() noexcept;
    void setImportedTimelineClipCacheLimits(ImportedTimelineClipCacheLimits limits);
    [[nodiscard]] ImportedTimelineClipCacheLimits getImportedTimelineClipCacheLimits() const noexcept;
    [[nodiscard]] TimelineVoicePlaybackPreparation playCachedTimelineVoiceWindow(
        double outputSampleRateHz,
        std::int64_t minimumRenderFrameCount,
        std::string& error);
    [[nodiscard]] TimelinePlaybackPreparation playFromCachedTimeline(double outputSampleRateHz,
                                                                     std::string& error);
    [[nodiscard]] TimelinePlaybackPreparation playFromTimeline(
        const std::filesystem::path& packageDirectory,
        double outputSampleRateHz,
        std::string& error);
    [[nodiscard]] TimelinePlaybackPreparation playFromTimeline(
        const std::filesystem::path& packageDirectory,
        double outputSampleRateHz,
        const WavDecodeOptions& decodeOptions,
        std::string& error);
    void stop() noexcept;
    void togglePlayback() noexcept;
    void advanceSeconds(double seconds) noexcept;

    void setTempoBpm(double tempoBpm) noexcept;
    [[nodiscard]] bool setTimeSignature(int numerator, int denominator) noexcept;
    [[nodiscard]] const ProjectLoopRegion& getLoopRegion() const noexcept;
    [[nodiscard]] bool setLoopRegion(double startBeats, double lengthBeats, std::string& error);
    void clearLoopRegion() noexcept;
    [[nodiscard]] const TimelineViewportState& getTimelineViewport() const noexcept;
    void setTimelineViewport(double viewStartBeats, double beatsPerPixel) noexcept;
    void setTimelineViewStartBeats(double viewStartBeats) noexcept;
    void setTimelineBeatsPerPixel(double beatsPerPixel) noexcept;
    void nudgeTimelineViewStartBeats(double deltaBeats) noexcept;
    void scaleTimelineBeatsPerPixel(double multiplier) noexcept;
    [[nodiscard]] const std::string& getSelectedClipId() const noexcept;
    [[nodiscard]] bool selectImportedAudioClip(const std::string& clipId, std::string& error);
    [[nodiscard]] bool selectAdjacentImportedAudioClip(ImportedAudioClipSelectionDirection direction,
                                                       std::string& error);
    void clearSelectedClip() noexcept;
    [[nodiscard]] bool setTrackMixState(const std::string& trackId,
                                        float volume,
                                        float pan,
                                        bool muted,
                                        bool solo,
                                        std::string& error);

    [[nodiscard]] bool saveProjectPackage(const std::filesystem::path& packageDirectory, std::string& error) const;
    [[nodiscard]] bool loadProjectPackage(const std::filesystem::path& packageDirectory, std::string& error);
    [[nodiscard]] std::optional<ProjectAudioImportResult> importPcm16WavIntoProjectPackage(
        const std::filesystem::path& packageDirectory,
        const std::filesystem::path& sourceWavPath,
        std::string& error);
    [[nodiscard]] std::optional<ProjectAudioImportResult> importPcm16WavIntoProjectPackage(
        const std::filesystem::path& packageDirectory,
        const std::filesystem::path& sourceWavPath,
        std::optional<double> requestedStartBeats,
        std::string& error);
    [[nodiscard]] bool setImportedAudioClipStartBeats(const std::string& clipId,
                                                      double startBeats,
                                                      std::string& error);
    [[nodiscard]] ImportedClipEditKind getNextImportedClipUndoEditKind() const noexcept;
    [[nodiscard]] ImportedClipEditKind getNextImportedClipRedoEditKind() const noexcept;
    [[nodiscard]] bool canUndoImportedClipEdit() const noexcept;
    [[nodiscard]] bool canRedoImportedClipEdit() const noexcept;
    [[nodiscard]] bool undoImportedClipEdit(std::string& error);
    [[nodiscard]] bool redoImportedClipEdit(std::string& error);
    [[nodiscard]] bool canUndoImportedClipPlacementEdit() const noexcept;
    [[nodiscard]] bool canRedoImportedClipPlacementEdit() const noexcept;
    [[nodiscard]] bool undoImportedClipPlacementEdit(std::string& error);
    [[nodiscard]] bool redoImportedClipPlacementEdit(std::string& error);
    [[nodiscard]] bool canUndoImportedClipMediaReplacementEdit() const noexcept;
    [[nodiscard]] bool canRedoImportedClipMediaReplacementEdit() const noexcept;
    [[nodiscard]] bool undoImportedClipMediaReplacementEdit(std::string& error);
    [[nodiscard]] bool redoImportedClipMediaReplacementEdit(std::string& error);
    [[nodiscard]] bool replaceImportedAudioClipMedia(const std::string& clipId,
                                                     std::string relativePath,
                                                     std::string analysisPath,
                                                     double lengthBeats,
                                                     std::string& error);

    [[nodiscard]] bool shouldPlayGeneratedTone() const noexcept;

private:
    struct CachedImportedTimelineClip
    {
        std::string clipId;
        std::string relativePath;
        PreparedMonoAudioClip preparedClip;
        std::shared_ptr<const std::vector<float>> samples;
    };

    struct ImportedClipMediaState
    {
        std::string relativePath;
        std::string analysisPath;
        double lengthBeats = 0.0;

        [[nodiscard]] bool operator==(const ImportedClipMediaState& other) const = default;
    };

    struct ImportedClipEdit
    {
        ImportedClipEditKind kind = ImportedClipEditKind::placement;
        std::string clipId;
        double beforeStartBeats = 0.0;
        double afterStartBeats = 0.0;
        ImportedClipMediaState beforeMedia;
        ImportedClipMediaState afterMedia;
    };

    [[nodiscard]] const CachedImportedTimelineClip* findCachedImportedTimelineClip(
        const TimelinePlaybackClipPlan& clip) const noexcept;
    [[nodiscard]] std::shared_ptr<const std::vector<float>> storeCachedImportedTimelineClip(
        CachedImportedTimelineClip cache);
    [[nodiscard]] std::size_t importedTimelineClipCacheSampleBytes() const noexcept;
    [[nodiscard]] bool canUndoImportedClipEditOfKind(ImportedClipEditKind kind) const noexcept;
    [[nodiscard]] bool canRedoImportedClipEditOfKind(ImportedClipEditKind kind) const noexcept;
    [[nodiscard]] bool undoImportedClipEditOfKind(ImportedClipEditKind kind, std::string& error);
    [[nodiscard]] bool redoImportedClipEditOfKind(ImportedClipEditKind kind, std::string& error);
    [[nodiscard]] bool applyImportedClipEdit(const ImportedClipEdit& edit,
                                             bool useAfterState,
                                             std::string& error);
    void trimImportedTimelineClipCacheToLimits();
    void clearImportedTimelineClipCacheForClip(const std::string& clipId) noexcept;
    void clearImportedClipEditHistory() noexcept;

    ProjectModel project_;
    ImportedTimelineClipCacheLimits importedTimelineClipCacheLimits_;
    TimelineViewportState timelineViewport_;
    std::vector<CachedImportedTimelineClip> importedTimelineClipCache_;
    std::vector<ImportedClipEdit> importedClipUndoStack_;
    std::vector<ImportedClipEdit> importedClipRedoStack_;
    bool generatedToneActive_ = false;
};
} // namespace projectname
