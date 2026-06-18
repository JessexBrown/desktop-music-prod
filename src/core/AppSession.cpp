#include "AppSession.h"

#include "PackagePath.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>

namespace projectname
{
namespace
{
void applyLoopRegionAfterAdvance(ProjectModel& project) noexcept
{
    auto& transport = project.getTransport();
    const auto loop = project.getLoopRegion();
    if (!transport.isPlaying() || !loop.enabled || loop.lengthBeats <= 0.0)
        return;

    const auto loopEndBeats = loop.startBeats + loop.lengthBeats;
    auto positionBeats = transport.getPositionBeats();
    if (!std::isfinite(positionBeats) || positionBeats < loopEndBeats)
        return;

    auto offsetBeats = std::fmod(positionBeats - loop.startBeats, loop.lengthBeats);
    if (offsetBeats < 0.0)
        offsetBeats += loop.lengthBeats;

    transport.setPositionBeats(loop.startBeats + offsetBeats);
}

[[nodiscard]] const ProjectClip* findProjectClipById(const ProjectModel& project,
                                                     const std::string& clipId) noexcept
{
    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (clip.id == clipId)
                return &clip;
        }
    }

    return nullptr;
}

void sanitizePreparedSamples(std::vector<float>& samples)
{
    for (auto& sample : samples)
    {
        if (!std::isfinite(sample))
            sample = 0.0f;
        else
            sample = std::clamp(sample, -1.0f, 1.0f);
    }
}

[[nodiscard]] std::size_t preparedSampleByteCount(
    const std::shared_ptr<const std::vector<float>>& samples) noexcept
{
    if (samples == nullptr)
        return 0;

    if (samples->size() > std::numeric_limits<std::size_t>::max() / sizeof(float))
        return std::numeric_limits<std::size_t>::max();

    return samples->size() * sizeof(float);
}

[[nodiscard]] double normalizeOutputSampleRate(double outputSampleRateHz) noexcept
{
    return (std::isfinite(outputSampleRateHz) && outputSampleRateHz > 0.0)
        ? outputSampleRateHz
        : 44100.0;
}

[[nodiscard]] std::int64_t makeVoiceWindowFrameCount(
    const TimelinePlaybackActivation& activation,
    std::int64_t transportTimelineSample,
    std::int64_t minimumRenderFrameCount) noexcept
{
    const auto safeMinimum = std::max<std::int64_t>(1, minimumRenderFrameCount);
    const auto remainingClipFrames = std::max<std::int64_t>(
        1,
        activation.clipLengthSamples - activation.clipLocalStartOffsetSamples);

    if (activation.timelinePlaybackStartSample <= transportTimelineSample)
        return safeMinimum < remainingClipFrames ? remainingClipFrames : safeMinimum;

    const auto preRollFrames = activation.timelinePlaybackStartSample - transportTimelineSample;
    if (preRollFrames > std::numeric_limits<std::int64_t>::max() - remainingClipFrames)
        return safeMinimum;

    const auto futureClipWindow = preRollFrames + remainingClipFrames;
    return safeMinimum < futureClipWindow ? futureClipWindow : safeMinimum;
}

[[nodiscard]] bool hasPreparedBufferForClip(const std::vector<PreparedTrackVoiceBuffer>& buffers,
                                            const std::string& clipId) noexcept
{
    return std::any_of(buffers.begin(),
                       buffers.end(),
                       [&clipId](const PreparedTrackVoiceBuffer& buffer)
                       {
                           return buffer.clipId == clipId;
                       });
}

[[nodiscard]] bool sampleRatesDiffer(double sourceSampleRateHz, double outputSampleRateHz) noexcept
{
    return std::isfinite(sourceSampleRateHz)
        && std::isfinite(outputSampleRateHz)
        && sourceSampleRateHz > 0.0
        && outputSampleRateHz > 0.0
        && std::abs(sourceSampleRateHz - outputSampleRateHz) > 1.0;
}

[[nodiscard]] double normalizeTimelineViewStartBeats(double viewStartBeats) noexcept
{
    return std::isfinite(viewStartBeats) && viewStartBeats > 0.0 ? viewStartBeats : 0.0;
}

