// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PackageMediaQuarantinePreflightPlan.h"

#include "PackagePath.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] PackageMediaQuarantinePreflightResult makeError(
    PackageMediaQuarantinePreflightStatus status,
    std::string error)
{
    PackageMediaQuarantinePreflightResult result;
    result.status = status;
    result.error = std::move(error);
    return result;
}

[[nodiscard]] PackageMediaQuarantineEntryKind toQuarantineKind(
    ImportedMediaPackageAssetKind kind) noexcept
{
    return kind == ImportedMediaPackageAssetKind::audio
        ? PackageMediaQuarantineEntryKind::audio
        : PackageMediaQuarantineEntryKind::analysis;
}

[[nodiscard]] std::string quarantineGroup(PackageMediaQuarantineEntryKind kind)
{
    switch (kind)
    {
        case PackageMediaQuarantineEntryKind::audio:
            return "audio";
        case PackageMediaQuarantineEntryKind::analysis:
            return "analysis";
        case PackageMediaQuarantineEntryKind::stagingDirectory:
            return "staging";
    }

    return "audio";
}

[[nodiscard]] std::filesystem::path pathTailAfterRoot(const std::filesystem::path& path)
{
    auto iterator = path.begin();
    if (iterator == path.end())
        return {};

    ++iterator;

    std::filesystem::path tail;
    for (; iterator != path.end(); ++iterator)
        tail /= *iterator;

    return tail;
}

[[nodiscard]] bool isSafeNormalizedRelativePath(const std::filesystem::path& path)
{
    if (path.empty()
        || path.has_root_name()
        || path.has_root_directory()
        || path.is_absolute()
        || !isSafePackageRelativePath(path))
    {
        return false;
    }

    for (const auto& part : path)
    {
        if (part == ".")
            return false;
    }

    return path.lexically_normal() == path;
}

[[nodiscard]] std::optional<PackageMediaQuarantineMovedEntry> makeMovedEntry(
    PackageMediaQuarantineEntryKind kind,
    const std::string& cleanupId,
    const std::string& originalRelativePath,
    const std::filesystem::path& absolutePath,
    std::string& error)
{
    const auto originalPath = std::filesystem::path(originalRelativePath);
    if (!isSafeNormalizedRelativePath(originalPath))
    {
        error = "Quarantine preflight candidate path must be safe, normalized, and package-relative.";
        return std::nullopt;
    }

    auto tail = pathTailAfterRoot(originalPath);
    if (tail.empty())
    {
        error = "Quarantine preflight candidate path must include a name below its package folder.";
        return std::nullopt;
    }

    PackageMediaQuarantineMovedEntry entry;
    entry.kind = kind;
    entry.originalRelativePath = originalPath.generic_string();
    entry.quarantineRelativePath =
        (std::filesystem::path("backups")
         / "media-trash"
         / cleanupId
         / quarantineGroup(kind)
         / tail).generic_string();

    if (kind != PackageMediaQuarantineEntryKind::stagingDirectory
        && !absolutePath.empty())
    {
        std::error_code filesystemError;
        const auto byteSize = std::filesystem::file_size(absolutePath, filesystemError);
        if (!filesystemError)
            entry.byteSize = byteSize;
    }

    return entry;
}

[[nodiscard]] bool containsRequestedPath(const std::vector<std::string>& requestedPaths,
                                         const std::string& relativePath)
{
    return std::find(requestedPaths.begin(), requestedPaths.end(), relativePath)
        != requestedPaths.end();
}

[[nodiscard]] bool hasRequestedPaths(const PackageMediaQuarantinePreflightRequest& request)
{
    return !request.requestedOriginalRelativePaths.empty();
}

[[nodiscard]] bool rejectDuplicateRequestedPaths(
    const PackageMediaQuarantinePreflightRequest& request,
    PackageMediaQuarantinePreflightResult& result)
{
    std::set<std::string> seen;
    for (const auto& path : request.requestedOriginalRelativePaths)
    {
        if (!seen.insert(path).second)
        {
            result = makeError(PackageMediaQuarantinePreflightStatus::duplicateQuarantineDestination,
                               "Quarantine preflight requested the same destination more than once.");
            return true;
        }
    }

    return false;
}

