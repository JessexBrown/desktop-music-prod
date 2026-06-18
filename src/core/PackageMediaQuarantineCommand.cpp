// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PackageMediaQuarantineCommand.h"

#include "PackagePath.h"

#include <algorithm>
#include <set>
#include <system_error>
#include <utility>

namespace projectname
{
namespace
{
struct MoveFailure
{
    PackageMediaQuarantineCommandStatus status =
        PackageMediaQuarantineCommandStatus::moveFailed;
    std::string error;
};

[[nodiscard]] PackageMediaQuarantineCommandResult makeResult(
    PackageMediaQuarantineCommandStatus status,
    std::string error,
    std::filesystem::path manifestPath,
    std::filesystem::path temporaryManifestPath)
{
    PackageMediaQuarantineCommandResult result;
    result.status = status;
    result.error = std::move(error);
    result.restoreManifestPath = std::move(manifestPath);
    result.temporaryRestoreManifestPath = std::move(temporaryManifestPath);
    return result;
}

[[nodiscard]] std::filesystem::path restoreManifestDirectory(
    const std::filesystem::path& packageDirectory,
    const std::string& cleanupId)
{
    return packageDirectory / "backups" / "media-trash" / cleanupId;
}

[[nodiscard]] std::filesystem::path restoreManifestPath(
    const std::filesystem::path& packageDirectory,
    const std::string& cleanupId)
{
    return restoreManifestDirectory(packageDirectory, cleanupId) / "restore-manifest.json";
}

[[nodiscard]] std::filesystem::path temporaryRestoreManifestPath(
    const std::filesystem::path& packageDirectory,
    const std::string& cleanupId)
{
    return restoreManifestDirectory(packageDirectory, cleanupId) / "restore-manifest.json.tmp";
}

[[nodiscard]] bool sourceExistsForEntry(const std::filesystem::path& source,
                                        PackageMediaQuarantineEntryKind kind,
                                        std::error_code& filesystemError)
{
    filesystemError.clear();

    if (kind == PackageMediaQuarantineEntryKind::stagingDirectory)
        return std::filesystem::is_directory(source, filesystemError);

    return std::filesystem::is_regular_file(source, filesystemError);
}

[[nodiscard]] bool manifestPathIsPackageLocal(const std::filesystem::path& packageDirectory,
                                              const std::filesystem::path& manifestPath)
{
    const auto relative = manifestPath.lexically_relative(packageDirectory);
    if (relative.empty() || !isSafePackageRelativePath(relative))
        return false;

    auto part = relative.begin();
    if (part == relative.end() || *part != "backups")
        return false;

    ++part;
    if (part == relative.end() || *part != "media-trash")
        return false;

    ++part;
    if (part == relative.end())
        return false;

    ++part;
    return part != relative.end()
        && *part == "restore-manifest.json"
        && ++part == relative.end();
}

[[nodiscard]] std::optional<MoveFailure> moveEntryToQuarantine(
    const std::filesystem::path& packageDirectory,
    const PackageMediaQuarantineMovedEntry& entry)
{
    std::error_code filesystemError;
    const auto source = resolvePackagePath(packageDirectory, entry.originalRelativePath);
    if (!sourceExistsForEntry(source, entry.kind, filesystemError))
    {
        MoveFailure failure;
        failure.status = PackageMediaQuarantineCommandStatus::sourceMissing;
        failure.error = "Quarantine source is missing: " + entry.originalRelativePath + ".";
        return failure;
    }

    const auto destination = resolvePackagePath(packageDirectory, entry.quarantineRelativePath);
    if (std::filesystem::exists(destination, filesystemError))
    {
        MoveFailure failure;
        failure.status = PackageMediaQuarantineCommandStatus::destinationOccupied;
        failure.error = "Quarantine destination already exists: "
            + entry.quarantineRelativePath + ".";
        return failure;
    }

    filesystemError.clear();
    std::filesystem::create_directories(destination.parent_path(), filesystemError);
    if (filesystemError)
    {
        MoveFailure failure;
        failure.error = "Could not create quarantine destination directory: "
            + filesystemError.message() + ".";
        return failure;
    }

    std::filesystem::rename(source, destination, filesystemError);
    if (filesystemError)
    {
        MoveFailure failure;
        failure.error = "Could not move package media into quarantine: "
            + filesystemError.message() + ".";
        return failure;
    }

    return std::nullopt;
}

[[nodiscard]] bool rollbackMovedEntries(
    const std::filesystem::path& packageDirectory,
    PackageMediaQuarantineRestoreManifest& manifest,
    std::size_t movedCount,
    std::string& rollbackError)
{
    auto restoredAll = true;

    for (auto index = movedCount; index > 0; --index)
    {
        auto& entry = manifest.movedEntries[index - 1];
        const auto original = resolvePackagePath(packageDirectory, entry.originalRelativePath);
        const auto quarantine = resolvePackagePath(packageDirectory, entry.quarantineRelativePath);

        std::error_code filesystemError;
        if (std::filesystem::exists(original, filesystemError))
        {
            entry.error = "Rollback could not restore because the original path is occupied.";
            rollbackError = entry.error;
            restoredAll = false;
            continue;
        }

        filesystemError.clear();
        if (!std::filesystem::exists(quarantine, filesystemError))
        {
            entry.error = "Rollback could not restore because the quarantine path is missing.";
            rollbackError = entry.error;
            restoredAll = false;
            continue;
        }

        filesystemError.clear();
        std::filesystem::create_directories(original.parent_path(), filesystemError);
        if (filesystemError)
        {
            entry.error = "Rollback could not create original parent directory: "
                + filesystemError.message() + ".";
            rollbackError = entry.error;
            restoredAll = false;
            continue;
        }

        filesystemError.clear();
        std::filesystem::rename(quarantine, original, filesystemError);
        if (filesystemError)
        {
            entry.error = "Rollback could not restore original path: "
                + filesystemError.message() + ".";
            rollbackError = entry.error;
            restoredAll = false;
            continue;
        }

        entry.restored = true;
    }

    return restoredAll;
}

[[nodiscard]] bool writePartialFailureManifest(
    const std::filesystem::path& temporaryManifestPath,
    const std::filesystem::path& finalManifestPath,
    const PackageMediaQuarantineRestoreManifest& manifest,
    std::string& error)
{
    std::error_code filesystemError;
    std::filesystem::create_directories(temporaryManifestPath.parent_path(), filesystemError);
    if (filesystemError)
    {
        error = "Could not create partial quarantine manifest directory: "
            + filesystemError.message() + ".";
        return false;
    }

    if (!savePackageMediaQuarantineRestoreManifest(manifest, temporaryManifestPath, error))
        return false;

    filesystemError.clear();
    std::filesystem::rename(temporaryManifestPath, finalManifestPath, filesystemError);
    if (filesystemError)
    {
        error = "Could not commit partial quarantine manifest: "
            + filesystemError.message() + ".";
        return false;
    }

    return true;
}

[[nodiscard]] PackageMediaQuarantineCommandResult handleMoveFailure(
    const std::filesystem::path& packageDirectory,
    PackageMediaQuarantineRestoreManifest manifest,
    std::size_t movedCount,
    MoveFailure failure,
    const std::filesystem::path& finalManifestPath,
    const std::filesystem::path& temporaryManifestPath)
{
    if (movedCount < manifest.movedEntries.size())
        manifest.movedEntries[movedCount].error = failure.error;

    std::string rollbackError;
    const auto rollbackSucceeded = rollbackMovedEntries(packageDirectory,
                                                        manifest,
                                                        movedCount,
                                                        rollbackError);

    if (rollbackSucceeded)
    {
        std::error_code ignored;
        std::filesystem::remove(temporaryManifestPath, ignored);
        return makeResult(failure.status,
                          std::move(failure.error),
                          finalManifestPath,
                          temporaryManifestPath);
    }

    manifest.state = PackageMediaQuarantineManifestState::partialFailure;
    manifest.error = failure.error + " Rollback was incomplete: " + rollbackError;

    std::string manifestError;
    if (!writePartialFailureManifest(temporaryManifestPath,
                                     finalManifestPath,
                                     manifest,
                                     manifestError))
    {
        return makeResult(PackageMediaQuarantineCommandStatus::rollbackFailed,
                          manifest.error + " Partial manifest could not be written: "
                              + manifestError,
                          finalManifestPath,
                          temporaryManifestPath);
    }

    auto result = makeResult(PackageMediaQuarantineCommandStatus::rollbackFailed,
                             manifest.error,
                             finalManifestPath,
                             temporaryManifestPath);
    result.restoreManifest = std::move(manifest);
    return result;
}

[[nodiscard]] bool commitTemporaryManifest(const std::filesystem::path& temporaryManifestPath,
                                           const std::filesystem::path& finalManifestPath,
                                           std::string& error)
{
    std::error_code filesystemError;
    std::filesystem::rename(temporaryManifestPath, finalManifestPath, filesystemError);
    if (!filesystemError)
        return true;

    error = "Could not commit quarantine restore manifest: "
        + filesystemError.message() + ".";
    return false;
}

[[nodiscard]] std::filesystem::path restoreCommandTemporaryManifestPath(
    const std::filesystem::path& manifestPath)
{
    auto temporaryPath = manifestPath;
    temporaryPath += ".tmp";
    return temporaryPath;
}

[[nodiscard]] PackageMediaQuarantineRestoreCommandResult makeRestoreResult(
    PackageMediaQuarantineRestoreCommandStatus status,
    std::string error,
    std::filesystem::path manifestPath,
    std::filesystem::path temporaryManifestPath)
{
    PackageMediaQuarantineRestoreCommandResult result;
    result.status = status;
    result.error = std::move(error);
    result.restoreManifestPath = std::move(manifestPath);
    result.temporaryRestoreManifestPath = std::move(temporaryManifestPath);
    return result;
}

[[nodiscard]] bool hasSelection(const PackageMediaQuarantineRestoreCommandRequest& request)
{
    return !request.selectedOriginalRelativePaths.empty();
}

[[nodiscard]] bool selectedForRestore(const PackageMediaQuarantineRestoreCommandRequest& request,
                                      const PackageMediaQuarantineMovedEntry& entry)
{
    if (!hasSelection(request))
        return true;

    return std::find(request.selectedOriginalRelativePaths.begin(),
                     request.selectedOriginalRelativePaths.end(),
                     entry.originalRelativePath)
        != request.selectedOriginalRelativePaths.end();
}

[[nodiscard]] std::optional<std::string> validateRestoreSelection(
    const PackageMediaQuarantineRestoreCommandRequest& request,
    const PackageMediaQuarantineRestoreManifest& manifest)
{
    std::set<std::string> seen;
    for (const auto& selected : request.selectedOriginalRelativePaths)
    {
        if (!seen.insert(selected).second)
            return "Restore command selected the same original path more than once: " + selected + ".";

        const auto found = std::any_of(
            manifest.movedEntries.begin(),
            manifest.movedEntries.end(),
            [&selected](const auto& entry)
            {
                return entry.originalRelativePath == selected;
            });

        if (!found)
            return "Restore command selected an unknown original path: " + selected + ".";
    }

    return std::nullopt;
}

[[nodiscard]] bool writeUpdatedRestoreManifest(const PackageMediaQuarantineRestoreManifest& manifest,
                                               const std::filesystem::path& manifestPath,
                                               const std::filesystem::path& temporaryManifestPath,
                                               std::string& error)
{
    std::error_code filesystemError;
    std::filesystem::remove(temporaryManifestPath, filesystemError);
    filesystemError.clear();

    if (!savePackageMediaQuarantineRestoreManifest(manifest, temporaryManifestPath, error))
        return false;

    std::filesystem::copy_file(temporaryManifestPath,
                               manifestPath,
                               std::filesystem::copy_options::overwrite_existing,
                               filesystemError);
    if (filesystemError)
    {
        error = "Could not commit updated restore manifest: "
            + filesystemError.message() + ".";
        std::filesystem::remove(temporaryManifestPath, filesystemError);
        return false;
    }

    filesystemError.clear();
    std::filesystem::remove(temporaryManifestPath, filesystemError);
    if (filesystemError)
    {
        error = "Could not remove temporary restore manifest: "
            + filesystemError.message() + ".";
        return false;
    }

    return true;
}

[[nodiscard]] PackageMediaQuarantineRestoreCommandStatus chooseRestoreStatus(
    const PackageMediaQuarantineRestoreCommandResult& result,
    bool hasMoveFailure)
{
    if (hasMoveFailure)
        return PackageMediaQuarantineRestoreCommandStatus::moveFailed;

    if (result.missingCount > 0)
        return PackageMediaQuarantineRestoreCommandStatus::missingQuarantinePath;

    if (result.conflictCount > 0)
        return PackageMediaQuarantineRestoreCommandStatus::restoreConflict;

    return PackageMediaQuarantineRestoreCommandStatus::restored;
}

void updateRestoreManifestState(PackageMediaQuarantineRestoreManifest& manifest,
                                PackageMediaQuarantineRestoreCommandStatus status)
{
    if (status == PackageMediaQuarantineRestoreCommandStatus::missingQuarantinePath
        || status == PackageMediaQuarantineRestoreCommandStatus::moveFailed)
    {
        manifest.state = PackageMediaQuarantineManifestState::partialFailure;
        manifest.error = "One or more quarantine entries could not be restored.";
        return;
    }

    if (status == PackageMediaQuarantineRestoreCommandStatus::restoreConflict)
    {
        manifest.state = PackageMediaQuarantineManifestState::restoreConflict;
        manifest.error = "One or more quarantine entries could not be restored because original paths are occupied.";
        return;
    }

    const auto allRestored = std::all_of(
        manifest.movedEntries.begin(),
        manifest.movedEntries.end(),
        [](const auto& entry)
        {
            return entry.restored;
        });

    manifest.state = allRestored
        ? PackageMediaQuarantineManifestState::restored
        : PackageMediaQuarantineManifestState::completed;
    manifest.error.clear();
}
} // namespace

PackageMediaQuarantineCommandResult quarantinePackageMedia(
    PackageMediaQuarantineCommandRequest request)
{
    const auto finalManifestPath =
        restoreManifestPath(request.packageDirectory, request.restoreManifestDraft.cleanupId);
    const auto temporaryManifestPath =
        temporaryRestoreManifestPath(request.packageDirectory, request.restoreManifestDraft.cleanupId);

    std::string validationError;
    if (!validatePackageMediaQuarantineRestoreManifest(request.restoreManifestDraft, validationError)
        || request.restoreManifestDraft.state != PackageMediaQuarantineManifestState::completed
        || request.restoreManifestDraft.movedEntries.empty())
    {
        return makeResult(PackageMediaQuarantineCommandStatus::invalidRequest,
                          validationError.empty()
                              ? "Quarantine command requires a completed restore-manifest draft with moved entries."
                              : validationError,
                          finalManifestPath,
                          temporaryManifestPath);
    }

    std::error_code filesystemError;
    if (std::filesystem::exists(finalManifestPath, filesystemError))
    {
        return makeResult(PackageMediaQuarantineCommandStatus::destinationOccupied,
                          "Quarantine restore manifest already exists for cleanup id: "
                              + request.restoreManifestDraft.cleanupId + ".",
                          finalManifestPath,
                          temporaryManifestPath);
    }

    filesystemError.clear();
    std::filesystem::create_directories(temporaryManifestPath.parent_path(), filesystemError);
    if (filesystemError)
    {
        return makeResult(PackageMediaQuarantineCommandStatus::manifestWriteFailed,
                          "Could not create quarantine manifest directory: "
                              + filesystemError.message() + ".",
                          finalManifestPath,
                          temporaryManifestPath);
    }

    std::filesystem::remove(temporaryManifestPath, filesystemError);
    filesystemError.clear();
    if (!savePackageMediaQuarantineRestoreManifest(request.restoreManifestDraft,
                                                   temporaryManifestPath,
                                                   validationError))
    {
        return makeResult(PackageMediaQuarantineCommandStatus::manifestWriteFailed,
                          validationError,
                          finalManifestPath,
                          temporaryManifestPath);
    }

    auto movedCount = std::size_t {};
    for (const auto& entry : request.restoreManifestDraft.movedEntries)
    {
        if (auto failure = moveEntryToQuarantine(request.packageDirectory, entry))
        {
            return handleMoveFailure(request.packageDirectory,
                                     std::move(request.restoreManifestDraft),
                                     movedCount,
                                     std::move(*failure),
                                     finalManifestPath,
                                     temporaryManifestPath);
        }

        ++movedCount;
    }

    if (!commitTemporaryManifest(temporaryManifestPath, finalManifestPath, validationError))
    {
        MoveFailure failure;
        failure.status = PackageMediaQuarantineCommandStatus::manifestCommitFailed;
        failure.error = validationError;
        return handleMoveFailure(request.packageDirectory,
                                 std::move(request.restoreManifestDraft),
                                 movedCount,
                                 std::move(failure),
                                 finalManifestPath,
                                 temporaryManifestPath);
    }

    PackageMediaQuarantineCommandResult result;
    result.status = PackageMediaQuarantineCommandStatus::completed;
    result.restoreManifestPath = finalManifestPath;
    result.temporaryRestoreManifestPath = temporaryManifestPath;
    result.restoreManifest = std::move(request.restoreManifestDraft);
    return result;
}

PackageMediaQuarantineRestoreCommandResult restorePackageMediaFromQuarantine(
    PackageMediaQuarantineRestoreCommandRequest request)
{
    const auto temporaryManifestPath =
        restoreCommandTemporaryManifestPath(request.restoreManifestPath);

    if (request.packageDirectory.empty()
        || request.restoreManifestPath.empty()
        || !manifestPathIsPackageLocal(request.packageDirectory, request.restoreManifestPath))
    {
        return makeRestoreResult(PackageMediaQuarantineRestoreCommandStatus::invalidRequest,
                                 "Restore command requires a package-local restore manifest path.",
                                 request.restoreManifestPath,
                                 temporaryManifestPath);
    }

    std::string error;
    auto manifest = loadPackageMediaQuarantineRestoreManifest(request.restoreManifestPath, error);
    if (!manifest.has_value())
    {
        return makeRestoreResult(PackageMediaQuarantineRestoreCommandStatus::manifestLoadFailed,
                                 error,
                                 request.restoreManifestPath,
                                 temporaryManifestPath);
    }

    if (auto selectionError = validateRestoreSelection(request, *manifest))
    {
        return makeRestoreResult(PackageMediaQuarantineRestoreCommandStatus::invalidRequest,
                                 *selectionError,
                                 request.restoreManifestPath,
                                 temporaryManifestPath);
    }

    PackageMediaQuarantineRestoreCommandResult result;
    result.restoreManifestPath = request.restoreManifestPath;
    result.temporaryRestoreManifestPath = temporaryManifestPath;

    auto hasMoveFailure = false;
    for (auto& entry : manifest->movedEntries)
    {
        if (!selectedForRestore(request, entry) || entry.restored)
            continue;

        entry.error.clear();
        entry.restoreConflict = false;

        const auto original = resolvePackagePath(request.packageDirectory, entry.originalRelativePath);
        const auto quarantine = resolvePackagePath(request.packageDirectory, entry.quarantineRelativePath);

        std::error_code filesystemError;
        if (std::filesystem::exists(original, filesystemError))
        {
            entry.restoreConflict = true;
            entry.error = "Original path is occupied.";
            ++result.conflictCount;
            continue;
        }

        filesystemError.clear();
        if (!sourceExistsForEntry(quarantine, entry.kind, filesystemError))
        {
            entry.error = "Quarantine path is missing.";
            ++result.missingCount;
            continue;
        }

        filesystemError.clear();
        std::filesystem::create_directories(original.parent_path(), filesystemError);
        if (filesystemError)
        {
            entry.error = "Could not create restore destination directory: "
                + filesystemError.message() + ".";
            hasMoveFailure = true;
            continue;
        }

        filesystemError.clear();
        std::filesystem::rename(quarantine, original, filesystemError);
        if (filesystemError)
        {
            entry.error = "Could not restore quarantine entry: "
                + filesystemError.message() + ".";
            hasMoveFailure = true;
            continue;
        }

        entry.restored = true;
        ++result.restoredCount;
    }

    result.status = chooseRestoreStatus(result, hasMoveFailure);
    updateRestoreManifestState(*manifest, result.status);

    if (!writeUpdatedRestoreManifest(*manifest,
                                     request.restoreManifestPath,
                                     temporaryManifestPath,
                                     error))
    {
        result.status = error.find("commit") != std::string::npos
            ? PackageMediaQuarantineRestoreCommandStatus::manifestCommitFailed
            : PackageMediaQuarantineRestoreCommandStatus::manifestWriteFailed;
        result.error = error;
        result.restoreManifest = std::move(manifest);
        return result;
    }

    if (result.status == PackageMediaQuarantineRestoreCommandStatus::restored)
        result.error.clear();
    else if (result.status == PackageMediaQuarantineRestoreCommandStatus::restoreConflict)
        result.error = "One or more quarantine entries could not be restored because original paths are occupied.";
    else if (result.status == PackageMediaQuarantineRestoreCommandStatus::missingQuarantinePath)
        result.error = "One or more quarantine entries could not be restored because quarantine paths are missing.";
    else if (result.status == PackageMediaQuarantineRestoreCommandStatus::moveFailed)
        result.error = "One or more quarantine entries could not be restored.";

    result.restoreManifest = std::move(manifest);
    return result;
}
} // namespace projectname
