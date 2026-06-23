// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ProjectPackageSaveAsRetry.h"

#include "PackagePath.h"

#include <array>
#include <algorithm>
#include <system_error>
#include <utility>

namespace projectname
{
namespace
{
constexpr const char* manifestFileName = "manifest.json";

const std::array<const char*, 4> packageAssetFolders {
    "audio",
    "analysis",
    "samples",
    "presets",
};

[[nodiscard]] ProjectPackageSaveAsRetryPreflightResult makeResult(
    ProjectPackageSaveAsRetryPreflightStatus status,
    std::string message,
    std::filesystem::path path = {})
{
    ProjectPackageSaveAsRetryPreflightResult result;
    result.status = status;
    result.message = std::move(message);
    result.path = std::move(path);
    return result;
}

[[nodiscard]] bool pathsMatch(const std::filesystem::path& first,
                              const std::filesystem::path& second)
{
    std::error_code error;
    if (std::filesystem::exists(first, error) && std::filesystem::exists(second, error))
    {
        error.clear();
        if (std::filesystem::equivalent(first, second, error))
            return true;
    }

    return first.lexically_normal() == second.lexically_normal();
}

[[nodiscard]] std::string firstPathPart(const std::filesystem::path& path)
{
    const auto iterator = path.begin();
    return iterator == path.end() ? std::string {} : iterator->generic_string();
}

[[nodiscard]] bool isPackageAssetReference(const std::filesystem::path& path)
{
    const auto firstPart = firstPathPart(path);
    return std::find(packageAssetFolders.begin(), packageAssetFolders.end(), firstPart)
        != packageAssetFolders.end();
}

[[nodiscard]] ProjectPackageSaveAsRetryPreflightResult preflightTargetManifestPath(
    const std::filesystem::path& targetPackageDirectory)
{
    const auto manifestPath = targetPackageDirectory / manifestFileName;

    std::error_code error;
    const auto status = std::filesystem::symlink_status(manifestPath, error);
    if (status.type() == std::filesystem::file_type::not_found)
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::ready, {});

    if (error)
    {
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::filesystemError,
                          "Could not inspect failed Save As target manifest: " + error.message(),
                          manifestPath);
    }

    if (std::filesystem::is_regular_file(status))
    {
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::targetManifestExists,
                          "Retry failed: target package already contains manifest.json. "
                          "Open that package, choose a fresh Save As target, or clean it up manually.",
                          manifestPath);
    }

    return makeResult(ProjectPackageSaveAsRetryPreflightStatus::targetManifestConflict,
                      "Retry failed: target manifest path is occupied by a non-regular entry.",
                      manifestPath);
}

[[nodiscard]] ProjectPackageSaveAsRetryPreflightResult preflightPackageAssetReference(
    const std::filesystem::path& targetPackageDirectory,
    const std::string& pathText)
{
    if (pathText.empty())
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::ready, {});

    const auto relativePath = std::filesystem::path(pathText);
    if (relativePath.is_absolute())
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::ready, {});

    if (!isSafePackageRelativePath(relativePath))
    {
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::invalidPackageAssetReference,
                          "Retry failed: project contains an unsafe package asset reference: "
                              + relativePath.generic_string(),
                          relativePath);
    }

    if (!isPackageAssetReference(relativePath))
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::ready, {});

    const auto targetAssetPath = resolvePackagePath(targetPackageDirectory, relativePath);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(targetAssetPath, error);
    if (status.type() == std::filesystem::file_type::not_found)
    {
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::missingPackageAsset,
                          "Retry failed: copied target package is missing required asset "
                              + relativePath.generic_string()
                              + ". Use Save As again so package assets can be copied fresh.",
                          targetAssetPath);
    }
    if (error)
    {
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::filesystemError,
                          "Could not inspect copied Save As target asset: " + error.message(),
                          targetAssetPath);
    }

    if (std::filesystem::is_symlink(status))
    {
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::missingPackageAsset,
                          "Retry failed: copied target package asset is a symlink instead of a "
                          "regular file: "
                              + relativePath.generic_string()
                              + ". Use Save As again so package assets can be copied fresh.",
                          targetAssetPath);
    }

    if (std::filesystem::is_regular_file(status))
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::ready, {});

    return makeResult(ProjectPackageSaveAsRetryPreflightStatus::missingPackageAsset,
                      "Retry failed: copied target package is missing required asset "
                          + relativePath.generic_string()
                          + ". Use Save As again so package assets can be copied fresh.",
                      targetAssetPath);
}

[[nodiscard]] ProjectPackageSaveAsRetryPreflightResult preflightCopiedPackageAssets(
    const ProjectPackageSaveAsRetryState& recoveryState)
{
    for (const auto& track : recoveryState.projectSnapshot.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (clip.type != "audio-file")
                continue;

            auto result = preflightPackageAssetReference(recoveryState.targetPackageDirectory,
                                                         clip.relativePath);
            if (result.status != ProjectPackageSaveAsRetryPreflightStatus::ready)
                return result;

            result = preflightPackageAssetReference(recoveryState.targetPackageDirectory,
                                                    clip.analysisPath);
            if (result.status != ProjectPackageSaveAsRetryPreflightStatus::ready)
                return result;
        }
    }

    return makeResult(ProjectPackageSaveAsRetryPreflightStatus::ready, {});
}
} // namespace

ProjectPackageSaveAsRetryPreflightResult preflightProjectPackageSaveAsRetry(
    const ProjectPackageSaveAsRetryState* recoveryState,
    const ProjectModel& currentProject,
    const std::filesystem::path& activePackageDirectory)
{
    if (recoveryState == nullptr)
    {
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::missingRecoveryState,
                          "No failed Save As target is available to retry.");
    }

    if (recoveryState->copiedFileCount == 0)
    {
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::noCopiedAssets,
                          "Retry failed: the failed Save As did not copy target package assets.");
    }

    if (!pathsMatch(activePackageDirectory, recoveryState->sourcePackageDirectory))
    {
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::activePackageChanged,
                          "Retry failed: active project package changed. Use Save As again.");
    }

    if (!(currentProject == recoveryState->projectSnapshot))
    {
        return makeResult(ProjectPackageSaveAsRetryPreflightStatus::projectChanged,
                          "Retry failed: project changed after the failed Save As. Use Save As again.");
    }

    auto result = preflightTargetManifestPath(recoveryState->targetPackageDirectory);
    if (result.status != ProjectPackageSaveAsRetryPreflightStatus::ready)
        return result;

    result = preflightCopiedPackageAssets(*recoveryState);
    if (result.status != ProjectPackageSaveAsRetryPreflightStatus::ready)
        return result;

    return makeResult(ProjectPackageSaveAsRetryPreflightStatus::ready,
                      "Failed Save As target is ready for manifest retry.");
}
} // namespace projectname
