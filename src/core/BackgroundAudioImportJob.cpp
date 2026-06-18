// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BackgroundAudioImportJob.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] int phaseToInt(BackgroundAudioImportPhase phase) noexcept
{
    return static_cast<int>(phase);
}

[[nodiscard]] BackgroundAudioImportPhase phaseFromInt(int value) noexcept
{
    switch (static_cast<BackgroundAudioImportPhase>(value))
    {
        case BackgroundAudioImportPhase::pending:
        case BackgroundAudioImportPhase::decoding:
        case BackgroundAudioImportPhase::readyToCommit:
        case BackgroundAudioImportPhase::copying:
        case BackgroundAudioImportPhase::committing:
        case BackgroundAudioImportPhase::completed:
        case BackgroundAudioImportPhase::failed:
        case BackgroundAudioImportPhase::cancelled:
            return static_cast<BackgroundAudioImportPhase>(value);
    }

    return BackgroundAudioImportPhase::pending;
}

void storeProgress(const std::shared_ptr<std::atomic_int>& phase,
                   const std::shared_ptr<std::atomic_int>& progressPercent,
                   BackgroundAudioImportPhase nextPhase,
                   int nextPercent) noexcept
{
    progressPercent->store(std::clamp(nextPercent, 0, 100), std::memory_order_release);
    phase->store(phaseToInt(nextPhase), std::memory_order_release);
}

void storeBytes(const std::shared_ptr<std::atomic<std::uintmax_t>>& bytesProcessed,
                const std::shared_ptr<std::atomic<std::uintmax_t>>& bytesTotal,
                std::uintmax_t nextBytesProcessed,
                std::uintmax_t nextBytesTotal) noexcept
{
    bytesProcessed->store(nextBytesProcessed, std::memory_order_release);
    bytesTotal->store(nextBytesTotal, std::memory_order_release);
}

void storeFrames(const std::shared_ptr<std::atomic<std::uintmax_t>>& framesProcessed,
                 const std::shared_ptr<std::atomic<std::uintmax_t>>& framesTotal,
                 std::uintmax_t nextFramesProcessed,
                 std::uintmax_t nextFramesTotal) noexcept
{
    framesProcessed->store(nextFramesProcessed, std::memory_order_release);
    framesTotal->store(nextFramesTotal, std::memory_order_release);
}
} // namespace

BackgroundAudioImportJob::BackgroundAudioImportJob(BackgroundAudioImportRequest request)
    : request_(std::move(request)),
      cancelRequested_(std::make_shared<std::atomic_bool>(false)),
      phase_(std::make_shared<std::atomic_int>(phaseToInt(BackgroundAudioImportPhase::pending))),
      progressPercent_(std::make_shared<std::atomic_int>(0)),
      framesProcessed_(std::make_shared<std::atomic<std::uintmax_t>>(0)),
      framesTotal_(std::make_shared<std::atomic<std::uintmax_t>>(0)),
      bytesProcessed_(std::make_shared<std::atomic<std::uintmax_t>>(0)),
      bytesTotal_(std::make_shared<std::atomic<std::uintmax_t>>(0))
{
}

BackgroundAudioImportJob::~BackgroundAudioImportJob()
{
    requestCancel();
    if (future_.valid())
        future_.wait();
}

void BackgroundAudioImportJob::start()
{
    if (started_)
        return;

    started_ = true;
    if (cancelRequested_->load(std::memory_order_acquire))
        storeProgress(phase_, progressPercent_, BackgroundAudioImportPhase::cancelled, 0);
    else
        storeProgress(phase_, progressPercent_, BackgroundAudioImportPhase::decoding, 10);
    future_ = std::async(std::launch::async,
                         &BackgroundAudioImportJob::run,
                         std::move(request_),
                         cancelRequested_,
                         phase_,
                         progressPercent_,
                         framesProcessed_,
                         framesTotal_,
                         bytesProcessed_,
                         bytesTotal_);
}

void BackgroundAudioImportJob::requestCancel() noexcept
{
    cancelRequested_->store(true, std::memory_order_release);

    if (!started_)
        storeProgress(phase_, progressPercent_, BackgroundAudioImportPhase::cancelled, 0);
}

