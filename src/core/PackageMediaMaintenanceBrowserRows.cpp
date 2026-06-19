// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PackageMediaMaintenanceBrowserRows.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace projectname
{
namespace
{
[[nodiscard]] std::string shortenBrowserValue(const std::string& value)
{
    constexpr auto maxCharacters = std::size_t { 22 };
    if (value.size() <= maxCharacters)
        return value;

    return value.substr(0, maxCharacters - 3) + "...";
}

[[nodiscard]] std::string statusLabel(PackageMediaCleanupStatusKind kind)
{
    switch (kind)
    {
        case PackageMediaCleanupStatusKind::quarantineCompleted:
            return "ready";
        case PackageMediaCleanupStatusKind::restoreCompleted:
            return "restored";
        case PackageMediaCleanupStatusKind::restoreConflict:
            return "conflict";
        case PackageMediaCleanupStatusKind::partialFailure:
            return "needs review";
        case PackageMediaCleanupStatusKind::cleanupFailed:
            return "failed";
        case PackageMediaCleanupStatusKind::reviewAvailable:
            return "review";
        case PackageMediaCleanupStatusKind::missingReferences:
            return "missing";
        case PackageMediaCleanupStatusKind::unsafeReferences:
            return "unsafe";
        case PackageMediaCleanupStatusKind::noCandidates:
            return "clean";
        case PackageMediaCleanupStatusKind::idle:
        case PackageMediaCleanupStatusKind::inventoryRunning:
        case PackageMediaCleanupStatusKind::preflightRunning:
        case PackageMediaCleanupStatusKind::quarantineRunning:
        case PackageMediaCleanupStatusKind::restoreRunning:
        case PackageMediaCleanupStatusKind::operationCompleted:
        case PackageMediaCleanupStatusKind::preflightReady:
        case PackageMediaCleanupStatusKind::activePackageWork:
        case PackageMediaCleanupStatusKind::cancelled:
            return "status";
    }

    return "status";
}

[[nodiscard]] std::string restoreSummary(const PackageMediaMaintenanceViewModel& model)
{
    if (model.restoreActionEnabled)
    {
        return "Restore: "
            + std::to_string(model.restoreEntrySelection.selectedRestorableEntryCount)
            + " selected";
    }

    if (!model.hasSelectedBatch)
        return "Restore: select batch";

    const auto& row = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];

    if (row.errorCount > 0)
        return "Restore: failure review";

    if (row.conflictCount > 0 || model.restoreEntrySelection.blockedByReviewState)
        return "Restore: conflict review";

    if (row.movedEntryCount > 0 && row.restoredEntryCount == row.movedEntryCount)
        return "Restore: already restored";

    if (model.restoreEntrySelection.hasRestorableEntries)
        return "Restore: select entries";

    return "Restore: unavailable";
}

[[nodiscard]] std::string restoreDisabledReason(
    const PackageMediaMaintenanceViewModel& model,
    bool packageWorkInProgress)
{
    if (packageWorkInProgress)
        return "Package files are busy.";

    if (model.restoreActionEnabled)
        return {};

    if (!model.restoreUnavailableReason.empty())
        return model.restoreUnavailableReason;

    return model.hasSelectedBatch
        ? "Selected cleanup batch cannot be restored."
        : "Select a cleanup batch to restore.";
}

[[nodiscard]] std::string cleanupDisabledReason(
    const PackageMediaMaintenanceViewModel& model,
    bool packageWorkInProgress)
{
    if (packageWorkInProgress)
        return "Package files are busy.";

    if (model.cleanupReviewAvailable)
        return {};

    if (model.unsafeReferenceCount > 0)
        return "Resolve unsafe package media references before cleanup.";

    if (model.missingReferenceCount > 0)
        return "Resolve missing package media references before cleanup.";

    if (model.inventoryStatus.kind == PackageMediaCleanupStatusKind::activePackageWork)
        return "Package files are busy.";

    if (model.inventoryStatus.kind == PackageMediaCleanupStatusKind::inventoryRunning)
        return "Package media scan is running.";

    if (model.cleanupCandidateCount == 0 && model.staleStagingCandidateCount == 0)
        return "No cleanup candidates.";

    if (!model.inventoryStatus.statusText.empty())
        return model.inventoryStatus.statusText;

    return "Package media cleanup is unavailable.";
}

[[nodiscard]] std::string cleanupSummary(
    const PackageMediaMaintenanceViewModel& model,
    bool packageWorkInProgress)
{
    if (!packageWorkInProgress && model.cleanupReviewAvailable)
        return "Cleanup: available";

    return "Cleanup: " + cleanupDisabledReason(model, packageWorkInProgress);
}

[[nodiscard]] std::string makeBatchLine(
    const PackageMediaMaintenanceBatchRow& row,
    std::size_t batchIndex)
{
    auto line = "Batch "
        + std::to_string(batchIndex + 1)
        + ": "
        + shortenBrowserValue(row.cleanupId)
        + " | "
        + statusLabel(row.status.kind);

    if (row.selected)
        line += " | selected";

    return line;
}

[[nodiscard]] std::string selectedBatchEntrySummary(
    const PackageMediaMaintenanceBatchRow& row,
    const PackageMediaRestoreEntrySelection& selection)
{
    return "Entries: "
        + std::to_string(row.movedEntryCount)
        + " moved / "
        + std::to_string(row.restoredEntryCount)
        + " restored / "
        + std::to_string(row.restorableEntryCount)
        + " restorable / "
        + std::to_string(selection.selectedRestorableEntryCount)
        + " selected";
}

[[nodiscard]] std::string selectedBatchReviewSummary(
    const PackageMediaMaintenanceBatchRow& row)
{
    return "Review: "
        + std::to_string(row.conflictCount)
        + " conflicts / "
        + std::to_string(row.errorCount)
        + " errors";
}

[[nodiscard]] std::string selectedBatchEntryPath(
    const PackageMediaRestoreEntrySelectionItem& entry,
    std::size_t entryIndex)
{
    auto state = entry.restorable ? std::string("restorable") : std::string("unavailable");
    if (entry.selected)
        state = "selected";
    else if (!entry.unavailableReason.empty())
        state = entry.unavailableReason;

    return "Entry "
        + std::to_string(entryIndex + 1)
        + ": "
        + state
        + " | "
        + entry.originalRelativePath;
}

[[nodiscard]] std::vector<std::size_t> makeVisibleBatchIndexes(
    const PackageMediaMaintenanceViewModel& model,
    std::size_t maxBatchRows)
{
    std::vector<std::size_t> indexes;
    if (maxBatchRows == 0 || model.batches.empty())
        return indexes;

    const auto visibleCount = std::min(maxBatchRows, model.batches.size());
    indexes.reserve(visibleCount);
    for (std::size_t index = 0; index < visibleCount; ++index)
        indexes.push_back(index);

    if (!model.hasSelectedBatch)
        return indexes;

    const auto selectedIndex = static_cast<std::size_t>(model.selectedBatchIndex);
    if (std::find(indexes.begin(), indexes.end(), selectedIndex) != indexes.end())
        return indexes;

    indexes.back() = selectedIndex;
    return indexes;
}

[[nodiscard]] bool canUseRestoreSelectionKeyboardCommands(
    const PackageMediaMaintenanceViewModel& model,
    bool packageWorkInProgress) noexcept
{
    return !packageWorkInProgress
        && model.hasSelectedBatch
        && model.restoreEntrySelection.hasRestorableEntries
        && !model.restoreEntrySelection.blockedByReviewState;
}

void addRow(PackageMediaMaintenanceBrowserRows& rows,
            PackageMediaMaintenanceBrowserRowKind kind,
            std::string text)
{
    PackageMediaMaintenanceBrowserRow row;
    row.kind = kind;
    row.text = std::move(text);
    rows.rows.push_back(std::move(row));
}

[[nodiscard]] std::string makeBatchSelectionId(const std::string& cleanupId)
{
    return std::string(packageMediaMaintenanceBrowserSelectionIds::batchPrefix) + cleanupId;
}

[[nodiscard]] std::string makeRestoreEntrySelectionId(const std::string& originalRelativePath)
{
    return std::string(packageMediaMaintenanceBrowserSelectionIds::restoreEntryPrefix)
        + originalRelativePath;
}

void addSelectableRow(PackageMediaMaintenanceBrowserRows& rows,
                      PackageMediaMaintenanceBrowserRowKind kind,
                      std::string text,
                      std::string selectionId,
                      bool selected = false)
{
    PackageMediaMaintenanceBrowserRow row;
    row.kind = kind;
    row.text = std::move(text);
    row.selectionId = std::move(selectionId);
    row.selectable = true;
    row.selected = selected;
    rows.rows.push_back(std::move(row));
}

void addRestoreSelectionControlRows(PackageMediaMaintenanceBrowserRows& rows,
                                    const PackageMediaRestoreEntrySelection& selection,
                                    bool restoreSelectionInteractive)
{
    if (!selection.hasRestorableEntries || !restoreSelectionInteractive)
        return;

    addSelectableRow(rows,
                     PackageMediaMaintenanceBrowserRowKind::restoreSelectAll,
                     "Restore entries: select all",
                     std::string(packageMediaMaintenanceBrowserSelectionIds::restoreSelectAll));

    addSelectableRow(rows,
                     PackageMediaMaintenanceBrowserRowKind::restoreClearSelection,
                     "Restore entries: clear selection",
                     std::string(packageMediaMaintenanceBrowserSelectionIds::restoreClearSelection));
}

void addSelectedBatchDetailRows(PackageMediaMaintenanceBrowserRows& rows,
                                const PackageMediaMaintenanceViewModel& model,
                                std::size_t maxEntryPreviewRows,
                                bool restoreSelectionInteractive)
{
    if (!model.hasSelectedBatch)
    {
        addRow(rows,
               PackageMediaMaintenanceBrowserRowKind::selectedBatchEntrySummary,
               "Entries: no cleanup batch selected");
        addRow(rows,
               PackageMediaMaintenanceBrowserRowKind::selectedBatchReviewSummary,
               "Review: no cleanup batch selected");
        addRow(rows,
               PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
               "Entry paths: no cleanup batch selected");
        return;
    }

    const auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
    addRow(rows,
           PackageMediaMaintenanceBrowserRowKind::selectedBatchEntrySummary,
           selectedBatchEntrySummary(selected, model.restoreEntrySelection));
    addRow(rows,
           PackageMediaMaintenanceBrowserRowKind::selectedBatchReviewSummary,
           selectedBatchReviewSummary(selected));
    addRestoreSelectionControlRows(rows, model.restoreEntrySelection, restoreSelectionInteractive);

    if (model.restoreEntrySelection.entries.empty() || maxEntryPreviewRows == 0)
    {
        addRow(rows,
               PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
               "Entry paths: no moved entries");
        return;
    }

    const auto previewCount = std::min(maxEntryPreviewRows, model.restoreEntrySelection.entries.size());
    for (std::size_t index = 0; index < previewCount; ++index)
    {
        const auto& entry = model.restoreEntrySelection.entries[index];
        PackageMediaMaintenanceBrowserRow row;
        row.kind = PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath;
        row.text = selectedBatchEntryPath(entry, index);
        row.restoreOriginalRelativePath = entry.originalRelativePath;
        row.selectionId = makeRestoreEntrySelectionId(entry.originalRelativePath);
        row.selectable = restoreSelectionInteractive && entry.restorable;
        row.selected = entry.selected;
        rows.rows.push_back(std::move(row));
    }

    if (model.restoreEntrySelection.entries.size() > previewCount)
    {
        addRow(rows,
               PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
               "Entry paths: +"
                   + std::to_string(model.restoreEntrySelection.entries.size() - previewCount)
                   + " more");
    }
}
} // namespace

