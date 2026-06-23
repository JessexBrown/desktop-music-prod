// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ProjectPackageSaveAsPolicy.h"

#include "PackagePath.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace projectname
{
namespace
{
const std::array<const char*, 4> cloneFolderNames { "audio", "analysis", "samples", "presets" };
constexpr const char* backupsFolderName = "backups";
constexpr std::size_t copyBufferSizeBytes = 64U * 1024U;

enum class CopyEntryKind
{
    directory,
    file,
};

struct CopyEntry
{
    CopyEntryKind kind = CopyEntryKind::file;
    std::filesystem::path sourcePath;
    std::filesystem::path targetPath;
    std::filesystem::path packageRelativePath;
    std::uintmax_t byteSize = 0;
};

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

[[nodiscard]] bool pathKeyIsSameOrChild(std::string parentKey, const std::string& childKey)
{
    if (parentKey.empty() || childKey.empty())
        return false;

    if (parentKey == childKey)
        return true;

    if (parentKey.back() != '/')
        parentKey += '/';

    return childKey.rfind(parentKey, 0) == 0;
}

[[nodiscard]] bool isMissingPathError(const std::error_code& error) noexcept
{
    return error == std::errc::no_such_file_or_directory
        || error == std::errc::not_a_directory;
}

[[nodiscard]] bool copyWasCancelled(const std::atomic_bool* cancelRequested) noexcept
{
    return cancelRequested != nullptr
        && cancelRequested->load(std::memory_order_acquire);
}

void emitCopyProgress(const ProjectPackageSaveAsCopyRequest& request,
                      ProjectPackageSaveAsCopyProgressStage stage,
                      int percent,
                      std::size_t filesCopied,
                      std::size_t filesTotal,
                      std::uintmax_t bytesCopied,
                      std::uintmax_t bytesTotal)
{
    if (!request.progressCallback)
        return;

    ProjectPackageSaveAsCopyProgress progress;
    progress.stage = stage;
    progress.percent = std::clamp(percent, 0, 100);
    progress.filesCopied = filesCopied;
    progress.filesTotal = filesTotal;
    progress.bytesCopied = bytesCopied;
    progress.bytesTotal = bytesTotal;
    request.progressCallback(progress);
}

[[nodiscard]] std::size_t countFileEntries(const std::vector<CopyEntry>& entries) noexcept
{
    return static_cast<std::size_t>(
        std::count_if(entries.begin(),
                      entries.end(),
                      [](const CopyEntry& entry)
                      {
                          return entry.kind == CopyEntryKind::file;
                      }));
}

[[nodiscard]] std::uintmax_t countEntryBytes(const std::vector<CopyEntry>& entries) noexcept
{
    std::uintmax_t bytes = 0;
    for (const auto& entry : entries)
    {
        if (entry.kind == CopyEntryKind::file)
            bytes += entry.byteSize;
    }

    return bytes;
}

[[nodiscard]] int copyPercent(std::uintmax_t copiedBytes,
                              std::uintmax_t totalBytes,
                              std::size_t copiedFiles,
                              std::size_t totalFiles) noexcept
{
    if (totalBytes > 0)
    {
        const auto clampedBytes = std::min(copiedBytes, totalBytes);
        return 25 + static_cast<int>((clampedBytes * 70U) / totalBytes);
    }

    if (totalFiles > 0)
    {
        const auto clampedFiles = std::min(copiedFiles, totalFiles);
        return 25 + static_cast<int>((clampedFiles * 70U) / totalFiles);
    }

    return 95;
}

[[nodiscard]] int cancellationPercent(const ProjectPackageSaveAsCopyResult& result) noexcept
{
    if (result.status == ProjectPackageSaveAsCopyStatus::rollbackFailed)
        return 100;

    if (result.totalBytes > 0 || result.totalFileCount > 0)
    {
        return copyPercent(result.copiedBytes,
                           result.totalBytes,
                           result.copiedFileCount,
                           result.totalFileCount);
    }

    return 0;
}

[[nodiscard]] ProjectPackageSaveAsCopyResult makeCopyResult(
    ProjectPackageSaveAsCopyStatus status,
    std::string error,
    ProjectPackageSaveAsPlan plan)
{
    ProjectPackageSaveAsCopyResult result;
    result.status = status;
    result.error = std::move(error);
    result.plan = std::move(plan);
    return result;
}

[[nodiscard]] const char* copyStatusPrefix(ProjectPackageSaveAsCopyStatus status) noexcept
{
    switch (status)
    {
        case ProjectPackageSaveAsCopyStatus::completed:
            return "Save As package asset copy completed";

        case ProjectPackageSaveAsCopyStatus::noCopyNeeded:
            return "Save As package asset copy was not needed";

        case ProjectPackageSaveAsCopyStatus::cancelled:
            return "Save As package asset copy was cancelled";

        case ProjectPackageSaveAsCopyStatus::invalidRequest:
            return "Save As package asset copy request is invalid";

        case ProjectPackageSaveAsCopyStatus::targetConflict:
            return "Save As package asset copy target is occupied";

        case ProjectPackageSaveAsCopyStatus::unsupportedSourceEntry:
            return "Save As package asset copy found an unsupported source entry";

        case ProjectPackageSaveAsCopyStatus::copyFailed:
            return "Save As package asset copy failed";

        case ProjectPackageSaveAsCopyStatus::rollbackFailed:
            return "Save As package asset copy rollback failed";
    }

    return "Save As package asset copy failed";
}

[[nodiscard]] bool collectCopyEntriesForFolder(
    const std::filesystem::path& sourcePackageDirectory,
    const std::filesystem::path& targetPackageDirectory,
    const std::string& folderName,
    std::vector<CopyEntry>& entries,
    std::string& error)
{
    const auto sourceFolder = sourcePackageDirectory / folderName;
    const auto targetFolder = targetPackageDirectory / folderName;

    std::error_code filesystemError;
    const auto sourceStatus = std::filesystem::symlink_status(sourceFolder, filesystemError);
    if (filesystemError)
    {
        error = "Source package folder is unreadable: " + folderName + ": "
            + filesystemError.message() + ".";
        return false;
    }

    if (std::filesystem::is_symlink(sourceStatus))
    {
        error = "Source package folder is a symlink: " + folderName + ".";
        return false;
    }

    if (!std::filesystem::is_directory(sourceStatus))
    {
        error = "Source package folder is missing or not a directory: " + folderName + ".";
        return false;
    }

    entries.push_back({ CopyEntryKind::directory, sourceFolder, targetFolder, folderName });

    std::filesystem::recursive_directory_iterator iterator(
        sourceFolder,
        std::filesystem::directory_options::skip_permission_denied,
        filesystemError);
    if (filesystemError)
    {
        error = "Could not scan source package folder: " + folderName + ": "
            + filesystemError.message() + ".";
        return false;
    }

    const std::filesystem::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(filesystemError))
    {
        if (filesystemError)
        {
            error = "Could not scan source package folder entry: " + folderName + ": "
                + filesystemError.message() + ".";
            return false;
        }

        const auto entryStatus = iterator->symlink_status(filesystemError);
        if (filesystemError)
        {
            error = "Could not inspect source package entry: "
                + iterator->path().generic_string() + ": " + filesystemError.message() + ".";
            return false;
        }

        if (std::filesystem::is_symlink(entryStatus))
        {
            error = "Source package entry is a symlink: "
                + iterator->path().lexically_relative(sourcePackageDirectory).generic_string() + ".";
            return false;
        }

        CopyEntry entry;
        if (std::filesystem::is_directory(entryStatus))
        {
            entry.kind = CopyEntryKind::directory;
        }
        else if (std::filesystem::is_regular_file(entryStatus))
        {
            entry.kind = CopyEntryKind::file;
            entry.byteSize = std::filesystem::file_size(iterator->path(), filesystemError);
            if (filesystemError)
            {
                error = "Could not inspect source package file size: "
                    + iterator->path().lexically_relative(sourcePackageDirectory).generic_string()
                    + ": " + filesystemError.message() + ".";
                return false;
            }
        }
        else
        {
            error = "Source package entry is not a regular file or directory: "
                + iterator->path().lexically_relative(sourcePackageDirectory).generic_string() + ".";
            return false;
        }

        const auto folderRelativePath = iterator->path().lexically_relative(sourceFolder);
        entry.sourcePath = iterator->path();
        entry.packageRelativePath = std::filesystem::path(folderName) / folderRelativePath;
        if (!isSafePackageRelativePath(entry.packageRelativePath))
        {
            error = "Source package entry resolved outside the package: "
                + entry.packageRelativePath.generic_string() + ".";
            return false;
        }

        entry.targetPath = resolvePackagePath(targetPackageDirectory, entry.packageRelativePath);
        entries.push_back(std::move(entry));
    }

    return true;
}

[[nodiscard]] bool preflightCopyEntries(const std::vector<CopyEntry>& entries,
                                        ProjectPackageSaveAsCopyStatus& status,
                                        std::string& error)
{
    for (const auto& entry : entries)
    {
        std::error_code filesystemError;
        const auto targetStatus = std::filesystem::symlink_status(entry.targetPath, filesystemError);
        if (filesystemError)
        {
            if (isMissingPathError(filesystemError))
                continue;

            status = ProjectPackageSaveAsCopyStatus::copyFailed;
            error = "Could not inspect Save As copy target: "
                + entry.packageRelativePath.generic_string() + ": "
                + filesystemError.message() + ".";
            return false;
        }

        if (!std::filesystem::exists(targetStatus))
            continue;

        if (std::filesystem::is_symlink(targetStatus))
        {
            status = ProjectPackageSaveAsCopyStatus::targetConflict;
            error = "Target package path is a symlink: "
                + entry.packageRelativePath.generic_string() + ".";
            return false;
        }

        if (entry.kind == CopyEntryKind::directory
            && std::filesystem::is_directory(targetStatus))
        {
            continue;
        }

        status = ProjectPackageSaveAsCopyStatus::targetConflict;
        error = "Target package already contains: " + entry.packageRelativePath.generic_string() + ".";
        return false;
    }

    return true;
}

[[nodiscard]] bool ensureDirectoryForCopy(const std::filesystem::path& directory,
                                          std::vector<std::filesystem::path>& createdPaths,
                                          std::string& error)
{
    if (directory.empty())
        return true;

    std::vector<std::filesystem::path> missingDirectories;
    auto current = directory;

    while (!current.empty())
    {
        std::error_code filesystemError;
        const auto currentStatus = std::filesystem::symlink_status(current, filesystemError);
        if (filesystemError)
        {
            if (!isMissingPathError(filesystemError))
            {
                error = "Could not inspect target directory: "
                    + current.generic_string() + ": " + filesystemError.message() + ".";
                return false;
            }
        }
        else if (std::filesystem::exists(currentStatus))
        {
            if (std::filesystem::is_symlink(currentStatus))
            {
                error = "Target directory path is a symlink: "
                    + current.generic_string() + ".";
                return false;
            }

            if (!std::filesystem::is_directory(currentStatus))
            {
                error = "Target path exists but is not a directory: "
                    + current.generic_string() + ".";
                return false;
            }

            break;
        }

        missingDirectories.push_back(current);

        const auto parent = current.parent_path();
        if (parent == current)
            break;

        current = parent;
    }

    for (auto iterator = missingDirectories.rbegin(); iterator != missingDirectories.rend(); ++iterator)
    {
        std::error_code createError;
        if (!std::filesystem::create_directory(*iterator, createError) || createError)
        {
            std::error_code inspectError;
            const auto existingStatus = std::filesystem::symlink_status(*iterator, inspectError);
            if (!inspectError
                && !std::filesystem::is_symlink(existingStatus)
                && std::filesystem::is_directory(existingStatus))
            {
                continue;
            }

            const auto detail = createError
                ? createError.message()
                : (inspectError ? inspectError.message() : std::string("target already exists"));
            error = "Could not create target directory: "
                + iterator->generic_string() + ": " + detail + ".";
            return false;
        }

        createdPaths.push_back(*iterator);
    }

    return true;
}

[[nodiscard]] bool rollbackCreatedPaths(const std::vector<std::filesystem::path>& createdPaths,
                                        std::string& error)
{
    for (auto iterator = createdPaths.rbegin(); iterator != createdPaths.rend(); ++iterator)
    {
        std::error_code filesystemError;
        if (!std::filesystem::exists(*iterator, filesystemError))
            continue;

        filesystemError.clear();
        std::filesystem::remove(*iterator, filesystemError);
        if (filesystemError)
        {
            error = "Could not roll back created path: "
                + iterator->generic_string() + ": " + filesystemError.message() + ".";
            return false;
        }
    }

    return true;
}

void rollbackCopyResult(ProjectPackageSaveAsCopyResult& result,
                        ProjectPackageSaveAsCopyStatus failureStatus)
{
    std::string rollbackError;
    result.status = failureStatus;
    if (!rollbackCreatedPaths(result.createdPaths, rollbackError))
    {
        result.status = ProjectPackageSaveAsCopyStatus::rollbackFailed;
        if (!result.error.empty())
            result.error += " ";
        result.error += rollbackError;
    }
}

[[nodiscard]] bool copyFileContents(const CopyEntry& entry,
                                    const ProjectPackageSaveAsCopyRequest& request,
                                    ProjectPackageSaveAsCopyResult& result)
{
    std::ifstream source(entry.sourcePath, std::ios::binary);
    if (!source)
    {
        result.error = "Could not open package asset for copying: "
            + entry.packageRelativePath.generic_string() + ".";
        return false;
    }

    std::ofstream target(entry.targetPath, std::ios::binary | std::ios::trunc);
    if (!target)
    {
        result.error = "Could not open Save As copy target: "
            + entry.packageRelativePath.generic_string() + ".";
        return false;
    }

    result.createdPaths.push_back(entry.targetPath);

    std::vector<char> buffer(copyBufferSizeBytes);
    while (source)
    {
        if (copyWasCancelled(request.cancelRequested))
        {
            result.error = "Save As package asset copy was cancelled.";
            return false;
        }

        source.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = source.gcount();
        if (bytesRead <= 0)
            break;

        target.write(buffer.data(), bytesRead);
        if (!target)
        {
            result.error = "Could not write Save As package asset: "
                + entry.packageRelativePath.generic_string() + ".";
            return false;
        }

        result.copiedBytes += static_cast<std::uintmax_t>(bytesRead);
        emitCopyProgress(request,
                         ProjectPackageSaveAsCopyProgressStage::copying,
                         copyPercent(result.copiedBytes,
                                     result.totalBytes,
                                     result.copiedFileCount,
                                     result.totalFileCount),
                         result.copiedFileCount,
                         result.totalFileCount,
                         result.copiedBytes,
                         result.totalBytes);
    }

    if (!source.eof())
    {
        result.error = "Could not read package asset: "
            + entry.packageRelativePath.generic_string() + ".";
        return false;
    }

    target.close();
    if (!target)
    {
        result.error = "Could not finish Save As package asset: "
            + entry.packageRelativePath.generic_string() + ".";
        return false;
    }

    ++result.copiedFileCount;
    result.copiedBytes = std::min(result.copiedBytes, result.totalBytes);
    emitCopyProgress(request,
                     ProjectPackageSaveAsCopyProgressStage::copying,
                     copyPercent(result.copiedBytes,
                                 result.totalBytes,
                                 result.copiedFileCount,
                                 result.totalFileCount),
                     result.copiedFileCount,
                     result.totalFileCount,
                     result.copiedBytes,
                     result.totalBytes);
    return true;
}

[[nodiscard]] ProjectPackageSaveAsCopyResult copyEntries(
    ProjectPackageSaveAsPlan plan,
    const std::vector<CopyEntry>& entries,
    const ProjectPackageSaveAsCopyRequest& request)
{
    ProjectPackageSaveAsCopyResult result;
    result.status = ProjectPackageSaveAsCopyStatus::completed;
    result.plan = std::move(plan);
    result.totalFileCount = countFileEntries(entries);
    result.totalBytes = countEntryBytes(entries);

    for (const auto& entry : entries)
    {
        if (copyWasCancelled(request.cancelRequested))
        {
            result.error = "Save As package asset copy was cancelled.";
            rollbackCopyResult(result, ProjectPackageSaveAsCopyStatus::cancelled);
            emitCopyProgress(request,
                             result.status == ProjectPackageSaveAsCopyStatus::rollbackFailed
                                 ? ProjectPackageSaveAsCopyProgressStage::failed
                                 : ProjectPackageSaveAsCopyProgressStage::cancelled,
                             cancellationPercent(result),
                             result.copiedFileCount,
                             result.totalFileCount,
                             result.copiedBytes,
                             result.totalBytes);
            return result;
        }

        if (entry.kind == CopyEntryKind::directory)
        {
            if (!ensureDirectoryForCopy(entry.targetPath, result.createdPaths, result.error))
            {
                rollbackCopyResult(result, ProjectPackageSaveAsCopyStatus::copyFailed);
                emitCopyProgress(request,
                                 ProjectPackageSaveAsCopyProgressStage::failed,
                                 100,
                                 result.copiedFileCount,
                                 result.totalFileCount,
                                 result.copiedBytes,
                                 result.totalBytes);
                return result;
            }

            ++result.copiedDirectoryCount;
            continue;
        }

        if (!ensureDirectoryForCopy(entry.targetPath.parent_path(), result.createdPaths, result.error))
        {
            rollbackCopyResult(result, ProjectPackageSaveAsCopyStatus::copyFailed);
            emitCopyProgress(request,
                             ProjectPackageSaveAsCopyProgressStage::failed,
                             100,
                             result.copiedFileCount,
                             result.totalFileCount,
                             result.copiedBytes,
                             result.totalBytes);
            return result;
        }

        if (!copyFileContents(entry, request, result))
        {
            rollbackCopyResult(result,
                               copyWasCancelled(request.cancelRequested)
                                   ? ProjectPackageSaveAsCopyStatus::cancelled
                                   : ProjectPackageSaveAsCopyStatus::copyFailed);
            emitCopyProgress(request,
                             result.status == ProjectPackageSaveAsCopyStatus::cancelled
                                 ? ProjectPackageSaveAsCopyProgressStage::cancelled
                                 : ProjectPackageSaveAsCopyProgressStage::failed,
                             result.status == ProjectPackageSaveAsCopyStatus::cancelled
                                 ? copyPercent(result.copiedBytes,
                                               result.totalBytes,
                                               result.copiedFileCount,
                                               result.totalFileCount)
                                 : 100,
                             result.copiedFileCount,
                             result.totalFileCount,
                             result.copiedBytes,
                             result.totalBytes);
            return result;
        }
    }

    emitCopyProgress(request,
                     ProjectPackageSaveAsCopyProgressStage::completed,
                     100,
                     result.copiedFileCount,
                     result.totalFileCount,
                     result.copiedBytes,
                     result.totalBytes);
    return result;
}

[[nodiscard]] bool folderContainsAnyEntry(const std::filesystem::path& folder)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(folder, error);
    if (!error && std::filesystem::is_symlink(status))
        return true;

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

ProjectPackageSaveAsCopyResult copyProjectPackageAssetsForSaveAs(
    ProjectPackageSaveAsCopyRequest request)
{
    emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::planning, 5, 0, 0, 0, 0);

    if (request.sourcePackageDirectory.empty() || request.targetPackageDirectory.empty())
    {
        emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::failed, 100, 0, 0, 0, 0);
        return makeCopyResult(ProjectPackageSaveAsCopyStatus::invalidRequest,
                              "Save As package asset copy requires source and target package directories.",
                              {});
    }

    if (copyWasCancelled(request.cancelRequested))
    {
        emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::cancelled, 0, 0, 0, 0, 0);
        return makeCopyResult(ProjectPackageSaveAsCopyStatus::cancelled,
                              "Save As package asset copy was cancelled before it started.",
                              {});
    }

    auto plan = buildProjectPackageSaveAsPlan(
        request.project,
        request.sourcePackageDirectory,
        request.targetPackageDirectory);

    if (plan.samePackage || !plan.requiresPackageAssetCopy)
    {
        emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::completed, 100, 0, 0, 0, 0);
        return makeCopyResult(ProjectPackageSaveAsCopyStatus::noCopyNeeded,
                              describeProjectPackageSaveAsPlan(plan),
                              std::move(plan));
    }

    const auto sourceKey = normalisedPathKey(request.sourcePackageDirectory);
    const auto targetKey = normalisedPathKey(request.targetPackageDirectory);
    if (pathKeyIsSameOrChild(sourceKey, targetKey))
    {
        emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::failed, 100, 0, 0, 0, 0);
        return makeCopyResult(ProjectPackageSaveAsCopyStatus::invalidRequest,
                              "Save As target package cannot be inside the source package.",
                              std::move(plan));
    }

    emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::preflight, 15, 0, 0, 0, 0);
    std::vector<CopyEntry> entries;
    for (const auto& folder : plan.folders)
    {
        if (copyWasCancelled(request.cancelRequested))
        {
            emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::cancelled, 15, 0, 0, 0, 0);
            return makeCopyResult(ProjectPackageSaveAsCopyStatus::cancelled,
                                  "Save As package asset copy was cancelled before preflight completed.",
                                  std::move(plan));
        }

        if (folder.action != ProjectPackageSaveAsFolderAction::cloneContents
            || !folder.containsSourceContent)
        {
            continue;
        }

        std::string error;
        if (!collectCopyEntriesForFolder(request.sourcePackageDirectory,
                                         request.targetPackageDirectory,
                                         folder.folderName,
                                         entries,
                                         error))
        {
            emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::failed, 100, 0, 0, 0, 0);
            return makeCopyResult(ProjectPackageSaveAsCopyStatus::unsupportedSourceEntry,
                                  std::move(error),
                                  std::move(plan));
        }
    }

    if (entries.empty())
    {
        emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::completed, 100, 0, 0, 0, 0);
        return makeCopyResult(ProjectPackageSaveAsCopyStatus::noCopyNeeded,
                              describeProjectPackageSaveAsPlan(plan),
                              std::move(plan));
    }

    const auto totalFiles = countFileEntries(entries);
    const auto totalBytes = countEntryBytes(entries);
    emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::preflight, 20, 0, totalFiles, 0, totalBytes);

    ProjectPackageSaveAsCopyStatus preflightStatus = ProjectPackageSaveAsCopyStatus::completed;
    std::string preflightError;
    if (!preflightCopyEntries(entries, preflightStatus, preflightError))
    {
        emitCopyProgress(request,
                         preflightStatus == ProjectPackageSaveAsCopyStatus::cancelled
                             ? ProjectPackageSaveAsCopyProgressStage::cancelled
                             : ProjectPackageSaveAsCopyProgressStage::failed,
                         preflightStatus == ProjectPackageSaveAsCopyStatus::cancelled ? 20 : 100,
                         0,
                         totalFiles,
                         0,
                         totalBytes);
        return makeCopyResult(preflightStatus, std::move(preflightError), std::move(plan));
    }

    if (copyWasCancelled(request.cancelRequested))
    {
        emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::cancelled, 20, 0, totalFiles, 0, totalBytes);
        return makeCopyResult(ProjectPackageSaveAsCopyStatus::cancelled,
                              "Save As package asset copy was cancelled before package mutation.",
                              std::move(plan));
    }

    emitCopyProgress(request, ProjectPackageSaveAsCopyProgressStage::copying, 25, 0, totalFiles, 0, totalBytes);
    auto result = copyEntries(std::move(plan), entries, request);
    if (result.error.empty())
        result.error = copyStatusPrefix(result.status);
    return result;
}
} // namespace projectname
