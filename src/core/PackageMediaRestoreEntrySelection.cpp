// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PackageMediaRestoreEntrySelection.h"

#include <set>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] bool batchBlocksRestore(const PackageMediaQuarantineRestoreManifest& manifest) noexcept
{
    return manifest.state == PackageMediaQuarantineManifestState::restoreConflict
        || manifest.state == PackageMediaQuarantineManifestState::partialFailure;
}

[[nodiscard]] bool entryIsRestorable(const PackageMediaQuarantineMovedEntry& entry) noexcept
{
    return !entry.restored
        && !entry.restoreConflict
        && entry.error.empty();
}

[[nodiscard]] std::string entryUnavailableReason(const PackageMediaQuarantineMovedEntry& entry)
{
    if (entry.restored)
        return "Entry has already been restored.";

    if (entry.restoreConflict)
        return "Entry has a restore conflict.";

    if (!entry.error.empty())
        return "Entry has a partial restore failure.";

    return {};
}

[[nodiscard]] std::string restoreUnavailableReason(
    const PackageMediaRestoreEntrySelection& selection)
{
    if (selection.entries.empty())
        return "This cleanup batch has no media entries to restore.";

    if (!selection.hasRestorableEntries)
        return "This cleanup batch has no unrestored media entries.";

    return "Select media entries to restore.";
}

void refreshSelectionSummary(PackageMediaRestoreEntrySelection& selection)
{
    selection.selectedOriginalRelativePaths.clear();
    selection.selectedRestorableEntryCount = 0;

    for (const auto& entry : selection.entries)
    {
        if (!entry.selected || !entry.restorable)
            continue;

        selection.selectedOriginalRelativePaths.push_back(entry.originalRelativePath);
        ++selection.selectedRestorableEntryCount;
    }

    selection.hasSelection = selection.selectedRestorableEntryCount > 0;
    selection.restoreActionEnabled = selection.hasSelection && !selection.blockedByReviewState;
    selection.restoreUnavailableReason.clear();

    if (selection.restoreActionEnabled)
        return;

    if (selection.blockedByReviewState)
    {
        selection.restoreUnavailableReason =
            "Review restore conflicts or partial failures before restoring selected entries.";
        return;
    }

    selection.restoreUnavailableReason = restoreUnavailableReason(selection);
}
} // namespace

PackageMediaRestoreEntrySelection buildPackageMediaRestoreEntrySelection(
    const PackageMediaQuarantineRestoreManifest& manifest,
    const std::vector<std::string>& selectedOriginalRelativePaths)
{
    PackageMediaRestoreEntrySelection selection;
    selection.cleanupId = manifest.cleanupId;
    selection.entries.reserve(manifest.movedEntries.size());

    std::set<std::string> requestedSelections(
        selectedOriginalRelativePaths.begin(),
        selectedOriginalRelativePaths.end());

    for (const auto& entry : manifest.movedEntries)
    {
        PackageMediaRestoreEntrySelectionItem item;
        item.originalRelativePath = entry.originalRelativePath;
        item.restorable = entryIsRestorable(entry);
        item.selected = item.restorable
            && requestedSelections.erase(entry.originalRelativePath) > 0;
        item.unavailableReason = entryUnavailableReason(entry);

        if (item.restorable)
            ++selection.restorableEntryCount;

        selection.entries.push_back(std::move(item));
    }

    selection.staleSelectedOriginalRelativePaths.assign(
        requestedSelections.begin(),
        requestedSelections.end());
    selection.hasRestorableEntries = selection.restorableEntryCount > 0;
    selection.blockedByReviewState = batchBlocksRestore(manifest);

    refreshSelectionSummary(selection);
    return selection;
}

PackageMediaRestoreEntrySelection selectAllPackageMediaRestoreEntries(
    PackageMediaRestoreEntrySelection selection)
{
    selection.staleSelectedOriginalRelativePaths.clear();

    for (auto& entry : selection.entries)
        entry.selected = entry.restorable;

    refreshSelectionSummary(selection);
    return selection;
}

PackageMediaRestoreEntrySelection clearPackageMediaRestoreEntrySelection(
    PackageMediaRestoreEntrySelection selection)
{
    selection.staleSelectedOriginalRelativePaths.clear();

    for (auto& entry : selection.entries)
        entry.selected = false;

    refreshSelectionSummary(selection);
    return selection;
}

PackageMediaRestoreEntrySelection togglePackageMediaRestoreEntrySelection(
    PackageMediaRestoreEntrySelection selection,
    const std::string& originalRelativePath)
{
    for (auto& entry : selection.entries)
    {
        if (entry.originalRelativePath == originalRelativePath && entry.restorable)
        {
            entry.selected = !entry.selected;
            break;
        }
    }

    refreshSelectionSummary(selection);
    return selection;
}
} // namespace projectname
