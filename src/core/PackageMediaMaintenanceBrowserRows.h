// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "PackageMediaMaintenanceViewModel.h"

#include <cstddef>
#include <string>
#include <vector>

namespace projectname
{
enum class PackageMediaMaintenanceBrowserRowKind
{
    library,
    scanState,
    mediaStatus,
    candidateSummary,
    cleanupSummary,
    batchCount,
    selectedBatch,
    restoreSummary,
    batch,
    discoveryIssues,
};

struct PackageMediaMaintenanceBrowserRow
{
    PackageMediaMaintenanceBrowserRowKind kind = PackageMediaMaintenanceBrowserRowKind::library;
    std::string text;
    std::string cleanupId;
    bool selectable = false;
    bool selected = false;
};

struct PackageMediaMaintenanceBrowserRowsOptions
{
    bool hasSnapshot = false;
    bool scanRunning = false;
    std::size_t maxBatchRows = 2;
    bool packageWorkInProgress = false;
};

struct PackageMediaMaintenanceBrowserAction
{
    std::string text;
    bool visible = false;
    bool enabled = false;
    std::string disabledReason;
};

struct PackageMediaMaintenanceBrowserRows
{
    std::vector<PackageMediaMaintenanceBrowserRow> rows;
    PackageMediaMaintenanceBrowserAction cleanupAction;
    PackageMediaMaintenanceBrowserAction restoreAction;
    int selectedRowIndex = -1;
};

enum class PackageMediaMaintenanceBrowserSelectionDirection
{
    previous,
    next,
};

[[nodiscard]] PackageMediaMaintenanceBrowserRows buildPackageMediaMaintenanceBrowserRows(
    const PackageMediaMaintenanceViewModel& model,
    PackageMediaMaintenanceBrowserRowsOptions options);

[[nodiscard]] std::string selectAdjacentPackageMediaCleanupId(
    const PackageMediaMaintenanceViewModel& model,
    PackageMediaMaintenanceBrowserSelectionDirection direction);
} // namespace projectname
