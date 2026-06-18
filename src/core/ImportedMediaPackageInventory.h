// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace projectname
{
enum class ImportedMediaPackageAssetKind
{
    audio,
    analysis,
};

enum class ImportedMediaPackageReferenceSource
{
    currentManifest,
    previousManifestBackup,
    sessionProtected,
};

struct ImportedMediaPackageProtectedReference
{
    ImportedMediaPackageAssetKind kind = ImportedMediaPackageAssetKind::audio;
    std::string relativePath;
};

struct ImportedMediaPackageInventoryOptions
{
    std::vector<ImportedMediaPackageProtectedReference> protectedSessionReferences;
    bool packageWorkInProgress = false;
};

struct ImportedMediaPackageAsset
{
    ImportedMediaPackageAssetKind kind = ImportedMediaPackageAssetKind::audio;
    std::string relativePath;
    std::filesystem::path absolutePath;
    std::vector<ImportedMediaPackageReferenceSource> referenceSources;
    bool unreferencedCandidate = false;
};

struct ImportedMediaPackageMissingReference
{
    ImportedMediaPackageAssetKind kind = ImportedMediaPackageAssetKind::audio;
    std::string relativePath;
    std::vector<ImportedMediaPackageReferenceSource> referenceSources;
};

struct ImportedMediaPackageUnsafeReference
{
    ImportedMediaPackageAssetKind kind = ImportedMediaPackageAssetKind::audio;
    ImportedMediaPackageReferenceSource source = ImportedMediaPackageReferenceSource::currentManifest;
    std::string relativePath;
    std::string reason;
};

struct ImportedMediaPackageStagingDirectory
{
    std::string relativePath;
    std::filesystem::path absolutePath;
    bool staleCandidate = false;
};

struct ImportedMediaPackageInventory
{
    bool currentManifestRead = false;
    bool previousManifestBackupRead = false;
    std::string error;
    std::vector<ImportedMediaPackageAsset> assets;
    std::vector<ImportedMediaPackageMissingReference> missingReferences;
    std::vector<ImportedMediaPackageUnsafeReference> unsafeReferences;
    std::vector<ImportedMediaPackageStagingDirectory> stagingDirectories;
};

[[nodiscard]] ImportedMediaPackageInventory buildImportedMediaPackageInventory(
    const std::filesystem::path& packageDirectory,
    const ImportedMediaPackageInventoryOptions& options = {});
} // namespace projectname
