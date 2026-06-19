// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectModel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace projectname
{
enum class ProjectPackageSaveAsFolderAction
{
    noCopyNeeded,
    cloneContents,
    startFresh,
};

struct ProjectPackageSaveAsFolderPolicy
{
    std::string folderName;
    ProjectPackageSaveAsFolderAction action = ProjectPackageSaveAsFolderAction::noCopyNeeded;
    bool containsSourceContent = false;
    std::string reason;
};

enum class ProjectPackageSaveAsReferenceAction
{
    clonePackageAsset,
    preserveExternalReference,
    reportMissingPackageAsset,
    reportUnsafeReference,
};

struct ProjectPackageSaveAsReference
{
    std::string clipId;
    std::string role;
    std::string path;
    ProjectPackageSaveAsReferenceAction action = ProjectPackageSaveAsReferenceAction::preserveExternalReference;
    bool existsInSourcePackage = false;
};

struct ProjectPackageSaveAsPlan
{
    bool samePackage = false;
    bool requiresPackageAssetCopy = false;
    bool canSaveManifestOnly = true;
    std::vector<ProjectPackageSaveAsFolderPolicy> folders;
    std::vector<ProjectPackageSaveAsReference> references;
    std::string warning;
};

[[nodiscard]] ProjectPackageSaveAsPlan buildProjectPackageSaveAsPlan(
    const ProjectModel& project,
    const std::filesystem::path& sourcePackageDirectory,
    const std::filesystem::path& targetPackageDirectory);

[[nodiscard]] std::string describeProjectPackageSaveAsPlan(const ProjectPackageSaveAsPlan& plan);
} // namespace projectname
