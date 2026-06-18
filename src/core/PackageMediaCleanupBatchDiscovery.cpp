// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PackageMediaCleanupBatchDiscovery.h"

#include "PackagePath.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] std::filesystem::path mediaTrashRootRelativePath()
{
    return std::filesystem::path("backups") / "media-trash";
}

[[nodiscard]] std::filesystem::path restoreManifestRelativePath(const std::string& cleanupId)
{
    return mediaTrashRootRelativePath() / cleanupId / "restore-manifest.json";
}

void addIssue(PackageMediaCleanupBatchDiscoveryResult& result,
              PackageMediaCleanupBatchDiscoveryIssueKind kind,
              std::string cleanupId,
              std::filesystem::path manifestRelativePath,
              std::filesystem::path manifestPath,
              std::string message)
{
    PackageMediaCleanupBatchDiscoveryIssue issue;
    issue.kind = kind;
    issue.cleanupId = std::move(cleanupId);
    issue.manifestRelativePath = std::move(manifestRelativePath);
    issue.manifestPath = std::move(manifestPath);
    issue.message = std::move(message);
    result.issues.push_back(std::move(issue));
}

[[nodiscard]] bool isRegularFile(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

void sortBatchesNewestFirst(std::vector<PackageMediaCleanupBatch>& batches)
{
    std::sort(batches.begin(),
              batches.end(),
              [](const PackageMediaCleanupBatch& left,
                 const PackageMediaCleanupBatch& right)
              {
                  if (left.createdAtUtc != right.createdAtUtc)
                      return left.createdAtUtc > right.createdAtUtc;

                  return left.cleanupId > right.cleanupId;
              });
}

void sortIssues(std::vector<PackageMediaCleanupBatchDiscoveryIssue>& issues)
{
    std::sort(issues.begin(),
              issues.end(),
              [](const PackageMediaCleanupBatchDiscoveryIssue& left,
                 const PackageMediaCleanupBatchDiscoveryIssue& right)
              {
                  if (left.cleanupId != right.cleanupId)
                      return left.cleanupId < right.cleanupId;

                  return left.message < right.message;
              });
}
} // namespace

PackageMediaCleanupBatchDiscoveryResult discoverPackageMediaCleanupBatches(
    const std::filesystem::path& packageDirectory)
{
    PackageMediaCleanupBatchDiscoveryResult result;
    const auto rootRelativePath = mediaTrashRootRelativePath();
    const auto rootPath = resolvePackagePath(packageDirectory, rootRelativePath);

    std::error_code error;
    const auto rootExists = std::filesystem::exists(rootPath, error);
    if (error)
    {
        result.error = "Could not inspect package media trash: " + error.message();
        addIssue(result,
                 PackageMediaCleanupBatchDiscoveryIssueKind::scanFailed,
                 {},
                 rootRelativePath,
                 rootPath,
                 result.error);
        return result;
    }

    if (!rootExists)
        return result;

    if (!std::filesystem::is_directory(rootPath, error))
    {
        if (error)
            result.error = "Could not inspect package media trash: " + error.message();
        else
            result.error = "Package media trash path is not a directory.";

        addIssue(result,
                 PackageMediaCleanupBatchDiscoveryIssueKind::scanFailed,
                 {},
                 rootRelativePath,
                 rootPath,
                 result.error);
        return result;
    }

    std::filesystem::directory_iterator iterator(rootPath,
                                                 std::filesystem::directory_options::skip_permission_denied,
                                                 error);
    if (error)
    {
        result.error = "Could not scan package media trash: " + error.message();
        addIssue(result,
                 PackageMediaCleanupBatchDiscoveryIssueKind::scanFailed,
                 {},
                 rootRelativePath,
                 rootPath,
                 result.error);
        return result;
    }

    const std::filesystem::directory_iterator end;
    for (; iterator != end; iterator.increment(error))
    {
        if (error)
        {
            addIssue(result,
                     PackageMediaCleanupBatchDiscoveryIssueKind::scanFailed,
                     {},
                     rootRelativePath,
                     rootPath,
                     "Could not continue scanning package media trash: " + error.message());
            error.clear();
            break;
        }

        std::error_code entryError;
        const auto isDirectory = iterator->is_directory(entryError);
        if (entryError)
        {
            addIssue(result,
                     PackageMediaCleanupBatchDiscoveryIssueKind::scanFailed,
                     {},
                     rootRelativePath,
                     iterator->path(),
                     "Could not inspect cleanup batch directory: " + entryError.message());
            continue;
        }

        if (!isDirectory)
            continue;

        const auto cleanupId = iterator->path().filename().string();
        const auto manifestRelativePath = restoreManifestRelativePath(cleanupId);
        const auto manifestPath = resolvePackagePath(packageDirectory, manifestRelativePath);

        if (!isValidPackageMediaQuarantineCleanupId(cleanupId))
        {
            addIssue(result,
                     PackageMediaCleanupBatchDiscoveryIssueKind::invalidCleanupId,
                     cleanupId,
                     manifestRelativePath,
                     manifestPath,
                     "Package media cleanup id is not filesystem-safe.");
            continue;
        }

        if (!isSafePackageRelativePath(manifestRelativePath))
        {
            addIssue(result,
                     PackageMediaCleanupBatchDiscoveryIssueKind::invalidManifestPath,
                     cleanupId,
                     manifestRelativePath,
                     manifestPath,
                     "Package media cleanup restore manifest path is not package-relative.");
            continue;
        }

        if (!isRegularFile(manifestPath))
        {
            addIssue(result,
                     PackageMediaCleanupBatchDiscoveryIssueKind::manifestMissing,
                     cleanupId,
                     manifestRelativePath,
                     manifestPath,
                     "Package media cleanup restore manifest is missing.");
            continue;
        }

        std::string loadError;
        auto manifest = loadPackageMediaQuarantineRestoreManifest(manifestPath, loadError);
        if (!manifest.has_value())
        {
            addIssue(result,
                     PackageMediaCleanupBatchDiscoveryIssueKind::manifestLoadFailed,
                     cleanupId,
                     manifestRelativePath,
                     manifestPath,
                     loadError.empty()
                         ? "Package media cleanup restore manifest could not be loaded."
                         : loadError);
            continue;
        }

        if (manifest->cleanupId != cleanupId)
        {
            addIssue(result,
                     PackageMediaCleanupBatchDiscoveryIssueKind::manifestCleanupIdMismatch,
                     cleanupId,
                     manifestRelativePath,
                     manifestPath,
                     "Package media cleanup restore manifest cleanup id does not match its directory.");
            continue;
        }

        PackageMediaCleanupBatch batch;
        batch.cleanupId = cleanupId;
        batch.createdAtUtc = manifest->createdAtUtc;
        batch.manifestRelativePath = manifestRelativePath;
        batch.manifestPath = manifestPath;
        batch.manifest = std::move(*manifest);
        batch.status = describePackageMediaCleanupRestoreManifest(batch.manifest);
        result.batches.push_back(std::move(batch));
    }

    sortBatchesNewestFirst(result.batches);
    sortIssues(result.issues);
    return result;
}
} // namespace projectname
