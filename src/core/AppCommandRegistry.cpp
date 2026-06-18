// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AppCommandRegistry.h"

#include <algorithm>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] AppCommandMetadata makeCommand(std::string_view id,
                                             std::string label,
                                             std::string description,
                                             AppCommandScope scope)
{
    AppCommandMetadata metadata;
    metadata.id = std::string(id);
    metadata.label = std::move(label);
    metadata.description = std::move(description);
    metadata.scope = scope;
    return metadata;
}

void registerPrototypeCommand(AppCommandRegistry& registry,
                              std::string_view id,
                              std::string label,
                              std::string description,
                              AppCommandScope scope,
                              bool enabled,
                              std::string disabledReason)
{
    static_cast<void>(registry.registerCommand(makeCommand(id,
                                                          std::move(label),
                                                          std::move(description),
                                                          scope),
                                             enabled,
                                             std::move(disabledReason)));
}
} // namespace

bool AppCommandRegistry::registerCommand(AppCommandMetadata metadata,
                                         bool enabled,
                                         std::string disabledReason)
{
    if (metadata.id.empty() || findCommand(metadata.id) != nullptr)
        return false;

    if (enabled)
        disabledReason.clear();

    commands_.push_back({ std::move(metadata), enabled, std::move(disabledReason) });
    return true;
}

AppCommandResult AppCommandResult::handled()
{
    return { AppCommandResultStatus::handled, {} };
}

AppCommandResult AppCommandResult::handledWithStatus(std::string message)
{
    return { AppCommandResultStatus::handledWithStatus, std::move(message) };
}

AppCommandResult AppCommandResult::disabled(std::string reason)
{
    return { AppCommandResultStatus::disabled, std::move(reason) };
}

AppCommandResult AppCommandResult::failed(std::string error)
{
    return { AppCommandResultStatus::failed, std::move(error) };
}

const AppCommandState* AppCommandRegistry::findCommand(std::string_view id) const noexcept
{
    const auto iterator = std::find_if(commands_.begin(),
                                       commands_.end(),
                                       [id](const AppCommandState& command)
                                       {
                                           return command.metadata.id == id;
                                       });

    return iterator == commands_.end() ? nullptr : &*iterator;
}

bool AppCommandRegistry::isEnabled(std::string_view id) const noexcept
{
    const auto* command = findCommand(id);
    return command != nullptr && command->enabled;
}

std::size_t AppCommandRegistry::size() const noexcept
{
    return commands_.size();
}

const std::vector<AppCommandState>& AppCommandRegistry::commands() const noexcept
{
    return commands_;
}

bool AppCommandDispatcher::registerHandler(std::string id, Handler handler)
{
    if (id.empty() || !handler)
        return false;

    const auto iterator = std::find_if(handlers_.begin(),
                                       handlers_.end(),
                                       [&id](const HandlerEntry& entry)
                                       {
                                           return entry.id == id;
                                       });
    if (iterator != handlers_.end())
        return false;

    handlers_.push_back({ std::move(id), std::move(handler) });
    return true;
}

AppCommandResult AppCommandDispatcher::dispatch(const AppCommandRegistry& registry,
                                                std::string_view id) const
{
    const auto* command = registry.findCommand(id);
    if (command == nullptr)
        return AppCommandResult::failed("Unknown command: " + std::string(id));

    if (!command->enabled)
    {
        if (!command->disabledReason.empty())
            return AppCommandResult::disabled(command->disabledReason);

        return AppCommandResult::disabled("Command is disabled.");
    }

    const auto iterator = std::find_if(handlers_.begin(),
                                       handlers_.end(),
                                       [id](const HandlerEntry& entry)
                                       {
                                           return entry.id == id;
                                       });
    if (iterator == handlers_.end())
        return AppCommandResult::failed("No handler registered for command: " + std::string(id));

    return iterator->handler();
}

AppCommandRegistry makePrototypeAppCommandRegistry(AppCommandAvailability availability)
{
    AppCommandRegistry registry;

    registerPrototypeCommand(registry,
                             AppCommandIds::transportPlay,
                             "Play",
                             "Start transport playback.",
                             AppCommandScope::transport,
                             availability.canPlay,
                             "Playback is unavailable.");
    registerPrototypeCommand(registry,
                             AppCommandIds::transportStop,
                             "Stop",
                             "Stop transport playback.",
                             AppCommandScope::transport,
                             availability.canStop,
                             "Stopping is unavailable.");
    registerPrototypeCommand(registry,
                             AppCommandIds::projectNew,
                             "New Project",
                             "Create a new project package with a native chooser.",
                             AppCommandScope::project,
                             availability.canNewProject,
                             "Project creation is unavailable.");
    registerPrototypeCommand(registry,
                             AppCommandIds::projectSave,
                             "Save",
                             "Save the current project package.",
                             AppCommandScope::project,
                             availability.canSave,
                             "Project saving is unavailable.");
    registerPrototypeCommand(registry,
                             AppCommandIds::projectSaveAs,
                             "Save As",
                             "Save the current project package to a chosen package.",
                             AppCommandScope::project,
                             availability.canSaveAs,
                             "Project Save As is unavailable.");
    registerPrototypeCommand(registry,
                             AppCommandIds::projectOpen,
                             "Open",
                             "Open a project package with a native chooser.",
                             AppCommandScope::project,
                             availability.canOpen,
                             "Project opening is unavailable.");
    registerPrototypeCommand(registry,
                             AppCommandIds::editUndo,
                             "Undo",
                             "Undo the latest imported clip edit.",
                             AppCommandScope::project,
                             availability.canUndoImportedClipEdit,
                             "No imported clip edit is available to undo.");
    registerPrototypeCommand(registry,
                             AppCommandIds::editRedo,
                             "Redo",
                             "Redo the latest imported clip edit.",
                             AppCommandScope::project,
                             availability.canRedoImportedClipEdit,
                             "No imported clip edit is available to redo.");
    registerPrototypeCommand(registry,
                             AppCommandIds::audioImport,
                             "Import Audio",
                             "Import a PCM16 WAV file into the project package.",
                             AppCommandScope::project,
                             availability.canImportAudio,
                             "Audio import is already in progress.");
    registerPrototypeCommand(registry,
                             AppCommandIds::audioImportCancel,
                             "Cancel Import",
                             "Cancel the active audio import when it is still safe to stop.",
                             AppCommandScope::project,
                             availability.canCancelImport,
                             "No cancellable audio import is running.");
    registerPrototypeCommand(registry,
                             AppCommandIds::timelinePreparationCancel,
                             "Cancel Timeline Preparation",
                             "Cancel the active background timeline audio preparation.",
                             AppCommandScope::transport,
                             availability.canCancelTimelinePreparation,
                             "No cancellable timeline preparation is running.");
    registerPrototypeCommand(registry,
                             AppCommandIds::audioSettingsShow,
                             "Audio/MIDI Settings",
                             "Show the Audio/MIDI device setup dialog.",
                             AppCommandScope::audioDevice,
                             availability.canShowAudioSettings,
                             "Audio/MIDI settings are unavailable.");

    return registry;
}
} // namespace projectname
