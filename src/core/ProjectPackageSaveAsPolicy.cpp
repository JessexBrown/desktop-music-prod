// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ProjectPackageSaveAsPolicy.h"

#include "PackagePath.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>
#include <utility>

namespace projectname
{
namespace
{
const std::array<const char*, 4> cloneFolderNames { "audio", "analysis", "samples", "presets" };
constexpr const char* backupsFolderName = "backups";

[[nodiscard]] std::string normalisedPathKey(const std::filesystem::path& path)
{
    std::error_code error;
    auto absolutePath = std::filesystem::absolute(path, error);
    if (error)
        absolutePath = path;

    auto key = absolutePath.lexically_normal().generic_string();

#ifdef _WIN32
    std::transform(key.begin(),
                   key.end(),
                   key.begin(),
                   [](unsigned char value)
                   {
                       return static_cast<char>(std::tolower(value));
                   });
#endif

    return key;
}

[[nodiscard]] bool folderContainsAnyEntry(const std::filesystem::path& folder)
{
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error) || error)
        return false;

    std::filesystem::recursive_directory_iterator iterator(
        folder,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    if (error)
        return false;

    const std::filesystem::recursive_directory_iterator end;
    return iterator != end;
}

[[nodiscard]] bool isCloneFolder(const std::string& folderName) noexcept
{
    return std::find(cloneFolderNames.begin(), cloneFolderNames.end(), folderName) != cloneFolderNames.end();
}

[[nodiscard]] std::string firstPathPart(const std::filesystem::path& path)
{
    const auto iterator = path.begin();
    if (iterator == path.end())
        return {};

    return iterator->generic_string();
}

void addReference(ProjectPackageSaveAsPlan& plan,
                  const std::filesystem::path& sourcePackageDirectory,
                  const ProjectClip& clip,
                  const char* role,
                  const std::string& pathText)
{
    if (pathText.empty())
        return;

    ProjectPackageSaveAsReference reference;
    reference.clipId = clip.id;
    reference.role = role;
    reference.path = pathText;

    const auto path = std::filesystem::path(pathText);
    if (path.is_absolute())
    {
        reference.action = ProjectPackageSaveAsReferenceAction::preserveExternalReference;
        plan.references.push_back(std::move(reference));
        return;
    }

    if (!isSafePackageRelativePath(path))
    {
        reference.action = ProjectPackageSaveAsReferenceAction::reportUnsafeReference;
        plan.references.push_back(std::move(reference));
        return;
    }

    if (!isCloneFolder(firstPathPart(path)))
    {
        reference.action = ProjectPackageSaveAsReferenceAction::preserveExternalReference;
        plan.references.push_back(std::move(reference));
        return;
    }

    std::error_code error;
    reference.existsInSourcePackage = std::filesystem::is_regular_file(
        resolvePackagePath(sourcePackageDirectory, path),
        error);
    reference.action = reference.existsInSourcePackage
        ? ProjectPackageSaveAsReferenceAction::clonePackageAsset
        : ProjectPackageSaveAsReferenceAction::reportMissingPackageAsset;
    plan.references.push_back(std::move(reference));
}

[[nodiscard]] std::string makeWarning(const ProjectPackageSaveAsPlan& plan)
{
    if (plan.samePackage)
        return {};

    if (plan.requiresPackageAssetCopy)
    {
        return "Save As needs package asset copy before switching packages; audio, analysis, samples, "
               "and presets will be cloned by the copy step, while backups start fresh in the target package.";
    }

    const auto hasExternalOrUnsafe = std::any_of(
        plan.references.begin(),
        plan.references.end(),
        [](const ProjectPackageSaveAsReference& reference)
        {
            return reference.action == ProjectPackageSaveAsReferenceAction::preserveExternalReference
                || reference.action == ProjectPackageSaveAsReferenceAction::reportUnsafeReference;
        });

    if (hasExternalOrUnsafe)
        return "Save As will preserve external or unsupported media references without copying them.";

    return {};
}
} // namespace

ProjectPackageSaveAsPlan buildProjectPackageSaveAsPlan(
    const ProjectModel& project,
    const std::filesystem::path& sourcePackageDirectory,
    const std::filesystem::path& targetPackageDirectory)
{
    ProjectPackageSaveAsPlan plan;
    plan.samePackage = normalisedPathKey(sourcePackageDirectory) == normalisedPathKey(targetPackageDirectory);

    for (const auto* folderName : cloneFolderNames)
    {
        ProjectPackageSaveAsFolderPolicy folder;
        folder.folderName = folderName;
        folder.action = ProjectPackageSaveAsFolderAction::cloneContents;
        folder.containsSourceContent = folderContainsAnyEntry(sourcePackageDirectory / folderName);
        folder.reason = "Package-local project assets must move with Save As to keep relative references valid.";

        if (!plan.samePackage && folder.containsSourceContent)
            plan.requiresPackageAssetCopy = true;

        plan.folders.push_back(std::move(folder));
    }

    ProjectPackageSaveAsFolderPolicy backupsFolder;
    backupsFolder.folderName = backupsFolderName;
    backupsFolder.action = ProjectPackageSaveAsFolderAction::startFresh;
    backupsFolder.containsSourceContent = folderContainsAnyEntry(sourcePackageDirectory / backupsFolderName);
    backupsFolder.reason = "Backups and media-trash restore history stay with the source package; the target package "
                           "starts its own backup history.";
    plan.folders.push_back(std::move(backupsFolder));

    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (clip.type != "audio-file")
                continue;

            addReference(plan, sourcePackageDirectory, clip, "audio", clip.relativePath);
            addReference(plan, sourcePackageDirectory, clip, "analysis", clip.analysisPath);
        }
    }

    if (!plan.samePackage)
    {
        for (const auto& reference : plan.references)
        {
            if (reference.action == ProjectPackageSaveAsReferenceAction::clonePackageAsset)
            {
                plan.requiresPackageAssetCopy = true;
                break;
            }
        }
    }

    plan.canSaveManifestOnly = plan.samePackage || !plan.requiresPackageAssetCopy;
    plan.warning = makeWarning(plan);
    return plan;
}

std::string describeProjectPackageSaveAsPlan(const ProjectPackageSaveAsPlan& plan)
{
    if (plan.samePackage)
        return "Save As target is the current package; normal save is safe.";

    if (!plan.warning.empty())
        return plan.warning;

    return "Save As can write the manifest and create fresh package folders without copying package assets.";
}
} // namespace projectname