[[nodiscard]] const ImportedMediaPackageAsset* findAsset(
    const ImportedMediaPackageInventory& inventory,
    const std::string& relativePath)
{
    for (const auto& asset : inventory.assets)
    {
        if (asset.relativePath == relativePath)
            return &asset;
    }

    return nullptr;
}

[[nodiscard]] bool requestedMissingOrUnsafePath(
    const PackageMediaQuarantinePreflightRequest& request,
    std::string& path)
{
    if (!hasRequestedPaths(request))
        return false;

    for (const auto& requested : request.requestedOriginalRelativePaths)
    {
        if (findAsset(request.inventory, requested) != nullptr)
            continue;

        const auto stagingMatch = std::find_if(
            request.inventory.stagingDirectories.begin(),
            request.inventory.stagingDirectories.end(),
            [&requested](const auto& staging)
            {
                return staging.relativePath == requested;
            });
        if (stagingMatch != request.inventory.stagingDirectories.end())
            continue;

        path = requested;
        return true;
    }

    return false;
}

void addSkippedProtectedEntries(PackageMediaQuarantineRestoreManifest& manifest,
                                const ImportedMediaPackageInventory& inventory)
{
    for (const auto& asset : inventory.assets)
    {
        if (asset.unreferencedCandidate)
            continue;

        PackageMediaQuarantineSkippedEntry skipped;
        skipped.kind = toQuarantineKind(asset.kind);
        skipped.originalRelativePath = asset.relativePath;
        skipped.reason = "protected-reference";
        skipped.detail = "Protected by manifest, backup, or session reference.";
        manifest.skippedEntries.push_back(std::move(skipped));
    }

    for (const auto& missing : inventory.missingReferences)
    {
        PackageMediaQuarantineSkippedEntry skipped;
        skipped.kind = toQuarantineKind(missing.kind);
        skipped.originalRelativePath = missing.relativePath;
        skipped.reason = "missing-reference";
        skipped.detail = "Referenced package media is missing.";
        manifest.skippedEntries.push_back(std::move(skipped));
    }

    for (const auto& unsafe : inventory.unsafeReferences)
    {
        PackageMediaQuarantineSkippedEntry skipped;
        skipped.kind = toQuarantineKind(unsafe.kind);
        skipped.originalRelativePath = unsafe.relativePath.empty() ? "audio/unknown" : unsafe.relativePath;
        skipped.reason = "unsafe-reference";
        skipped.detail = unsafe.reason;
        manifest.skippedEntries.push_back(std::move(skipped));
    }
}

[[nodiscard]] std::string makeInventorySummary(const ImportedMediaPackageInventory& inventory)
{
    return "assets=" + std::to_string(inventory.assets.size())
        + ", stagingDirectories=" + std::to_string(inventory.stagingDirectories.size())
        + ", missingReferences=" + std::to_string(inventory.missingReferences.size())
        + ", unsafeReferences=" + std::to_string(inventory.unsafeReferences.size());
}
} // namespace

