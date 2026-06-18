// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <filesystem>

namespace projectname
{
[[nodiscard]] bool isSafePackageRelativePath(const std::filesystem::path& path);

[[nodiscard]] std::filesystem::path resolvePackagePath(const std::filesystem::path& packageDirectory,
                                                       const std::filesystem::path& relativePath);
} // namespace projectname
