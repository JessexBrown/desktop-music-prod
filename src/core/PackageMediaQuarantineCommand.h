// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "PackageMediaQuarantineRestoreManifest.h"

#include <filesystem>
#include <optional>
#include <string>

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
} // namespace projectname
