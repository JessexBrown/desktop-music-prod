// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "PackageMediaQuarantineRestoreManifest.h"

#include <cstddef>
#include <string>
#include <vector>

namespace projectname
{
struct PackageMediaRestoreEntrySelectionItem
{
    std::string originalRelativePath;
    bool restorable = false;
    bool selected = false;
    std::string unavailableReason;
};

struct PackageMediaRestoreEntrySelection
{
    std::string cleanupId;
    std::vector<PackageMediaRestoreEntrySelectionItem> entries;
    std::vector<std::string> selectedOriginalRelativePaths;
    std::vector<std::string> staleSelectedOriginalRelativePaths;
    std::size_t restorableEntryCount = 0;
    std::size_t selectedRestorableEntryCount = 0;
    bool hasRestorableEntries = false;
    bool hasSelection = false;
    bool blockedByReviewState = false;
    bool restoreActionEnabled = false;
    std::string restoreUnavailableReason;
};

[[nodiscard]] PackageMediaRestoreEntrySelection buildPackageMediaRestoreEntrySelection(
    const PackageMediaQuarantineRestoreManifest& manifest,
    const std::vector<std::string>& selectedOriginalRelativePaths = {});

[[nodiscard]] PackageMediaRestoreEntrySelection selectAllPackageMediaRestoreEntries(
    PackageMediaRestoreEntrySelection selection);

[[nodiscard]] PackageMediaRestoreEntrySelection clearPackageMediaRestoreEntrySelection(
    PackageMediaRestoreEntrySelection selection);

[[nodiscard]] PackageMediaRestoreEntrySelection togglePackageMediaRestoreEntrySelection(
    PackageMediaRestoreEntrySelection selection,
    const std::string& originalRelativePath);
} // namespace projectname
