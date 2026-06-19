// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectModel.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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

enum class ProjectPackageSaveAsCopyStatus
{
    completed,
    noCopyNeeded,
    cancelled,
    invalidRequest,
    targetConflict,
    unsupportedSourceEntry,
    copyFailed,
    rollbackFailed,
};

enum class ProjectPackageSaveAsCopyProgressStage
{
    planning,
    preflight,
    copying,
    completed,
    failed,
    cancelled,
};

struct ProjectPackageSaveAsCopyProgress
{
    ProjectPackageSaveAsCopyProgressStage stage =
        ProjectPackageSaveAsCopyProgressStage::planning;
    int percent = 0;
    std::size_t filesCopied = 0;
    std::size_t filesTotal = 0;
    std::uintmax_t bytesCopied = 0;
    std::uintmax_t bytesTotal = 0;
};

struct ProjectPackageSaveAsCopyRequest
{
    ProjectModel project;
    std::filesystem::path sourcePackageDirectory;
    std::filesystem::path targetPackageDirectory;
    std::atomic_bool* cancelRequested = nullptr;
    std::function<void(const ProjectPackageSaveAsCopyProgress&)> progressCallback;
};

struct ProjectPackageSaveAsCopyResult
{
    ProjectPackageSaveAsCopyStatus status = ProjectPackageSaveAsCopyStatus::completed;
    std::string error;
    ProjectPackageSaveAsPlan plan;
    std::size_t copiedFileCount = 0;
    std::size_t copiedDirectoryCount = 0;
    std::size_t totalFileCount = 0;
    std::uintmax_t copiedBytes = 0;
    std::uintmax_t totalBytes = 0;
    std::vector<std::filesystem::path> createdPaths;
};

[[nodiscard]] ProjectPackageSaveAsPlan buildProjectPackageSaveAsPlan(
    const ProjectModel& project,
    const std::filesystem::path& sourcePackageDirectory,
    const std::filesystem::path& targetPackageDirectory);

[[nodiscard]] std::string describeProjectPackageSaveAsPlan(const ProjectPackageSaveAsPlan& plan);

[[nodiscard]] ProjectPackageSaveAsCopyResult copyProjectPackageAssetsForSaveAs(
    ProjectPackageSaveAsCopyRequest request);
} // namespace projectname