PackageMediaQuarantinePreflightResult buildPackageMediaQuarantinePreflightPlan(
    PackageMediaQuarantinePreflightRequest request)
{
    if (!isValidPackageMediaQuarantineCleanupId(request.cleanupId))
    {
        return makeError(PackageMediaQuarantinePreflightStatus::invalidCleanupId,
                         "Quarantine preflight cleanup id is invalid.");
    }

    if (request.packageWorkInProgress)
    {
        return makeError(PackageMediaQuarantinePreflightStatus::activePackageWork,
                         "Quarantine preflight cannot run while package work is active.");
    }

    for (const auto& staging : request.inventory.stagingDirectories)
    {
        if (!staging.staleCandidate)
        {
            return makeError(PackageMediaQuarantinePreflightStatus::activePackageWork,
                             "Quarantine preflight found active staging directories.");
        }
    }

    if (!request.inventory.error.empty())
    {
        return makeError(PackageMediaQuarantinePreflightStatus::inventoryError,
                         request.inventory.error);
    }

    if (!request.inventory.unsafeReferences.empty())
    {
        return makeError(PackageMediaQuarantinePreflightStatus::unsafeReferences,
                         "Quarantine preflight requires unsafe references to be resolved first.");
    }

    if (!request.inventory.missingReferences.empty())
    {
        return makeError(PackageMediaQuarantinePreflightStatus::missingReferences,
                         "Quarantine preflight requires missing references to be resolved first.");
    }

    PackageMediaQuarantinePreflightResult duplicateResult;
    if (rejectDuplicateRequestedPaths(request, duplicateResult))
        return duplicateResult;

    std::string unknownRequestedPath;
    if (requestedMissingOrUnsafePath(request, unknownRequestedPath))
    {
        return makeError(PackageMediaQuarantinePreflightStatus::missingReferences,
                         "Quarantine preflight requested an unknown package path: "
                             + unknownRequestedPath);
    }

    PackageMediaQuarantineRestoreManifest manifest;
    manifest.cleanupId = request.cleanupId;
    manifest.createdAtUtc = request.createdAtUtc;
    manifest.packageDisplayPath = request.packageDisplayPath;
    manifest.inventorySummary = makeInventorySummary(request.inventory);
    manifest.manifestMarker = request.manifestMarker;
    addSkippedProtectedEntries(manifest, request.inventory);

    for (const auto& asset : request.inventory.assets)
    {
        if (hasRequestedPaths(request)
            && !containsRequestedPath(request.requestedOriginalRelativePaths, asset.relativePath))
        {
            continue;
        }

        if (!asset.unreferencedCandidate)
        {
            if (!hasRequestedPaths(request))
                continue;

            return makeError(PackageMediaQuarantinePreflightStatus::protectedReference,
                             "Quarantine preflight requested a protected package asset: "
                                 + asset.relativePath);
        }

        std::string error;
        auto movedEntry = makeMovedEntry(toQuarantineKind(asset.kind),
                                         request.cleanupId,
                                         asset.relativePath,
                                         asset.absolutePath,
                                         error);
        if (!movedEntry.has_value())
            return makeError(PackageMediaQuarantinePreflightStatus::invalidInventoryPath, error);

        manifest.movedEntries.push_back(std::move(*movedEntry));
    }

    for (const auto& staging : request.inventory.stagingDirectories)
    {
        if (hasRequestedPaths(request)
            && !containsRequestedPath(request.requestedOriginalRelativePaths, staging.relativePath))
        {
            continue;
        }

        if (!staging.staleCandidate)
        {
            return makeError(PackageMediaQuarantinePreflightStatus::activePackageWork,
                             "Quarantine preflight requested active staging.");
        }

        std::string error;
        auto movedEntry = makeMovedEntry(PackageMediaQuarantineEntryKind::stagingDirectory,
                                         request.cleanupId,
                                         staging.relativePath,
                                         {},
                                         error);
        if (!movedEntry.has_value())
            return makeError(PackageMediaQuarantinePreflightStatus::invalidInventoryPath, error);

        manifest.movedEntries.push_back(std::move(*movedEntry));
    }

    if (manifest.movedEntries.empty())
    {
        return makeError(PackageMediaQuarantinePreflightStatus::noMovableCandidates,
                         "Quarantine preflight found no movable candidates.");
    }

    std::string validationError;
    if (!validatePackageMediaQuarantineRestoreManifest(manifest, validationError))
    {
        const auto status = validationError.find("duplicate quarantine paths") != std::string::npos
            ? PackageMediaQuarantinePreflightStatus::duplicateQuarantineDestination
            : PackageMediaQuarantinePreflightStatus::invalidInventoryPath;
        return makeError(status, validationError);
    }

    PackageMediaQuarantinePreflightResult result;
    result.status = PackageMediaQuarantinePreflightStatus::planned;
    result.restoreManifest = std::move(manifest);
    return result;
}
} // namespace projectname
