// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "core/AppCommandRegistry.h"
#include "core/AppSession.h"
#include "core/AppSettings.h"
#include "core/AudioDeviceService.h"
#include "core/BackgroundAudioImportJob.h"
#include "core/BackgroundMediaRelinkPreparationJob.h"
#include "core/BackgroundPackageMediaCleanupJob.h"
#include "core/BackgroundSaveAsPackageCopyJob.h"
#include "core/BackgroundTimelinePlaybackPreparationJob.h"
#include "core/ImportedClipInspector.h"
#include "core/ImportedClipInspectorEditDraft.h"
#include "core/PackageMediaMaintenanceBrowserRows.h"
#include "core/PackageMediaMaintenanceViewModel.h"
#include "core/TimelineClipLane.h"
#include "core/TimelinePlaybackPreparationCompletion.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct WorkspacePanelRow
{
    juce::String text;
    std::string selectionId;
    bool selectable = false;
    bool selected = false;
    bool keyboardFocused = false;
};

class WorkspacePanel final : public juce::Component
{
public:
    WorkspacePanel(juce::String title, juce::String subtitle, juce::StringArray lines);

    void paint(juce::Graphics& graphics) override;
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    void setTimelineClipLane(projectname::TimelineClipLaneLayout layout);
    void setTimelineClipSelectedCallback(std::function<void(std::string)> callback);
    void setTimelineClipKeyboardSelectionCallbacks(std::function<void()> previousCallback,
                                                   std::function<void()> nextCallback);
    void setTimelineViewportKeyboardCallbacks(std::function<void()> panLeftCallback,
                                              std::function<void()> panRightCallback,
                                              std::function<void()> zoomInCallback,
                                              std::function<void()> zoomOutCallback);
    void setTimelineViewportControlCallbacks(std::function<void()> panLeftCallback,
                                             std::function<void()> resetStartCallback,
                                             std::function<void()> fitClipsCallback,
                                             std::function<void()> centerSelectedCallback,
                                             std::function<void()> zoomOutCallback,
                                             std::function<void()> zoomInCallback,
                                             std::function<void()> panRightCallback);
    void setSelectableRowCallback(std::function<void(std::string)> callback);
    void setSelectableRowKeyboardSelectionCallbacks(std::function<void()> previousCallback,
                                                    std::function<void()> nextCallback);
    void setSelectableRowKeyboardFocusCallbacks(std::function<void()> previousCallback,
                                                std::function<void()> nextCallback,
                                                std::function<void()> activateCallback);
    void setRestoreSelectionKeyboardCallbacks(std::function<void()> selectAllCallback,
                                              std::function<void()> clearCallback);
    void setFocusedRowDetailKeyboardCallback(std::function<void()> copyCallback);
    void setPanelAction(juce::String text,
                        bool enabled,
                        juce::String tooltip,
                        std::function<void()> callback);
    void setSecondaryPanelAction(juce::String text,
                                 bool enabled,
                                 juce::String tooltip,
                                 std::function<void()> callback);
    [[nodiscard]] int getTimelineClipViewportWidthPixels() const;
    void setSubtitle(juce::String subtitle);
    void setLines(juce::StringArray lines);
    void setRows(std::vector<WorkspacePanelRow> rows);
    void resized() override;

private:
    [[nodiscard]] juce::Rectangle<int> getRowsContentBounds() const;
    [[nodiscard]] juce::Rectangle<int> getTimelineContentBounds() const;
    [[nodiscard]] juce::Rectangle<int> getViewportControlBounds() const;
    [[nodiscard]] juce::Rectangle<int> getActionButtonBounds() const;
    [[nodiscard]] juce::Rectangle<int> getSecondaryActionButtonBounds() const;
    [[nodiscard]] std::optional<std::size_t> hitTestSelectableRow(juce::Point<int> position) const;
    [[nodiscard]] bool shouldShowViewportControls() const;
    [[nodiscard]] bool shouldShowActionButton() const;
    [[nodiscard]] bool shouldShowSecondaryActionButton() const;
    [[nodiscard]] bool shouldPaintKeyboardFocus() const;
    void paintTimelineClipLane(juce::Graphics& graphics, juce::Rectangle<int> bounds);