[[nodiscard]] double normalizeTimelineBeatsPerPixel(double beatsPerPixel) noexcept
{
    constexpr auto minBeatsPerPixel = 1.0 / 64.0;
    constexpr auto maxBeatsPerPixel = 4.0;

    if (!std::isfinite(beatsPerPixel))
        return TimelineViewportState {}.beatsPerPixel;

    return std::clamp(beatsPerPixel, minBeatsPerPixel, maxBeatsPerPixel);
}

[[nodiscard]] bool isUsableImportedTimelineClip(const ProjectClip& clip) noexcept
{
    const auto endBeats = clip.startBeats + clip.lengthBeats;
    return clip.type == "audio-file"
        && std::isfinite(clip.startBeats)
        && std::isfinite(clip.lengthBeats)
        && std::isfinite(endBeats)
        && clip.lengthBeats > 0.0;
}

[[nodiscard]] const ProjectClip* findUsableImportedTimelineClipById(const ProjectModel& project,
                                                                    const std::string& clipId) noexcept
{
    if (clipId.empty())
        return nullptr;

    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (clip.id == clipId && isUsableImportedTimelineClip(clip))
                return &clip;
        }
    }

    return nullptr;
}

[[nodiscard]] TimelinePlaybackSampleRateMismatch makeSampleRateMismatch(
    const TimelinePlaybackClipPlan& clip,
    double sourceSampleRateHz,
    double outputSampleRateHz)
{
    TimelinePlaybackSampleRateMismatch mismatch;
    mismatch.clipId = clip.clipId;
    mismatch.clipName = clip.clipName;
    mismatch.relativePath = clip.relativePath;
    mismatch.sourceSampleRateHz = sourceSampleRateHz;
    mismatch.outputSampleRateHz = outputSampleRateHz;
    return mismatch;
}

[[nodiscard]] std::vector<TrackMixState> buildTrackMixStates(const ProjectModel& project)
{
    std::vector<TrackMixState> mixStates;
    mixStates.reserve(project.getTracks().size());

    for (const auto& track : project.getTracks())
    {
        TrackMixState state;
        state.trackId = track.id;
        state.gain = track.volume;
        state.pan = track.pan;
        state.muted = track.muted;
        state.solo = track.solo;
        mixStates.push_back(std::move(state));
    }

    return mixStates;
}

} // namespace

std::string formatTimelineViewportIndicator(const TimelineViewportState& viewport)
{
    std::ostringstream stream;
    stream << std::fixed
           << "Timeline view: start "
           << std::setprecision(2)
           << normalizeTimelineViewStartBeats(viewport.viewStartBeats)
           << " beats | "
           << std::setprecision(4)
           << normalizeTimelineBeatsPerPixel(viewport.beatsPerPixel)
           << " beats/px";
    return stream.str();
}

std::optional<TimelineViewportState> fitTimelineViewportToImportedAudioClips(
    const ProjectModel& project,
    int viewportWidthPixels,
    double paddingBeats)
{
    if (viewportWidthPixels <= 0)
        return std::nullopt;

    const auto safePaddingBeats =
        std::isfinite(paddingBeats) && paddingBeats > 0.0 ? paddingBeats : 0.0;

    auto firstStartBeats = std::numeric_limits<double>::infinity();
    auto lastEndBeats = -std::numeric_limits<double>::infinity();
    auto foundImportedClip = false;

    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (!isUsableImportedTimelineClip(clip))
                continue;

            firstStartBeats = std::min(firstStartBeats, clip.startBeats);
            lastEndBeats = std::max(lastEndBeats, clip.startBeats + clip.lengthBeats);
            foundImportedClip = true;
        }
    }

    if (!foundImportedClip)
        return std::nullopt;

    const auto viewStartBeats = normalizeTimelineViewStartBeats(firstStartBeats - safePaddingBeats);
    const auto viewEndBeats = std::max(lastEndBeats + safePaddingBeats,
                                       viewStartBeats + TimelineViewportState {}.beatsPerPixel);
    const auto beatsPerPixel = normalizeTimelineBeatsPerPixel(
        (viewEndBeats - viewStartBeats) / static_cast<double>(viewportWidthPixels));

    return TimelineViewportState { viewStartBeats, beatsPerPixel };
}

