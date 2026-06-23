// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace projectname
{
namespace AppCommandIds
{
inline constexpr std::string_view transportPlay = "transport.play";
inline constexpr std::string_view transportStop = "transport.stop";
inline constexpr std::string_view projectNew = "project.new";
inline constexpr std::string_view projectSave = "project.save";
inline constexpr std::string_view projectSaveAs = "project.saveAs";
inline constexpr std::string_view projectSaveAsCancel = "project.saveAs.cancel";
inline constexpr std::string_view projectCopyFailedSaveAsTarget =
    "project.saveAs.copyFailedTarget";
inline constexpr std::string_view projectRetryFailedSaveAsTargetManifest =
    "project.saveAs.retryFailedTargetManifest";
inline constexpr std::string_view projectOpen = "project.open";
inline constexpr std::string_view editUndo = "edit.undo";
inline constexpr std::string_view editRedo = "edit.redo";
inline constexpr std::string_view audioImport = "audio.import";
inline constexpr std::string_view audioImportCancel = "audio.import.cancel";
inline constexpr std::string_view timelinePreparationCancel = "timeline.preparation.cancel";
inline constexpr std::string_view audioSettingsShow = "audio.settings.show";
inline constexpr std::string_view audioSettingsReset = "audio.settings.reset";
} // namespace AppCommandIds

enum class AppCommandScope
{
    focusedSurface,
    project,
    transport,
    audioDevice,
    app,
};

struct AppCommandMetadata
{
    std::string id;
    std::string label;
    std::string description;
    AppCommandScope scope = AppCommandScope::app;
};

struct AppCommandState
{
    AppCommandMetadata metadata;
    bool enabled = true;
    std::string disabledReason;
};

struct AppCommandAvailability
{
    bool canPlay = true;
    bool canStop = true;
    bool canNewProject = true;
    bool canSave = true;
    bool canSaveAs = true;
    bool canCancelSaveAs = false;
    bool canCopyFailedSaveAsTarget = false;
    bool canRetryFailedSaveAsTargetManifest = false;
    bool canOpen = true;
    bool canUndoImportedClipEdit = false;
    bool canRedoImportedClipEdit = false;
    bool canImportAudio = true;
    bool canCancelImport = false;
    bool canCancelTimelinePreparation = false;
    bool canShowAudioSettings = true;
    bool canResetAudioSettings = true;
};

enum class AppCommandResultStatus
{
    handled,
    handledWithStatus,
    disabled,
    failed,
};

struct AppCommandResult
{
    AppCommandResultStatus status = AppCommandResultStatus::handled;
    std::string message;

    [[nodiscard]] static AppCommandResult handled();
    [[nodiscard]] static AppCommandResult handledWithStatus(std::string message);
    [[nodiscard]] static AppCommandResult disabled(std::string reason);
    [[nodiscard]] static AppCommandResult failed(std::string error);
};

class AppCommandRegistry
{
public:
    [[nodiscard]] bool registerCommand(AppCommandMetadata metadata,
                                       bool enabled,
                                       std::string disabledReason = {});
    [[nodiscard]] const AppCommandState* findCommand(std::string_view id) const noexcept;
    [[nodiscard]] bool isEnabled(std::string_view id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const std::vector<AppCommandState>& commands() const noexcept;

private:
    std::vector<AppCommandState> commands_;
};

class AppCommandDispatcher
{
public:
    using Handler = std::function<AppCommandResult()>;

    [[nodiscard]] bool registerHandler(std::string id, Handler handler);
    [[nodiscard]] AppCommandResult dispatch(const AppCommandRegistry& registry,
                                            std::string_view id) const;

private:
    struct HandlerEntry
    {
        std::string id;
        Handler handler;
    };

    std::vector<HandlerEntry> handlers_;
};

[[nodiscard]] AppCommandRegistry makePrototypeAppCommandRegistry(
    AppCommandAvailability availability = {});
} // namespace projectname