    juce::TextButton panLeftViewportButton_ { "<" };
    juce::TextButton resetViewportButton_ { "0" };
    juce::TextButton fitClipsViewportButton_ { "Fit" };
    juce::TextButton centerSelectedViewportButton_ { "C" };
    juce::TextButton zoomOutViewportButton_ { "-" };
    juce::TextButton zoomInViewportButton_ { "+" };
    juce::TextButton panRightViewportButton_ { ">" };
    juce::TextButton actionButton_ { "Action" };
    juce::TextButton secondaryActionButton_ { "Action" };
    juce::String title_;
    juce::String subtitle_;
    std::vector<WorkspacePanelRow> rows_;
    projectname::TimelineClipLaneLayout timelineClipLane_;
    std::function<void(std::string)> timelineClipSelected_;
    std::function<void(std::string)> selectableRowSelected_;
    std::function<void()> panelActionRequested_;
    std::function<void()> secondaryPanelActionRequested_;
    std::function<void()> previousTimelineClipRequested_;
    std::function<void()> nextTimelineClipRequested_;
    std::function<void()> previousSelectableRowRequested_;
    std::function<void()> nextSelectableRowRequested_;
    std::function<void()> previousSelectableRowFocusRequested_;
    std::function<void()> nextSelectableRowFocusRequested_;
    std::function<void()> activateFocusedSelectableRowRequested_;
    std::function<void()> restoreSelectionSelectAllRequested_;
    std::function<void()> restoreSelectionClearRequested_;
    std::function<void()> copyFocusedRowDetailRequested_;
    std::function<void()> timelinePanLeftRequested_;
    std::function<void()> timelinePanRightRequested_;
    std::function<void()> timelineZoomInRequested_;
    std::function<void()> timelineZoomOutRequested_;
    std::function<void()> timelinePanLeftControlRequested_;
    std::function<void()> timelineResetStartRequested_;
    std::function<void()> timelineFitClipsRequested_;
    std::function<void()> timelineCenterSelectedRequested_;
    std::function<void()> timelineZoomOutControlRequested_;
    std::function<void()> timelineZoomInControlRequested_;
    std::function<void()> timelinePanRightControlRequested_;
};

struct PackageMediaMaintenanceScanResult
{
    int generation = 0;
    projectname::PackageMediaMaintenanceViewModel viewModel;
};

class MainComponent final : public juce::Component,
                            private juce::Button::Listener,
                            private juce::Slider::Listener,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    [[nodiscard]] bool runProjectChooserSmokeTest(const std::filesystem::path& scratchRoot,
                                                  std::string& error);
    [[nodiscard]] bool runAppSettingsCorruptionSmokeTest(const std::filesystem::path& scratchRoot,
                                                         std::string& error);
    [[nodiscard]] bool runPackageMediaRestoreDetailSmokeTest(const std::filesystem::path& scratchRoot,
                                                             std::string& error);
    [[nodiscard]] bool runAudioMidiResetSmokeTest(const std::filesystem::path& scratchRoot,
                                                  std::string& error);

