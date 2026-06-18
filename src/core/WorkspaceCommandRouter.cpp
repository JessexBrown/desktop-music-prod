// SPDX-License-Identifier: AGPL-3.0-or-later

#include "WorkspaceCommandRouter.h"

namespace projectname
{
std::optional<WorkspaceCommand> routeWorkspaceCommand(WorkspaceCommandShortcut shortcut,
                                                      WorkspaceCommandAvailability availability) noexcept
{
    if (shortcut.commandModifier)
    {
        if (shortcut.key == WorkspaceCommandKey::left && availability.canPanViewportLeft)
            return WorkspaceCommand::panViewportLeft;

        if (shortcut.key == WorkspaceCommandKey::right && availability.canPanViewportRight)
            return WorkspaceCommand::panViewportRight;

        if (shortcut.key == WorkspaceCommandKey::up && availability.canZoomViewportIn)
            return WorkspaceCommand::zoomViewportIn;

        if (shortcut.key == WorkspaceCommandKey::down && availability.canZoomViewportOut)
            return WorkspaceCommand::zoomViewportOut;

        return std::nullopt;
    }

    if (shortcut.key == WorkspaceCommandKey::left && availability.canSelectPreviousClip)
        return WorkspaceCommand::selectPreviousClip;

    if (shortcut.key == WorkspaceCommandKey::right && availability.canSelectNextClip)
        return WorkspaceCommand::selectNextClip;

    return std::nullopt;
}
} // namespace projectname
