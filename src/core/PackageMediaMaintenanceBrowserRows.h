// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "PackageMediaMaintenanceViewModel.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace projectname
{
namespace packageMediaMaintenanceBrowserSelectionIds
{
inline constexpr std::string_view batchPrefix = "batch:";
inline constexpr std::string_view restoreEntryPrefix = "restore-entry:";
inline constexpr std::string_view restoreSelectAll = "restore-select-all";
inline constexpr std::string_view restoreClearSelection = "restore-clear-selection";
} // namespace packageMediaMaintenanceBrowserSelectionIds

enum class PackageMediaMaintenanceBrowserRowKind
{
    library,
    scanState,
    mediaStatus,
    candidateSummary,
    cleanupSummary,
    batchCount,
    selectedBatch,
    selectedBatchEntrySummary,
    selectedBatchReviewSummary,
    restoreSelectAll,
    restoreClearSelection,
    selectedBatchEntryPath,
    restoreSummary,
    batch,
    discoveryIssues,
};

struct PackageMediaMaintenanceBrowserRow
{
    PackageMediaMaintenanceBrowserRowKind kind = PackageMediaMaintenanceBrowserRowKind::library;
    std::string text;
    std::string selectionId;
    std::string cleanupId;
    std::string restoreOriginalRelativePath;
    bool selectable = false;
    bool selected = false;
    bool keyboardFocused = false;
};

struct PackageMediaMaintenanceBrowserRowsOptions
{
    bool hasSnapshot = false;
    bool scanRunning = false;
    std::size_t maxBatchRows = 2;
    bool packageWorkInProgress = false;
    std::size_t maxEntryPreviewRows = 2;
    std::string focusedSelectionId;
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
    int focusedRowIndex = -1;
    std::string focusedSelectionId;
    bool restoreSelectAllKeyboardEnabled = false;
    bool restoreClearSelectionKeyboardEnabled = false;
    bool restoreToggleFocusedEntryKeyboardEnabled = false;
    std::string focusedRestoreEntryOriginalRelativePath;
};

enum class PackageMediaMaintenanceBrowserSelectionDirection
{
    previous,
    next,
};

enum class PackageMediaMaintenanceBrowserFocusDirection
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

[[nodiscard]] std::string focusAdjacentPackageMediaMaintenanceBrowserSelectionId(
    const PackageMediaMaintenanceBrowserRows& rows,
    PackageMediaMaintenanceBrowserFocusDirection direction);
} // namespace projectname