private:
    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void timerCallback() override;

    void configureControls();
    [[nodiscard]] projectname::AppCommandRegistry buildAppCommandRegistry() const;
    void refreshAppCommandEnabledState();
    projectname::AppCommandResult dispatchAppCommand(std::string_view commandId);
    void handleAppCommandResult(const projectname::AppCommandResult& result);
    void updateTransportLabels();
    void startPlayback();
    void schedulePreparedTimelinePlayback(projectname::TimelinePlaybackPreparation playback);
    void schedulePreparedTimelineVoicePlayback(projectname::TimelineVoicePlaybackPreparation playback);
    void startTimelinePlaybackPreparation(double outputSampleRateHz, std::int64_t minimumRenderFrameCount);
    void showProjectMenu();
    void newProject();
    void saveProject();
    void saveProjectAs();
    void openProject();
    void handleProjectNewResult(const juce::FileChooser& chooser);
    void handleProjectSaveAsResult(const juce::FileChooser& chooser);
    void handleProjectOpenResult(const juce::FileChooser& chooser);
    [[nodiscard]] bool createProjectFromChooserSelection(juce::File selectedFile,
                                                         std::string& error);
    [[nodiscard]] bool beginSaveAsFromChooserSelection(juce::File selectedFile,
                                                       std::string& error);
    [[nodiscard]] bool openProjectFromChooserSelection(juce::File selectedFile,
                                                       std::string& error);
    [[nodiscard]] bool finishSaveAsPackageCopyForSmoke(
        projectname::ProjectPackageSaveAsCopyStatus& status,
        std::string& error);
    [[nodiscard]] bool finishFailedSaveAsPackageCopyForSmoke(
        projectname::ProjectPackageSaveAsCopyStatus& status,
        std::string& error);
    [[nodiscard]] bool addProjectChooserSmokePackageAsset(std::string& error);
    void refreshAfterProjectPackageChange(juce::String status);
    [[nodiscard]] projectname::AppCommandResult undoImportedClipEdit();
    [[nodiscard]] projectname::AppCommandResult redoImportedClipEdit();
    void importAudio();
    void relinkSelectedClipMedia();
    void cancelMediaRelinkPreparation();
    void cancelAudioImport();
    void cancelSaveAsPackageCopy();
    void cancelTimelinePlaybackPreparation();
    void handleAudioImportResult(const juce::FileChooser& chooser);
    void handleMediaRelinkResult(const juce::FileChooser& chooser);
    void pollAudioImportJob();
    void pollMediaRelinkPreparationJob();
    void pollSaveAsPackageCopyJob();
    void pollTimelinePlaybackPreparationJob();
    void pollPackageMediaCleanupJob();
    void pollPackageMediaMaintenanceScan();
    void updateAudioImportProgress(const projectname::BackgroundAudioImportProgress& progress);
    void updateMediaRelinkPreparationProgress(
        const projectname::BackgroundMediaRelinkPreparationProgress& progress);
    void updateSaveAsPackageCopyProgress(
        const projectname::BackgroundSaveAsPackageCopyProgress& progress);
    void updateTimelinePlaybackPreparationProgress(
        const projectname::BackgroundTimelinePlaybackPreparationProgress& progress);
    void updatePackageMediaCleanupProgress(
        const projectname::BackgroundPackageMediaCleanupProgress& progress);
    void applyCompletedAudioImport(projectname::BackgroundAudioImportResult result);
    void applyCompletedMediaRelinkPreparation(projectname::BackgroundMediaRelinkPreparationResult result);
    void applyCompletedSaveAsPackageCopy(projectname::BackgroundSaveAsPackageCopyResult result);
    void applyCompletedTimelinePlaybackPreparation(projectname::BackgroundTimelinePlaybackPreparationResult result);
    void applyCompletedPackageMediaCleanup(projectname::BackgroundPackageMediaCleanupResult result);
    void requestPackageMediaMaintenanceRefresh();
    void applyPackageMediaMaintenanceScanResult(PackageMediaMaintenanceScanResult result);
    void refreshBrowserPanel();
    [[nodiscard]] bool isPackageMediaMaintenanceScanRunning() const;
    [[nodiscard]] projectname::PackageMediaMaintenanceBrowserRows buildPackageMediaMaintenanceBrowserRowsForUi()
        const;
    void copyPackageMediaMaintenanceDetailAction(
        const projectname::PackageMediaMaintenanceDetailAction& action);
    void activatePackageMediaMaintenanceDetailAction(
        const projectname::PackageMediaMaintenanceDetailAction& action);
    void handlePackageMediaBrowserSelection(std::string selectionId);
    void selectPackageMediaCleanupBatch(std::string cleanupId);
    void selectAdjacentPackageMediaCleanupBatch(
        projectname::PackageMediaMaintenanceBrowserSelectionDirection direction);
    void focusAdjacentPackageMediaBrowserRow(projectname::PackageMediaMaintenanceBrowserFocusDirection direction);
    void activateFocusedPackageMediaBrowserRow();
    void selectAllPackageMediaRestoreEntries();
    void clearPackageMediaRestoreEntries();
    void togglePackageMediaRestoreEntry(std::string originalRelativePath);
    void startPackageMediaRestore();
    void startPackageMediaCleanup();
    [[nodiscard]] bool hasActivePackageFileWork() const;
    void refreshWorkspaceTimelineLane();
    void refreshInspectorPanel();
    void refreshInspectorStartBeatControl(const projectname::ImportedClipInspectorState& inspector);
    void refreshInspectorRelinkButtonState();
    [[nodiscard]] juce::Rectangle<int> getInspectorStartBeatRowBounds() const;
    void commitInspectorStartBeatEdit();
    void cancelInspectorStartBeatEdit();
    void selectTimelineClip(std::string clipId);
    void selectAdjacentTimelineClip(projectname::ImportedAudioClipSelectionDirection direction);
    void panTimelineViewport(double deltaBeats);
    void zoomTimelineViewport(double multiplier);
    void resetTimelineViewportStart();
    void fitTimelineViewportToImportedClips();
    void centerTimelineViewportOnSelectedClip();
    void refreshMixerControls();
    void refreshDevicePanel(bool force = false);
    void applyMixerControlChange();
    void showAudioSettings();
    [[nodiscard]] projectname::AppCommandResult resetAudioSetupPreferences();
    void dismissAudioSetupPrompt();
    void loadApplicationSettings();
    void loadApplicationSettingsFromPath(std::filesystem::path settingsPath);
    [[nodiscard]] bool persistApplicationSettings(juce::String failureStatus);
    void persistAudioSetupPreferencesIfChanged(const projectname::AudioDeviceSummary& summary);
    void setStatus(juce::String status);
    [[nodiscard]] std::filesystem::path getDefaultApplicationSettingsPath() const;
    [[nodiscard]] std::filesystem::path getDefaultProjectPackagePath() const;
    [[nodiscard]] const std::filesystem::path& getCurrentProjectPackagePath() const noexcept;
    [[nodiscard]] bool hasProjectChooserOpen() const noexcept;

    projectname::AudioDeviceService audioService_;
    projectname::AppSession session_;
    projectname::AppSettings appSettings_;

    juce::TextButton playButton_ { "Play" };
    juce::TextButton stopButton_ { "Stop" };
    juce::TextButton projectButton_ { "Project" };
    juce::TextButton importButton_ { "Import" };
    juce::TextButton cancelImportButton_ { "Cancel" };
    juce::TextButton cancelSaveAsButton_ { "Cancel Save" };
    juce::TextButton cancelTimelinePreparationButton_ { "Cancel Prep" };
    juce::TextButton audioButton_ { "Audio/MIDI" };
    juce::Slider tempoSlider_;
    juce::Slider trackVolumeSlider_;
    juce::Slider trackPanSlider_;
    juce::Label tempoLabel_;
    juce::Label positionLabel_;
    juce::Label signatureLabel_;
    juce::Label statusLabel_;
    juce::Label mixerTrackLabel_;
    juce::Label trackVolumeLabel_;
    juce::Label trackPanLabel_;
    juce::Label inspectorStartBeatLabel_;
    juce::TextEditor inspectorStartBeatEditor_;
    juce::TextButton inspectorRelinkButton_ { "Relink" };
    juce::TextButton inspectorCancelRelinkButton_ { "Cancel" };
    juce::ToggleButton muteToggle_ { "Mute" };
    juce::ToggleButton soloToggle_ { "Solo" };
    juce::String statusText_;
    juce::String audioSetupInitializationError_;
    std::filesystem::path appSettingsPath_;
    std::filesystem::path currentProjectPackagePath_;
    std::unique_ptr<juce::FileChooser> projectNewChooser_;
    std::unique_ptr<juce::FileChooser> projectSaveAsChooser_;
    std::unique_ptr<juce::FileChooser> projectOpenChooser_;
    std::unique_ptr<juce::FileChooser> audioImportChooser_;
    std::unique_ptr<juce::FileChooser> mediaRelinkChooser_;
    std::unique_ptr<projectname::BackgroundAudioImportJob> audioImportJob_;
    std::unique_ptr<projectname::BackgroundMediaRelinkPreparationJob> mediaRelinkPreparationJob_;
    std::unique_ptr<projectname::BackgroundSaveAsPackageCopyJob> saveAsPackageCopyJob_;
    std::unique_ptr<projectname::BackgroundTimelinePlaybackPreparationJob> timelinePlaybackPreparationJob_;
    std::unique_ptr<projectname::BackgroundPackageMediaCleanupJob> packageMediaCleanupJob_;
    std::future<PackageMediaMaintenanceScanResult> packageMediaMaintenanceScan_;
    projectname::PackageMediaMaintenanceViewModel packageMediaMaintenanceViewModel_;
    bool hasPackageMediaMaintenanceSnapshot_ = false;
    bool packageMediaMaintenanceRefreshPending_ = false;
    int packageMediaMaintenanceScanGeneration_ = 0;
    std::string selectedPackageMediaCleanupId_;
    std::string packageMediaBrowserFocusedSelectionId_;
    std::vector<std::string> selectedPackageMediaRestoreOriginalPaths_;
    bool canCancelAudioImport_ = false;
    bool canCancelMediaRelinkPreparation_ = false;
    bool canCancelSaveAsPackageCopy_ = false;
    bool canCancelTimelinePlaybackPreparation_ = false;
    bool audioSetupPromptDismissed_ = false;
    std::string lastPersistedAudioSetupPreferenceSignature_;
    std::string lastAudioSetupStatusSignature_;

    WorkspacePanel browserPanel_;
    WorkspacePanel workspacePanel_;
    WorkspacePanel inspectorPanel_;
    WorkspacePanel devicePanel_;
    WorkspacePanel mixerPanel_;
    double lastInspectorOutputSampleRateHz_ = -1.0;
    bool refreshingMixerControls_ = false;
    projectname::ImportedClipInspectorEditDraft inspectorEditDraft_;
};
