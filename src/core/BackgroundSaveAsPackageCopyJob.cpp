// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BackgroundSaveAsPackageCopyJob.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] int phaseToInt(BackgroundSaveAsPackageCopyPhase phase) noexcept
{
    return static_cast<int>(phase);
}

[[nodiscard]] BackgroundSaveAsPackageCopyPhase phaseFromInt(int value) noexcept
{
    switch (static_cast<BackgroundSaveAsPackageCopyPhase>(value))
    {
        case BackgroundSaveAsPackageCopyPhase::pending:
        case BackgroundSaveAsPackageCopyPhase::planning:
        case BackgroundSaveAsPackageCopyPhase::preflight:
        case BackgroundSaveAsPackageCopyPhase::copying:
        case BackgroundSaveAsPackageCopyPhase::completed:
        case BackgroundSaveAsPackageCopyPhase::failed:
        case BackgroundSaveAsPackageCopyPhase::cancelled:
            return static_cast<BackgroundSaveAsPackageCopyPhase>(value);
    }

    return BackgroundSaveAsPackageCopyPhase::pending;
}

[[nodiscard]] BackgroundSaveAsPackageCopyPhase toBackgroundPhase(
    ProjectPackageSaveAsCopyProgressStage stage) noexcept
{
    switch (stage)
    {
        case ProjectPackageSaveAsCopyProgressStage::planning:
            return BackgroundSaveAsPackageCopyPhase::planning;

        case ProjectPackageSaveAsCopyProgressStage::preflight:
            return BackgroundSaveAsPackageCopyPhase::preflight;

        case ProjectPackageSaveAsCopyProgressStage::copying:
            return BackgroundSaveAsPackageCopyPhase::copying;

        case ProjectPackageSaveAsCopyProgressStage::completed:
            return BackgroundSaveAsPackageCopyPhase::completed;

        case ProjectPackageSaveAsCopyProgressStage::failed:
            return BackgroundSaveAsPackageCopyPhase::failed;

        case ProjectPackageSaveAsCopyProgressStage::cancelled:
            return BackgroundSaveAsPackageCopyPhase::cancelled;
    }

    return BackgroundSaveAsPackageCopyPhase::pending;
}

void storeProgress(const std::shared_ptr<std::atomic_int>& phase,
                   const std::shared_ptr<std::atomic_int>& progressPercent,
                   BackgroundSaveAsPackageCopyPhase nextPhase,
                   int nextPercent) noexcept
{
    progressPercent->store(std::clamp(nextPercent, 0, 100), std::memory_order_release);
    phase->store(phaseToInt(nextPhase), std::memory_order_release);
}

void storeCopyCounts(const std::shared_ptr<std::atomic<std::size_t>>& filesCopied,
                     const std::shared_ptr<std::atomic<std::size_t>>& filesTotal,
                     const std::shared_ptr<std::atomic<std::uintmax_t>>& bytesCopied,
                     const std::shared_ptr<std::atomic<std::uintmax_t>>& bytesTotal,
                     const ProjectPackageSaveAsCopyProgress& progress) noexcept
{
    filesCopied->store(progress.filesCopied, std::memory_order_release);
    filesTotal->store(progress.filesTotal, std::memory_order_release);
    bytesCopied->store(progress.bytesCopied, std::memory_order_release);
    bytesTotal->store(progress.bytesTotal, std::memory_order_release);
}

[[nodiscard]] bool isSuccessfulCopyStatus(ProjectPackageSaveAsCopyStatus status) noexcept
{
    return status == ProjectPackageSaveAsCopyStatus::completed
        || status == ProjectPackageSaveAsCopyStatus::noCopyNeeded;
}
} // namespace

BackgroundSaveAsPackageCopyJob::BackgroundSaveAsPackageCopyJob(
    BackgroundSaveAsPackageCopyRequest request)
    : request_(std::move(request)),
      cancelRequested_(std::make_shared<std::atomic_bool>(false)),
      phase_(std::make_shared<std::atomic_int>(phaseToInt(BackgroundSaveAsPackageCopyPhase::pending))),
      progressPercent_(std::make_shared<std::atomic_int>(0)),
      filesCopied_(std::make_shared<std::atomic<std::size_t>>(0)),
      filesTotal_(std::make_shared<std::atomic<std::size_t>>(0)),
      bytesCopied_(std::make_shared<std::atomic<std::uintmax_t>>(0)),
      bytesTotal_(std::make_shared<std::atomic<std::uintmax_t>>(0))
{
}

BackgroundSaveAsPackageCopyJob::~BackgroundSaveAsPackageCopyJob()
{
    requestCancel();
    if (future_.valid())
        future_.wait();
}

