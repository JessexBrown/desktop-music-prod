// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace projectname
{
enum class PackageMediaQuarantineEntryKind
{
    audio,
    analysis,
    stagingDirectory,
};

enum class PackageMediaQuarantineManifestState
{
    completed,
    partialFailure,
    restored,
    restoreConflict,
};

struct PackageMediaQuarantineMovedEntry
{
    PackageMediaQuarantineEntryKind kind = PackageMediaQuarantineEntryKind::audio;
    std::string originalRelativePath;
    std::string quarantineRelativePath;
    std::optional<std::uintmax_t> byteSize;
    std::string contentHash;
    bool restored = false;
    bool restoreConflict = false;
    std::string error;
};

struct PackageMediaQuarantineSkippedEntry
{
    PackageMediaQuarantineEntryKind kind = PackageMediaQuarantineEntryKind::audio;
    std::string originalRelativePath;
    std::string reason;
    std::string detail;
};

struct PackageMediaQuarantineRestoreManifest
{
    int schemaVersion = 1;
    std::string application = "ProjectName";
    std::string cleanupId;
    std::string createdAtUtc;
    std::string packageDisplayPath;
    std::string inventorySummary;
    std::string manifestMarker;
    PackageMediaQuarantineManifestState state = PackageMediaQuarantineManifestState::completed;
    std::string error;
    std::vector<PackageMediaQuarantineMovedEntry> movedEntries;
    std::vector<PackageMediaQuarantineSkippedEntry> skippedEntries;
};

[[nodiscard]] bool isValidPackageMediaQuarantineCleanupId(std::string_view cleanupId) noexcept;

[[nodiscard]] bool validatePackageMediaQuarantineRestoreManifest(
    const PackageMediaQuarantineRestoreManifest& manifest,
    std::string& error);

[[nodiscard]] bool savePackageMediaQuarantineRestoreManifest(
    const PackageMediaQuarantineRestoreManifest& manifest,
    const std::filesystem::path& manifestPath,
    std::string& error);

[[nodiscard]] std::optional<PackageMediaQuarantineRestoreManifest>
loadPackageMediaQuarantineRestoreManifest(const std::filesystem::path& manifestPath,
                                          std::string& error);
} // namespace projectname
