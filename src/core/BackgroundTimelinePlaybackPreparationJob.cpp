// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BackgroundTimelinePlaybackPreparationJob.h"

#include "PackagePath.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] int phaseToInt(BackgroundTimelinePlaybackPreparationPhase phase) noexcept
{
    return static_cast<int>(phase);
}

[[nodiscard]] BackgroundTimelinePlaybackPreparationPhase phaseFromInt(int value) noexcept
{
    switch (static_cast<BackgroundTimelinePlaybackPreparationPhase>(value))
    {
        case BackgroundTimelinePlaybackPreparationPhase::pending:
        case BackgroundTimelinePlaybackPreparationPhase::planning:
        case BackgroundTimelinePlaybackPreparationPhase::decoding:
        case BackgroundTimelinePlaybackPreparationPhase::completed:
        case BackgroundTimelinePlaybackPreparationPhase::failed:
        case BackgroundTimelinePlaybackPreparationPhase::cancelled:
            return static_cast<BackgroundTimelinePlaybackPreparationPhase>(value);
    }

    return BackgroundTimelinePlaybackPreparationPhase::pending;
}

void storeProgress(const std::shared_ptr<std::atomic_int>& phase,
                   const std::shared_ptr<std::atomic_int>& progressPercent,
                   BackgroundTimelinePlaybackPreparationPhase nextPhase,
                   int nextPercent) noexcept
{
    progressPercent->store(std::clamp(nextPercent, 0, 100), std::memory_order_release);
    phase->store(phaseToInt(nextPhase), std::memory_order_release);
}

void storeFrames(const std::shared_ptr<std::atomic<std::uintmax_t>>& framesProcessed,
                 const std::shared_ptr<std::atomic<std::uintmax_t>>& framesTotal,
                 std::uintmax_t nextFramesProcessed,
                 std::uintmax_t nextFramesTotal) noexcept
{
    framesProcessed->store(nextFramesProcessed, std::memory_order_release);
    framesTotal->store(nextFramesTotal, std::memory_order_release);
}

void setPreparedSingleClipResult(TimelinePlaybackPreparation& preparation,
                                 const TimelinePlaybackPreparation& candidate,
                                 const BackgroundPreparedTimelineClip& preparedClip)
{
    preparation = candidate;
    preparation.status = TimelinePlaybackPreparationStatus::importedClipReady;
    preparation.clip = preparedClip.clip;
    preparation.preparedClip = preparedClip.preparedClip;
    preparation.preparedSamples = preparedClip.preparedSamples;
    preparation.audioPath = preparedClip.audioPath;
    preparation.usedCachedBuffer = false;
    preparation.message = "Prepared imported timeline clip.";
}
} // namespace

BackgroundTimelinePlaybackPreparationJob::BackgroundTimelinePlaybackPreparationJob(
    BackgroundTimelinePlaybackPreparationRequest request)
    : request_(std::move(request)),
      cancelRequested_(std::make_shared<std::atomic_bool>(false)),
      phase_(std::make_shared<std::atomic_int>(phaseToInt(BackgroundTimelinePlaybackPreparationPhase::pending))),
      progressPercent_(std::make_shared<std::atomic_int>(0)),
      framesProcessed_(std::make_shared<std::atomic<std::uintmax_t>>(0)),
      framesTotal_(std::make_shared<std::atomic<std::uintmax_t>>(0))
{
}

BackgroundTimelinePlaybackPreparationJob::~BackgroundTimelinePlaybackPreparationJob()
{
    requestCancel();
    if (future_.valid())
        future_.wait();
}

void BackgroundTimelinePlaybackPreparationJob::start()
{
    if (started_)
        return;

    started_ = true;
    if (cancelRequested_->load(std::memory_order_acquire))
        storeProgress(phase_, progressPercent_, BackgroundTimelinePlaybackPreparationPhase::cancelled, 0);
    else
        storeProgress(phase_, progressPercent_, BackgroundTimelinePlaybackPreparationPhase::planning, 5);

    future_ = std::async(std::launch::async,
                         &BackgroundTimelinePlaybackPreparationJob::run,
                         std::move(request_),
                         cancelRequested_,
                         phase_,
                         progressPercent_,
                         framesProcessed_,
                         framesTotal_);
}