std::optional<TimelineViewportState> centerTimelineViewportOnSelectedImportedAudioClip(
    const ProjectModel& project,
    TimelineViewportState currentViewport,
    int viewportWidthPixels)
{
    if (viewportWidthPixels <= 0)
        return std::nullopt;

    const auto* selectedClip = findUsableImportedTimelineClipById(project, project.getSelectedClipId());
    if (selectedClip == nullptr)
        return std::nullopt;

    const auto beatsPerPixel = normalizeTimelineBeatsPerPixel(currentViewport.beatsPerPixel);
    const auto visibleBeats = static_cast<double>(viewportWidthPixels) * beatsPerPixel;
    const auto clipCenterBeats = selectedClip->startBeats + selectedClip->lengthBeats * 0.5;
    const auto viewStartBeats = normalizeTimelineViewStartBeats(clipCenterBeats - visibleBeats * 0.5);

    return TimelineViewportState { viewStartBeats, beatsPerPixel };
}

AppSession::AppSession()
    : project_(ProjectModel::createDefault())
{
}

AppSession::AppSession(ProjectModel project)
    : project_(std::move(project))
{
}

ProjectModel& AppSession::getProject() noexcept
{
    return project_;
}

const ProjectModel& AppSession::getProject() const noexcept
{
    return project_;
}

void AppSession::replaceProject(ProjectModel project)
{
    project_ = std::move(project);
    clearImportedTimelineClipCache();
    clearImportedClipPlacementEditHistory();
    generatedToneActive_ = false;
}

TransportState& AppSession::getTransport() noexcept
{
    return project_.getTransport();
}

const TransportState& AppSession::getTransport() const noexcept
{
    return project_.getTransport();
}

void AppSession::play() noexcept
{
    project_.getTransport().play();
    generatedToneActive_ = true;
}

std::shared_ptr<const std::vector<float>> AppSession::cacheImportedTimelineClip(
    const ProjectClip& clip,
    PreparedMonoAudioClip preparedClip)
{
    const auto* currentClip = findProjectClipById(project_, clip.id);
    if (currentClip == nullptr
        || currentClip->type != "audio-file"
        || currentClip->relativePath != clip.relativePath
        || preparedClip.samples.empty())
    {
        return {};
    }

    sanitizePreparedSamples(preparedClip.samples);

    CachedImportedTimelineClip cache;
    cache.clipId = clip.id;
    cache.relativePath = clip.relativePath;
    cache.samples = std::make_shared<const std::vector<float>>(std::move(preparedClip.samples));
    preparedClip.samples.clear();
    cache.preparedClip = std::move(preparedClip);

    return storeCachedImportedTimelineClip(std::move(cache));
}

std::shared_ptr<const std::vector<float>> AppSession::cacheImportedTimelineClip(
    const ProjectClip& clip,
    PreparedMonoAudioClip preparedClip,
    std::shared_ptr<const std::vector<float>> preparedSamples)
{
    const auto* currentClip = findProjectClipById(project_, clip.id);
    if (currentClip == nullptr
        || currentClip->type != "audio-file"
        || currentClip->relativePath != clip.relativePath
        || preparedSamples == nullptr
        || preparedSamples->empty())
    {
        return {};
    }

    CachedImportedTimelineClip cache;
    cache.clipId = clip.id;
    cache.relativePath = clip.relativePath;
    preparedClip.samples.clear();
    cache.preparedClip = std::move(preparedClip);
    cache.samples = std::move(preparedSamples);

    return storeCachedImportedTimelineClip(std::move(cache));
}

void AppSession::clearImportedTimelineClipCache() noexcept
{
    importedTimelineClipCache_.clear();
}

void AppSession::setImportedTimelineClipCacheLimits(ImportedTimelineClipCacheLimits limits)
{
    importedTimelineClipCacheLimits_ = limits;
    trimImportedTimelineClipCacheToLimits();
}

ImportedTimelineClipCacheLimits AppSession::getImportedTimelineClipCacheLimits() const noexcept
{
    return importedTimelineClipCacheLimits_;
}

