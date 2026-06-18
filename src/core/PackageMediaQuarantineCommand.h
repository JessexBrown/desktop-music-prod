// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "PackageMediaQuarantineRestoreManifest.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace projectname
{
enum class PackageMediaQuarantineCommandStatus
{
    completed,
    invalidRequest,
    sourceMissing,
    destinationOccupied,
    moveFailed,
    manifestWriteFailed,
    manifestCommitFailed,
    rollbackFailed,
};

struct PackageMediaQuarantineCommandRequest
{
    std::filesystem::path packageDirectory;
    PackageMediaQuarantineRestoreManifest restoreManifestDraft;
};

struct PackageMediaQuarantineCommandResult
{
    PackageMediaQuarantineCommandStatus status =
        PackageMediaQuarantineCommandStatus::completed;
    std::string error;
    std::filesystem::path restoreManifestPath;
    std::filesystem::path temporaryRestoreManifestPath;
    std::optional<PackageMediaQuarantineRestoreManifest> restoreManifest;
};

[[nodiscard]] PackageMediaQuarantineCommandResult quarantinePackageMedia(
    PackageMediaQuarantineCommandRequest request);

enum class PackageMediaQuarantineRestoreCommandStatus
{
    restored,
    restoreConflict,
    invalidRequest,
    manifestLoadFailed,
    missingQuarantinePath,
    moveFailed,
    manifestWriteFailed,
    manifestCommitFailed,
};

struct PackageMediaQuarantineRestoreCommandRequest
{
    std::filesystem::path packageDirectory;
    std::filesystem::path restoreManifestPath;
    std::vector<std::string> selectedOriginalRelativePaths;
};

struct PackageMediaQuarantineRestoreCommandResult
{
    PackageMediaQuarantineRestoreCommandStatus status =
        PackageMediaQuarantineRestoreCommandStatus::restored;
    std::string error;
    std::filesystem::path restoreManifestPath;
    std::filesystem::path temporaryRestoreManifestPath;
    std::size_t restoredCount = 0;
    std::size_t conflictCount = 0;
    std::size_t missingCount = 0;
    std::optional<PackageMediaQuarantineRestoreManifest> restoreManifest;
};

[[nodiscard]] PackageMediaQuarantineRestoreCommandResult restorePackageMediaFromQuarantine(
    PackageMediaQuarantineRestoreCommandRequest request);
} // namespace projectname
