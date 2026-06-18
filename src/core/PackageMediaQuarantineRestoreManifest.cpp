// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PackageMediaQuarantineRestoreManifest.h"

#include "PackagePath.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

namespace projectname
{
namespace
{
constexpr int currentSchemaVersion = 1;

[[nodiscard]] std::string toString(PackageMediaQuarantineEntryKind kind)
{
    switch (kind)
    {
        case PackageMediaQuarantineEntryKind::audio:
            return "audio";
        case PackageMediaQuarantineEntryKind::analysis:
            return "analysis";
        case PackageMediaQuarantineEntryKind::stagingDirectory:
            return "staging-directory";
    }

    return "audio";
}

[[nodiscard]] std::optional<PackageMediaQuarantineEntryKind> parseEntryKind(const std::string& value)
{
    if (value == "audio")
        return PackageMediaQuarantineEntryKind::audio;
    if (value == "analysis")
        return PackageMediaQuarantineEntryKind::analysis;
    if (value == "staging-directory")
        return PackageMediaQuarantineEntryKind::stagingDirectory;

    return std::nullopt;
}

[[nodiscard]] std::string toString(PackageMediaQuarantineManifestState state)
{
    switch (state)
    {
        case PackageMediaQuarantineManifestState::completed:
            return "completed";
        case PackageMediaQuarantineManifestState::partialFailure:
            return "partial-failure";
        case PackageMediaQuarantineManifestState::restored:
            return "restored";
        case PackageMediaQuarantineManifestState::restoreConflict:
            return "restore-conflict";
    }

    return "completed";
}

[[nodiscard]] std::optional<PackageMediaQuarantineManifestState> parseState(const std::string& value)
{
    if (value == "completed")
        return PackageMediaQuarantineManifestState::completed;
    if (value == "partial-failure")
        return PackageMediaQuarantineManifestState::partialFailure;
    if (value == "restored")
        return PackageMediaQuarantineManifestState::restored;
    if (value == "restore-conflict")
        return PackageMediaQuarantineManifestState::restoreConflict;

    return std::nullopt;
}

[[nodiscard]] std::vector<std::string> pathParts(const std::filesystem::path& path)
{
    std::vector<std::string> parts;
    for (const auto& part : path)
        parts.push_back(part.generic_string());

    return parts;
}

[[nodiscard]] bool isSafeNormalizedPackageRelativePath(const std::filesystem::path& path)
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

[[nodiscard]] bool pathStartsWith(const std::filesystem::path& path,
                                  std::initializer_list<std::string_view> expectedParts)
{
    const auto parts = pathParts(path);
    if (parts.size() < expectedParts.size())
        return false;

    auto index = std::size_t {};
    for (const auto expected : expectedParts)
    {
        if (parts[index] != expected)
            return false;

        ++index;
    }

    return true;
}

[[nodiscard]] const char* expectedOriginalRoot(PackageMediaQuarantineEntryKind kind) noexcept
{
    switch (kind)
    {
        case PackageMediaQuarantineEntryKind::audio:
            return "audio";
        case PackageMediaQuarantineEntryKind::analysis:
            return "analysis";
        case PackageMediaQuarantineEntryKind::stagingDirectory:
            return ".projectname-staging";
    }

    return "audio";
}

[[nodiscard]] const char* expectedQuarantineGroup(PackageMediaQuarantineEntryKind kind) noexcept
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

[[nodiscard]] bool validateOriginalPath(PackageMediaQuarantineEntryKind kind,
                                        const std::string& rawPath,
                                        std::string& error)
{
    const auto path = std::filesystem::path(rawPath);
    if (!isSafeNormalizedPackageRelativePath(path))
    {
        error = "Original quarantine manifest path must be safe, normalized, and package-relative.";
        return false;
    }

    if (!pathStartsWith(path, { expectedOriginalRoot(kind) }))
    {
        error = std::string("Original quarantine manifest path must stay under ")
            + expectedOriginalRoot(kind) + "/.";
        return false;
    }

    return true;
}

[[nodiscard]] bool validateQuarantinePath(PackageMediaQuarantineEntryKind kind,
                                          const std::string& cleanupId,
                                          const std::string& rawPath,
                                          std::string& error)
{
    const auto path = std::filesystem::path(rawPath);
    if (!isSafeNormalizedPackageRelativePath(path))
    {
        error = "Quarantine manifest path must be safe, normalized, and package-relative.";
        return false;
    }

    if (!pathStartsWith(path,
                        {
                            "backups",
                            "media-trash",
                            cleanupId,
                            expectedQuarantineGroup(kind),
                        }))
    {
        error = "Quarantine manifest path must stay under backups/media-trash/<cleanup-id>/.";
        return false;
    }

    return true;
}

[[nodiscard]] nlohmann::json makeMovedEntryJson(const PackageMediaQuarantineMovedEntry& entry)
{
    auto json = nlohmann::json {
        { "kind", toString(entry.kind) },
        { "originalPath", entry.originalRelativePath },
        { "quarantinePath", entry.quarantineRelativePath },
        { "restored", entry.restored },
        { "restoreConflict", entry.restoreConflict },
    };

    if (entry.byteSize.has_value())
        json["byteSize"] = *entry.byteSize;

    if (!entry.contentHash.empty())
        json["contentHash"] = entry.contentHash;

    if (!entry.error.empty())
        json["error"] = entry.error;

    return json;
}

[[nodiscard]] nlohmann::json makeSkippedEntryJson(const PackageMediaQuarantineSkippedEntry& entry)
{
    auto json = nlohmann::json {
        { "kind", toString(entry.kind) },
        { "originalPath", entry.originalRelativePath },
        { "reason", entry.reason },
    };

    if (!entry.detail.empty())
        json["detail"] = entry.detail;

    return json;
}

[[nodiscard]] nlohmann::json makeManifestJson(const PackageMediaQuarantineRestoreManifest& manifest)
{
    auto movedEntries = nlohmann::json::array();
    for (const auto& entry : manifest.movedEntries)
        movedEntries.push_back(makeMovedEntryJson(entry));

    auto skippedEntries = nlohmann::json::array();
    for (const auto& entry : manifest.skippedEntries)
        skippedEntries.push_back(makeSkippedEntryJson(entry));

    auto json = nlohmann::json {
        { "schemaVersion", manifest.schemaVersion },
        { "application", manifest.application },
        { "cleanupId", manifest.cleanupId },
        { "createdAtUtc", manifest.createdAtUtc },
        { "packageDisplayPath", manifest.packageDisplayPath },
        { "inventorySummary", manifest.inventorySummary },
        { "state", toString(manifest.state) },
        { "movedEntries", std::move(movedEntries) },
        { "skippedEntries", std::move(skippedEntries) },
    };

    if (!manifest.manifestMarker.empty())
        json["manifestMarker"] = manifest.manifestMarker;

    if (!manifest.error.empty())
        json["error"] = manifest.error;

    return json;
}

[[nodiscard]] bool requireObject(const nlohmann::json& json, std::string& error)
{
    if (json.is_object())
        return true;

    error = "Restore manifest must be a JSON object.";
    return false;
}

[[nodiscard]] bool readRequiredString(const nlohmann::json& json,
                                      const char* key,
                                      std::string& value,
                                      std::string& error)
{
    const auto found = json.find(key);
    if (found == json.end() || !found->is_string())
    {
        error = std::string("Restore manifest is missing string field: ") + key + ".";
        return false;
    }

    value = found->get<std::string>();
    return true;
}

[[nodiscard]] bool readOptionalString(const nlohmann::json& json,
                                      const char* key,
                                      std::string& value,
                                      std::string& error)
{
    const auto found = json.find(key);
    if (found == json.end())
    {
        value.clear();
        return true;
    }

    if (!found->is_string())
    {
        error = std::string("Restore manifest field must be a string: ") + key + ".";
        return false;
    }

    value = found->get<std::string>();
    return true;
}

[[nodiscard]] bool readMovedEntries(const nlohmann::json& json,
                                    std::vector<PackageMediaQuarantineMovedEntry>& entries,
                                    std::string& error)
{
    const auto moved = json.find("movedEntries");
    if (moved == json.end() || !moved->is_array())
    {
        error = "Restore manifest movedEntries must be an array.";
        return false;
    }

    for (const auto& entryJson : *moved)
    {
        if (!entryJson.is_object())
        {
            error = "Restore manifest moved entry must be an object.";
            return false;
        }

        std::string kindText;
        if (!readRequiredString(entryJson, "kind", kindText, error))
            return false;

        const auto kind = parseEntryKind(kindText);
        if (!kind.has_value())
        {
            error = "Restore manifest moved entry has an unknown kind.";
            return false;
        }

        PackageMediaQuarantineMovedEntry entry;
        entry.kind = *kind;
        if (!readRequiredString(entryJson, "originalPath", entry.originalRelativePath, error)
            || !readRequiredString(entryJson, "quarantinePath", entry.quarantineRelativePath, error)
            || !readOptionalString(entryJson, "contentHash", entry.contentHash, error)
            || !readOptionalString(entryJson, "error", entry.error, error))
        {
            return false;
        }

        entry.restored = entryJson.value("restored", false);
        entry.restoreConflict = entryJson.value("restoreConflict", false);

        const auto byteSize = entryJson.find("byteSize");
        if (byteSize != entryJson.end())
        {
            if (!byteSize->is_number_unsigned())
            {
                error = "Restore manifest byteSize must be an unsigned number.";
                return false;
            }

            entry.byteSize = byteSize->get<std::uintmax_t>();
        }

        entries.push_back(std::move(entry));
    }

    return true;
}

[[nodiscard]] bool readSkippedEntries(const nlohmann::json& json,
                                      std::vector<PackageMediaQuarantineSkippedEntry>& entries,
                                      std::string& error)
{
    const auto skipped = json.find("skippedEntries");
    if (skipped == json.end() || !skipped->is_array())
    {
        error = "Restore manifest skippedEntries must be an array.";
        return false;
    }

    for (const auto& entryJson : *skipped)
    {
        if (!entryJson.is_object())
        {
            error = "Restore manifest skipped entry must be an object.";
            return false;
        }

        std::string kindText;
        if (!readRequiredString(entryJson, "kind", kindText, error))
            return false;

        const auto kind = parseEntryKind(kindText);
        if (!kind.has_value())
        {
            error = "Restore manifest skipped entry has an unknown kind.";
            return false;
        }

        PackageMediaQuarantineSkippedEntry entry;
        entry.kind = *kind;
        if (!readRequiredString(entryJson, "originalPath", entry.originalRelativePath, error)
            || !readRequiredString(entryJson, "reason", entry.reason, error)
            || !readOptionalString(entryJson, "detail", entry.detail, error))
        {
            return false;
        }

        entries.push_back(std::move(entry));
    }

    return true;
}

[[nodiscard]] std::optional<PackageMediaQuarantineRestoreManifest> parseManifestJson(
    const nlohmann::json& json,
    std::string& error)
{
    if (!requireObject(json, error))
        return std::nullopt;

    PackageMediaQuarantineRestoreManifest manifest;
    manifest.schemaVersion = json.value("schemaVersion", 0);
    if (manifest.schemaVersion != currentSchemaVersion)
    {
        error = "Unsupported restore manifest schema version.";
        return std::nullopt;
    }

    std::string stateText;
    if (!readRequiredString(json, "application", manifest.application, error)
        || !readRequiredString(json, "cleanupId", manifest.cleanupId, error)
        || !readRequiredString(json, "createdAtUtc", manifest.createdAtUtc, error)
        || !readRequiredString(json, "packageDisplayPath", manifest.packageDisplayPath, error)
        || !readRequiredString(json, "inventorySummary", manifest.inventorySummary, error)
        || !readRequiredString(json, "state", stateText, error)
        || !readOptionalString(json, "manifestMarker", manifest.manifestMarker, error)
        || !readOptionalString(json, "error", manifest.error, error))
    {
        return std::nullopt;
    }

    const auto state = parseState(stateText);
    if (!state.has_value())
    {
        error = "Restore manifest has an unknown state.";
        return std::nullopt;
    }

    manifest.state = *state;

    if (!readMovedEntries(json, manifest.movedEntries, error)
        || !readSkippedEntries(json, manifest.skippedEntries, error))
    {
        return std::nullopt;
    }

    if (!validatePackageMediaQuarantineRestoreManifest(manifest, error))
        return std::nullopt;

    return manifest;
}
} // namespace

bool isValidPackageMediaQuarantineCleanupId(std::string_view cleanupId) noexcept
{
    if (cleanupId.empty())
        return false;

    return std::all_of(cleanupId.begin(),
                       cleanupId.end(),
                       [](const char character)
                       {
                           return (character >= 'a' && character <= 'z')
                               || (character >= 'A' && character <= 'Z')
                               || (character >= '0' && character <= '9')
                               || character == '-'
                               || character == '_';
                       });
}

bool validatePackageMediaQuarantineRestoreManifest(
    const PackageMediaQuarantineRestoreManifest& manifest,
    std::string& error)
{
    error.clear();

    if (manifest.schemaVersion != currentSchemaVersion)
    {
        error = "Unsupported restore manifest schema version.";
        return false;
    }

    if (manifest.application.empty())
    {
        error = "Restore manifest application is required.";
        return false;
    }

    if (!isValidPackageMediaQuarantineCleanupId(manifest.cleanupId))
    {
        error = "Restore manifest cleanup id must be non-empty and filesystem-safe.";
        return false;
    }

    if (manifest.createdAtUtc.empty())
    {
        error = "Restore manifest creation time is required.";
        return false;
    }

    if (manifest.inventorySummary.empty())
    {
        error = "Restore manifest inventory summary is required.";
        return false;
    }

    if (manifest.state == PackageMediaQuarantineManifestState::partialFailure
        && manifest.error.empty())
    {
        error = "Partial-failure restore manifest state requires an error message.";
        return false;
    }

    if (manifest.state == PackageMediaQuarantineManifestState::restoreConflict)
    {
        const auto hasConflict = std::any_of(
            manifest.movedEntries.begin(),
            manifest.movedEntries.end(),
            [](const auto& entry)
            {
                return entry.restoreConflict;
            });
        if (!hasConflict)
        {
            error = "Restore-conflict manifest state requires at least one conflicting entry.";
            return false;
        }
    }

    std::set<std::string> originalPaths;
    std::set<std::string> quarantinePaths;
    for (const auto& entry : manifest.movedEntries)
    {
        if (!validateOriginalPath(entry.kind, entry.originalRelativePath, error))
            return false;

        if (!validateQuarantinePath(entry.kind,
                                    manifest.cleanupId,
                                    entry.quarantineRelativePath,
                                    error))
        {
            return false;
        }

        if (!originalPaths.insert(entry.originalRelativePath).second)
        {
            error = "Restore manifest contains duplicate original paths.";
            return false;
        }

        if (!quarantinePaths.insert(entry.quarantineRelativePath).second)
        {
            error = "Restore manifest contains duplicate quarantine paths.";
            return false;
        }
    }

    for (const auto& entry : manifest.skippedEntries)
    {
        if (!validateOriginalPath(entry.kind, entry.originalRelativePath, error))
            return false;

        if (entry.reason.empty())
        {
            error = "Restore manifest skipped entry reason is required.";
            return false;
        }
    }

    return true;
}

bool savePackageMediaQuarantineRestoreManifest(
    const PackageMediaQuarantineRestoreManifest& manifest,
    const std::filesystem::path& manifestPath,
    std::string& error)
{
    if (!validatePackageMediaQuarantineRestoreManifest(manifest, error))
        return false;

    std::ofstream output(manifestPath, std::ios::trunc);
    if (!output)
    {
        error = "Could not open restore manifest for writing.";
        return false;
    }

    output << makeManifestJson(manifest).dump(2) << '\n';
    output.close();
    if (!output)
    {
        error = "Could not write restore manifest.";
        return false;
    }

    error.clear();
    return true;
}

std::optional<PackageMediaQuarantineRestoreManifest>
loadPackageMediaQuarantineRestoreManifest(const std::filesystem::path& manifestPath,
                                          std::string& error)
{
    std::ifstream input(manifestPath);
    if (!input)
    {
        error = "Could not open restore manifest.";
        return std::nullopt;
    }

    try
    {
        const auto json = nlohmann::json::parse(input);
        return parseManifestJson(json, error);
    }
    catch (const nlohmann::json::exception& exception)
    {
        error = std::string("Restore manifest is not valid JSON: ") + exception.what();
        return std::nullopt;
    }
}
} // namespace projectname
