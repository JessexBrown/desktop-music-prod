// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PackagePath.h"

namespace projectname
{
bool isSafePackageRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute())
        return false;

    for (const auto& part : path)
    {
        if (part == "..")
            return false;
    }

    return true;
}

std::filesystem::path resolvePackagePath(const std::filesystem::path& packageDirectory,
                                         const std::filesystem::path& relativePath)
{
    return packageDirectory / relativePath;
}
} // namespace projectname
