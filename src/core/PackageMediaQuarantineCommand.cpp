// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PackageMediaQuarantineCommand.h"

#include "PackagePath.h"

#include <algorithm>
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
} // namespace projectname
