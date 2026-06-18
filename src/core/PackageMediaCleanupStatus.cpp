// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PackageMediaCleanupStatus.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] std::string makeCountDetail(std::size_t count, std::string_view noun)
{
    if (count == 0)
        return {};

    return std::to_string(count) + " " + std::string(noun) + "(s) need review.";
}

[[nodiscard]] std::string makeMovedDetail(std::size_t count)
{
    if (count == 0)
        return {};

    return std::to_string(count) + " item(s) moved to Project Media Trash.";
}

[[nodiscard]] std::string makeReadyDetail(std::size_t count)
{
    if (count == 0)
        return {};

    return std::to_string(count) + " item(s) ready for Project Media Trash.";
}

[[nodiscard]] std::string makeRestoredDetail(std::size_t count)
{
    if (count == 0)
        return {};

    return std::to_string(count) + " item(s) restored.";
}

[[nodiscard]] std::string detailOrFallback(const std::string& detail, std::string fallback)
{
    return detail.empty() ? std::move(fallback) : detail;
}

[[nodiscard]] PackageMediaCleanupStatus makeStatus(
    PackageMediaCleanupStatusKind kind,
    PackageMediaCleanupStatusSeverity severity,
    std::string statusText,
    std::string detailText,
    std::string_view browserAffordanceId,
    std::string_view inspectorAffordanceId,
    std::string_view statusBarAffordanceId)
{
    PackageMediaCleanupStatus status;
    status.kind = kind;
    status.severity = severity;
    status.statusText = std::move(statusText);
    status.detailText = std::move(detailText);
    status.browserAffordanceId = std::string(browserAffordanceId);
    status.inspectorAffordanceId = std::string(inspectorAffordanceId);
    status.statusBarAffordanceId = std::string(statusBarAffordanceId);
    return status;
}

[[nodiscard]] PackageMediaCleanupStatus makeIdleStatus()
{
    return makeStatus(PackageMediaCleanupStatusKind::idle,
                      PackageMediaCleanupStatusSeverity::neutral,
                      "Package media cleanup is ready.",
                      {},
                      packageMediaCleanupAffordanceIds::browserReviewAction,
                      packageMediaCleanupAffordanceIds::inspectorNone,
                      packageMediaCleanupAffordanceIds::statusBarNone);
}

[[nodiscard]] PackageMediaCleanupStatus makeCancelledStatus()
{
    return makeStatus(PackageMediaCleanupStatusKind::cancelled,
                      PackageMediaCleanupStatusSeverity::neutral,
                      "Cleanup was cancelled before moving media.",
                      {},
                      packageMediaCleanupAffordanceIds::browserReviewAction,
                      packageMediaCleanupAffordanceIds::inspectorNone,
                      packageMediaCleanupAffordanceIds::statusBarReview);
}

[[nodiscard]] PackageMediaCleanupStatus makeCleanupFailedStatus(std::string detail)
{
    return makeStatus(PackageMediaCleanupStatusKind::cleanupFailed,
                      PackageMediaCleanupStatusSeverity::error,
                      "Cleanup needs review. No files were permanently deleted.",
                      std::move(detail),
                      packageMediaCleanupAffordanceIds::browserFailureReview,
                      packageMediaCleanupAffordanceIds::inspectorPassiveNotice,
                      packageMediaCleanupAffordanceIds::statusBarReview);
}

[[nodiscard]] std::size_t countUnreferencedAssets(const ImportedMediaPackageInventory& inventory)
{
    return static_cast<std::size_t>(
        std::count_if(inventory.assets.begin(),
                      inventory.assets.end(),
                      [](const ImportedMediaPackageAsset& asset)
                      {
                          return asset.unreferencedCandidate;
                      }));
}

[[nodiscard]] std::size_t countStaleStagingDirectories(
    const ImportedMediaPackageInventory& inventory)
{
    return static_cast<std::size_t>(
        std::count_if(inventory.stagingDirectories.begin(),
                      inventory.stagingDirectories.end(),
                      [](const ImportedMediaPackageStagingDirectory& staging)
                      {
                          return staging.staleCandidate;
                      }));
}

