// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "BackgroundPackageMediaCleanupJob.h"
#include "ImportedMediaPackageInventory.h"
#include "PackageMediaQuarantineCommand.h"
#include "PackageMediaQuarantinePreflightPlan.h"
#include "PackageMediaQuarantineRestoreManifest.h"

#include <string>
#include <string_view>

namespace projectname
{
enum class PackageMediaCleanupStatusKind
{
    idle,
    inventoryRunning,
    preflightRunning,
    quarantineRunning,
    restoreRunning,
    operationCompleted,
    reviewAvailable,
    preflightReady,
    activePackageWork,
    unsafeReferences,
    missingReferences,
    noCandidates,
    quarantineCompleted,
    cleanupFailed,
    restoreCompleted,
    restoreConflict,
    partialFailure,
    cancelled,
};

enum class PackageMediaCleanupStatusSeverity
{
    neutral,
    progress,
    success,
    warning,
    error,
};

namespace packageMediaCleanupAffordanceIds
{
inline constexpr std::string_view none = "none";
inline constexpr std::string_view browserProgressRow = "browser.package-media.progress-row";
inline constexpr std::string_view browserReviewAction = "browser.package-media.review";
inline constexpr std::string_view browserCleanupAction = "browser.package-media.cleanup.confirm";
inline constexpr std::string_view browserCleanupDisabled = "browser.package-media.cleanup.disabled";
inline constexpr std::string_view browserRestoreList = "browser.package-media.restore-list";
inline constexpr std::string_view browserFailureReview = "browser.package-media.failure-review";
inline constexpr std::string_view inspectorNone = "inspector.package-media.none";
inline constexpr std::string_view inspectorPassiveNotice = "inspector.package-media.passive-notice";
inline constexpr std::string_view inspectorRestoreOptions = "inspector.package-media.restore-options";
inline constexpr std::string_view inspectorConflictNotice = "inspector.package-media.conflict-notice";
inline constexpr std::string_view statusBarNone = "status-bar.package-media.none";
inline constexpr std::string_view statusBarProgress = "status-bar.package-media.progress";
inline constexpr std::string_view statusBarReview = "status-bar.package-media.review";
} // namespace packageMediaCleanupAffordanceIds

struct PackageMediaCleanupStatus
{
    PackageMediaCleanupStatusKind kind = PackageMediaCleanupStatusKind::idle;
    PackageMediaCleanupStatusSeverity severity = PackageMediaCleanupStatusSeverity::neutral;
    std::string statusText;
    std::string detailText;
    std::string browserAffordanceId = std::string(packageMediaCleanupAffordanceIds::none);
    std::string inspectorAffordanceId = std::string(packageMediaCleanupAffordanceIds::inspectorNone);
    std::string statusBarAffordanceId = std::string(packageMediaCleanupAffordanceIds::statusBarNone);
};

[[nodiscard]] PackageMediaCleanupStatus describePackageMediaCleanupProgress(
    const BackgroundPackageMediaCleanupProgress& progress);

[[nodiscard]] PackageMediaCleanupStatus describePackageMediaCleanupInventory(
    const ImportedMediaPackageInventory& inventory);

[[nodiscard]] PackageMediaCleanupStatus describePackageMediaCleanupPreflight(
    const PackageMediaQuarantinePreflightResult& preflight);

[[nodiscard]] PackageMediaCleanupStatus describePackageMediaCleanupQuarantine(
    const PackageMediaQuarantineCommandResult& quarantine);

[[nodiscard]] PackageMediaCleanupStatus describePackageMediaCleanupRestore(
    const PackageMediaQuarantineRestoreCommandResult& restore);

[[nodiscard]] PackageMediaCleanupStatus describePackageMediaCleanupRestoreManifest(
    const PackageMediaQuarantineRestoreManifest& manifest);

[[nodiscard]] PackageMediaCleanupStatus describePackageMediaCleanupResult(
    const BackgroundPackageMediaCleanupResult& result);
} // namespace projectname
