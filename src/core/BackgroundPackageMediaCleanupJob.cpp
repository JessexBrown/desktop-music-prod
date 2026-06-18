// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BackgroundPackageMediaCleanupJob.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] int phaseToInt(BackgroundPackageMediaCleanupPhase phase) noexcept
{
    return static_cast<int>(phase);
}

[[nodiscard]] BackgroundPackageMediaCleanupPhase phaseFromInt(int value) noexcept
{
    switch (static_cast<BackgroundPackageMediaCleanupPhase>(value))
    {
        case BackgroundPackageMediaCleanupPhase::pending:
        case BackgroundPackageMediaCleanupPhase::inventory:
        case BackgroundPackageMediaCleanupPhase::preflight:
        case BackgroundPackageMediaCleanupPhase::quarantining:
        case BackgroundPackageMediaCleanupPhase::restoring:
        case BackgroundPackageMediaCleanupPhase::completed:
        case BackgroundPackageMediaCleanupPhase::failed:
        case BackgroundPackageMediaCleanupPhase::cancelled:
            return static_cast<BackgroundPackageMediaCleanupPhase>(value);
    }

    return BackgroundPackageMediaCleanupPhase::pending;
}

void storeProgress(const std::shared_ptr<std::atomic_int>& phase,
                   const std::shared_ptr<std::atomic_int>& progressPercent,
                   BackgroundPackageMediaCleanupPhase nextPhase,
                   int nextPercent) noexcept
{
    progressPercent->store(std::clamp(nextPercent, 0, 100), std::memory_order_release);
    phase->store(phaseToInt(nextPhase), std::memory_order_release);
}

[[nodiscard]] bool isCancelled(const std::shared_ptr<std::atomic_bool>& cancelRequested) noexcept
{
    return cancelRequested->load(std::memory_order_acquire);
}

[[nodiscard]] BackgroundPackageMediaCleanupResult makeCancelledResult(
    BackgroundPackageMediaCleanupRequest&& request,
    std::string error)
{
    BackgroundPackageMediaCleanupResult result;
    result.operation = request.operation;
    result.cancelled = true;
    result.error = std::move(error);
    return result;
}

[[nodiscard]] std::string preflightFailureMessage(
    const PackageMediaQuarantinePreflightResult& preflight)
{
    if (!preflight.error.empty())
        return preflight.error;

    return "Package media cleanup preflight failed.";
}

[[nodiscard]] std::string quarantineFailureMessage(
    const PackageMediaQuarantineCommandResult& quarantine)
{
    if (!quarantine.error.empty())
        return quarantine.error;

    return "Package media quarantine command failed.";
}

[[nodiscard]] std::string restoreFailureMessage(
    const PackageMediaQuarantineRestoreCommandResult& restore)
{
    if (!restore.error.empty())
        return restore.error;

    return "Package media restore command failed.";
}
} // namespace

BackgroundPackageMediaCleanupJob::BackgroundPackageMediaCleanupJob(
    BackgroundPackageMediaCleanupRequest request)
    : request_(std::move(request)),
      cancelRequested_(std::make_shared<std::atomic_bool>(false)),
      phase_(std::make_shared<std::atomic_int>(phaseToInt(BackgroundPackageMediaCleanupPhase::pending))),
      progressPercent_(std::make_shared<std::atomic_int>(0))
{
}

BackgroundPackageMediaCleanupJob::~BackgroundPackageMediaCleanupJob()
{
    requestCancel();
    if (future_.valid())
        future_.wait();
}

void BackgroundPackageMediaCleanupJob::start()
{
    if (started_)
        return;

    started_ = true;
    if (cancelRequested_->load(std::memory_order_acquire))
        storeProgress(phase_, progressPercent_, BackgroundPackageMediaCleanupPhase::cancelled, 0);
    else if (request_.operation == BackgroundPackageMediaCleanupOperation::quarantine)
        storeProgress(phase_, progressPercent_, BackgroundPackageMediaCleanupPhase::inventory, 5);
    else
        storeProgress(phase_, progressPercent_, BackgroundPackageMediaCleanupPhase::restoring, 10);

    future_ = std::async(std::launch::async,
                         &BackgroundPackageMediaCleanupJob::run,
                         std::move(request_),
                         cancelRequested_,
                         phase_,
                         progressPercent_);
}

void BackgroundPackageMediaCleanupJob::requestCancel() noexcept
{
    cancelRequested_->store(true, std::memory_order_release);

    if (!started_)
        storeProgress(phase_, progressPercent_, BackgroundPackageMediaCleanupPhase::cancelled, 0);
}

