// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <optional>

namespace projectname
{
enum class WorkspaceCommandKey
{
    other,
    left,
    right,
    up,
    down,
};

struct WorkspaceCommandShortcut
{
    WorkspaceCommandKey key = WorkspaceCommandKey::other;
    bool commandModifier = false;
};

enum class WorkspaceCommand
{
    selectPreviousClip,
    selectNextClip,
    panViewportLeft,
    panViewportRight,
    zoomViewportIn,
    zoomViewportOut,
};

struct WorkspaceCommandAvailability
{
    bool canSelectPreviousClip = false;
    bool canSelectNextClip = false;
    bool canPanViewportLeft = false;
    bool canPanViewportRight = false;
    bool canZoomViewportIn = false;
    bool canZoomViewportOut = false;
};

[[nodiscard]] std::optional<WorkspaceCommand> routeWorkspaceCommand(
    WorkspaceCommandShortcut shortcut,
    WorkspaceCommandAvailability availability) noexcept;
} // namespace projectname
