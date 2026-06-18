// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ImportedMediaPackageInventory.h"
#include "PackageMediaQuarantineCommand.h"
#include "PackageMediaQuarantinePreflightPlan.h"

#include <atomic>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace projectname
{
enum class BackgroundPackageMediaCleanupOperation
{
    quarantine,
    restore,
};

enum class BackgroundPackageMediaCleanupPhase
{
    pending,
    inventory,
    preflight,
    quarantining,
    restoring,
    completed,
    failed,
    cancelled,
};

struct BackgroundPackageMediaCleanupRequest
{
    BackgroundPackageMediaCleanupOperation operation =
        BackgroundPackageMediaCleanupOperation::quarantine;
    std::filesystem::path packageDirectory;

    ImportedMediaPackageInventoryOptions inventoryOptions;
    bool packageWorkInProgress = false;
    std::string cleanupId;
    std::string createdAtUtc;
    std::string packageDisplayPath;
    std::string manifestMarker;
    std::vector<std::string> requestedOriginalRelativePaths;

    std::filesystem::path restoreManifestPath;
    std::vector<std::string> selectedRestoreOriginalRelativePaths;
};

struct BackgroundPackageMediaCleanupProgress
{
    BackgroundPackageMediaCleanupPhase phase =
        BackgroundPackageMediaCleanupPhase::pending;
    int percent = 0;
    bool cancelRequested = false;
};

struct BackgroundPackageMediaCleanupResult
{
    BackgroundPackageMediaCleanupOperation operation =
        BackgroundPackageMediaCleanupOperation::quarantine;
    std::string error;
    bool cancelled = false;
    ImportedMediaPackageInventory inventory;
    PackageMediaQuarantinePreflightResult preflight;
    PackageMediaQuarantineCommandResult quarantine;
    PackageMediaQuarantineRestoreCommandResult restore;
};

class BackgroundPackageMediaCleanupJob
{
public:
    explicit BackgroundPackageMediaCleanupJob(BackgroundPackageMediaCleanupRequest request);
    ~BackgroundPackageMediaCleanupJob();

    BackgroundPackageMediaCleanupJob(const BackgroundPackageMediaCleanupJob&) = delete;
    BackgroundPackageMediaCleanupJob& operator=(const BackgroundPackageMediaCleanupJob&) = delete;

    void start();
    void requestCancel() noexcept;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool hasStarted() const noexcept;
    [[nodiscard]] BackgroundPackageMediaCleanupProgress getProgress() const noexcept;
    [[nodiscard]] BackgroundPackageMediaCleanupResult waitForResult();

private:
    [[nodiscard]] static BackgroundPackageMediaCleanupResult run(
        BackgroundPackageMediaCleanupRequest request,
        std::shared_ptr<std::atomic_bool> cancelRequested,
        std::shared_ptr<std::atomic_int> phase,
        std::shared_ptr<std::atomic_int> progressPercent);

    BackgroundPackageMediaCleanupRequest request_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::shared_ptr<std::atomic_int> phase_;
    std::shared_ptr<std::atomic_int> progressPercent_;
    mutable std::future<BackgroundPackageMediaCleanupResult> future_;
    bool started_ = false;
};
} // namespace projectname