PackageMediaMaintenanceBrowserRows buildPackageMediaMaintenanceBrowserRows(
    const PackageMediaMaintenanceViewModel& model,
    PackageMediaMaintenanceBrowserRowsOptions options)
{
    PackageMediaMaintenanceBrowserRows rows;
    rows.cleanupAction.text = "Clean";
    rows.cleanupAction.visible = options.hasSnapshot;
    rows.cleanupAction.enabled =
        options.hasSnapshot && model.cleanupReviewAvailable && !options.packageWorkInProgress;
    rows.cleanupAction.disabledReason = options.hasSnapshot
        ? cleanupDisabledReason(model, options.packageWorkInProgress)
        : "Waiting for package media scan.";

    rows.restoreAction.text = "Restore";
    rows.restoreAction.visible = options.hasSnapshot;
    rows.restoreAction.enabled =
        options.hasSnapshot && model.restoreActionEnabled && !options.packageWorkInProgress;
    rows.restoreAction.disabledReason = options.hasSnapshot
        ? restoreDisabledReason(model, options.packageWorkInProgress)
        : "Waiting for package media scan.";

    addRow(rows,
           PackageMediaMaintenanceBrowserRowKind::library,
           "Library: Samples / Plugins / Presets");

    if (options.scanRunning)
        addRow(rows, PackageMediaMaintenanceBrowserRowKind::scanState, "Media: scanning package");

    if (!options.hasSnapshot)
    {
        addRow(rows, PackageMediaMaintenanceBrowserRowKind::batchCount, "Batches: waiting for scan");
        addRow(rows, PackageMediaMaintenanceBrowserRowKind::restoreSummary, "Restore: unavailable");
        return rows;
    }

    addRow(rows,
           PackageMediaMaintenanceBrowserRowKind::mediaStatus,
           "Media: " + model.inventoryStatus.statusText);
    addRow(rows,
           PackageMediaMaintenanceBrowserRowKind::candidateSummary,
           "Candidates: "
               + std::to_string(model.cleanupCandidateCount)
               + " media / "
               + std::to_string(model.staleStagingCandidateCount)
               + " staging");
    addRow(rows,
           PackageMediaMaintenanceBrowserRowKind::cleanupSummary,
           cleanupSummary(model, options.packageWorkInProgress));
    addRow(rows,
           PackageMediaMaintenanceBrowserRowKind::batchCount,
           "Batches: " + std::to_string(model.batches.size()));

    if (model.hasSelectedBatch)
    {
        const auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
        addRow(rows,
               PackageMediaMaintenanceBrowserRowKind::selectedBatch,
               "Selected: "
                   + shortenBrowserValue(selected.cleanupId)
                   + " | "
                   + statusLabel(selected.status.kind));
    }
    else
    {
        addRow(rows, PackageMediaMaintenanceBrowserRowKind::selectedBatch, "Selected: none");
    }

    addRow(rows, PackageMediaMaintenanceBrowserRowKind::restoreSummary, restoreSummary(model));

    const auto restoreSelectionKeyboardAvailable =
        canUseRestoreSelectionKeyboardCommands(model, options.packageWorkInProgress);
    addSelectedBatchDetailRows(rows,
                               model,
                               options.maxEntryPreviewRows,
                               restoreSelectionKeyboardAvailable);

    for (const auto index : makeVisibleBatchIndexes(model, options.maxBatchRows))
    {
        const auto& batch = model.batches[index];
        PackageMediaMaintenanceBrowserRow row;
        row.kind = PackageMediaMaintenanceBrowserRowKind::batch;
        row.text = makeBatchLine(batch, index);
        row.selectionId = makeBatchSelectionId(batch.cleanupId);
        row.cleanupId = batch.cleanupId;
        row.selectable = true;
        row.selected = batch.selected;

        if (row.selected)
            rows.selectedRowIndex = static_cast<int>(rows.rows.size());

        rows.rows.push_back(std::move(row));
    }

    const auto issueCount = !model.discoveryIssues.empty()
        ? model.discoveryIssues.size()
        : (model.discoveryError.empty() ? std::size_t { 0 } : std::size_t { 1 });
    addRow(rows,
           PackageMediaMaintenanceBrowserRowKind::discoveryIssues,
           model.hasDiscoveryIssues
               ? "Issues: " + std::to_string(issueCount) + " discovery"
               : "Issues: none");

    rows.restoreSelectAllKeyboardEnabled = restoreSelectionKeyboardAvailable;
    rows.restoreClearSelectionKeyboardEnabled =
        restoreSelectionKeyboardAvailable && model.restoreEntrySelection.hasSelection;

    auto focusedIndex = rows.rows.end();
    if (!options.focusedSelectionId.empty())
    {
        focusedIndex = std::find_if(rows.rows.begin(),
                                    rows.rows.end(),
                                    [&options](const PackageMediaMaintenanceBrowserRow& row)
                                    {
                                        return row.selectable
                                            && row.selectionId == options.focusedSelectionId;
                                    });
    }

    if (focusedIndex == rows.rows.end())
    {
        focusedIndex = std::find_if(rows.rows.begin(),
                                    rows.rows.end(),
                                    [](const PackageMediaMaintenanceBrowserRow& row)
                                    {
                                        return row.selectable
                                            && row.kind == PackageMediaMaintenanceBrowserRowKind::batch
                                            && row.selected;
                                    });
    }

    if (focusedIndex == rows.rows.end())
    {
        focusedIndex = std::find_if(rows.rows.begin(),
                                    rows.rows.end(),
                                    [](const PackageMediaMaintenanceBrowserRow& row)
                                    {
                                        return row.selectable;
                                    });
    }

    if (focusedIndex != rows.rows.end())
    {
        focusedIndex->keyboardFocused = true;
        rows.focusedRowIndex = static_cast<int>(std::distance(rows.rows.begin(), focusedIndex));
        rows.focusedSelectionId = focusedIndex->selectionId;
        rows.restoreToggleFocusedEntryKeyboardEnabled =
            restoreSelectionKeyboardAvailable
            && focusedIndex->kind == PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath
            && focusedIndex->selectable;
        rows.focusedRestoreEntryOriginalRelativePath = focusedIndex->restoreOriginalRelativePath;
    }

    return rows;
}