TimelineVoicePlaybackPreparation AppSession::playCachedTimelineVoiceWindow(
    double outputSampleRateHz,
    std::int64_t minimumRenderFrameCount,
    std::string& error)
{
    error.clear();

    auto makeFallback = [this, &error](std::string message)
    {
        project_.getTransport().play();
        generatedToneActive_ = true;

        TimelineVoicePlaybackPreparation preparation;
        preparation.status = TimelineVoicePlaybackPreparationStatus::generatedToneFallback;
        preparation.message = std::move(message);
        error = preparation.message;
        return preparation;
    };

    const auto sampleRateHz = normalizeOutputSampleRate(outputSampleRateHz);
    const auto transportTimelineSample = beatsToTimelineSamples(project_.getTransport().getPositionBeats(),
                                                                project_.getTransport().getTempoBpm(),
                                                                sampleRateHz);
    if (!transportTimelineSample.has_value())
        return makeFallback("Could not map the current transport position to timeline samples.");

    TimelinePlaybackPlanOptions options;
    options.sampleRateHz = sampleRateHz;
    const auto plan = buildImportedAudioTimelinePlaybackPlan(project_, options);
    const auto activation = findNextImportedAudioClipPlaybackActivation(plan, *transportTimelineSample);
    if (!activation.has_value())
        return makeFallback("No imported timeline clip is available at or after the current transport position.");

    const auto frameCount = makeVoiceWindowFrameCount(*activation,
                                                      *transportTimelineSample,
                                                      minimumRenderFrameCount);
    auto schedule = buildTrackVoiceSchedule(plan,
                                            *transportTimelineSample,
                                            frameCount,
                                            buildTrackMixStates(project_));

    TimelineVoicePlaybackPreparation preparation;
    preparation.transportTimelineSample = *transportTimelineSample;
    preparation.requestedFrameCount = std::max<std::int64_t>(1, minimumRenderFrameCount);
    preparation.schedule = std::move(schedule);

    if (preparation.schedule.voices.empty())
        return makeFallback("No imported timeline voice overlaps the prepared playback window.");

    for (const auto& voice : preparation.schedule.voices)
    {
        if (voice.clipIndex >= plan.clips.size())
            continue;

        const auto& clip = plan.clips[voice.clipIndex];
        const auto* cachedClip = findCachedImportedTimelineClip(clip);
        if (cachedClip == nullptr || cachedClip->samples == nullptr || cachedClip->samples->empty())
        {
            preparation.missingClips.push_back(clip);
            continue;
        }

        if (!hasPreparedBufferForClip(preparation.preparedBuffers, clip.clipId))
        {
            PreparedTrackVoiceBuffer buffer;
            buffer.clipId = clip.clipId;
            buffer.samples = cachedClip->samples;
            preparation.preparedBuffers.push_back(std::move(buffer));
        }

        if (sampleRatesDiffer(cachedClip->preparedClip.sampleRateHz, sampleRateHz))
        {
            preparation.sampleRateMismatches.push_back(
                makeSampleRateMismatch(clip, cachedClip->preparedClip.sampleRateHz, sampleRateHz));
        }
    }

    if (!preparation.missingClips.empty())
    {
        preparation.status = TimelineVoicePlaybackPreparationStatus::backgroundPreparationRequired;
        preparation.message = "Imported timeline voice window needs background preparation.";
        return preparation;
    }

    project_.getTransport().play();
    generatedToneActive_ = false;

    preparation.status = TimelineVoicePlaybackPreparationStatus::voiceWindowReady;
    preparation.usedCachedBuffers = !preparation.preparedBuffers.empty();
    preparation.message = preparation.sampleRateMismatches.empty()
        ? "Prepared imported timeline voice window from cache."
        : "Prepared imported timeline voice window from cache with sample-rate mismatch warning.";
    return preparation;
}

