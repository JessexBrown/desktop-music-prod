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
        return "Restore: available";

    if (!model.hasSelectedBatch)
        return "Restore: select batch";

    const auto& row = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
    if (row.conflictCount > 0)
        return "Restore: conflict review";

    if (row.errorCount > 0)
        return "Restore: failure review";

    if (row.movedEntryCount > 0 && row.restoredEntryCount == row.movedEntryCount)
        return "Restore: already restored";

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

[[nodiscard]] std::string entryStateLabel(
    const PackageMediaMaintenanceBatchEntryPreview& entry)
{
    if (entry.restoreConflict)
        return "conflict";

    if (entry.hasError)
        return "error";

    if (entry.restored)
        return "restored";

    return "restorable";
}

[[nodiscard]] std::string selectedBatchEntrySummary(
    const PackageMediaMaintenanceBatchRow& row)
{
    return "Entries: "
        + std::to_string(row.movedEntryCount)
        + " moved / "
        + std::to_string(row.restoredEntryCount)
        + " restored / "
        + std::to_string(row.restorableEntryCount)
        + " restorable";
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
    const PackageMediaMaintenanceBatchEntryPreview& entry,
    std::size_t entryIndex)
{
    return "Entry "
        + std::to_string(entryIndex + 1)
        + ": "
        + entryStateLabel(entry)
        + " | "
        + entry.originalRelativePath
        + " -> "
        + entry.quarantineRelativePath;
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

void addRow(PackageMediaMaintenanceBrowserRows& rows,
            PackageMediaMaintenanceBrowserRowKind kind,
            std::string text)
{
    PackageMediaMaintenanceBrowserRow row;
    row.kind = kind;
    row.text = std::move(text);
    rows.rows.push_back(std::move(row));
}

void addSelectedBatchDetailRows(PackageMediaMaintenanceBrowserRows& rows,
                                const PackageMediaMaintenanceViewModel& model,
                                std::size_t maxEntryPreviewRows)
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
           selectedBatchEntrySummary(selected));
    addRow(rows,
           PackageMediaMaintenanceBrowserRowKind::selectedBatchReviewSummary,
           selectedBatchReviewSummary(selected));

    if (selected.entryPreviews.empty() || maxEntryPreviewRows == 0)
    {
        addRow(rows,
               PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
               "Entry paths: no moved entries");
        return;
    }

    const auto previewCount = std::min(maxEntryPreviewRows, selected.entryPreviews.size());
    for (std::size_t index = 0; index < previewCount; ++index)
    {
        addRow(rows,
               PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
               selectedBatchEntryPath(selected.entryPreviews[index], index));
    }

    if (selected.entryPreviews.size() > previewCount)
    {
        addRow(rows,
               PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
               "Entry paths: +"
                   + std::to_string(selected.entryPreviews.size() - previewCount)
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
    addSelectedBatchDetailRows(rows, model, options.maxEntryPreviewRows);

    for (const auto index : makeVisibleBatchIndexes(model, options.maxBatchRows))
    {
        const auto& batch = model.batches[index];
        PackageMediaMaintenanceBrowserRow row;
        row.kind = PackageMediaMaintenanceBrowserRowKind::batch;
        row.text = makeBatchLine(batch, index);
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
} // namespace projectname
