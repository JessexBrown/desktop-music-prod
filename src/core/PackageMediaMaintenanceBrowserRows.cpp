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

[[nodiscard]] std::string restoreDisabledReason(const PackageMediaMaintenanceViewModel& model)
{
    if (model.restoreActionEnabled)
        return {};

    if (!model.restoreUnavailableReason.empty())
        return model.restoreUnavailableReason;

    return model.hasSelectedBatch
        ? "Selected cleanup batch cannot be restored."
        : "Select a cleanup batch to restore.";
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
} // namespace

PackageMediaMaintenanceBrowserRows buildPackageMediaMaintenanceBrowserRows(
    const PackageMediaMaintenanceViewModel& model,
    PackageMediaMaintenanceBrowserRowsOptions options)
{
    PackageMediaMaintenanceBrowserRows rows;
    rows.restoreAction.text = "Restore";
    rows.restoreAction.visible = options.hasSnapshot;
    rows.restoreAction.enabled = options.hasSnapshot && model.restoreActionEnabled;
    rows.restoreAction.disabledReason = options.hasSnapshot
        ? restoreDisabledReason(model)
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