TimelinePlaybackPreparation AppSession::playFromCachedTimeline(double outputSampleRateHz,
                                                               std::string& error)
{
    error.clear();

    auto makeFallback = [this, &error](std::string message)
    {
        project_.getTransport().play();
        generatedToneActive_ = true;

        TimelinePlaybackPreparation preparation;
        preparation.status = TimelinePlaybackPreparationStatus::generatedToneFallback;
        preparation.message = std::move(message);
        error = preparation.message;
        return preparation;
    };

    const auto sampleRateHz = normalizeOutputSampleRate(outputSampleRateHz);
    const auto transportTimelineSample = beatsToTimelineSamples(project_.getTransport().getPositionBeats(),
                                                                project_.getTransport().getTempoBpm(),
                                                                sampleRateHz);
    if (!transportTimelineSample.has_value())
        return makeFallback("Could not map the current transport position to timeline samples.");

    TimelinePlaybackPlanOptions options;
    options.sampleRateHz = sampleRateHz;
    const auto plan = buildImportedAudioTimelinePlaybackPlan(project_, options);
    const auto activation = findNextImportedAudioClipPlaybackActivation(plan, *transportTimelineSample);
    if (!activation.has_value())
        return makeFallback("No imported timeline clip is available at or after the current transport position.");

    const auto& clip = plan.clips[activation->clipIndex];
    const auto* cachedClip = findCachedImportedTimelineClip(clip);
    if (cachedClip != nullptr && cachedClip->samples != nullptr && !cachedClip->samples->empty())
    {
        project_.getTransport().play();
        generatedToneActive_ = false;

        TimelinePlaybackPreparation preparation;
        preparation.status = TimelinePlaybackPreparationStatus::importedClipReady;
        preparation.clip = clip;
        preparation.activation = *activation;
        preparation.preparedClip = cachedClip->preparedClip;
        preparation.preparedSamples = cachedClip->samples;
        preparation.transportTimelineSample = *transportTimelineSample;
        preparation.usedCachedBuffer = true;
        preparation.message = "Prepared imported timeline clip from cache.";
        return preparation;
    }

    TimelinePlaybackPreparation preparation;
    preparation.status = TimelinePlaybackPreparationStatus::backgroundPreparationRequired;
    preparation.clip = clip;
    preparation.activation = *activation;
    preparation.transportTimelineSample = *transportTimelineSample;
    preparation.message = "Imported timeline clip needs background preparation.";
    return preparation;
}

TimelinePlaybackPreparation AppSession::playFromTimeline(const std::filesystem::path& packageDirectory,
                                                         double outputSampleRateHz,
                                                         std::string& error)
{
    WavDecodeOptions decodeOptions;
    return playFromTimeline(packageDirectory, outputSampleRateHz, decodeOptions, error);
}

TimelinePlaybackPreparation AppSession::playFromTimeline(const std::filesystem::path& packageDirectory,
                                                         double outputSampleRateHz,
                                                         const WavDecodeOptions& decodeOptions,
                                                         std::string& error)
{
    error.clear();

    auto makeFallback = [this, &error](std::string message)
    {
        project_.getTransport().play();
        generatedToneActive_ = true;

        TimelinePlaybackPreparation preparation;
        preparation.status = TimelinePlaybackPreparationStatus::generatedToneFallback;
        preparation.message = std::move(message);
        error = preparation.message;
        return preparation;
    };

    const auto sampleRateHz = normalizeOutputSampleRate(outputSampleRateHz);
    const auto transportTimelineSample = beatsToTimelineSamples(project_.getTransport().getPositionBeats(),
                                                                project_.getTransport().getTempoBpm(),
                                                                sampleRateHz);
    if (!transportTimelineSample.has_value())
        return makeFallback("Could not map the current transport position to timeline samples.");

    TimelinePlaybackPlanOptions options;
    options.sampleRateHz = sampleRateHz;
    const auto plan = buildImportedAudioTimelinePlaybackPlan(project_, options);
    const auto activation = findNextImportedAudioClipPlaybackActivation(plan, *transportTimelineSample);
    if (!activation.has_value())
        return makeFallback("No imported timeline clip is available at or after the current transport position.");

    const auto& clip = plan.clips[activation->clipIndex];
    const auto* cachedClip = findCachedImportedTimelineClip(clip);
    if (cachedClip != nullptr && cachedClip->samples != nullptr && !cachedClip->samples->empty())
    {
        project_.getTransport().play();
        generatedToneActive_ = false;

        TimelinePlaybackPreparation preparation;
        preparation.status = TimelinePlaybackPreparationStatus::importedClipReady;
        preparation.clip = clip;
        preparation.activation = *activation;
        preparation.preparedClip = cachedClip->preparedClip;
        preparation.preparedSamples = cachedClip->samples;
        preparation.transportTimelineSample = *transportTimelineSample;
        preparation.usedCachedBuffer = true;
        preparation.message = "Prepared imported timeline clip from cache.";
        return preparation;
    }

    const auto relativePath = std::filesystem::path(clip.relativePath);
    if (!isSafePackageRelativePath(relativePath))
        return makeFallback("Imported audio clip path is not package-relative.");

    const auto audioPath = resolvePackagePath(packageDirectory, relativePath);
    auto preparedClip = loadPcm16WavAsPreparedMonoClip(audioPath, decodeOptions, error);
    if (!preparedClip.has_value())
    {
        const auto detail = error.empty() ? std::string("unknown error") : error;
        return makeFallback("Could not prepare imported timeline clip: " + detail);
    }

    project_.getTransport().play();
    generatedToneActive_ = false;

    ProjectClip preparedProjectClip;
    preparedProjectClip.id = clip.clipId;
    preparedProjectClip.name = clip.clipName;
    preparedProjectClip.type = "audio-file";
    preparedProjectClip.relativePath = clip.relativePath;
    preparedProjectClip.startBeats = clip.startBeats;
    preparedProjectClip.lengthBeats = clip.lengthBeats;
    auto preparedSamples = cacheImportedTimelineClip(preparedProjectClip, std::move(*preparedClip));
    if (preparedSamples == nullptr || preparedSamples->empty())
        return makeFallback("Could not cache prepared imported timeline clip.");

    TimelinePlaybackPreparation preparation;
    preparation.status = TimelinePlaybackPreparationStatus::importedClipReady;
    preparation.clip = clip;
    preparation.activation = *activation;
    if (const auto* refreshedCache = findCachedImportedTimelineClip(clip))
        preparation.preparedClip = refreshedCache->preparedClip;
    preparation.preparedSamples = std::move(preparedSamples);
    preparation.audioPath = audioPath;
    preparation.transportTimelineSample = *transportTimelineSample;
    preparation.message = "Prepared imported timeline clip.";
    return preparation;
}