bool BackgroundAudioImportJob::isReady() const
{
    if (!future_.valid())
        return false;

    return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool BackgroundAudioImportJob::hasStarted() const noexcept
{
    return started_;
}

BackgroundAudioImportProgress BackgroundAudioImportJob::getProgress() const noexcept
{
    BackgroundAudioImportProgress progress;
    progress.phase = phaseFromInt(phase_->load(std::memory_order_acquire));
    progress.percent = std::clamp(progressPercent_->load(std::memory_order_acquire), 0, 100);
    progress.framesProcessed = framesProcessed_->load(std::memory_order_acquire);
    progress.framesTotal = framesTotal_->load(std::memory_order_acquire);
    progress.bytesProcessed = bytesProcessed_->load(std::memory_order_acquire);
    progress.bytesTotal = bytesTotal_->load(std::memory_order_acquire);
    progress.cancelRequested = cancelRequested_->load(std::memory_order_acquire);
    return progress;
}

BackgroundAudioImportResult BackgroundAudioImportJob::waitForResult()
{
    if (!started_)
        start();

    return future_.get();
}

BackgroundAudioImportResult BackgroundAudioImportJob::run(BackgroundAudioImportRequest request,
                                                          std::shared_ptr<std::atomic_bool> cancelRequested,
                                                          std::shared_ptr<std::atomic_int> phase,
                                                          std::shared_ptr<std::atomic_int> progressPercent,
                                                          std::shared_ptr<std::atomic<std::uintmax_t>> framesProcessed,
                                                          std::shared_ptr<std::atomic<std::uintmax_t>> framesTotal,
                                                          std::shared_ptr<std::atomic<std::uintmax_t>> bytesProcessed,
                                                          std::shared_ptr<std::atomic<std::uintmax_t>> bytesTotal)
{
    BackgroundAudioImportResult result;
    result.project = std::move(request.project);
    storeFrames(framesProcessed, framesTotal, 0U, 0U);
    storeBytes(bytesProcessed, bytesTotal, 0U, 0U);

    if (cancelRequested->load(std::memory_order_acquire))
    {
        result.cancelled = true;
        result.error = "Audio import was cancelled before it started.";
        storeProgress(phase, progressPercent, BackgroundAudioImportPhase::cancelled, 0);
        return result;
    }

    std::string error;
    WavDecodeOptions decodeOptions;
    decodeOptions.cancelRequested = cancelRequested.get();
    decodeOptions.progressCallback =
        [phase, progressPercent, framesProcessed, framesTotal](const WavDecodeProgress& progress)
        {
            storeFrames(framesProcessed, framesTotal, progress.framesDecoded, progress.totalFrames);
            storeProgress(phase,
                          progressPercent,
                          BackgroundAudioImportPhase::decoding,
                          10 + ((std::clamp(progress.percent, 0, 100) * 45) / 100));
        };

    auto preparedClip = loadPcm16WavAsPreparedMonoClip(request.sourceWavPath, decodeOptions, error);
    if (!preparedClip.has_value())
    {
        result.error = std::move(error);
        if (cancelRequested->load(std::memory_order_acquire))
        {
            result.cancelled = true;
            storeProgress(phase, progressPercent, BackgroundAudioImportPhase::cancelled, progressPercent->load(std::memory_order_acquire));
        }
        else
        {
            storeProgress(phase, progressPercent, BackgroundAudioImportPhase::failed, 100);
        }
        return result;
    }

    storeProgress(phase, progressPercent, BackgroundAudioImportPhase::readyToCommit, 60);
    if (cancelRequested->load(std::memory_order_acquire))
    {
        result.cancelled = true;
        result.error = "Audio import was cancelled before project package mutation.";
        storeProgress(phase, progressPercent, BackgroundAudioImportPhase::cancelled, 60);
        return result;
    }

    ProjectAudioImportOptions options;
    options.cancelRequested = cancelRequested.get();
    options.requestedStartBeats = request.requestedStartBeats;
    options.progressCallback =
        [phase, progressPercent, bytesProcessed, bytesTotal](const ProjectAudioImportProgress& progress)
        {
            storeBytes(bytesProcessed, bytesTotal, progress.bytesCopied, progress.totalBytes);

            if (progress.stage == ProjectAudioImportStage::copying)
            {
                storeProgress(phase,
                              progressPercent,
                              BackgroundAudioImportPhase::copying,
                              60 + ((std::clamp(progress.percent, 0, 100) * 25) / 100));
                return;
            }

            if (progress.stage == ProjectAudioImportStage::committing)
            {
                storeProgress(phase, progressPercent, BackgroundAudioImportPhase::committing, 90);
                return;
            }

            if (progress.stage == ProjectAudioImportStage::completed)
                storeProgress(phase, progressPercent, BackgroundAudioImportPhase::completed, 100);
        };

    storeProgress(phase, progressPercent, BackgroundAudioImportPhase::committing, 75);
    auto import = commitPreparedAudioImportToProjectPackage(result.project,
                                                            request.packageDirectory,
                                                            request.sourceWavPath,
                                                            std::move(*preparedClip),
                                                            options,
                                                            error);
    if (!import.has_value())
    {
        result.error = std::move(error);
        if (cancelRequested->load(std::memory_order_acquire))
        {
            result.cancelled = true;
            storeProgress(phase, progressPercent, BackgroundAudioImportPhase::cancelled, progressPercent->load(std::memory_order_acquire));
        }
        else
        {
            storeProgress(phase, progressPercent, BackgroundAudioImportPhase::failed, 100);
        }
        return result;
    }

    result.import = std::move(*import);
    storeProgress(phase, progressPercent, BackgroundAudioImportPhase::completed, 100);
    return result;
}
} // namespace projectname
