// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ImportedMediaPackageInventory.h"
#include "PackageMediaQuarantineRestoreManifest.h"

#include <optional>
#include <string>
#include <vector>

namespace projectname
{
enum class PackageMediaQuarantinePreflightStatus
{
    planned,
    invalidCleanupId,
    activePackageWork,
    inventoryError,
    unsafeReferences,
    missingReferences,
    protectedReference,
    duplicateQuarantineDestination,
    noMovableCandidates,
    invalidInventoryPath,
};

struct PackageMediaQuarantinePreflightRequest
{
    ImportedMediaPackageInventory inventory;
    std::string cleanupId;
    std::string createdAtUtc;
    std::string packageDisplayPath;
    std::string manifestMarker;
    std::vector<std::string> requestedOriginalRelativePaths;
    bool packageWorkInProgress = false;
};

struct PackageMediaQuarantinePreflightResult
{
    PackageMediaQuarantinePreflightStatus status =
        PackageMediaQuarantinePreflightStatus::planned;
    std::string error;
    std::optional<PackageMediaQuarantineRestoreManifest> restoreManifest;
};

[[nodiscard]] PackageMediaQuarantinePreflightResult buildPackageMediaQuarantinePreflightPlan(
    PackageMediaQuarantinePreflightRequest request);
} // namespace projectname