void AppSession::stop() noexcept
{
    project_.getTransport().stop();
    generatedToneActive_ = false;
}

void AppSession::togglePlayback() noexcept
{
    if (project_.getTransport().isPlaying())
        stop();
    else
        play();
}

void AppSession::advanceSeconds(double seconds) noexcept
{
    project_.getTransport().advanceSeconds(seconds);
    applyLoopRegionAfterAdvance(project_);
}

void AppSession::setTempoBpm(double tempoBpm) noexcept
{
    project_.getTransport().setTempoBpm(tempoBpm);
}

bool AppSession::setTimeSignature(int numerator, int denominator) noexcept
{
    return project_.getTransport().setTimeSignature(numerator, denominator);
}

const ProjectLoopRegion& AppSession::getLoopRegion() const noexcept
{
    return project_.getLoopRegion();
}

bool AppSession::setLoopRegion(double startBeats, double lengthBeats, std::string& error)
{
    return project_.setLoopRegion(startBeats, lengthBeats, error);
}

void AppSession::clearLoopRegion() noexcept
{
    project_.clearLoopRegion();
}

const TimelineViewportState& AppSession::getTimelineViewport() const noexcept
{
    return timelineViewport_;
}

void AppSession::setTimelineViewport(double viewStartBeats, double beatsPerPixel) noexcept
{
    timelineViewport_.viewStartBeats = normalizeTimelineViewStartBeats(viewStartBeats);
    timelineViewport_.beatsPerPixel = normalizeTimelineBeatsPerPixel(beatsPerPixel);
}

void AppSession::setTimelineViewStartBeats(double viewStartBeats) noexcept
{
    timelineViewport_.viewStartBeats = normalizeTimelineViewStartBeats(viewStartBeats);
}

void AppSession::setTimelineBeatsPerPixel(double beatsPerPixel) noexcept
{
    timelineViewport_.beatsPerPixel = normalizeTimelineBeatsPerPixel(beatsPerPixel);
}

void AppSession::nudgeTimelineViewStartBeats(double deltaBeats) noexcept
{
    if (!std::isfinite(deltaBeats))
        return;

    setTimelineViewStartBeats(timelineViewport_.viewStartBeats + deltaBeats);
}

void AppSession::scaleTimelineBeatsPerPixel(double multiplier) noexcept
{
    if (!std::isfinite(multiplier) || multiplier <= 0.0)
        return;

    setTimelineBeatsPerPixel(timelineViewport_.beatsPerPixel * multiplier);
}

