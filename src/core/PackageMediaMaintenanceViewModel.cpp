// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PackageMediaMaintenanceViewModel.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] std::size_t countCleanupCandidates(const ImportedMediaPackageInventory& inventory)
{
    return static_cast<std::size_t>(
        std::count_if(inventory.assets.begin(),
                      inventory.assets.end(),
                      [](const ImportedMediaPackageAsset& asset)
                      {
                          return asset.unreferencedCandidate;
                      }));
}

[[nodiscard]] std::size_t countStaleStagingCandidates(
    const ImportedMediaPackageInventory& inventory)
{
    return static_cast<std::size_t>(
        std::count_if(inventory.stagingDirectories.begin(),
                      inventory.stagingDirectories.end(),
                      [](const ImportedMediaPackageStagingDirectory& staging)
                      {
                          return staging.staleCandidate;
                      }));
}

[[nodiscard]] std::size_t countRestoredEntries(
    const PackageMediaQuarantineRestoreManifest& manifest)
{
    return static_cast<std::size_t>(
        std::count_if(manifest.movedEntries.begin(),
                      manifest.movedEntries.end(),
                      [](const PackageMediaQuarantineMovedEntry& entry)
                      {
                          return entry.restored;
                      }));
}

[[nodiscard]] std::size_t countConflictedEntries(
    const PackageMediaQuarantineRestoreManifest& manifest)
{
    return static_cast<std::size_t>(
        std::count_if(manifest.movedEntries.begin(),
                      manifest.movedEntries.end(),
                      [](const PackageMediaQuarantineMovedEntry& entry)
                      {
                          return entry.restoreConflict;
                      }));
}

[[nodiscard]] std::size_t countEntryErrors(const PackageMediaQuarantineRestoreManifest& manifest)
{
    return static_cast<std::size_t>(
        std::count_if(manifest.movedEntries.begin(),
                      manifest.movedEntries.end(),
                      [](const PackageMediaQuarantineMovedEntry& entry)
                      {
                          return !entry.error.empty();
                      }));
}

[[nodiscard]] std::size_t countRestorableEntries(
    const PackageMediaQuarantineRestoreManifest& manifest)
{
    return static_cast<std::size_t>(
        std::count_if(manifest.movedEntries.begin(),
                      manifest.movedEntries.end(),
                      [](const PackageMediaQuarantineMovedEntry& entry)
                      {
                          return !entry.restored
                              && !entry.restoreConflict
                              && entry.error.empty();
                      }));
}

[[nodiscard]] std::string restoreUnavailableReason(
    const PackageMediaMaintenanceBatchRow& row)
{
    if (row.movedEntryCount == 0)
        return "This cleanup batch has no media entries to restore.";

    if (row.restoredEntryCount == row.movedEntryCount)
        return "This cleanup batch has already been restored.";

    if (row.conflictCount > 0)
        return "Review restore conflicts before running restore again.";

    if (row.errorCount > 0)
        return "Review partial restore failures before running restore again.";

    return "No unrestored media is available in this cleanup batch.";
}

[[nodiscard]] bool canRestoreFromBatch(const PackageMediaMaintenanceBatchRow& row) noexcept
{
    return row.restorableEntryCount > 0
        && row.conflictCount == 0
        && row.errorCount == 0;
}

[[nodiscard]] PackageMediaMaintenanceBatchRow makeBatchRow(
    const PackageMediaCleanupBatch& batch,
    const std::vector<std::string>& selectedRestoreOriginalRelativePaths = {})
{
    PackageMediaMaintenanceBatchRow row;
    row.cleanupId = batch.cleanupId;
    row.createdAtUtc = batch.createdAtUtc;
    row.manifestRelativePath = batch.manifestRelativePath;
    row.manifestPath = batch.manifestPath;
    row.status = batch.status;
    row.movedEntryCount = batch.manifest.movedEntries.size();
    row.restoredEntryCount = countRestoredEntries(batch.manifest);
    row.conflictCount = countConflictedEntries(batch.manifest);
    row.errorCount = countEntryErrors(batch.manifest);
    row.restorableEntryCount = countRestorableEntries(batch.manifest);
    row.restoreEntrySelection =
        buildPackageMediaRestoreEntrySelection(batch.manifest, selectedRestoreOriginalRelativePaths);
    row.entryPreviews.reserve(batch.manifest.movedEntries.size());
    for (const auto& entry : batch.manifest.movedEntries)
    {
        PackageMediaMaintenanceBatchEntryPreview preview;
        preview.originalRelativePath = entry.originalRelativePath;
        preview.quarantineRelativePath = entry.quarantineRelativePath;
        preview.restored = entry.restored;
        preview.restoreConflict = entry.restoreConflict;
        preview.hasError = !entry.error.empty();
        row.entryPreviews.push_back(std::move(preview));
    }

    row.restoreActionEnabled = canRestoreFromBatch(row);
    if (!row.restoreActionEnabled)
        row.restoreUnavailableReason = restoreUnavailableReason(row);
    return row;
}