std::string selectAdjacentPackageMediaCleanupId(
    const PackageMediaMaintenanceViewModel& model,
    PackageMediaMaintenanceBrowserSelectionDirection direction)
{
    if (model.batches.empty())
        return {};

    auto index = model.selectedBatchIndex >= 0
        ? static_cast<std::size_t>(model.selectedBatchIndex)
        : std::size_t { 0 };

    if (index >= model.batches.size())
        index = 0;

    if (direction == PackageMediaMaintenanceBrowserSelectionDirection::previous)
    {
        if (index > 0)
            --index;
    }
    else if (index + 1 < model.batches.size())
    {
        ++index;
    }

    return model.batches[index].cleanupId;
}

std::string focusAdjacentPackageMediaMaintenanceBrowserSelectionId(
    const PackageMediaMaintenanceBrowserRows& rows,
    PackageMediaMaintenanceBrowserFocusDirection direction)
{
    std::vector<std::size_t> selectableIndexes;
    selectableIndexes.reserve(rows.rows.size());
    for (std::size_t index = 0; index < rows.rows.size(); ++index)
    {
        if (rows.rows[index].selectable && !rows.rows[index].selectionId.empty())
            selectableIndexes.push_back(index);
    }

    if (selectableIndexes.empty())
        return {};

    auto current = selectableIndexes.begin();
    if (rows.focusedRowIndex >= 0)
    {
        const auto focusedIndex = static_cast<std::size_t>(rows.focusedRowIndex);
        current = std::find(selectableIndexes.begin(), selectableIndexes.end(), focusedIndex);
    }

    if (current == selectableIndexes.end())
        current = selectableIndexes.begin();

    if (direction == PackageMediaMaintenanceBrowserFocusDirection::previous)
    {
        if (current != selectableIndexes.begin())
            --current;
    }
    else if (current + 1 != selectableIndexes.end())
    {
        ++current;
    }

    return rows.rows[*current].selectionId;
}
} // namespace projectname