void BackgroundTimelinePlaybackPreparationJob::requestCancel() noexcept
{
    cancelRequested_->store(true, std::memory_order_release);

    if (!started_)
        storeProgress(phase_, progressPercent_, BackgroundTimelinePlaybackPreparationPhase::cancelled, 0);
}

bool BackgroundTimelinePlaybackPreparationJob::isReady() const
{
    if (!future_.valid())
        return false;

    return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool BackgroundTimelinePlaybackPreparationJob::hasStarted() const noexcept
{
    return started_;
}

BackgroundTimelinePlaybackPreparationProgress BackgroundTimelinePlaybackPreparationJob::getProgress() const noexcept
{
    BackgroundTimelinePlaybackPreparationProgress progress;
    progress.phase = phaseFromInt(phase_->load(std::memory_order_acquire));
    progress.percent = std::clamp(progressPercent_->load(std::memory_order_acquire), 0, 100);
    progress.framesProcessed = framesProcessed_->load(std::memory_order_acquire);
    progress.framesTotal = framesTotal_->load(std::memory_order_acquire);
    progress.cancelRequested = cancelRequested_->load(std::memory_order_acquire);
    return progress;
}

BackgroundTimelinePlaybackPreparationResult BackgroundTimelinePlaybackPreparationJob::waitForResult()
{
    if (!started_)
        start();

    return future_.get();
}

BackgroundTimelinePlaybackPreparationResult BackgroundTimelinePlaybackPreparationJob::run(
    BackgroundTimelinePlaybackPreparationRequest request,
    std::shared_ptr<std::atomic_bool> cancelRequested,
    std::shared_ptr<std::atomic_int> phase,
    std::shared_ptr<std::atomic_int> progressPercent,
    std::shared_ptr<std::atomic<std::uintmax_t>> framesProcessed,
    std::shared_ptr<std::atomic<std::uintmax_t>> framesTotal)
{
    BackgroundTimelinePlaybackPreparationResult result;
    result.minimumRenderFrameCount = request.minimumRenderFrameCount;
    storeFrames(framesProcessed, framesTotal, 0U, 0U);

    if (cancelRequested->load(std::memory_order_acquire))
    {
        result.cancelled = true;
        result.error = "Timeline playback preparation was cancelled before it started.";
        storeProgress(phase, progressPercent, BackgroundTimelinePlaybackPreparationPhase::cancelled, 0);
        return result;
    }

    AppSession session(std::move(request.project));
    std::string voiceError;
    result.voicePreparation = session.playCachedTimelineVoiceWindow(request.outputSampleRateHz,
                                                                    request.minimumRenderFrameCount,
                                                                    voiceError);

    if (result.voicePreparation.status == TimelineVoicePlaybackPreparationStatus::generatedToneFallback)
    {
        result.preparation.status = TimelinePlaybackPreparationStatus::generatedToneFallback;
        result.preparation.message = result.voicePreparation.message;
        result.error = voiceError.empty() ? result.voicePreparation.message : std::move(voiceError);
        storeProgress(phase, progressPercent, BackgroundTimelinePlaybackPreparationPhase::failed, 100);
        return result;
    }

    if (result.voicePreparation.status == TimelineVoicePlaybackPreparationStatus::voiceWindowReady)
    {
        storeProgress(phase, progressPercent, BackgroundTimelinePlaybackPreparationPhase::completed, 100);
        return result;
    }

    auto singlePreparation = session.playFromCachedTimeline(request.outputSampleRateHz, result.error);
    if (singlePreparation.status == TimelinePlaybackPreparationStatus::generatedToneFallback)
    {
        result.preparation = std::move(singlePreparation);
        storeProgress(phase, progressPercent, BackgroundTimelinePlaybackPreparationPhase::failed, 100);
        return result;
    }

    const auto missingClipCount = std::max<std::size_t>(result.voicePreparation.missingClips.size(), 1U);
    for (std::size_t missingIndex = 0; missingIndex < result.voicePreparation.missingClips.size(); ++missingIndex)
    {
        if (cancelRequested->load(std::memory_order_acquire))
        {
            result.cancelled = true;
            result.error = "Timeline playback preparation was cancelled.";
            storeProgress(phase,
                          progressPercent,
                          BackgroundTimelinePlaybackPreparationPhase::cancelled,
                          progressPercent->load(std::memory_order_acquire));
            return result;
        }

        const auto& clip = result.voicePreparation.missingClips[missingIndex];
        const auto relativePath = std::filesystem::path(clip.relativePath);
        if (!isSafePackageRelativePath(relativePath))
        {
            result.preparation.status = TimelinePlaybackPreparationStatus::generatedToneFallback;
            result.error = "Imported audio clip path is not package-relative.";
            result.preparation.message = result.error;
            storeProgress(phase, progressPercent, BackgroundTimelinePlaybackPreparationPhase::failed, 100);
            return result;
        }

        WavDecodeOptions decodeOptions;
        decodeOptions.cancelRequested = cancelRequested.get();
        decodeOptions.progressCallback =
            [missingIndex,
             missingClipCount,
             phase,
             progressPercent,
             framesProcessed,
             framesTotal](const WavDecodeProgress& progress)
            {
                storeFrames(framesProcessed, framesTotal, progress.framesDecoded, progress.totalFrames);
                const auto boundedPercent = std::clamp(progress.percent, 0, 100);
                const auto aggregatePercent =
                    ((static_cast<int>(missingIndex) * 100) + boundedPercent)
                    / static_cast<int>(missingClipCount);
                storeProgress(phase,
                              progressPercent,
                              BackgroundTimelinePlaybackPreparationPhase::decoding,
                              10 + ((std::clamp(aggregatePercent, 0, 100) * 85) / 100));
            };

        auto audioPath = resolvePackagePath(request.packageDirectory, relativePath);
        auto preparedClip = loadPcm16WavAsPreparedMonoClip(audioPath, decodeOptions, result.error);
        if (!preparedClip.has_value())
        {
            const auto detail = result.error.empty() ? std::string("unknown error") : result.error;
            result.preparation.status = TimelinePlaybackPreparationStatus::generatedToneFallback;
            result.error = "Could not prepare imported timeline clip: " + detail;
            result.preparation.message = result.error;
            result.preparedClips.clear();
            storeProgress(phase, progressPercent, BackgroundTimelinePlaybackPreparationPhase::failed, 100);
            return result;
        }

        BackgroundPreparedTimelineClip preparedTimelineClip;
        preparedTimelineClip.clip = clip;
        preparedTimelineClip.audioPath = std::move(audioPath);
        preparedTimelineClip.preparedSamples =
            std::make_shared<const std::vector<float>>(std::move(preparedClip->samples));
        preparedClip->samples.clear();
        preparedTimelineClip.preparedClip = std::move(*preparedClip);

        if (singlePreparation.clip.has_value()
            && singlePreparation.clip->clipId == preparedTimelineClip.clip.clipId)
        {
            setPreparedSingleClipResult(result.preparation,
                                        singlePreparation,
                                        preparedTimelineClip);
        }

        result.preparedClips.push_back(std::move(preparedTimelineClip));
    }

    if (cancelRequested->load(std::memory_order_acquire))
    {
        result.cancelled = true;
        result.error = "Timeline playback preparation was cancelled.";
        storeProgress(phase,
                      progressPercent,
                      BackgroundTimelinePlaybackPreparationPhase::cancelled,
                      progressPercent->load(std::memory_order_acquire));
        return result;
    }

    if (result.preparation.status != TimelinePlaybackPreparationStatus::importedClipReady
        && !result.preparedClips.empty())
    {
        setPreparedSingleClipResult(result.preparation,
                                    singlePreparation,
                                    result.preparedClips.front());
    }

    if (!result.preparedClips.empty())
    {
        storeProgress(phase, progressPercent, BackgroundTimelinePlaybackPreparationPhase::completed, 100);
        return result;
    }

    if (result.error.empty())
        result.error = result.voicePreparation.message.empty()
            ? "Timeline playback preparation did not produce any prepared clips."
            : result.voicePreparation.message;

    result.preparation.status = TimelinePlaybackPreparationStatus::generatedToneFallback;
    result.preparation.message = result.error;
    storeProgress(phase, progressPercent, BackgroundTimelinePlaybackPreparationPhase::failed, 100);
    return result;
}
} // namespace projectname