[[nodiscard]] std::size_t countRestorableEntries(
    const PackageMediaQuarantineRestoreManifest& manifest)
{
    return static_cast<std::size_t>(
        std::count_if(manifest.movedEntries.begin(),
                      manifest.movedEntries.end(),
                      [](const PackageMediaQuarantineMovedEntry& entry)
                      {
                          return !entry.restored;
                      }));
}

[[nodiscard]] std::size_t countRestoredEntries(
    const PackageMediaQuarantineRestoreManifest& manifest)
{
    return static_cast<std::size_t>(
        std::count_if(manifest.movedEntries.begin(),
                      manifest.movedEntries.end(),
                      [](const PackageMediaQuarantineMovedEntry& entry)
                      {
                          return entry.restored;
                      }));
}

[[nodiscard]] std::size_t countConflictedEntries(
    const PackageMediaQuarantineRestoreManifest& manifest)
{
    return static_cast<std::size_t>(
        std::count_if(manifest.movedEntries.begin(),
                      manifest.movedEntries.end(),
                      [](const PackageMediaQuarantineMovedEntry& entry)
                      {
                          return entry.restoreConflict;
                      }));
}

[[nodiscard]] std::size_t countEntryErrors(const PackageMediaQuarantineRestoreManifest& manifest)
{
    return static_cast<std::size_t>(
        std::count_if(manifest.movedEntries.begin(),
                      manifest.movedEntries.end(),
                      [](const PackageMediaQuarantineMovedEntry& entry)
                      {
                          return !entry.error.empty();
                      }));
}
} // namespace

