// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectModel.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace projectname
{
struct ProjectPackageSaveAsRetryState
{
    std::filesystem::path sourcePackageDirectory;
    std::filesystem::path targetPackageDirectory;
    ProjectModel projectSnapshot;
    std::size_t copiedFileCount = 0;
    std::uintmax_t copiedBytes = 0;
};

enum class ProjectPackageSaveAsRetryPreflightStatus
{
    ready,
    missingRecoveryState,
    noCopiedAssets,
    activePackageChanged,
    projectChanged,
    targetManifestExists,
    targetManifestConflict,
    missingPackageAsset,
    invalidPackageAssetReference,
    filesystemError,
};

struct ProjectPackageSaveAsRetryPreflightResult
{
    ProjectPackageSaveAsRetryPreflightStatus status =
        ProjectPackageSaveAsRetryPreflightStatus::missingRecoveryState;
    std::string message;
    std::filesystem::path path;
};

[[nodiscard]] ProjectPackageSaveAsRetryPreflightResult preflightProjectPackageSaveAsRetry(
    const ProjectPackageSaveAsRetryState* recoveryState,
    const ProjectModel& currentProject,
    const std::filesystem::path& activePackageDirectory);
} // namespace projectname
