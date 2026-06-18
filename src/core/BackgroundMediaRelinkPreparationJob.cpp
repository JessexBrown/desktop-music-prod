// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BackgroundMediaRelinkPreparationJob.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] int phaseToInt(BackgroundMediaRelinkPreparationPhase phase) noexcept
{
    return static_cast<int>(phase);
}

[[nodiscard]] BackgroundMediaRelinkPreparationPhase phaseFromInt(int value) noexcept
{
    switch (static_cast<BackgroundMediaRelinkPreparationPhase>(value))
    {
        case BackgroundMediaRelinkPreparationPhase::pending:
        case BackgroundMediaRelinkPreparationPhase::decoding:
        case BackgroundMediaRelinkPreparationPhase::copying:
        case BackgroundMediaRelinkPreparationPhase::analysing:
        case BackgroundMediaRelinkPreparationPhase::completed:
        case BackgroundMediaRelinkPreparationPhase::failed:
        case BackgroundMediaRelinkPreparationPhase::cancelled:
            return static_cast<BackgroundMediaRelinkPreparationPhase>(value);
    }

    return BackgroundMediaRelinkPreparationPhase::pending;
}

void storeProgress(const std::shared_ptr<std::atomic_int>& phase,
                   const std::shared_ptr<std::atomic_int>& progressPercent,
                   BackgroundMediaRelinkPreparationPhase nextPhase,
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

void storeBytes(const std::shared_ptr<std::atomic<std::uintmax_t>>& bytesProcessed,
                const std::shared_ptr<std::atomic<std::uintmax_t>>& bytesTotal,
                std::uintmax_t nextBytesProcessed,
                std::uintmax_t nextBytesTotal) noexcept
{
    bytesProcessed->store(nextBytesProcessed, std::memory_order_release);
    bytesTotal->store(nextBytesTotal, std::memory_order_release);
}

[[nodiscard]] BackgroundMediaRelinkPreparationPhase phaseForRelinkStage(
    ImportedClipMediaRelinkPreparationStage stage) noexcept
{
    switch (stage)
    {
        case ImportedClipMediaRelinkPreparationStage::preparing:
        case ImportedClipMediaRelinkPreparationStage::decoding:
            return BackgroundMediaRelinkPreparationPhase::decoding;

        case ImportedClipMediaRelinkPreparationStage::copying:
            return BackgroundMediaRelinkPreparationPhase::copying;

        case ImportedClipMediaRelinkPreparationStage::analysing:
            return BackgroundMediaRelinkPreparationPhase::analysing;

        case ImportedClipMediaRelinkPreparationStage::prepared:
            return BackgroundMediaRelinkPreparationPhase::completed;
    }

    return BackgroundMediaRelinkPreparationPhase::pending;
}

[[nodiscard]] int percentForRelinkProgress(
    const ImportedClipMediaRelinkProgress& progress) noexcept
{
    switch (progress.stage)
    {
        case ImportedClipMediaRelinkPreparationStage::preparing:
        case ImportedClipMediaRelinkPreparationStage::decoding:
            return 5;

        case ImportedClipMediaRelinkPreparationStage::copying:
            return 55 + ((std::clamp(progress.percent, 0, 100) * 30) / 100);

        case ImportedClipMediaRelinkPreparationStage::analysing:
            return 90;

        case ImportedClipMediaRelinkPreparationStage::prepared:
            return 100;
    }

    return 0;
}
} // namespace

BackgroundMediaRelinkPreparationJob::BackgroundMediaRelinkPreparationJob(
    BackgroundMediaRelinkPreparationRequest request)
    : request_(std::move(request)),
      cancelRequested_(std::make_shared<std::atomic_bool>(false)),
      phase_(std::make_shared<std::atomic_int>(phaseToInt(BackgroundMediaRelinkPreparationPhase::pending))),
      progressPercent_(std::make_shared<std::atomic_int>(0)),
      framesProcessed_(std::make_shared<std::atomic<std::uintmax_t>>(0)),
      framesTotal_(std::make_shared<std::atomic<std::uintmax_t>>(0)),
      bytesProcessed_(std::make_shared<std::atomic<std::uintmax_t>>(0)),
      bytesTotal_(std::make_shared<std::atomic<std::uintmax_t>>(0))
{
}

BackgroundMediaRelinkPreparationJob::~BackgroundMediaRelinkPreparationJob()
{
    requestCancel();
    if (future_.valid())
        future_.wait();
}

void BackgroundMediaRelinkPreparationJob::start()
{
    if (started_)
        return;

    started_ = true;
    if (cancelRequested_->load(std::memory_order_acquire))
        storeProgress(phase_, progressPercent_, BackgroundMediaRelinkPreparationPhase::cancelled, 0);
    else
        storeProgress(phase_, progressPercent_, BackgroundMediaRelinkPreparationPhase::decoding, 5);

    future_ = std::async(std::launch::async,
                         &BackgroundMediaRelinkPreparationJob::run,
                         std::move(request_),
                         cancelRequested_,
                         phase_,
                         progressPercent_,
                         framesProcessed_,
                         framesTotal_,
                         bytesProcessed_,
                         bytesTotal_);
}