const std::string& AppSession::getSelectedClipId() const noexcept
{
    return project_.getSelectedClipId();
}

bool AppSession::selectImportedAudioClip(const std::string& clipId, std::string& error)
{
    return project_.selectImportedAudioClip(clipId, error);
}

bool AppSession::selectAdjacentImportedAudioClip(ImportedAudioClipSelectionDirection direction,
                                                 std::string& error)
{
    return project_.selectAdjacentImportedAudioClip(direction, error);
}

void AppSession::clearSelectedClip() noexcept
{
    project_.clearSelectedClip();
}

bool AppSession::setTrackMixState(const std::string& trackId,
                                  float volume,
                                  float pan,
                                  bool muted,
                                  bool solo,
                                  std::string& error)
{
    return project_.setTrackMixState(trackId, volume, pan, muted, solo, error);
}

bool AppSession::saveProjectPackage(const std::filesystem::path& packageDirectory, std::string& error) const
{
    return project_.savePackage(packageDirectory, error);
}

bool AppSession::loadProjectPackage(const std::filesystem::path& packageDirectory, std::string& error)
{
    auto loadedProject = ProjectModel::loadPackage(packageDirectory, error);
    if (!loadedProject.has_value())
        return false;

    project_ = std::move(*loadedProject);
    clearImportedTimelineClipCache();
    clearImportedClipPlacementEditHistory();
    generatedToneActive_ = false;
    return true;
}

std::optional<ProjectAudioImportResult> AppSession::importPcm16WavIntoProjectPackage(
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& sourceWavPath,
    std::string& error)
{
    return importPcm16WavIntoProjectPackage(packageDirectory, sourceWavPath, std::nullopt, error);
}

std::optional<ProjectAudioImportResult> AppSession::importPcm16WavIntoProjectPackage(
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& sourceWavPath,
    std::optional<double> requestedStartBeats,
    std::string& error)
{
    ProjectAudioImportOptions options;
    options.requestedStartBeats = requestedStartBeats;
    auto result = projectname::importPcm16WavIntoProjectPackage(project_, packageDirectory, sourceWavPath, options, error);
    if (result.has_value())
    {
        [[maybe_unused]] const auto cachedSamples = cacheImportedTimelineClip(result->clip, result->preparedClip);
        std::string selectionError;
        [[maybe_unused]] const auto selected = selectImportedAudioClip(result->clip.id, selectionError);
    }

    if (result.has_value())
        generatedToneActive_ = false;

    return result;
}

bool AppSession::setImportedAudioClipStartBeats(const std::string& clipId,
                                                double startBeats,
                                                std::string& error)
{
    const auto* clipBefore = findProjectClipById(project_, clipId);
    const auto beforeStartBeats = clipBefore != nullptr ? clipBefore->startBeats : 0.0;

    if (!project_.setImportedAudioClipStartBeats(clipId, startBeats, error))
        return false;

    const auto* clipAfter = findProjectClipById(project_, clipId);
    if (clipAfter == nullptr || std::abs(beforeStartBeats - clipAfter->startBeats) < 0.0000001)
        return true;

    importedClipPlacementUndoStack_.push_back({ clipId, beforeStartBeats, clipAfter->startBeats });
    importedClipPlacementRedoStack_.clear();
    return true;
}

bool AppSession::canUndoImportedClipPlacementEdit() const noexcept
{
    return !importedClipPlacementUndoStack_.empty();
}

bool AppSession::canRedoImportedClipPlacementEdit() const noexcept
{
    return !importedClipPlacementRedoStack_.empty();
}

bool AppSession::undoImportedClipPlacementEdit(std::string& error)
{
    error.clear();

    if (importedClipPlacementUndoStack_.empty())
    {
        error = "No imported clip placement edit is available to undo.";
        return false;
    }

    const auto edit = importedClipPlacementUndoStack_.back();
    if (!project_.setImportedAudioClipStartBeats(edit.clipId, edit.beforeStartBeats, error))
        return false;

    importedClipPlacementUndoStack_.pop_back();
    importedClipPlacementRedoStack_.push_back(edit);
    return true;
}