bool BackgroundPackageMediaCleanupJob::isReady() const
{
    if (!future_.valid())
        return false;

    return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool BackgroundPackageMediaCleanupJob::hasStarted() const noexcept
{
    return started_;
}

BackgroundPackageMediaCleanupProgress BackgroundPackageMediaCleanupJob::getProgress() const noexcept
{
    BackgroundPackageMediaCleanupProgress progress;
    progress.phase = phaseFromInt(phase_->load(std::memory_order_acquire));
    progress.percent = std::clamp(progressPercent_->load(std::memory_order_acquire), 0, 100);
    progress.cancelRequested = cancelRequested_->load(std::memory_order_acquire);
    return progress;
}

BackgroundPackageMediaCleanupResult BackgroundPackageMediaCleanupJob::waitForResult()
{
    if (!started_)
        start();

    return future_.get();
}

BackgroundPackageMediaCleanupResult BackgroundPackageMediaCleanupJob::run(
    BackgroundPackageMediaCleanupRequest request,
    std::shared_ptr<std::atomic_bool> cancelRequested,
    std::shared_ptr<std::atomic_int> phase,
    std::shared_ptr<std::atomic_int> progressPercent)
{
    if (isCancelled(cancelRequested))
    {
        storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::cancelled, 0);
        return makeCancelledResult(std::move(request),
                                   "Package media cleanup was cancelled before it started.");
    }

    BackgroundPackageMediaCleanupResult result;
    result.operation = request.operation;

    if (request.operation == BackgroundPackageMediaCleanupOperation::restore)
    {
        storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::restoring, 35);
        PackageMediaQuarantineRestoreCommandRequest restoreRequest;
        restoreRequest.packageDirectory = std::move(request.packageDirectory);
        restoreRequest.restoreManifestPath = std::move(request.restoreManifestPath);
        restoreRequest.selectedOriginalRelativePaths =
            std::move(request.selectedRestoreOriginalRelativePaths);
        result.restore = restorePackageMediaFromQuarantine(std::move(restoreRequest));

        if (result.restore.status == PackageMediaQuarantineRestoreCommandStatus::restored)
        {
            storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::completed, 100);
            return result;
        }

        result.error = restoreFailureMessage(result.restore);
        storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::failed, 100);
        return result;
    }

    storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::inventory, 15);
    auto inventoryOptions = request.inventoryOptions;
    inventoryOptions.packageWorkInProgress =
        inventoryOptions.packageWorkInProgress || request.packageWorkInProgress;
    result.inventory = buildImportedMediaPackageInventory(request.packageDirectory, inventoryOptions);

    if (isCancelled(cancelRequested))
    {
        result.cancelled = true;
        result.error = "Package media cleanup was cancelled before preflight.";
        storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::cancelled, 25);
        return result;
    }

    storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::preflight, 35);
    PackageMediaQuarantinePreflightRequest preflightRequest;
    preflightRequest.inventory = result.inventory;
    preflightRequest.cleanupId = std::move(request.cleanupId);
    preflightRequest.createdAtUtc = std::move(request.createdAtUtc);
    preflightRequest.packageDisplayPath = std::move(request.packageDisplayPath);
    preflightRequest.manifestMarker = std::move(request.manifestMarker);
    preflightRequest.requestedOriginalRelativePaths =
        std::move(request.requestedOriginalRelativePaths);
    preflightRequest.packageWorkInProgress = request.packageWorkInProgress;
    result.preflight = buildPackageMediaQuarantinePreflightPlan(std::move(preflightRequest));

    if (result.preflight.status != PackageMediaQuarantinePreflightStatus::planned
        || !result.preflight.restoreManifest.has_value())
    {
        result.error = preflightFailureMessage(result.preflight);
        storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::failed, 100);
        return result;
    }

    if (isCancelled(cancelRequested))
    {
        result.cancelled = true;
        result.error = "Package media cleanup was cancelled before quarantine.";
        storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::cancelled, 55);
        return result;
    }

    storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::quarantining, 70);
    PackageMediaQuarantineCommandRequest quarantineRequest;
    quarantineRequest.packageDirectory = std::move(request.packageDirectory);
    quarantineRequest.restoreManifestDraft = std::move(*result.preflight.restoreManifest);
    result.quarantine = quarantinePackageMedia(std::move(quarantineRequest));

    if (result.quarantine.status == PackageMediaQuarantineCommandStatus::completed)
    {
        storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::completed, 100);
        return result;
    }

    result.error = quarantineFailureMessage(result.quarantine);
    storeProgress(phase, progressPercent, BackgroundPackageMediaCleanupPhase::failed, 100);
    return result;
}
} // namespace projectname
