// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ImportedMediaPackageInventory.h"
#include "PackageMediaCleanupBatchDiscovery.h"
#include "PackageMediaCleanupStatus.h"
#include "PackageMediaRestoreEntrySelection.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace projectname
{
struct PackageMediaMaintenanceBatchEntryPreview
{
    std::string originalRelativePath;
    std::string quarantineRelativePath;
    bool restored = false;
    bool restoreConflict = false;
    bool hasError = false;
};

struct PackageMediaMaintenanceBatchRow
{
    std::string cleanupId;
    std::string createdAtUtc;
    std::filesystem::path manifestRelativePath;
    std::filesystem::path manifestPath;
    PackageMediaCleanupStatus status;
    std::size_t movedEntryCount = 0;
    std::size_t restoredEntryCount = 0;
    std::size_t conflictCount = 0;
    std::size_t errorCount = 0;
    std::size_t restorableEntryCount = 0;
    std::vector<PackageMediaMaintenanceBatchEntryPreview> entryPreviews;
    PackageMediaRestoreEntrySelection restoreEntrySelection;
    bool selected = false;
    bool restoreActionEnabled = false;
    std::string restoreUnavailableReason;
};

struct PackageMediaMaintenanceViewModelRequest
{
    ImportedMediaPackageInventory inventory;
    PackageMediaCleanupBatchDiscoveryResult discovery;
    std::string selectedCleanupId;
    std::vector<std::string> selectedRestoreOriginalRelativePaths;
};

struct PackageMediaMaintenanceViewModel
{
    PackageMediaCleanupStatus inventoryStatus;
    std::size_t cleanupCandidateCount = 0;
    std::size_t staleStagingCandidateCount = 0;
    std::size_t missingReferenceCount = 0;
    std::size_t unsafeReferenceCount = 0;
    bool cleanupReviewAvailable = false;

    std::vector<PackageMediaMaintenanceBatchRow> batches;
    std::vector<PackageMediaCleanupBatchDiscoveryIssue> discoveryIssues;
    std::string discoveryError;
    bool hasDiscoveryIssues = false;

    std::string selectedCleanupId;
    int selectedBatchIndex = -1;
    bool hasSelectedBatch = false;
    PackageMediaRestoreEntrySelection restoreEntrySelection;
    bool restoreActionEnabled = false;
    std::string restoreActionText = "Restore Batch";
    std::string restoreUnavailableReason;
};

[[nodiscard]] PackageMediaMaintenanceViewModel buildPackageMediaMaintenanceViewModel(
    PackageMediaMaintenanceViewModelRequest request);

[[nodiscard]] PackageMediaMaintenanceViewModel selectPackageMediaMaintenanceBatch(
    PackageMediaMaintenanceViewModel model,
    std::string selectedCleanupId);

[[nodiscard]] PackageMediaMaintenanceViewModel selectAllPackageMediaRestoreEntriesInSelectedBatch(
    PackageMediaMaintenanceViewModel model);

[[nodiscard]] PackageMediaMaintenanceViewModel clearPackageMediaRestoreEntriesInSelectedBatch(
    PackageMediaMaintenanceViewModel model);

[[nodiscard]] PackageMediaMaintenanceViewModel togglePackageMediaRestoreEntryInSelectedBatch(
    PackageMediaMaintenanceViewModel model,
    const std::string& originalRelativePath);
} // namespace projectname