[[nodiscard]] int findSelectedBatchIndex(
    const std::vector<PackageMediaMaintenanceBatchRow>& rows,
    const std::string& selectedCleanupId) noexcept
{
    if (rows.empty())
        return -1;

    if (!selectedCleanupId.empty())
    {
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            if (rows[index].cleanupId == selectedCleanupId)
                return static_cast<int>(index);
        }
    }

    return 0;
}

void refreshModelRestoreSelection(PackageMediaMaintenanceViewModel& model)
{
    model.restoreEntrySelection = {};
    model.restoreActionEnabled = false;
    model.restoreUnavailableReason.clear();

    if (!model.hasSelectedBatch)
    {
        model.restoreUnavailableReason = "Select a cleanup batch to restore.";
        return;
    }

    auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
    model.restoreEntrySelection = selected.restoreEntrySelection;
    model.restoreActionEnabled = model.restoreEntrySelection.restoreActionEnabled;
    model.restoreUnavailableReason = model.restoreEntrySelection.restoreUnavailableReason;
    if (model.restoreUnavailableReason.empty() && !selected.restoreUnavailableReason.empty())
        model.restoreUnavailableReason = selected.restoreUnavailableReason;
}
} // namespace

PackageMediaMaintenanceViewModel buildPackageMediaMaintenanceViewModel(
    PackageMediaMaintenanceViewModelRequest request)
{
    PackageMediaMaintenanceViewModel model;
    model.inventoryStatus = describePackageMediaCleanupInventory(request.inventory);
    model.cleanupCandidateCount = countCleanupCandidates(request.inventory);
    model.staleStagingCandidateCount = countStaleStagingCandidates(request.inventory);
    model.missingReferenceCount = request.inventory.missingReferences.size();
    model.unsafeReferenceCount = request.inventory.unsafeReferences.size();
    model.cleanupReviewAvailable =
        model.inventoryStatus.kind == PackageMediaCleanupStatusKind::reviewAvailable;

    model.discoveryIssues = std::move(request.discovery.issues);
    model.discoveryError = std::move(request.discovery.error);
    model.hasDiscoveryIssues = !model.discoveryIssues.empty() || !model.discoveryError.empty();

    model.batches.reserve(request.discovery.batches.size());
    for (const auto& batch : request.discovery.batches)
    {
        const auto useSelectedRestorePaths = !request.selectedCleanupId.empty()
            && batch.cleanupId == request.selectedCleanupId;
        model.batches.push_back(
            makeBatchRow(batch,
                         useSelectedRestorePaths
                             ? request.selectedRestoreOriginalRelativePaths
                             : std::vector<std::string> {}));
    }

    return selectPackageMediaMaintenanceBatch(std::move(model), std::move(request.selectedCleanupId));
}

PackageMediaMaintenanceViewModel selectPackageMediaMaintenanceBatch(
    PackageMediaMaintenanceViewModel model,
    std::string selectedCleanupId)
{
    for (auto& row : model.batches)
        row.selected = false;

    model.selectedCleanupId.clear();
    model.selectedBatchIndex = findSelectedBatchIndex(model.batches, selectedCleanupId);
    model.hasSelectedBatch = model.selectedBatchIndex >= 0;

    if (!model.hasSelectedBatch)
    {
        refreshModelRestoreSelection(model);
        return model;
    }

    auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
    selected.selected = true;
    model.selectedCleanupId = selected.cleanupId;
    if (selected.cleanupId != selectedCleanupId)
        selected.restoreEntrySelection =
            clearPackageMediaRestoreEntrySelection(std::move(selected.restoreEntrySelection));

    refreshModelRestoreSelection(model);
    return model;
}

PackageMediaMaintenanceViewModel selectAllPackageMediaRestoreEntriesInSelectedBatch(
    PackageMediaMaintenanceViewModel model)
{
    if (!model.hasSelectedBatch)
    {
        refreshModelRestoreSelection(model);
        return model;
    }

    auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
    selected.restoreEntrySelection =
        selectAllPackageMediaRestoreEntries(std::move(selected.restoreEntrySelection));
    refreshModelRestoreSelection(model);
    return model;
}

PackageMediaMaintenanceViewModel clearPackageMediaRestoreEntriesInSelectedBatch(
    PackageMediaMaintenanceViewModel model)
{
    if (!model.hasSelectedBatch)
    {
        refreshModelRestoreSelection(model);
        return model;
    }

    auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
    selected.restoreEntrySelection =
        clearPackageMediaRestoreEntrySelection(std::move(selected.restoreEntrySelection));
    refreshModelRestoreSelection(model);
    return model;
}

PackageMediaMaintenanceViewModel togglePackageMediaRestoreEntryInSelectedBatch(
    PackageMediaMaintenanceViewModel model,
    const std::string& originalRelativePath)
{
    if (!model.hasSelectedBatch)
    {
        refreshModelRestoreSelection(model);
        return model;
    }

    auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
    selected.restoreEntrySelection =
        togglePackageMediaRestoreEntrySelection(std::move(selected.restoreEntrySelection),
                                                originalRelativePath);
    refreshModelRestoreSelection(model);
    return model;
}
} // namespace projectname