void BackgroundSaveAsPackageCopyJob::start()
{
    if (started_)
        return;

    started_ = true;
    if (cancelRequested_->load(std::memory_order_acquire))
        storeProgress(phase_, progressPercent_, BackgroundSaveAsPackageCopyPhase::cancelled, 0);
    else
        storeProgress(phase_, progressPercent_, BackgroundSaveAsPackageCopyPhase::planning, 5);

    future_ = std::async(std::launch::async,
                         &BackgroundSaveAsPackageCopyJob::run,
                         std::move(request_),
                         cancelRequested_,
                         phase_,
                         progressPercent_,
                         filesCopied_,
                         filesTotal_,
                         bytesCopied_,
                         bytesTotal_);
}

void BackgroundSaveAsPackageCopyJob::requestCancel() noexcept
{
    cancelRequested_->store(true, std::memory_order_release);

    if (!started_)
        storeProgress(phase_, progressPercent_, BackgroundSaveAsPackageCopyPhase::cancelled, 0);
}

bool BackgroundSaveAsPackageCopyJob::isReady() const
{
    if (!future_.valid())
        return false;

    return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool BackgroundSaveAsPackageCopyJob::hasStarted() const noexcept
{
    return started_;
}

BackgroundSaveAsPackageCopyProgress BackgroundSaveAsPackageCopyJob::getProgress() const noexcept
{
    BackgroundSaveAsPackageCopyProgress progress;
    progress.phase = phaseFromInt(phase_->load(std::memory_order_acquire));
    progress.percent = std::clamp(progressPercent_->load(std::memory_order_acquire), 0, 100);
    progress.filesCopied = filesCopied_->load(std::memory_order_acquire);
    progress.filesTotal = filesTotal_->load(std::memory_order_acquire);
    progress.bytesCopied = bytesCopied_->load(std::memory_order_acquire);
    progress.bytesTotal = bytesTotal_->load(std::memory_order_acquire);
    progress.cancelRequested = cancelRequested_->load(std::memory_order_acquire);
    return progress;
}

BackgroundSaveAsPackageCopyResult BackgroundSaveAsPackageCopyJob::waitForResult()
{
    if (!started_)
        start();

    return future_.get();
}

BackgroundSaveAsPackageCopyResult BackgroundSaveAsPackageCopyJob::run(
    BackgroundSaveAsPackageCopyRequest request,
    std::shared_ptr<std::atomic_bool> cancelRequested,
    std::shared_ptr<std::atomic_int> phase,
    std::shared_ptr<std::atomic_int> progressPercent,
    std::shared_ptr<std::atomic<std::size_t>> filesCopied,
    std::shared_ptr<std::atomic<std::size_t>> filesTotal,
    std::shared_ptr<std::atomic<std::uintmax_t>> bytesCopied,
    std::shared_ptr<std::atomic<std::uintmax_t>> bytesTotal)
{
    BackgroundSaveAsPackageCopyResult result;
    result.sourcePackageDirectory = request.sourcePackageDirectory;
    result.targetPackageDirectory = request.targetPackageDirectory;

    ProjectPackageSaveAsCopyRequest copyRequest;
    copyRequest.project = std::move(request.project);
    copyRequest.sourcePackageDirectory = std::move(request.sourcePackageDirectory);
    copyRequest.targetPackageDirectory = std::move(request.targetPackageDirectory);
    copyRequest.cancelRequested = cancelRequested.get();
    copyRequest.progressCallback =
        [phase, progressPercent, filesCopied, filesTotal, bytesCopied, bytesTotal](
            const ProjectPackageSaveAsCopyProgress& progress)
        {
            storeCopyCounts(filesCopied, filesTotal, bytesCopied, bytesTotal, progress);
            storeProgress(phase,
                          progressPercent,
                          toBackgroundPhase(progress.stage),
                          progress.percent);
        };

    result.copy = copyProjectPackageAssetsForSaveAs(std::move(copyRequest));
    result.error = result.copy.error;
    result.cancelled = result.copy.status == ProjectPackageSaveAsCopyStatus::cancelled;

    if (isSuccessfulCopyStatus(result.copy.status))
    {
        storeProgress(phase, progressPercent, BackgroundSaveAsPackageCopyPhase::completed, 100);
    }
    else if (result.cancelled)
    {
        storeProgress(phase,
                      progressPercent,
                      BackgroundSaveAsPackageCopyPhase::cancelled,
                      progressPercent->load(std::memory_order_acquire));
    }
    else
    {
        storeProgress(phase, progressPercent, BackgroundSaveAsPackageCopyPhase::failed, 100);
    }

    return result;
}
} // namespace projectname