bool AppSession::redoImportedClipPlacementEdit(std::string& error)
{
    error.clear();

    if (importedClipPlacementRedoStack_.empty())
    {
        error = "No imported clip placement edit is available to redo.";
        return false;
    }

    const auto edit = importedClipPlacementRedoStack_.back();
    if (!project_.setImportedAudioClipStartBeats(edit.clipId, edit.afterStartBeats, error))
        return false;

    importedClipPlacementRedoStack_.pop_back();
    importedClipPlacementUndoStack_.push_back(edit);
    return true;
}

bool AppSession::replaceImportedAudioClipMedia(const std::string& clipId,
                                               std::string relativePath,
                                               std::string analysisPath,
                                               double lengthBeats,
                                               std::string& error)
{
    const auto replaced = project_.replaceImportedAudioClipMedia(clipId,
                                                                std::move(relativePath),
                                                                std::move(analysisPath),
                                                                lengthBeats,
                                                                error);
    if (replaced)
        clearImportedTimelineClipCacheForClip(clipId);

    return replaced;
}

bool AppSession::shouldPlayGeneratedTone() const noexcept
{
    return generatedToneActive_;
}

const AppSession::CachedImportedTimelineClip* AppSession::findCachedImportedTimelineClip(
    const TimelinePlaybackClipPlan& clip) const noexcept
{
    for (const auto& cached : importedTimelineClipCache_)
    {
        if (cached.clipId == clip.clipId && cached.relativePath == clip.relativePath)
            return &cached;
    }

    return nullptr;
}

std::shared_ptr<const std::vector<float>> AppSession::storeCachedImportedTimelineClip(
    CachedImportedTimelineClip cache)
{
    const auto cacheBytes = preparedSampleByteCount(cache.samples);
    if (cacheBytes == 0
        || importedTimelineClipCacheLimits_.maxEntries == 0
        || cacheBytes > importedTimelineClipCacheLimits_.maxSampleBytes)
    {
        return {};
    }

    const auto matchingEntry = std::remove_if(
        importedTimelineClipCache_.begin(),
        importedTimelineClipCache_.end(),
        [&cache](const CachedImportedTimelineClip& cached)
        {
            return cached.clipId == cache.clipId && cached.relativePath == cache.relativePath;
        });
    importedTimelineClipCache_.erase(matchingEntry, importedTimelineClipCache_.end());

    importedTimelineClipCache_.push_back(std::move(cache));
    trimImportedTimelineClipCacheToLimits();

    return importedTimelineClipCache_.empty() ? nullptr : importedTimelineClipCache_.back().samples;
}

std::size_t AppSession::importedTimelineClipCacheSampleBytes() const noexcept
{
    std::size_t totalBytes = 0;
    for (const auto& entry : importedTimelineClipCache_)
    {
        const auto entryBytes = preparedSampleByteCount(entry.samples);
        if (entryBytes > std::numeric_limits<std::size_t>::max() - totalBytes)
            return std::numeric_limits<std::size_t>::max();

        totalBytes += entryBytes;
    }

    return totalBytes;
}

void AppSession::trimImportedTimelineClipCacheToLimits()
{
    if (importedTimelineClipCacheLimits_.maxEntries == 0
        || importedTimelineClipCacheLimits_.maxSampleBytes == 0)
    {
        importedTimelineClipCache_.clear();
        return;
    }

    while (!importedTimelineClipCache_.empty()
           && (importedTimelineClipCache_.size() > importedTimelineClipCacheLimits_.maxEntries
               || importedTimelineClipCacheSampleBytes() > importedTimelineClipCacheLimits_.maxSampleBytes))
    {
        importedTimelineClipCache_.erase(importedTimelineClipCache_.begin());
    }
}

void AppSession::clearImportedTimelineClipCacheForClip(const std::string& clipId) noexcept
{
    const auto matchingEntry = std::remove_if(
        importedTimelineClipCache_.begin(),
        importedTimelineClipCache_.end(),
        [&clipId](const CachedImportedTimelineClip& cached)
        {
            return cached.clipId == clipId;
        });
    importedTimelineClipCache_.erase(matchingEntry, importedTimelineClipCache_.end());
}

void AppSession::clearImportedClipPlacementEditHistory() noexcept
{
    importedClipPlacementUndoStack_.clear();
    importedClipPlacementRedoStack_.clear();
}
} // namespace projectname
