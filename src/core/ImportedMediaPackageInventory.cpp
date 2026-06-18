// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ImportedMediaPackageInventory.h"

#include "PackagePath.h"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace projectname
{
namespace
{
struct ValidReference
{
    ImportedMediaPackageAssetKind kind = ImportedMediaPackageAssetKind::audio;
    ImportedMediaPackageReferenceSource source = ImportedMediaPackageReferenceSource::currentManifest;
    std::string relativePath;
};

[[nodiscard]] const char* expectedFolder(ImportedMediaPackageAssetKind kind) noexcept
{
    return kind == ImportedMediaPackageAssetKind::audio ? "audio" : "analysis";
}

[[nodiscard]] bool hasSource(const std::vector<ImportedMediaPackageReferenceSource>& sources,
                             ImportedMediaPackageReferenceSource source)
{
    return std::find(sources.begin(), sources.end(), source) != sources.end();
}

void addSource(std::vector<ImportedMediaPackageReferenceSource>& sources,
               ImportedMediaPackageReferenceSource source)
{
    if (!hasSource(sources, source))
        sources.push_back(source);
}

[[nodiscard]] bool isExpectedFolderPath(const std::filesystem::path& path,
                                        ImportedMediaPackageAssetKind kind)
{
    auto part = path.begin();
    return part != path.end() && *part == expectedFolder(kind);
}

[[nodiscard]] bool isSafeImportedMediaPath(const std::filesystem::path& path,
                                           ImportedMediaPackageAssetKind kind,
                                           std::string& reason)
{
    if (path.empty())
    {
        reason = "Imported media path is empty.";
        return false;
    }

    if (path.has_root_name() || path.has_root_directory() || path.is_absolute())
    {
        reason = "Imported media path must be package-relative.";
        return false;
    }

    if (!isSafePackageRelativePath(path))
    {
        reason = "Imported media path must not escape the project package.";
        return false;
    }

    for (const auto& part : path)
    {
        if (part == ".")
        {
            reason = "Imported media path must be normalized.";
            return false;
        }
    }

    if (path.lexically_normal() != path)
    {
        reason = "Imported media path must be normalized.";
        return false;
    }

    if (!isExpectedFolderPath(path, kind))
    {
        reason = std::string("Imported media path must stay under ")
            + expectedFolder(kind) + "/.";
        return false;
    }

    return true;
}

void addUnsafeReference(ImportedMediaPackageInventory& inventory,
                        ImportedMediaPackageAssetKind kind,
                        ImportedMediaPackageReferenceSource source,
                        std::string relativePath,
                        std::string reason)
{
    ImportedMediaPackageUnsafeReference reference;
    reference.kind = kind;
    reference.source = source;
    reference.relativePath = std::move(relativePath);
    reference.reason = std::move(reason);
    inventory.unsafeReferences.push_back(std::move(reference));
}

void addValidReference(std::vector<ValidReference>& references,
                       ImportedMediaPackageAssetKind kind,
                       ImportedMediaPackageReferenceSource source,
                       std::string relativePath)
{
    for (auto& reference : references)
    {
        if (reference.kind == kind
            && reference.source == source
            && reference.relativePath == relativePath)
        {
            return;
        }
    }

    references.push_back(ValidReference { kind, source, std::move(relativePath) });
}

void collectReferencePath(std::vector<ValidReference>& references,
                          ImportedMediaPackageInventory& inventory,
                          ImportedMediaPackageAssetKind kind,
                          ImportedMediaPackageReferenceSource source,
                          const std::string& rawPath)
{
    const auto path = std::filesystem::path(rawPath);
    std::string reason;
    if (!isSafeImportedMediaPath(path, kind, reason))
    {
        addUnsafeReference(inventory, kind, source, rawPath, std::move(reason));
        return;
    }

    addValidReference(references,
                      kind,
                      source,
                      path.lexically_normal().generic_string());
}

void collectManifestReferences(const nlohmann::json& manifest,
                               ImportedMediaPackageReferenceSource source,
                               std::vector<ValidReference>& references,
                               ImportedMediaPackageInventory& inventory)
{
    const auto tracks = manifest.find("tracks");
    if (tracks == manifest.end() || !tracks->is_array())
        return;

    for (const auto& track : *tracks)
    {
        if (!track.is_object())
            continue;

        const auto clips = track.find("clips");
        if (clips == track.end() || !clips->is_array())
            continue;

        for (const auto& clip : *clips)
        {
            if (!clip.is_object() || clip.value("type", "") != "audio-file")
                continue;

            const auto relativePath = clip.find("relativePath");
            if (relativePath != clip.end() && relativePath->is_string())
            {
                collectReferencePath(references,
                                     inventory,
                                     ImportedMediaPackageAssetKind::audio,
                                     source,
                                     relativePath->get<std::string>());
            }
            else
            {
                addUnsafeReference(inventory,
                                   ImportedMediaPackageAssetKind::audio,
                                   source,
                                   {},
                                   "Imported audio clip is missing a media path.");
            }

            const auto analysisPath = clip.find("analysisPath");
            if (analysisPath != clip.end()
                && analysisPath->is_string()
                && !analysisPath->get<std::string>().empty())
            {
                collectReferencePath(references,
                                     inventory,
                                     ImportedMediaPackageAssetKind::analysis,
                                     source,
                                     analysisPath->get<std::string>());
            }
        }
    }
}

bool collectManifestFileReferences(const std::filesystem::path& manifestPath,
                                   ImportedMediaPackageReferenceSource source,
                                   bool required,
                                   bool& readFlag,
                                   std::vector<ValidReference>& references,
                                   ImportedMediaPackageInventory& inventory)
{
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(manifestPath, filesystemError))
    {
        if (required)
            inventory.error = "Project manifest was not found.";
        return false;
    }

    std::ifstream manifestFile(manifestPath);
    if (!manifestFile)
    {
        if (required || inventory.error.empty())
            inventory.error = "Project manifest could not be opened.";
        return false;
    }

    try
    {
        const auto manifest = nlohmann::json::parse(manifestFile);
        collectManifestReferences(manifest, source, references, inventory);
        readFlag = true;
        return true;
    }
    catch (const nlohmann::json::exception& exception)
    {
        if (required || inventory.error.empty())
            inventory.error = std::string("Project manifest is not valid JSON: ") + exception.what();
        return false;
    }
}

ImportedMediaPackageAsset* findAsset(std::vector<ImportedMediaPackageAsset>& assets,
                                     ImportedMediaPackageAssetKind kind,
                                     const std::string& relativePath)
{
    for (auto& asset : assets)
    {
        if (asset.kind == kind && asset.relativePath == relativePath)
            return &asset;
    }

    return nullptr;
}

ImportedMediaPackageMissingReference* findMissingReference(
    std::vector<ImportedMediaPackageMissingReference>& missingReferences,
    ImportedMediaPackageAssetKind kind,
    const std::string& relativePath)
{
    for (auto& missing : missingReferences)
    {
        if (missing.kind == kind && missing.relativePath == relativePath)
            return &missing;
    }

    return nullptr;
}

void addScannedAsset(ImportedMediaPackageInventory& inventory,
                     ImportedMediaPackageAssetKind kind,
                     const std::filesystem::path& packageDirectory,
                     const std::filesystem::path& absolutePath)
{
    const auto relativePath = absolutePath.lexically_relative(packageDirectory);
    if (relativePath.empty())
        return;

    const auto genericRelativePath = relativePath.generic_string();
    if (findAsset(inventory.assets, kind, genericRelativePath) != nullptr)
        return;

    ImportedMediaPackageAsset asset;
    asset.kind = kind;
    asset.relativePath = genericRelativePath;
    asset.absolutePath = absolutePath;
    inventory.assets.push_back(std::move(asset));
}

void scanAssetFolder(ImportedMediaPackageInventory& inventory,
                     ImportedMediaPackageAssetKind kind,
                     const std::filesystem::path& packageDirectory)
{
    const auto root = packageDirectory / expectedFolder(kind);
    std::error_code filesystemError;
    if (!std::filesystem::is_directory(root, filesystemError))
        return;

    std::filesystem::recursive_directory_iterator iterator(root,
                                                           std::filesystem::directory_options::skip_permission_denied,
                                                           filesystemError);
    const std::filesystem::recursive_directory_iterator end;
    while (!filesystemError && iterator != end)
    {
        if (std::filesystem::is_regular_file(iterator->path(), filesystemError))
            addScannedAsset(inventory, kind, packageDirectory, iterator->path());

        iterator.increment(filesystemError);
    }
}

void addMissingReference(ImportedMediaPackageInventory& inventory,
                         ImportedMediaPackageAssetKind kind,
                         const std::string& relativePath,
                         ImportedMediaPackageReferenceSource source)
{
    if (auto* missing = findMissingReference(inventory.missingReferences, kind, relativePath))
    {
        addSource(missing->referenceSources, source);
        return;
    }

    ImportedMediaPackageMissingReference missing;
    missing.kind = kind;
    missing.relativePath = relativePath;
    missing.referenceSources.push_back(source);
    inventory.missingReferences.push_back(std::move(missing));
}

void applyReferences(ImportedMediaPackageInventory& inventory,
                     const std::vector<ValidReference>& references)
{
    for (const auto& reference : references)
    {
        if (auto* asset = findAsset(inventory.assets, reference.kind, reference.relativePath))
        {
            addSource(asset->referenceSources, reference.source);
            continue;
        }

        addMissingReference(inventory, reference.kind, reference.relativePath, reference.source);
    }

    for (auto& asset : inventory.assets)
        asset.unreferencedCandidate = asset.referenceSources.empty();
}

void scanStagingDirectories(ImportedMediaPackageInventory& inventory,
                            const std::filesystem::path& packageDirectory,
                            bool packageWorkInProgress)
{
    const auto stagingRoot = packageDirectory / ".projectname-staging";
    std::error_code filesystemError;
    if (!std::filesystem::is_directory(stagingRoot, filesystemError))
        return;

    std::filesystem::directory_iterator iterator(stagingRoot,
                                                 std::filesystem::directory_options::skip_permission_denied,
                                                 filesystemError);
    const std::filesystem::directory_iterator end;
    while (!filesystemError && iterator != end)
    {
        if (std::filesystem::is_directory(iterator->path(), filesystemError))
        {
            ImportedMediaPackageStagingDirectory staging;
            staging.relativePath = iterator->path().lexically_relative(packageDirectory).generic_string();
            staging.absolutePath = iterator->path();
            staging.staleCandidate = !packageWorkInProgress;
            inventory.stagingDirectories.push_back(std::move(staging));
        }

        iterator.increment(filesystemError);
    }
}

void sortInventory(ImportedMediaPackageInventory& inventory)
{
    const auto byKindThenPath = [](const auto& left, const auto& right)
    {
        if (left.kind != right.kind)
            return left.kind < right.kind;

        return left.relativePath < right.relativePath;
    };

    std::sort(inventory.assets.begin(), inventory.assets.end(), byKindThenPath);
    std::sort(inventory.missingReferences.begin(), inventory.missingReferences.end(), byKindThenPath);
    std::sort(inventory.unsafeReferences.begin(), inventory.unsafeReferences.end(), byKindThenPath);
    std::sort(inventory.stagingDirectories.begin(),
              inventory.stagingDirectories.end(),
              [](const auto& left, const auto& right)
              {
                  return left.relativePath < right.relativePath;
              });
}
} // namespace