void BackgroundMediaRelinkPreparationJob::requestCancel() noexcept
{
    cancelRequested_->store(true, std::memory_order_release);

    if (!started_)
        storeProgress(phase_, progressPercent_, BackgroundMediaRelinkPreparationPhase::cancelled, 0);
}

bool BackgroundMediaRelinkPreparationJob::isReady() const
{
    if (!future_.valid())
        return false;

    return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool BackgroundMediaRelinkPreparationJob::hasStarted() const noexcept
{
    return started_;
}

BackgroundMediaRelinkPreparationProgress BackgroundMediaRelinkPreparationJob::getProgress() const noexcept
{
    BackgroundMediaRelinkPreparationProgress progress;
    progress.phase = phaseFromInt(phase_->load(std::memory_order_acquire));
    progress.percent = std::clamp(progressPercent_->load(std::memory_order_acquire), 0, 100);
    progress.framesProcessed = framesProcessed_->load(std::memory_order_acquire);
    progress.framesTotal = framesTotal_->load(std::memory_order_acquire);
    progress.bytesProcessed = bytesProcessed_->load(std::memory_order_acquire);
    progress.bytesTotal = bytesTotal_->load(std::memory_order_acquire);
    progress.cancelRequested = cancelRequested_->load(std::memory_order_acquire);
    return progress;
}

BackgroundMediaRelinkPreparationResult BackgroundMediaRelinkPreparationJob::waitForResult()
{
    if (!started_)
        start();

    return future_.get();
}

BackgroundMediaRelinkPreparationResult BackgroundMediaRelinkPreparationJob::run(
    BackgroundMediaRelinkPreparationRequest request,
    std::shared_ptr<std::atomic_bool> cancelRequested,
    std::shared_ptr<std::atomic_int> phase,
    std::shared_ptr<std::atomic_int> progressPercent,
    std::shared_ptr<std::atomic<std::uintmax_t>> framesProcessed,
    std::shared_ptr<std::atomic<std::uintmax_t>> framesTotal,
    std::shared_ptr<std::atomic<std::uintmax_t>> bytesProcessed,
    std::shared_ptr<std::atomic<std::uintmax_t>> bytesTotal)
{
    BackgroundMediaRelinkPreparationResult result;
    storeFrames(framesProcessed, framesTotal, 0U, 0U);
    storeBytes(bytesProcessed, bytesTotal, 0U, 0U);

    if (cancelRequested->load(std::memory_order_acquire))
    {
        result.status = ImportedClipMediaRelinkPreparationStatus::cancelled;
        result.cancelled = true;
        result.error = "Media relink was cancelled before it started.";
        storeProgress(phase, progressPercent, BackgroundMediaRelinkPreparationPhase::cancelled, 0);
        return result;
    }

    ImportedClipMediaRelinkPreparationRequest preparationRequest;
    preparationRequest.project = std::move(request.project);
    preparationRequest.packageDirectory = std::move(request.packageDirectory);
    preparationRequest.sourceWavPath = std::move(request.sourceWavPath);
    preparationRequest.selectedClipId = std::move(request.selectedClipId);
    preparationRequest.cancelRequested = cancelRequested.get();
    preparationRequest.copyChunkBytes = request.copyChunkBytes;
    preparationRequest.decodeProgressCallback =
        [phase, progressPercent, framesProcessed, framesTotal](const WavDecodeProgress& progress)
        {
            storeFrames(framesProcessed, framesTotal, progress.framesDecoded, progress.totalFrames);
            storeProgress(phase,
                          progressPercent,
                          BackgroundMediaRelinkPreparationPhase::decoding,
                          5 + ((std::clamp(progress.percent, 0, 100) * 45) / 100));
        };
    preparationRequest.progressCallback =
        [phase, progressPercent, bytesProcessed, bytesTotal](
            const ImportedClipMediaRelinkProgress& progress)
        {
            if (progress.stage == ImportedClipMediaRelinkPreparationStage::copying)
                storeBytes(bytesProcessed, bytesTotal, progress.bytesCopied, progress.totalBytes);
            storeProgress(phase,
                          progressPercent,
                          phaseForRelinkStage(progress.stage),
                          percentForRelinkProgress(progress));
        };

    auto preparation = prepareImportedClipMediaRelink(std::move(preparationRequest));
    result.status = preparation.status;
    result.error = std::move(preparation.error);
    result.preparation = std::move(preparation.preparation);

    if (result.status == ImportedClipMediaRelinkPreparationStatus::prepared)
    {
        storeProgress(phase, progressPercent, BackgroundMediaRelinkPreparationPhase::completed, 100);
        return result;
    }

    if (result.status == ImportedClipMediaRelinkPreparationStatus::cancelled
        || cancelRequested->load(std::memory_order_acquire))
    {
        result.cancelled = true;
        if (result.error.empty())
            result.error = "Media relink was cancelled.";
        storeProgress(phase,
                      progressPercent,
                      BackgroundMediaRelinkPreparationPhase::cancelled,
                      progressPercent->load(std::memory_order_acquire));
        return result;
    }

    storeProgress(phase, progressPercent, BackgroundMediaRelinkPreparationPhase::failed, 100);
    return result;
}
} // namespace projectname
