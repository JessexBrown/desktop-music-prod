// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "PackageMediaCleanupStatus.h"
#include "PackageMediaQuarantineRestoreManifest.h"

#include <filesystem>
#include <string>
#include <vector>

namespace projectname
{
enum class PackageMediaCleanupBatchDiscoveryIssueKind
{
    invalidCleanupId,
    invalidManifestPath,
    manifestMissing,
    manifestLoadFailed,
    manifestCleanupIdMismatch,
    scanFailed,
};

struct PackageMediaCleanupBatchDiscoveryIssue
{
    PackageMediaCleanupBatchDiscoveryIssueKind kind =
        PackageMediaCleanupBatchDiscoveryIssueKind::scanFailed;
    std::string cleanupId;
    std::filesystem::path manifestRelativePath;
    std::filesystem::path manifestPath;
    std::string message;
};

struct PackageMediaCleanupBatch
{
    std::string cleanupId;
    std::string createdAtUtc;
    std::filesystem::path manifestRelativePath;
    std::filesystem::path manifestPath;
    PackageMediaQuarantineRestoreManifest manifest;
    PackageMediaCleanupStatus status;
};

struct PackageMediaCleanupBatchDiscoveryResult
{
    std::vector<PackageMediaCleanupBatch> batches;
    std::vector<PackageMediaCleanupBatchDiscoveryIssue> issues;
    std::string error;
};

[[nodiscard]] PackageMediaCleanupBatchDiscoveryResult discoverPackageMediaCleanupBatches(
    const std::filesystem::path& packageDirectory);
} // namespace projectname