ImportedMediaPackageInventory buildImportedMediaPackageInventory(
    const std::filesystem::path& packageDirectory,
    const ImportedMediaPackageInventoryOptions& options)
{
    ImportedMediaPackageInventory inventory;
    std::vector<ValidReference> references;

    collectManifestFileReferences(packageDirectory / "manifest.json",
                                  ImportedMediaPackageReferenceSource::currentManifest,
                                  true,
                                  inventory.currentManifestRead,
                                  references,
                                  inventory);

    collectManifestFileReferences(packageDirectory / "backups" / "manifest.previous.json",
                                  ImportedMediaPackageReferenceSource::previousManifestBackup,
                                  false,
                                  inventory.previousManifestBackupRead,
                                  references,
                                  inventory);

    for (const auto& protectedReference : options.protectedSessionReferences)
    {
        collectReferencePath(references,
                             inventory,
                             protectedReference.kind,
                             ImportedMediaPackageReferenceSource::sessionProtected,
                             protectedReference.relativePath);
    }

    scanAssetFolder(inventory, ImportedMediaPackageAssetKind::audio, packageDirectory);
    scanAssetFolder(inventory, ImportedMediaPackageAssetKind::analysis, packageDirectory);
    applyReferences(inventory, references);
    scanStagingDirectories(inventory, packageDirectory, options.packageWorkInProgress);
    sortInventory(inventory);

    return inventory;
}
} // namespace projectname