PackageMediaCleanupStatus describePackageMediaCleanupProgress(
    const BackgroundPackageMediaCleanupProgress& progress)
{
    if (progress.cancelRequested
        || progress.phase == BackgroundPackageMediaCleanupPhase::cancelled)
        return makeCancelledStatus();

    switch (progress.phase)
    {
        case BackgroundPackageMediaCleanupPhase::pending:
            return makeIdleStatus();
        case BackgroundPackageMediaCleanupPhase::inventory:
            return makeStatus(PackageMediaCleanupStatusKind::inventoryRunning,
                              PackageMediaCleanupStatusSeverity::progress,
                              "Scanning project media...",
                              std::to_string(progress.percent) + "% complete.",
                              packageMediaCleanupAffordanceIds::browserProgressRow,
                              packageMediaCleanupAffordanceIds::inspectorNone,
                              packageMediaCleanupAffordanceIds::statusBarProgress);
        case BackgroundPackageMediaCleanupPhase::preflight:
            return makeStatus(PackageMediaCleanupStatusKind::preflightRunning,
                              PackageMediaCleanupStatusSeverity::progress,
                              "Preparing package media cleanup...",
                              std::to_string(progress.percent) + "% complete.",
                              packageMediaCleanupAffordanceIds::browserProgressRow,
                              packageMediaCleanupAffordanceIds::inspectorNone,
                              packageMediaCleanupAffordanceIds::statusBarProgress);
        case BackgroundPackageMediaCleanupPhase::quarantining:
            return makeStatus(PackageMediaCleanupStatusKind::quarantineRunning,
                              PackageMediaCleanupStatusSeverity::progress,
                              "Moving unused media to Project Media Trash...",
                              std::to_string(progress.percent) + "% complete.",
                              packageMediaCleanupAffordanceIds::browserProgressRow,
                              packageMediaCleanupAffordanceIds::inspectorNone,
                              packageMediaCleanupAffordanceIds::statusBarProgress);
        case BackgroundPackageMediaCleanupPhase::restoring:
            return makeStatus(PackageMediaCleanupStatusKind::restoreRunning,
                              PackageMediaCleanupStatusSeverity::progress,
                              "Restoring project media...",
                              std::to_string(progress.percent) + "% complete.",
                              packageMediaCleanupAffordanceIds::browserProgressRow,
                              packageMediaCleanupAffordanceIds::inspectorRestoreOptions,
                              packageMediaCleanupAffordanceIds::statusBarProgress);
        case BackgroundPackageMediaCleanupPhase::completed:
            return makeStatus(PackageMediaCleanupStatusKind::operationCompleted,
                              PackageMediaCleanupStatusSeverity::success,
                              "Package media operation completed.",
                              {},
                              packageMediaCleanupAffordanceIds::browserReviewAction,
                              packageMediaCleanupAffordanceIds::inspectorNone,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        case BackgroundPackageMediaCleanupPhase::failed:
            return makeCleanupFailedStatus({});
        case BackgroundPackageMediaCleanupPhase::cancelled:
            return makeCancelledStatus();
    }

    return makeIdleStatus();
}

PackageMediaCleanupStatus describePackageMediaCleanupInventory(
    const ImportedMediaPackageInventory& inventory)
{
    if (!inventory.error.empty())
        return makeCleanupFailedStatus(inventory.error);

    if (!inventory.unsafeReferences.empty())
    {
        return makeStatus(PackageMediaCleanupStatusKind::unsafeReferences,
                          PackageMediaCleanupStatusSeverity::warning,
                          "Resolve unsafe media references before cleanup.",
                          makeCountDetail(inventory.unsafeReferences.size(), "unsafe reference"),
                          packageMediaCleanupAffordanceIds::browserFailureReview,
                          packageMediaCleanupAffordanceIds::inspectorPassiveNotice,
                          packageMediaCleanupAffordanceIds::statusBarReview);
    }

    if (!inventory.missingReferences.empty())
    {
        return makeStatus(PackageMediaCleanupStatusKind::missingReferences,
                          PackageMediaCleanupStatusSeverity::warning,
                          "Some package media is missing. Review restore options before cleanup.",
                          makeCountDetail(inventory.missingReferences.size(), "missing reference"),
                          packageMediaCleanupAffordanceIds::browserRestoreList,
                          packageMediaCleanupAffordanceIds::inspectorRestoreOptions,
                          packageMediaCleanupAffordanceIds::statusBarReview);
    }

    const auto candidateCount =
        countUnreferencedAssets(inventory) + countStaleStagingDirectories(inventory);
    if (candidateCount == 0)
    {
        return makeStatus(PackageMediaCleanupStatusKind::noCandidates,
                          PackageMediaCleanupStatusSeverity::success,
                          "No unused package media to clean up.",
                          {},
                          packageMediaCleanupAffordanceIds::browserReviewAction,
                          packageMediaCleanupAffordanceIds::inspectorNone,
                          packageMediaCleanupAffordanceIds::statusBarNone);
    }

    return makeStatus(PackageMediaCleanupStatusKind::reviewAvailable,
                      PackageMediaCleanupStatusSeverity::neutral,
                      "Review unused project media.",
                      std::to_string(candidateCount) + " cleanup candidate(s) found.",
                      packageMediaCleanupAffordanceIds::browserReviewAction,
                      packageMediaCleanupAffordanceIds::inspectorNone,
                      packageMediaCleanupAffordanceIds::statusBarReview);
}

PackageMediaCleanupStatus describePackageMediaCleanupPreflight(
    const PackageMediaQuarantinePreflightResult& preflight)
{
    switch (preflight.status)
    {
        case PackageMediaQuarantinePreflightStatus::planned:
        {
            const auto count = preflight.restoreManifest.has_value()
                ? preflight.restoreManifest->movedEntries.size()
                : 0U;
            return makeStatus(PackageMediaCleanupStatusKind::preflightReady,
                              PackageMediaCleanupStatusSeverity::neutral,
                              "Ready to move unused package media to Project Media Trash.",
                              makeReadyDetail(count),
                              packageMediaCleanupAffordanceIds::browserCleanupAction,
                              packageMediaCleanupAffordanceIds::inspectorNone,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        }
        case PackageMediaQuarantinePreflightStatus::activePackageWork:
            return makeStatus(PackageMediaCleanupStatusKind::activePackageWork,
                              PackageMediaCleanupStatusSeverity::warning,
                              "Finish the current package operation before cleaning project media.",
                              preflight.error,
                              packageMediaCleanupAffordanceIds::browserCleanupDisabled,
                              packageMediaCleanupAffordanceIds::inspectorPassiveNotice,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        case PackageMediaQuarantinePreflightStatus::unsafeReferences:
            return makeStatus(PackageMediaCleanupStatusKind::unsafeReferences,
                              PackageMediaCleanupStatusSeverity::warning,
                              "Resolve unsafe media references before cleanup.",
                              preflight.error,
                              packageMediaCleanupAffordanceIds::browserFailureReview,
                              packageMediaCleanupAffordanceIds::inspectorPassiveNotice,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        case PackageMediaQuarantinePreflightStatus::missingReferences:
            return makeStatus(PackageMediaCleanupStatusKind::missingReferences,
                              PackageMediaCleanupStatusSeverity::warning,
                              "Some package media is missing. Review restore options before cleanup.",
                              preflight.error,
                              packageMediaCleanupAffordanceIds::browserRestoreList,
                              packageMediaCleanupAffordanceIds::inspectorRestoreOptions,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        case PackageMediaQuarantinePreflightStatus::noMovableCandidates:
            return makeStatus(PackageMediaCleanupStatusKind::noCandidates,
                              PackageMediaCleanupStatusSeverity::success,
                              "No unused package media to clean up.",
                              preflight.error,
                              packageMediaCleanupAffordanceIds::browserReviewAction,
                              packageMediaCleanupAffordanceIds::inspectorNone,
                              packageMediaCleanupAffordanceIds::statusBarNone);
        case PackageMediaQuarantinePreflightStatus::invalidCleanupId:
        case PackageMediaQuarantinePreflightStatus::inventoryError:
        case PackageMediaQuarantinePreflightStatus::protectedReference:
        case PackageMediaQuarantinePreflightStatus::duplicateQuarantineDestination:
        case PackageMediaQuarantinePreflightStatus::invalidInventoryPath:
            return makeCleanupFailedStatus(detailOrFallback(
                preflight.error,
                "Package media cleanup preflight failed."));
    }

    return makeCleanupFailedStatus("Package media cleanup preflight failed.");
}

PackageMediaCleanupStatus describePackageMediaCleanupQuarantine(
    const PackageMediaQuarantineCommandResult& quarantine)
{
    if (quarantine.status == PackageMediaQuarantineCommandStatus::completed)
    {
        const auto count = quarantine.restoreManifest.has_value()
            ? quarantine.restoreManifest->movedEntries.size()
            : 0U;
        return makeStatus(PackageMediaCleanupStatusKind::quarantineCompleted,
                          PackageMediaCleanupStatusSeverity::success,
                          "Moved unused media to Project Media Trash. Restore is available from Package Maintenance.",
                          makeMovedDetail(count),
                          packageMediaCleanupAffordanceIds::browserRestoreList,
                          packageMediaCleanupAffordanceIds::inspectorNone,
                          packageMediaCleanupAffordanceIds::statusBarReview);
    }

    return makeCleanupFailedStatus(detailOrFallback(quarantine.error,
                                                    "Package media quarantine command failed."));
}

PackageMediaCleanupStatus describePackageMediaCleanupRestore(
    const PackageMediaQuarantineRestoreCommandResult& restore)
{
    switch (restore.status)
    {
        case PackageMediaQuarantineRestoreCommandStatus::restored:
            return makeStatus(PackageMediaCleanupStatusKind::restoreCompleted,
                              PackageMediaCleanupStatusSeverity::success,
                              "Restored project media.",
                              makeRestoredDetail(restore.restoredCount),
                              packageMediaCleanupAffordanceIds::browserRestoreList,
                              packageMediaCleanupAffordanceIds::inspectorRestoreOptions,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        case PackageMediaQuarantineRestoreCommandStatus::restoreConflict:
            return makeStatus(PackageMediaCleanupStatusKind::restoreConflict,
                              PackageMediaCleanupStatusSeverity::warning,
                              "Some files were not restored because their original paths are occupied.",
                              makeCountDetail(restore.conflictCount, "restore conflict"),
                              packageMediaCleanupAffordanceIds::browserRestoreList,
                              packageMediaCleanupAffordanceIds::inspectorConflictNotice,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        case PackageMediaQuarantineRestoreCommandStatus::missingQuarantinePath:
        case PackageMediaQuarantineRestoreCommandStatus::moveFailed:
        case PackageMediaQuarantineRestoreCommandStatus::manifestWriteFailed:
        case PackageMediaQuarantineRestoreCommandStatus::manifestCommitFailed:
            return makeStatus(PackageMediaCleanupStatusKind::partialFailure,
                              PackageMediaCleanupStatusSeverity::error,
                              "Restore needs review. No active package media was overwritten.",
                              detailOrFallback(restore.error,
                                               makeCountDetail(restore.missingCount,
                                                               "missing quarantine path")),
                              packageMediaCleanupAffordanceIds::browserFailureReview,
                              packageMediaCleanupAffordanceIds::inspectorPassiveNotice,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        case PackageMediaQuarantineRestoreCommandStatus::invalidRequest:
        case PackageMediaQuarantineRestoreCommandStatus::manifestLoadFailed:
            return makeCleanupFailedStatus(detailOrFallback(
                restore.error,
                "Package media restore command failed."));
    }

    return makeCleanupFailedStatus("Package media restore command failed.");
}

PackageMediaCleanupStatus describePackageMediaCleanupRestoreManifest(
    const PackageMediaQuarantineRestoreManifest& manifest)
{
    switch (manifest.state)
    {
        case PackageMediaQuarantineManifestState::completed:
            return makeStatus(PackageMediaCleanupStatusKind::quarantineCompleted,
                              PackageMediaCleanupStatusSeverity::success,
                              "Moved unused media to Project Media Trash. Restore is available from Package Maintenance.",
                              makeMovedDetail(countRestorableEntries(manifest)),
                              packageMediaCleanupAffordanceIds::browserRestoreList,
                              packageMediaCleanupAffordanceIds::inspectorNone,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        case PackageMediaQuarantineManifestState::restored:
            return makeStatus(PackageMediaCleanupStatusKind::restoreCompleted,
                              PackageMediaCleanupStatusSeverity::success,
                              "Restored project media.",
                              makeRestoredDetail(countRestoredEntries(manifest)),
                              packageMediaCleanupAffordanceIds::browserRestoreList,
                              packageMediaCleanupAffordanceIds::inspectorRestoreOptions,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        case PackageMediaQuarantineManifestState::restoreConflict:
            return makeStatus(PackageMediaCleanupStatusKind::restoreConflict,
                              PackageMediaCleanupStatusSeverity::warning,
                              "Some files were not restored because their original paths are occupied.",
                              makeCountDetail(countConflictedEntries(manifest),
                                              "restore conflict"),
                              packageMediaCleanupAffordanceIds::browserRestoreList,
                              packageMediaCleanupAffordanceIds::inspectorConflictNotice,
                              packageMediaCleanupAffordanceIds::statusBarReview);
        case PackageMediaQuarantineManifestState::partialFailure:
            return makeStatus(PackageMediaCleanupStatusKind::partialFailure,
                              PackageMediaCleanupStatusSeverity::error,
                              "Restore needs review. No active package media was overwritten.",
                              detailOrFallback(manifest.error,
                                               makeCountDetail(countEntryErrors(manifest),
                                                               "entry error")),
                              packageMediaCleanupAffordanceIds::browserFailureReview,
                              packageMediaCleanupAffordanceIds::inspectorPassiveNotice,
                              packageMediaCleanupAffordanceIds::statusBarReview);
    }

    return makeCleanupFailedStatus("Package media restore manifest state is unknown.");
}

PackageMediaCleanupStatus describePackageMediaCleanupResult(
    const BackgroundPackageMediaCleanupResult& result)
{
    if (result.cancelled)
        return makeCancelledStatus();

    if (result.operation == BackgroundPackageMediaCleanupOperation::restore)
        return describePackageMediaCleanupRestore(result.restore);

    if (result.preflight.status != PackageMediaQuarantinePreflightStatus::planned)
        return describePackageMediaCleanupPreflight(result.preflight);

    if (result.quarantine.status != PackageMediaQuarantineCommandStatus::completed
        || result.quarantine.restoreManifest.has_value()
        || !result.quarantine.restoreManifestPath.empty())
    {
        return describePackageMediaCleanupQuarantine(result.quarantine);
    }

    if (result.preflight.restoreManifest.has_value())
        return describePackageMediaCleanupPreflight(result.preflight);

    return describePackageMediaCleanupInventory(result.inventory);
}
} // namespace projectname
