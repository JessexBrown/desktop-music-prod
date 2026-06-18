#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "core/AppCommandRegistry.h"
#include "core/AppSession.h"
#include "core/AudioDeviceService.h"
#include "core/BackgroundAudioImportJob.h"
#include "core/BackgroundMediaRelinkPreparationJob.h"
#include "core/BackgroundTimelinePlaybackPreparationJob.h"
#include "core/ImportedClipInspector.h"
#include "core/ImportedClipInspectorEditDraft.h"
#include "core/TimelineClipLane.h"
#include "core/TimelinePlaybackPreparationCompletion.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

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
    [[nodiscard]] int getTimelineClipViewportWidthPixels() const;
    void setSubtitle(juce::String subtitle);
    void setLines(juce::StringArray lines);
    void resized() override;

private:
    [[nodiscard]] juce::Rectangle<int> getTimelineContentBounds() const;
    [[nodiscard]] juce::Rectangle<int> getViewportControlBounds() const;
    [[nodiscard]] bool shouldShowViewportControls() const;
    [[nodiscard]] bool shouldPaintKeyboardFocus() const;
    void paintTimelineClipLane(juce::Graphics& graphics, juce::Rectangle<int> bounds);

    juce::TextButton panLeftViewportButton_ { "<" };
    juce::TextButton resetViewportButton_ { "0" };
    juce::TextButton fitClipsViewportButton_ { "Fit" };
    juce::TextButton centerSelectedViewportButton_ { "C" };
    juce::TextButton zoomOutViewportButton_ { "-" };
    juce::TextButton zoomInViewportButton_ { "+" };
    juce::TextButton panRightViewportButton_ { ">" };
    juce::String title_;
    juce::String subtitle_;
    juce::StringArray lines_;
    projectname::TimelineClipLaneLayout timelineClipLane_;
    std::function<void(std::string)> timelineClipSelected_;
    std::function<void()> previousTimelineClipRequested_;
    std::function<void()> nextTimelineClipRequested_;
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
    void saveProject();
    void openProject();
    [[nodiscard]] projectname::AppCommandResult undoImportedClipEdit();
    [[nodiscard]] projectname::AppCommandResult redoImportedClipEdit();
    void importAudio();
    void relinkSelectedClipMedia();
    void cancelMediaRelinkPreparation();
    void cancelAudioImport();
    void cancelTimelinePlaybackPreparation();
    void handleAudioImportResult(const juce::FileChooser& chooser);
    void handleMediaRelinkResult(const juce::FileChooser& chooser);
    void pollAudioImportJob();
    void pollMediaRelinkPreparationJob();
    void pollTimelinePlaybackPreparationJob();
    void updateAudioImportProgress(const projectname::BackgroundAudioImportProgress& progress);
    void updateMediaRelinkPreparationProgress(
        const projectname::BackgroundMediaRelinkPreparationProgress& progress);
    void updateTimelinePlaybackPreparationProgress(
        const projectname::BackgroundTimelinePlaybackPreparationProgress& progress);
    void applyCompletedAudioImport(projectname::BackgroundAudioImportResult result);
    void applyCompletedMediaRelinkPreparation(projectname::BackgroundMediaRelinkPreparationResult result);
    void applyCompletedTimelinePlaybackPreparation(projectname::BackgroundTimelinePlaybackPreparationResult result);
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
    void applyMixerControlChange();
    void showAudioSettings();
    void setStatus(juce::String status);
    [[nodiscard]] std::filesystem::path getDefaultProjectPackagePath() const;

    projectname::AudioDeviceService audioService_;
    projectname::AppSession session_;

    juce::TextButton playButton_ { "Play" };
    juce::TextButton stopButton_ { "Stop" };
    juce::TextButton saveButton_ { "Save" };
    juce::TextButton openButton_ { "Open" };
    juce::TextButton importButton_ { "Import" };
    juce::TextButton cancelImportButton_ { "Cancel" };
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
    std::unique_ptr<juce::FileChooser> audioImportChooser_;
    std::unique_ptr<juce::FileChooser> mediaRelinkChooser_;
    std::unique_ptr<projectname::BackgroundAudioImportJob> audioImportJob_;
    std::unique_ptr<projectname::BackgroundMediaRelinkPreparationJob> mediaRelinkPreparationJob_;
    std::unique_ptr<projectname::BackgroundTimelinePlaybackPreparationJob> timelinePlaybackPreparationJob_;
    bool canCancelAudioImport_ = false;
    bool canCancelMediaRelinkPreparation_ = false;
    bool canCancelTimelinePlaybackPreparation_ = false;

    WorkspacePanel browserPanel_;
    WorkspacePanel workspacePanel_;
    WorkspacePanel inspectorPanel_;
    WorkspacePanel devicePanel_;
    WorkspacePanel mixerPanel_;
    double lastInspectorOutputSampleRateHz_ = -1.0;
    bool refreshingMixerControls_ = false;
    projectname::ImportedClipInspectorEditDraft inspectorEditDraft_;
};
