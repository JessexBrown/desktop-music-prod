#include "MainComponent.h"

#include "core/AppCommandRegistry.h"
#include "core/AudioSetupStatus.h"
#include "core/BackgroundPackageMediaCleanupJob.h"
#include "core/ImportedMediaPackageInventory.h"
#include "core/PackageMediaCleanupBatchDiscovery.h"
#include "core/PackageMediaMaintenanceBrowserRows.h"
#include "core/PackageMediaMaintenanceViewModel.h"
#include "core/ProductIdentity.h"
#include "core/WorkspaceCommandRouter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
constexpr int outerMargin = 12;
constexpr int gap = 10;

const auto background = juce::Colour(0xff101114);
const auto panelBackground = juce::Colour(0xff191b20);
const auto panelOutline = juce::Colour(0xff30343a);
const auto textPrimary = juce::Colour(0xfff2f3ee);
const auto textSecondary = juce::Colour(0xffaeb4aa);
const auto accent = juce::Colour(0xff80d878);
const auto accentWarm = juce::Colour(0xffe4b86a);
const auto focusAccent = juce::Colour(0xff6aa8ff);

struct PackageMediaCleanupTimestamp
{
    std::string cleanupId;
    std::string createdAtUtc;
};

projectname::WorkspaceCommandKey toWorkspaceCommandKey(const juce::KeyPress& key) noexcept
{
    if (key.getKeyCode() == juce::KeyPress::leftKey)
        return projectname::WorkspaceCommandKey::left;

    if (key.getKeyCode() == juce::KeyPress::rightKey)
        return projectname::WorkspaceCommandKey::right;

    if (key.getKeyCode() == juce::KeyPress::upKey)
        return projectname::WorkspaceCommandKey::up;

    if (key.getKeyCode() == juce::KeyPress::downKey)
        return projectname::WorkspaceCommandKey::down;

    return projectname::WorkspaceCommandKey::other;
}

void configureLabel(juce::Label& label, float fontSize, juce::Justification justification)
{
    label.setFont(juce::FontOptions(fontSize, juce::Font::plain));
    label.setJustificationType(justification);
    label.setColour(juce::Label::textColourId, textPrimary);
    label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
}

void configureButton(juce::TextButton& button, juce::Colour colour)
{
    button.setColour(juce::TextButton::buttonColourId, colour);
    button.setColour(juce::TextButton::buttonOnColourId, colour.brighter(0.1f));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff101114));
    button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff101114));
}

void configureToggle(juce::ToggleButton& button)
{
    button.setColour(juce::ToggleButton::textColourId, textPrimary);
    button.setColour(juce::ToggleButton::tickColourId, accent);
}

void configureMixerSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 22);
    slider.setColour(juce::Slider::trackColourId, accent);
    slider.setColour(juce::Slider::thumbColourId, textPrimary);
    slider.setColour(juce::Slider::textBoxTextColourId, textPrimary);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff22252b));
}

void configureTextEditor(juce::TextEditor& editor)
{
    editor.setMultiLine(false);
    editor.setReturnKeyStartsNewLine(false);
    editor.setSelectAllWhenFocused(true);
    editor.setJustification(juce::Justification::centredLeft);
    editor.setFont(juce::FontOptions(13.0f, juce::Font::plain));
    editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff22252b));
    editor.setColour(juce::TextEditor::textColourId, textPrimary);
    editor.setColour(juce::TextEditor::highlightColourId, accent.withAlpha(0.38f));
    editor.setColour(juce::TextEditor::outlineColourId, panelOutline);
    editor.setColour(juce::TextEditor::focusedOutlineColourId, focusAccent);
}

void setButtonEnabledFromCommand(juce::Button& button,
                                 const projectname::AppCommandRegistry& registry,
                                 std::string_view commandId)
{
    if (const auto* command = registry.findCommand(commandId))
        button.setEnabled(command->enabled);
}

[[nodiscard]] std::string toCommandString(std::string_view commandId)
{
    return std::string(commandId);
}

[[nodiscard]] juce::File toJuceFile(const std::filesystem::path& path)
{
    return juce::File(juce::String(path.string()));
}

[[nodiscard]] std::filesystem::path projectPackagePathFromChooserResult(juce::File file,
                                                                        bool appendProjectExtension)
{
    if (appendProjectExtension && !file.hasFileExtension(".project"))
        file = file.withFileExtension(".project");

    return std::filesystem::path(file.getFullPathName().toStdString());
}

[[nodiscard]] bool projectManifestExists(const std::filesystem::path& packageDirectory)
{
    std::error_code error;
    return std::filesystem::is_regular_file(packageDirectory / "manifest.json", error);
}

[[nodiscard]] std::string projectNameFromPackagePath(const std::filesystem::path& packageDirectory)
{
    auto name = packageDirectory.stem().string();
    return name.empty() ? std::string("Untitled Song") : std::move(name);
}

[[nodiscard]] const projectname::ProjectTrack* findFirstAudioTrack(
    const projectname::ProjectModel& project) noexcept
{
    for (const auto& track : project.getTracks())
    {
        if (track.type == "audio")
            return &track;
    }

    return nullptr;
}

[[nodiscard]] const projectname::ProjectClip* findProjectClipById(
    const projectname::ProjectModel& project,
    const std::string& clipId) noexcept
{
    if (clipId.empty())
        return nullptr;

    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (clip.id == clipId)
                return &clip;
        }
    }

    return nullptr;
}

[[nodiscard]] juce::String formatPosition(double positionBeats, projectname::TimeSignature signature)
{
    const auto beatsPerBar = static_cast<double>(signature.numerator);
    const auto bar = static_cast<int>(std::floor(positionBeats / beatsPerBar)) + 1;
    const auto beat = std::fmod(positionBeats, beatsPerBar) + 1.0;

    return "Bar " + juce::String(bar) + "  Beat " + juce::String(beat, 2);
}

[[nodiscard]] juce::String formatTimelinePreparationPhase(
    projectname::BackgroundTimelinePlaybackPreparationPhase phase)
{
    switch (phase)
    {
        case projectname::BackgroundTimelinePlaybackPreparationPhase::pending:
            return "Preparing timeline audio";

        case projectname::BackgroundTimelinePlaybackPreparationPhase::planning:
            return "Planning timeline audio";

        case projectname::BackgroundTimelinePlaybackPreparationPhase::decoding:
            return "Decoding timeline audio";

        case projectname::BackgroundTimelinePlaybackPreparationPhase::completed:
            return "Timeline audio ready";

        case projectname::BackgroundTimelinePlaybackPreparationPhase::failed:
            return "Timeline audio preparation failed";

        case projectname::BackgroundTimelinePlaybackPreparationPhase::cancelled:
            return "Timeline audio preparation cancelled";
    }

    return "Preparing timeline audio";
}

[[nodiscard]] juce::String formatMediaRelinkPreparationPhase(
    projectname::BackgroundMediaRelinkPreparationPhase phase)
{
    switch (phase)
    {
        case projectname::BackgroundMediaRelinkPreparationPhase::pending:
            return "Preparing media relink";

        case projectname::BackgroundMediaRelinkPreparationPhase::decoding:
            return "Decoding relink audio";

        case projectname::BackgroundMediaRelinkPreparationPhase::copying:
            return "Staging relink audio";

        case projectname::BackgroundMediaRelinkPreparationPhase::analysing:
            return "Analysing relink audio";

        case projectname::BackgroundMediaRelinkPreparationPhase::completed:
            return "Relink audio ready";

        case projectname::BackgroundMediaRelinkPreparationPhase::failed:
            return "Media relink preparation failed";

        case projectname::BackgroundMediaRelinkPreparationPhase::cancelled:
            return "Media relink preparation cancelled";
    }

    return "Preparing media relink";
}

[[nodiscard]] juce::String formatPackageMediaCleanupPhase(
    projectname::BackgroundPackageMediaCleanupPhase phase)
{
    switch (phase)
    {
        case projectname::BackgroundPackageMediaCleanupPhase::pending:
            return "Preparing package media cleanup";
        case projectname::BackgroundPackageMediaCleanupPhase::inventory:
            return "Scanning package media";
        case projectname::BackgroundPackageMediaCleanupPhase::preflight:
            return "Checking package media cleanup";
        case projectname::BackgroundPackageMediaCleanupPhase::quarantining:
            return "Moving unused package media";
        case projectname::BackgroundPackageMediaCleanupPhase::restoring:
            return "Restoring package media";
        case projectname::BackgroundPackageMediaCleanupPhase::completed:
            return "Package media cleanup completed";
        case projectname::BackgroundPackageMediaCleanupPhase::failed:
            return "Package media cleanup failed";
        case projectname::BackgroundPackageMediaCleanupPhase::cancelled:
            return "Package media cleanup cancelled";
    }

    return "Preparing package media cleanup";
}

[[nodiscard]] std::tm toUtcTime(std::time_t time)
{
    std::tm utc {};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    return utc;
}

[[nodiscard]] PackageMediaCleanupTimestamp makePackageMediaCleanupTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto utc = toUtcTime(time);

    std::ostringstream cleanupId;
    cleanupId << std::put_time(&utc, "%Y-%m-%dT%H-%M-%SZ") << "-cleanup";

    std::ostringstream createdAt;
    createdAt << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");

    return { cleanupId.str(), createdAt.str() };
}

[[nodiscard]] std::int64_t makeTimelineVoiceWindowFrameCount(
    const projectname::AudioDeviceSummary& summary,
    double sampleRateHz) noexcept
{
    const auto sampleRateWindow = std::isfinite(sampleRateHz) && sampleRateHz > 0.0
        ? static_cast<std::int64_t>(std::llround(sampleRateHz * 8.0))
        : 0;
    const auto callbackWindow = summary.bufferSizeSamples > 0
        ? static_cast<std::int64_t>(summary.bufferSizeSamples) * 32
        : 0;

    return std::max<std::int64_t>(1, std::max(sampleRateWindow, callbackWindow));
}

[[nodiscard]] double currentOutputSampleRate(const projectname::AudioDeviceSummary& summary) noexcept
{
    return summary.isOpen && summary.sampleRate > 0.0 ? summary.sampleRate : 0.0;
}

[[nodiscard]] juce::String formatSampleRate(double sampleRateHz)
{
    if (!std::isfinite(sampleRateHz) || sampleRateHz <= 0.0)
        return "Unavailable";

    return juce::String(static_cast<int>(std::llround(sampleRateHz))) + " Hz";
}

[[nodiscard]] juce::String formatInspectorClipName(const projectname::ImportedClipInspectorState& state)
{
    if (!state.clipName.empty())
        return juce::String(state.clipName);

    return state.clipId.empty() ? juce::String("Imported audio") : juce::String(state.clipId);
}

[[nodiscard]] juce::StringArray makeImportedClipInspectorLines(
    const projectname::ImportedClipInspectorState& state)
{
    juce::StringArray lines;

    if (state.status == projectname::ImportedClipInspectorStatus::noImportedAudio)
    {
        lines.add("Project: no imported audio");
        lines.add("Import a PCM16 WAV to inspect clip metadata");
        return lines;
    }

    lines.add("Clip: " + formatInspectorClipName(state));
    lines.add("Path: " + juce::String(state.relativePath));

    if (state.usingSelectedClip)
        lines.add(" ");
    else
        lines.add("Start: " + juce::String(state.startBeats, 2) + " beats");

    if (state.status == projectname::ImportedClipInspectorStatus::ready)
    {
        lines.add("Duration: " + juce::String(state.durationSeconds, 2)
                  + " s / " + juce::String(state.lengthBeats, 2) + " beats");
        lines.add("Source: " + formatSampleRate(state.sourceSampleRateHz)
                  + " / " + juce::String(static_cast<double>(state.sourceFrameCount), 0) + " frames");
        lines.add("Device: " + formatSampleRate(state.outputSampleRateHz));

        if (state.sampleRateMismatch)
            lines.add("Warning: sample-rate mismatch");
    }
    else
    {
        lines.add("Duration: " + juce::String(state.lengthBeats, 2) + " beats");
        lines.add("Source: unavailable");
        lines.add("Analysis: " + juce::String(state.message));
    }

    return lines;
}

[[nodiscard]] juce::StringArray makeStringArray(const std::vector<std::string>& lines)
{
    juce::StringArray result;
    for (const auto& line : lines)
        result.add(juce::String(line));

    return result;
}

[[nodiscard]] std::vector<WorkspacePanelRow> makePackageMediaMaintenancePanelRows(
    const projectname::PackageMediaMaintenanceBrowserRows& modelRows)
{
    std::vector<WorkspacePanelRow> panelRows;
    panelRows.reserve(modelRows.rows.size());
    for (const auto& row : modelRows.rows)
    {
        WorkspacePanelRow panelRow;
        panelRow.text = juce::String(row.text);
        panelRow.selectionId = row.cleanupId;
        panelRow.selectable = row.selectable;
        panelRow.selected = row.selected;
        panelRows.push_back(std::move(panelRow));
    }

    return panelRows;
}

[[nodiscard]] PackageMediaMaintenanceScanResult runPackageMediaMaintenanceScan(
    std::filesystem::path packageDirectory,
    std::string selectedCleanupId,
    bool packageWorkInProgress,
    int generation)
{
    projectname::ImportedMediaPackageInventoryOptions inventoryOptions;
    inventoryOptions.packageWorkInProgress = packageWorkInProgress;

    projectname::PackageMediaMaintenanceViewModelRequest request;
    request.inventory = projectname::buildImportedMediaPackageInventory(packageDirectory, inventoryOptions);
    request.discovery = projectname::discoverPackageMediaCleanupBatches(packageDirectory);
    request.selectedCleanupId = std::move(selectedCleanupId);

    PackageMediaMaintenanceScanResult result;
    result.generation = generation;
    result.viewModel = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
    return result;
}
} // namespace

WorkspacePanel::WorkspacePanel(juce::String title, juce::String subtitle, juce::StringArray lines)
    : title_(std::move(title)),
      subtitle_(std::move(subtitle))
{
    setWantsKeyboardFocus(true);
    setLines(std::move(lines));

    configureButton(panLeftViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(resetViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(fitClipsViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(centerSelectedViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(zoomOutViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(zoomInViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(panRightViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(actionButton_, accent);
    configureButton(secondaryActionButton_, juce::Colour(0xffc6ccd5));

    panLeftViewportButton_.setTooltip("Pan timeline view left");
    resetViewportButton_.setTooltip("Reset timeline view to the project start");
    fitClipsViewportButton_.setTooltip("Fit imported audio clips in the timeline view");
    centerSelectedViewportButton_.setTooltip("Center the selected imported clip in the timeline view");
    zoomOutViewportButton_.setTooltip("Zoom timeline view out");
    zoomInViewportButton_.setTooltip("Zoom timeline view in");
    panRightViewportButton_.setTooltip("Pan timeline view right");
    actionButton_.setTooltip("Run panel action");
    secondaryActionButton_.setTooltip("Run panel action");

    panLeftViewportButton_.onClick = [this]()
    {
        if (timelinePanLeftControlRequested_)
            timelinePanLeftControlRequested_();
    };
    resetViewportButton_.onClick = [this]()
    {
        if (timelineResetStartRequested_)
            timelineResetStartRequested_();
    };
    fitClipsViewportButton_.onClick = [this]()
    {
        if (timelineFitClipsRequested_)
            timelineFitClipsRequested_();
    };
    centerSelectedViewportButton_.onClick = [this]()
    {
        if (timelineCenterSelectedRequested_)
            timelineCenterSelectedRequested_();
    };
    zoomOutViewportButton_.onClick = [this]()
    {
        if (timelineZoomOutControlRequested_)
            timelineZoomOutControlRequested_();
    };
    zoomInViewportButton_.onClick = [this]()
    {
        if (timelineZoomInControlRequested_)
            timelineZoomInControlRequested_();
    };
    panRightViewportButton_.onClick = [this]()
    {
        if (timelinePanRightControlRequested_)
            timelinePanRightControlRequested_();
    };
    actionButton_.onClick = [this]()
    {
        if (panelActionRequested_)
            panelActionRequested_();
    };
    secondaryActionButton_.onClick = [this]()
    {
        if (secondaryPanelActionRequested_)
            secondaryPanelActionRequested_();
    };

    addChildComponent(panLeftViewportButton_);
    addChildComponent(resetViewportButton_);
    addChildComponent(fitClipsViewportButton_);
    addChildComponent(centerSelectedViewportButton_);
    addChildComponent(zoomOutViewportButton_);
    addChildComponent(zoomInViewportButton_);
    addChildComponent(panRightViewportButton_);
    addChildComponent(actionButton_);
    addChildComponent(secondaryActionButton_);
    actionButton_.setVisible(false);
    secondaryActionButton_.setVisible(false);
}

void WorkspacePanel::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour(panelBackground);
    graphics.fillRoundedRectangle(bounds, 6.0f);

    graphics.setColour(panelOutline);
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    if (shouldPaintKeyboardFocus())
    {
        graphics.setColour(focusAccent.withAlpha(0.86f));
        graphics.drawRoundedRectangle(bounds.reduced(1.5f), 6.0f, 2.0f);
    }

    auto content = getLocalBounds().reduced(16);

    graphics.setColour(textPrimary);
    graphics.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    graphics.drawFittedText(title_, content.removeFromTop(24), juce::Justification::centredLeft, 1);

    graphics.setColour(textSecondary);
    graphics.setFont(juce::FontOptions(13.0f));
    auto subtitleArea = content.removeFromTop(22);
    if (shouldShowViewportControls())
        subtitleArea.removeFromRight(getViewportControlBounds().getWidth() + 10);
    if (shouldShowActionButton())
        subtitleArea.removeFromRight(getActionButtonBounds().getWidth() + 10);
    if (shouldShowSecondaryActionButton())
        subtitleArea.removeFromRight(getSecondaryActionButtonBounds().getWidth() + 6);
    graphics.drawFittedText(subtitle_, subtitleArea, juce::Justification::centredLeft, 1);

    content.removeFromTop(10);
    graphics.setFont(juce::FontOptions(13.0f));

    for (const auto& rowModel : rows_)
    {
        if (content.getHeight() < 76)
            break;

        const auto row = content.removeFromTop(28);
        if (rowModel.selected)
            graphics.setColour(accent.withAlpha(0.18f));
        else if (rowModel.selectable)
            graphics.setColour(juce::Colour(0xff2a2e35));
        else
            graphics.setColour(juce::Colour(0xff252830));

        graphics.fillRoundedRectangle(row.toFloat(), 4.0f);

        if (rowModel.selectable)
        {
            graphics.setColour(rowModel.selected ? accent.withAlpha(0.92f) : panelOutline);
            graphics.drawRoundedRectangle(row.toFloat().reduced(0.5f),
                                          4.0f,
                                          rowModel.selected ? 1.6f : 1.0f);
        }

        graphics.setColour(rowModel.selected ? textPrimary : textSecondary);
        graphics.drawFittedText(rowModel.text, row.reduced(10, 0), juce::Justification::centredLeft, 1);
        content.removeFromTop(6);
    }

    if (content.getHeight() >= 54)
        paintTimelineClipLane(graphics, content);
}

juce::Rectangle<int> WorkspacePanel::getRowsContentBounds() const
{
    auto content = getLocalBounds().reduced(16);
    content.removeFromTop(24);
    content.removeFromTop(22);
    content.removeFromTop(10);
    return content;
}

juce::Rectangle<int> WorkspacePanel::getTimelineContentBounds() const
{
    auto content = getRowsContentBounds();

    for (const auto& row : rows_)
    {
        juce::ignoreUnused(row);
        if (content.getHeight() < 76)
            break;

        content.removeFromTop(28);
        content.removeFromTop(6);
    }

    return content.getHeight() >= 54 ? content : juce::Rectangle<int> {};
}

std::optional<std::size_t> WorkspacePanel::hitTestSelectableRow(juce::Point<int> position) const
{
    auto content = getRowsContentBounds();
    for (std::size_t index = 0; index < rows_.size(); ++index)
    {
        if (content.getHeight() < 76)
            break;

        const auto row = content.removeFromTop(28);
        if (rows_[index].selectable && row.contains(position))
            return index;

        content.removeFromTop(6);
    }

    return std::nullopt;
}

bool WorkspacePanel::shouldPaintKeyboardFocus() const
{
    return (previousTimelineClipRequested_ || nextTimelineClipRequested_
            || previousSelectableRowRequested_ || nextSelectableRowRequested_
            || timelinePanLeftRequested_ || timelinePanRightRequested_
            || timelineZoomInRequested_ || timelineZoomOutRequested_)
        && hasKeyboardFocus(true);
}

juce::Rectangle<int> WorkspacePanel::getViewportControlBounds() const
{
    auto content = getLocalBounds().reduced(16);
    content.removeFromTop(24);
    auto subtitleArea = content.removeFromTop(22);
    return subtitleArea.removeFromRight(228).reduced(0, 1);
}

juce::Rectangle<int> WorkspacePanel::getActionButtonBounds() const
{
    auto content = getLocalBounds().reduced(16);
    content.removeFromTop(24);
    auto subtitleArea = content.removeFromTop(22);
    if (shouldShowViewportControls())
        subtitleArea.removeFromRight(getViewportControlBounds().getWidth() + 10);

    return subtitleArea.removeFromRight(82).reduced(0, 1);
}

juce::Rectangle<int> WorkspacePanel::getSecondaryActionButtonBounds() const
{
    auto content = getLocalBounds().reduced(16);
    content.removeFromTop(24);
    auto subtitleArea = content.removeFromTop(22);
    if (shouldShowViewportControls())
        subtitleArea.removeFromRight(getViewportControlBounds().getWidth() + 10);
    if (shouldShowActionButton())
        subtitleArea.removeFromRight(getActionButtonBounds().getWidth() + 6);

    return subtitleArea.removeFromRight(82).reduced(0, 1);
}

bool WorkspacePanel::shouldShowViewportControls() const
{
    return timelinePanLeftControlRequested_
        || timelineResetStartRequested_
        || timelineFitClipsRequested_
        || timelineCenterSelectedRequested_
        || timelineZoomOutControlRequested_
        || timelineZoomInControlRequested_
        || timelinePanRightControlRequested_;
}

bool WorkspacePanel::shouldShowActionButton() const
{
    return actionButton_.isVisible();
}

bool WorkspacePanel::shouldShowSecondaryActionButton() const
{
    return secondaryActionButton_.isVisible();
}

void WorkspacePanel::resized()
{
    const auto visible = shouldShowViewportControls();
    panLeftViewportButton_.setVisible(visible);
    resetViewportButton_.setVisible(visible);
    fitClipsViewportButton_.setVisible(visible);
    centerSelectedViewportButton_.setVisible(visible);
    zoomOutViewportButton_.setVisible(visible);
    zoomInViewportButton_.setVisible(visible);
    panRightViewportButton_.setVisible(visible);

    if (visible)
    {
        auto controls = getViewportControlBounds();
        panLeftViewportButton_.setBounds(controls.removeFromLeft(28));
        controls.removeFromLeft(4);
        resetViewportButton_.setBounds(controls.removeFromLeft(28));
        controls.removeFromLeft(4);
        fitClipsViewportButton_.setBounds(controls.removeFromLeft(36));
        controls.removeFromLeft(4);
        centerSelectedViewportButton_.setBounds(controls.removeFromLeft(28));
        controls.removeFromLeft(4);
        zoomOutViewportButton_.setBounds(controls.removeFromLeft(28));
        controls.removeFromLeft(4);
        zoomInViewportButton_.setBounds(controls.removeFromLeft(28));
        controls.removeFromLeft(4);
        panRightViewportButton_.setBounds(controls.removeFromLeft(28));
    }

    if (shouldShowActionButton())
        actionButton_.setBounds(getActionButtonBounds());

    if (shouldShowSecondaryActionButton())
        secondaryActionButton_.setBounds(getSecondaryActionButtonBounds());
}

void WorkspacePanel::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    if (selectableRowSelected_)
    {
        const auto rowIndex = hitTestSelectableRow(event.getPosition());
        if (rowIndex.has_value())
        {
            const auto& row = rows_[*rowIndex];
            if (!row.selectionId.empty())
                selectableRowSelected_(row.selectionId);

            return;
        }
    }

    if (!timelineClipSelected_ || timelineClipLane_.clips.empty())
        return;

    const auto timelineBounds = getTimelineContentBounds();
    if (timelineBounds.isEmpty())
        return;

    const auto clipArea = timelineBounds.reduced(0, 2).reduced(10, 8);
    if (!clipArea.contains(event.getPosition()))
        return;

    const auto hitPosition = event.getPosition() - clipArea.getPosition();
    const auto hit = projectname::hitTestTimelineClipLane(timelineClipLane_,
                                                          hitPosition.getX(),
                                                          hitPosition.getY());
    if (hit.has_value())
        timelineClipSelected_(hit->clipId);
}

bool WorkspacePanel::keyPressed(const juce::KeyPress& key)
{
    if (!key.getModifiers().isCommandDown())
    {
        if (key.getKeyCode() == juce::KeyPress::upKey && previousSelectableRowRequested_)
        {
            previousSelectableRowRequested_();
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::downKey && nextSelectableRowRequested_)
        {
            nextSelectableRowRequested_();
            return true;
        }
    }

    const projectname::WorkspaceCommandShortcut shortcut {
        toWorkspaceCommandKey(key),
        key.getModifiers().isCommandDown(),
    };
    const projectname::WorkspaceCommandAvailability availability {
        static_cast<bool>(previousTimelineClipRequested_),
        static_cast<bool>(nextTimelineClipRequested_),
        static_cast<bool>(timelinePanLeftRequested_),
        static_cast<bool>(timelinePanRightRequested_),
        static_cast<bool>(timelineZoomInRequested_),
        static_cast<bool>(timelineZoomOutRequested_),
    };

    const auto command = projectname::routeWorkspaceCommand(shortcut, availability);
    if (!command.has_value())
        return false;

    if (*command == projectname::WorkspaceCommand::selectPreviousClip)
    {
        previousTimelineClipRequested_();
        return true;
    }

    if (*command == projectname::WorkspaceCommand::selectNextClip)
    {
        nextTimelineClipRequested_();
        return true;
    }

    if (*command == projectname::WorkspaceCommand::panViewportLeft)
    {
        timelinePanLeftRequested_();
        return true;
    }

    if (*command == projectname::WorkspaceCommand::panViewportRight)
    {
        timelinePanRightRequested_();
        return true;
    }

    if (*command == projectname::WorkspaceCommand::zoomViewportIn)
    {
        timelineZoomInRequested_();
        return true;
    }

    if (*command == projectname::WorkspaceCommand::zoomViewportOut)
    {
        timelineZoomOutRequested_();
        return true;
    }

    return false;
}

void WorkspacePanel::focusGained(FocusChangeType cause)
{
    juce::ignoreUnused(cause);
    repaint();
}

void WorkspacePanel::focusLost(FocusChangeType cause)
{
    juce::ignoreUnused(cause);
    repaint();
}

void WorkspacePanel::setTimelineClipLane(projectname::TimelineClipLaneLayout layout)
{
    timelineClipLane_ = std::move(layout);
    repaint();
}

void WorkspacePanel::setTimelineClipSelectedCallback(std::function<void(std::string)> callback)
{
    timelineClipSelected_ = std::move(callback);
}

void WorkspacePanel::setTimelineClipKeyboardSelectionCallbacks(std::function<void()> previousCallback,
                                                               std::function<void()> nextCallback)
{
    previousTimelineClipRequested_ = std::move(previousCallback);
    nextTimelineClipRequested_ = std::move(nextCallback);
}

void WorkspacePanel::setTimelineViewportKeyboardCallbacks(std::function<void()> panLeftCallback,
                                                          std::function<void()> panRightCallback,
                                                          std::function<void()> zoomInCallback,
                                                          std::function<void()> zoomOutCallback)
{
    timelinePanLeftRequested_ = std::move(panLeftCallback);
    timelinePanRightRequested_ = std::move(panRightCallback);
    timelineZoomInRequested_ = std::move(zoomInCallback);
    timelineZoomOutRequested_ = std::move(zoomOutCallback);
}

void WorkspacePanel::setTimelineViewportControlCallbacks(std::function<void()> panLeftCallback,
                                                         std::function<void()> resetStartCallback,
                                                         std::function<void()> fitClipsCallback,
                                                         std::function<void()> centerSelectedCallback,
                                                         std::function<void()> zoomOutCallback,
                                                         std::function<void()> zoomInCallback,
                                                         std::function<void()> panRightCallback)
{
    timelinePanLeftControlRequested_ = std::move(panLeftCallback);
    timelineResetStartRequested_ = std::move(resetStartCallback);
    timelineFitClipsRequested_ = std::move(fitClipsCallback);
    timelineCenterSelectedRequested_ = std::move(centerSelectedCallback);
    timelineZoomOutControlRequested_ = std::move(zoomOutCallback);
    timelineZoomInControlRequested_ = std::move(zoomInCallback);
    timelinePanRightControlRequested_ = std::move(panRightCallback);
    resized();
    repaint();
}

void WorkspacePanel::setSelectableRowCallback(std::function<void(std::string)> callback)
{
    selectableRowSelected_ = std::move(callback);
}

void WorkspacePanel::setSelectableRowKeyboardSelectionCallbacks(std::function<void()> previousCallback,
                                                               std::function<void()> nextCallback)
{
    previousSelectableRowRequested_ = std::move(previousCallback);
    nextSelectableRowRequested_ = std::move(nextCallback);
    repaint();
}

void WorkspacePanel::setPanelAction(juce::String text,
                                    bool enabled,
                                    juce::String tooltip,
                                    std::function<void()> callback)
{
    const auto wasVisible = shouldShowActionButton();
    panelActionRequested_ = std::move(callback);
    actionButton_.setButtonText(text);
    actionButton_.setTooltip(tooltip);
    actionButton_.setEnabled(enabled);
    actionButton_.setVisible(!text.isEmpty() && static_cast<bool>(panelActionRequested_));

    if (wasVisible != shouldShowActionButton())
        resized();
    else if (shouldShowActionButton())
        actionButton_.setBounds(getActionButtonBounds());

    repaint();
}

void WorkspacePanel::setSecondaryPanelAction(juce::String text,
                                             bool enabled,
                                             juce::String tooltip,
                                             std::function<void()> callback)
{
    const auto wasVisible = shouldShowSecondaryActionButton();
    secondaryPanelActionRequested_ = std::move(callback);
    secondaryActionButton_.setButtonText(text);
    secondaryActionButton_.setTooltip(tooltip);
    secondaryActionButton_.setEnabled(enabled);
    secondaryActionButton_.setVisible(!text.isEmpty() && static_cast<bool>(secondaryPanelActionRequested_));

    if (wasVisible != shouldShowSecondaryActionButton())
        resized();
    else if (shouldShowSecondaryActionButton())
        secondaryActionButton_.setBounds(getSecondaryActionButtonBounds());

    repaint();
}

int WorkspacePanel::getTimelineClipViewportWidthPixels() const
{
    const auto timelineBounds = getTimelineContentBounds();
    if (timelineBounds.isEmpty())
        return projectname::TimelineClipLaneOptions {}.viewportWidthPixels;

    return std::max(1, timelineBounds.reduced(0, 2).reduced(10, 8).getWidth());
}

void WorkspacePanel::setSubtitle(juce::String subtitle)
{
    subtitle_ = std::move(subtitle);
    repaint();
}

void WorkspacePanel::setLines(juce::StringArray lines)
{
    std::vector<WorkspacePanelRow> rows;
    rows.reserve(static_cast<std::size_t>(lines.size()));
    for (const auto& line : lines)
    {
        WorkspacePanelRow row;
        row.text = line;
        rows.push_back(std::move(row));
    }

    rows_ = std::move(rows);
    repaint();
}

void WorkspacePanel::setRows(std::vector<WorkspacePanelRow> rows)
{
    rows_ = std::move(rows);
    repaint();
}

void WorkspacePanel::paintTimelineClipLane(juce::Graphics& graphics, juce::Rectangle<int> bounds)
{
    if (timelineClipLane_.clips.empty())
        return;

    const auto laneBounds = bounds.reduced(0, 2);
    graphics.setColour(juce::Colour(0xff15171c));
    graphics.fillRoundedRectangle(laneBounds.toFloat(), 4.0f);
    graphics.setColour(panelOutline);
    graphics.drawRoundedRectangle(laneBounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

    graphics.setColour(juce::Colour(0xff252830));
    for (auto x = laneBounds.getX() + 10; x < laneBounds.getRight(); x += 48)
        graphics.drawVerticalLine(x, static_cast<float>(laneBounds.getY() + 8), static_cast<float>(laneBounds.getBottom() - 8));

    auto clipArea = laneBounds.reduced(10, 8);
    const auto maxBottom = clipArea.getBottom();
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));

    if (timelineClipLane_.loopRange.has_value() && timelineClipLane_.loopRange->visible)
    {
        const auto& loop = *timelineClipLane_.loopRange;
        const auto rawLoopRect = juce::Rectangle<int>(clipArea.getX() + loop.x,
                                                      clipArea.getY(),
                                                      loop.width,
                                                      clipArea.getHeight());
        const auto loopRect = rawLoopRect.getIntersection(clipArea);
        if (!loopRect.isEmpty())
        {
            graphics.setColour(accentWarm.withAlpha(0.13f));
            graphics.fillRoundedRectangle(loopRect.toFloat(), 3.0f);
            graphics.setColour(accentWarm.withAlpha(0.62f));
            graphics.drawVerticalLine(loopRect.getX(),
                                      static_cast<float>(loopRect.getY()),
                                      static_cast<float>(loopRect.getBottom()));
            graphics.drawVerticalLine(loopRect.getRight() - 1,
                                      static_cast<float>(loopRect.getY()),
                                      static_cast<float>(loopRect.getBottom()));
        }
    }

    for (const auto& item : timelineClipLane_.clips)
    {
        const auto rawRect = juce::Rectangle<int>(clipArea.getX() + item.x,
                                                  clipArea.getY() + item.y,
                                                  item.width,
                                                  item.height);
        if (rawRect.getY() >= maxBottom)
            break;

        const auto clipRect = rawRect.getIntersection(clipArea);
        if (clipRect.isEmpty())
            continue;

        graphics.setColour(juce::Colour(0xff22252b));
        graphics.fillRoundedRectangle(clipRect.toFloat(), 4.0f);
        graphics.setColour(panelOutline);
        graphics.drawRoundedRectangle(clipRect.toFloat().reduced(0.5f), 4.0f, 1.0f);
        if (item.selected)
        {
            graphics.setColour(accent.withAlpha(0.94f));
            graphics.drawRoundedRectangle(clipRect.toFloat().reduced(1.0f), 4.0f, 2.0f);
        }

        auto labelArea = clipRect.reduced(8, 3).removeFromTop(15);
        if (item.waveform.state != projectname::WaveformThumbnailState::ready)
        {
            graphics.setColour(accentWarm);
            const auto text = item.waveform.state == projectname::WaveformThumbnailState::missingAnalysis
                ? juce::String("Analysis missing")
                : juce::String("Analysis unreadable");
            graphics.drawFittedText(text, labelArea, juce::Justification::centredLeft, 1);
            continue;
        }

        graphics.setColour(textPrimary);
        graphics.drawFittedText(juce::String(item.waveform.clip.name), labelArea, juce::Justification::centredLeft, 1);

        auto waveArea = clipRect.reduced(8, 5);
        waveArea.removeFromTop(18);
        if (waveArea.getWidth() <= 0 || waveArea.getHeight() <= 0)
            continue;

        const auto columns = projectname::makeWaveformPeakColumns(item.waveform.summary,
                                                                  static_cast<std::size_t>(waveArea.getWidth()));
        if (columns.empty())
            continue;

        const auto centreY = static_cast<float>(waveArea.getCentreY());
        const auto halfHeight = std::max(1.0f, static_cast<float>(waveArea.getHeight()) * 0.42f);
        graphics.setColour(accent.withAlpha(0.82f));

        for (std::size_t index = 0; index < columns.size(); ++index)
        {
            const auto x = static_cast<float>(waveArea.getX()) + static_cast<float>(index);
            const auto height = std::max(1.0f, columns[index] * halfHeight);
            graphics.drawVerticalLine(static_cast<int>(x), centreY - height, centreY + height);
        }
    }
}

MainComponent::MainComponent()
    : browserPanel_("Browser",
                    "Search-first project and device navigation",
                    { "Samples", "Plugins", "Presets", "Project assets" }),
      workspacePanel_("Session / Arrangement",
                      "A shared workspace for clips, lanes, and timeline editing",
                      { "Track 1: Generated Tone", "Grid: 4 bars", "Clips and arrangement editing next" }),
      inspectorPanel_("Inspector",
                      "Selected object properties",
                      { "Project: " + juce::String(session_.getProject().getName()), "Tempo follows transport", "Missing media recovery later" }),
      devicePanel_("Device Panel",
                   "Track device chain and editor area",
                   { "Generated tone source", "Device slots are placeholders", "Built-in devices next milestones" }),
      mixerPanel_("Mixer",
                  "Track and master controls",
                  { "Track mix", "Master" })
{
    currentProjectPackagePath_ = getDefaultProjectPackagePath();
    session_.getProject().setName(projectname::demoProjectName);
    configureControls();
    browserPanel_.setSelectableRowCallback(
        [this](std::string cleanupId)
        {
            selectPackageMediaCleanupBatch(std::move(cleanupId));
        });
    browserPanel_.setSelectableRowKeyboardSelectionCallbacks(
        [this]()
        {
            selectAdjacentPackageMediaCleanupBatch(
                projectname::PackageMediaMaintenanceBrowserSelectionDirection::previous);
        },
        [this]()
        {
            selectAdjacentPackageMediaCleanupBatch(
                projectname::PackageMediaMaintenanceBrowserSelectionDirection::next);
        });
    workspacePanel_.setTimelineClipSelectedCallback(
        [this](std::string clipId)
        {
            selectTimelineClip(std::move(clipId));
        });
    workspacePanel_.setTimelineClipKeyboardSelectionCallbacks(
        [this]()
        {
            selectAdjacentTimelineClip(projectname::ImportedAudioClipSelectionDirection::previous);
        },
        [this]()
        {
            selectAdjacentTimelineClip(projectname::ImportedAudioClipSelectionDirection::next);
        });
    workspacePanel_.setTimelineViewportKeyboardCallbacks(
        [this]()
        {
            panTimelineViewport(-4.0);
        },
        [this]()
        {
            panTimelineViewport(4.0);
        },
        [this]()
        {
            zoomTimelineViewport(0.5);
        },
        [this]()
        {
            zoomTimelineViewport(2.0);
        });
    workspacePanel_.setTimelineViewportControlCallbacks(
        [this]()
        {
            panTimelineViewport(-4.0);
        },
        [this]()
        {
            resetTimelineViewportStart();
        },
        [this]()
        {
            fitTimelineViewportToImportedClips();
        },
        [this]()
        {
            centerTimelineViewportOnSelectedClip();
        },
        [this]()
        {
            zoomTimelineViewport(2.0);
        },
        [this]()
        {
            zoomTimelineViewport(0.5);
        },
        [this]()
        {
            panTimelineViewport(4.0);
        });
    refreshWorkspaceTimelineLane();
    refreshMixerControls();
    refreshBrowserPanel();

    const auto error = audioService_.initialiseDefaultDevice();
    audioSetupInitializationError_ = error;
    refreshInspectorPanel();
    refreshDevicePanel(true);
    requestPackageMediaMaintenanceRefresh();
    setStatus(error.isEmpty() ? "Ready - press Play for a generated test tone"
                              : "Audio setup needs attention: " + error);

    updateTransportLabels();
    setSize(1280, 800);
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (audioImportJob_ != nullptr)
        audioImportJob_->requestCancel();
    if (mediaRelinkPreparationJob_ != nullptr)
        mediaRelinkPreparationJob_->requestCancel();
    if (timelinePlaybackPreparationJob_ != nullptr)
        timelinePlaybackPreparationJob_->requestCancel();
    if (packageMediaCleanupJob_ != nullptr)
        packageMediaCleanupJob_->requestCancel();

    audioImportJob_.reset();
    mediaRelinkPreparationJob_.reset();
    timelinePlaybackPreparationJob_.reset();
    packageMediaCleanupJob_.reset();
    if (packageMediaMaintenanceScan_.valid())
        packageMediaMaintenanceScan_.wait();
    audioService_.shutdown();
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(background);

    const auto topBar = getLocalBounds().reduced(outerMargin).removeFromTop(72).toFloat();
    graphics.setColour(juce::Colour(0xff17191d));
    graphics.fillRoundedRectangle(topBar, 6.0f);
    graphics.setColour(panelOutline);
    graphics.drawRoundedRectangle(topBar.reduced(0.5f), 6.0f, 1.0f);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(outerMargin);
    auto topBar = bounds.removeFromTop(72).reduced(12);
    bounds.removeFromTop(gap);

    playButton_.setBounds(topBar.removeFromLeft(74).reduced(0, 10));
    topBar.removeFromLeft(6);
    stopButton_.setBounds(topBar.removeFromLeft(74).reduced(0, 10));
    topBar.removeFromLeft(12);

    tempoLabel_.setBounds(topBar.removeFromLeft(48).reduced(0, 12));
    tempoSlider_.setBounds(topBar.removeFromLeft(170).reduced(0, 10));
    topBar.removeFromLeft(12);
    signatureLabel_.setBounds(topBar.removeFromLeft(58).reduced(0, 12));
    positionLabel_.setBounds(topBar.removeFromLeft(150).reduced(0, 12));

    audioButton_.setBounds(topBar.removeFromRight(110).reduced(0, 10));
    topBar.removeFromRight(8);
    cancelTimelinePreparationButton_.setBounds(topBar.removeFromRight(104).reduced(0, 10));
    topBar.removeFromRight(8);
    cancelImportButton_.setBounds(topBar.removeFromRight(76).reduced(0, 10));
    topBar.removeFromRight(8);
    importButton_.setBounds(topBar.removeFromRight(82).reduced(0, 10));
    topBar.removeFromRight(8);
    projectButton_.setBounds(topBar.removeFromRight(92).reduced(0, 10));
    topBar.removeFromRight(12);
    statusLabel_.setBounds(topBar.reduced(0, 12));

    auto mixerArea = bounds.removeFromBottom(82);
    bounds.removeFromBottom(gap);
    auto deviceArea = bounds.removeFromBottom(140);
    bounds.removeFromBottom(gap);

    auto left = bounds.removeFromLeft(220);
    bounds.removeFromLeft(gap);
    auto right = bounds.removeFromRight(240);
    bounds.removeFromRight(gap);

    browserPanel_.setBounds(left);
    workspacePanel_.setBounds(bounds);
    inspectorPanel_.setBounds(right);
    devicePanel_.setBounds(deviceArea);
    mixerPanel_.setBounds(mixerArea);

    auto inspectorStartRow = getInspectorStartBeatRowBounds();
    auto inspectorStartContent = inspectorStartRow.reduced(10, 3);
    if (inspectorCancelRelinkButton_.isVisible())
    {
        inspectorCancelRelinkButton_.setBounds(inspectorStartContent.removeFromRight(
            std::min(58, inspectorStartContent.getWidth())));
        inspectorStartContent.removeFromRight(std::min(6, inspectorStartContent.getWidth()));
    }
    inspectorRelinkButton_.setBounds(inspectorStartContent.removeFromRight(
        std::min(64, inspectorStartContent.getWidth())));
    inspectorStartContent.removeFromRight(std::min(8, inspectorStartContent.getWidth()));
    inspectorStartBeatLabel_.setBounds(inspectorStartContent.removeFromLeft(
        std::min(64, inspectorStartContent.getWidth())));
    inspectorStartContent.removeFromLeft(std::min(6, inspectorStartContent.getWidth()));
    inspectorStartBeatEditor_.setBounds(inspectorStartContent);

    auto mixerContent = mixerArea.reduced(16, 10);
    mixerTrackLabel_.setBounds(mixerContent.removeFromLeft(190));
    mixerContent.removeFromLeft(12);

    auto volumeArea = mixerContent.removeFromLeft(250);
    trackVolumeLabel_.setBounds(volumeArea.removeFromTop(20));
    trackVolumeSlider_.setBounds(volumeArea.reduced(0, 2));
    mixerContent.removeFromLeft(12);

    auto panArea = mixerContent.removeFromLeft(250);
    trackPanLabel_.setBounds(panArea.removeFromTop(20));
    trackPanSlider_.setBounds(panArea.reduced(0, 2));

    auto toggleArea = mixerContent.removeFromRight(150).reduced(0, 12);
    muteToggle_.setBounds(toggleArea.removeFromLeft(72));
    toggleArea.removeFromLeft(6);
    soloToggle_.setBounds(toggleArea.removeFromLeft(72));
}

void MainComponent::buttonClicked(juce::Button* button)
{
    if (button == &playButton_)
    {
        dispatchAppCommand(projectname::AppCommandIds::transportPlay);
    }
    else if (button == &stopButton_)
    {
        dispatchAppCommand(projectname::AppCommandIds::transportStop);
    }
    else if (button == &projectButton_)
    {
        showProjectMenu();
    }
    else if (button == &importButton_)
    {
        dispatchAppCommand(projectname::AppCommandIds::audioImport);
    }
    else if (button == &inspectorRelinkButton_)
    {
        relinkSelectedClipMedia();
    }
    else if (button == &inspectorCancelRelinkButton_)
    {
        cancelMediaRelinkPreparation();
    }
    else if (button == &cancelImportButton_)
    {
        dispatchAppCommand(projectname::AppCommandIds::audioImportCancel);
    }
    else if (button == &cancelTimelinePreparationButton_)
    {
        dispatchAppCommand(projectname::AppCommandIds::timelinePreparationCancel);
    }
    else if (button == &audioButton_)
    {
        dispatchAppCommand(projectname::AppCommandIds::audioSettingsShow);
    }
    else if (button == &muteToggle_ || button == &soloToggle_)
    {
        applyMixerControlChange();
    }

    updateTransportLabels();
}

void MainComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &tempoSlider_)
    {
        session_.setTempoBpm(tempoSlider_.getValue());
        updateTransportLabels();
    }
    else if (slider == &trackVolumeSlider_ || slider == &trackPanSlider_)
    {
        applyMixerControlChange();
    }
}

void MainComponent::timerCallback()
{
    pollAudioImportJob();
    pollMediaRelinkPreparationJob();
    pollTimelinePlaybackPreparationJob();
    pollPackageMediaCleanupJob();
    pollPackageMediaMaintenanceScan();
    session_.advanceSeconds(1.0 / 30.0);
    updateTransportLabels();

    const auto outputSampleRate = currentOutputSampleRate(audioService_.getDeviceSummary());
    if (std::abs(outputSampleRate - lastInspectorOutputSampleRateHz_) > 1.0)
        refreshInspectorPanel();

    refreshDevicePanel();
}

void MainComponent::configureControls()
{
    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(projectButton_);
    addAndMakeVisible(importButton_);
    addAndMakeVisible(cancelImportButton_);
    addAndMakeVisible(cancelTimelinePreparationButton_);
    addAndMakeVisible(audioButton_);
    addAndMakeVisible(tempoSlider_);
    addAndMakeVisible(trackVolumeSlider_);
    addAndMakeVisible(trackPanSlider_);
    addAndMakeVisible(tempoLabel_);
    addAndMakeVisible(positionLabel_);
    addAndMakeVisible(signatureLabel_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(mixerTrackLabel_);
    addAndMakeVisible(trackVolumeLabel_);
    addAndMakeVisible(trackPanLabel_);
    addAndMakeVisible(muteToggle_);
    addAndMakeVisible(soloToggle_);
    addAndMakeVisible(browserPanel_);
    addAndMakeVisible(workspacePanel_);
    addAndMakeVisible(inspectorPanel_);
    addAndMakeVisible(devicePanel_);
    addAndMakeVisible(mixerPanel_);
    addAndMakeVisible(inspectorStartBeatLabel_);
    addAndMakeVisible(inspectorStartBeatEditor_);
    addAndMakeVisible(inspectorRelinkButton_);
    addAndMakeVisible(inspectorCancelRelinkButton_);

    configureButton(playButton_, accent);
    configureButton(stopButton_, accentWarm);
    configureButton(projectButton_, juce::Colour(0xffc6ccd5));
    configureButton(importButton_, juce::Colour(0xffc6ccd5));
    configureButton(inspectorRelinkButton_, juce::Colour(0xffc6ccd5));
    configureButton(inspectorCancelRelinkButton_, accentWarm);
    configureButton(cancelImportButton_, accentWarm);
    configureButton(cancelTimelinePreparationButton_, accentWarm);
    configureButton(audioButton_, juce::Colour(0xffc6ccd5));

    playButton_.addListener(this);
    stopButton_.addListener(this);
    projectButton_.addListener(this);
    importButton_.addListener(this);
    inspectorRelinkButton_.addListener(this);
    inspectorCancelRelinkButton_.addListener(this);
    cancelImportButton_.addListener(this);
    cancelTimelinePreparationButton_.addListener(this);
    audioButton_.addListener(this);
    muteToggle_.addListener(this);
    soloToggle_.addListener(this);

    tempoSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    tempoSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
    tempoSlider_.setRange(projectname::TransportState::minTempoBpm,
                          projectname::TransportState::maxTempoBpm,
                          1.0);
    tempoSlider_.setValue(session_.getTransport().getTempoBpm(), juce::dontSendNotification);
    tempoSlider_.setColour(juce::Slider::trackColourId, accent);
    tempoSlider_.setColour(juce::Slider::thumbColourId, textPrimary);
    tempoSlider_.setColour(juce::Slider::textBoxTextColourId, textPrimary);
    tempoSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff22252b));
    tempoSlider_.addListener(this);

    configureMixerSlider(trackVolumeSlider_);
    trackVolumeSlider_.setRange(0.0, 2.0, 0.01);
    trackVolumeSlider_.addListener(this);

    configureMixerSlider(trackPanSlider_);
    trackPanSlider_.setRange(-1.0, 1.0, 0.01);
    trackPanSlider_.addListener(this);

    configureToggle(muteToggle_);
    configureToggle(soloToggle_);

    configureLabel(tempoLabel_, 13.0f, juce::Justification::centredLeft);
    configureLabel(positionLabel_, 13.0f, juce::Justification::centredLeft);
    configureLabel(signatureLabel_, 13.0f, juce::Justification::centredLeft);
    configureLabel(statusLabel_, 13.0f, juce::Justification::centredRight);
    configureLabel(mixerTrackLabel_, 13.0f, juce::Justification::centredLeft);
    configureLabel(trackVolumeLabel_, 12.0f, juce::Justification::centredLeft);
    configureLabel(trackPanLabel_, 12.0f, juce::Justification::centredLeft);
    configureLabel(inspectorStartBeatLabel_, 12.0f, juce::Justification::centredLeft);
    configureTextEditor(inspectorStartBeatEditor_);
    statusLabel_.setColour(juce::Label::textColourId, textSecondary);
    inspectorStartBeatLabel_.setColour(juce::Label::textColourId, textSecondary);

    tempoLabel_.setText("Tempo", juce::dontSendNotification);
    trackVolumeLabel_.setText("Volume", juce::dontSendNotification);
    trackPanLabel_.setText("Pan", juce::dontSendNotification);
    inspectorStartBeatLabel_.setText("Start beat", juce::dontSendNotification);
    inspectorStartBeatLabel_.setVisible(false);
    inspectorStartBeatEditor_.setVisible(false);
    inspectorRelinkButton_.setVisible(false);
    inspectorCancelRelinkButton_.setVisible(false);
    inspectorStartBeatEditor_.setTooltip("Selected imported clip start beat");
    inspectorRelinkButton_.setTooltip("Replace selected clip media with a PCM16 WAV file");
    inspectorCancelRelinkButton_.setTooltip("Cancel the active media relink preparation");
    inspectorStartBeatEditor_.onReturnKey = [this]()
    {
        commitInspectorStartBeatEdit();
    };
    inspectorStartBeatEditor_.onEscapeKey = [this]()
    {
        cancelInspectorStartBeatEdit();
    };

    refreshAppCommandEnabledState();
}

projectname::AppCommandRegistry MainComponent::buildAppCommandRegistry() const
{
    projectname::AppCommandAvailability availability;
    const auto projectChooserOpen = hasProjectChooserOpen();
    const auto projectFileWorkActive =
        audioImportJob_ != nullptr
        || mediaRelinkPreparationJob_ != nullptr
        || timelinePlaybackPreparationJob_ != nullptr
        || packageMediaCleanupJob_ != nullptr;
    const auto projectChooserAvailable =
        !projectChooserOpen
        && audioImportChooser_ == nullptr
        && mediaRelinkChooser_ == nullptr
        && !projectFileWorkActive;

    availability.canNewProject = projectChooserAvailable;
    availability.canSave = !projectChooserOpen && !projectFileWorkActive;
    availability.canSaveAs = projectChooserAvailable;
    availability.canOpen = projectChooserAvailable;
    availability.canUndoImportedClipEdit = session_.canUndoImportedClipEdit();
    availability.canRedoImportedClipEdit = session_.canRedoImportedClipEdit();
    availability.canImportAudio = projectChooserAvailable;
    availability.canCancelImport = audioImportJob_ != nullptr && canCancelAudioImport_;
    availability.canCancelTimelinePreparation =
        timelinePlaybackPreparationJob_ != nullptr && canCancelTimelinePlaybackPreparation_;

    return projectname::makePrototypeAppCommandRegistry(availability);
}

void MainComponent::refreshAppCommandEnabledState()
{
    const auto registry = buildAppCommandRegistry();

    setButtonEnabledFromCommand(playButton_, registry, projectname::AppCommandIds::transportPlay);
    setButtonEnabledFromCommand(stopButton_, registry, projectname::AppCommandIds::transportStop);
    projectButton_.setEnabled(registry.isEnabled(projectname::AppCommandIds::projectSave)
                              || registry.isEnabled(projectname::AppCommandIds::projectOpen)
                              || registry.isEnabled(projectname::AppCommandIds::projectNew)
                              || registry.isEnabled(projectname::AppCommandIds::projectSaveAs));
    setButtonEnabledFromCommand(importButton_, registry, projectname::AppCommandIds::audioImport);
    setButtonEnabledFromCommand(cancelImportButton_, registry, projectname::AppCommandIds::audioImportCancel);
    setButtonEnabledFromCommand(cancelTimelinePreparationButton_,
                                registry,
                                projectname::AppCommandIds::timelinePreparationCancel);
    setButtonEnabledFromCommand(audioButton_, registry, projectname::AppCommandIds::audioSettingsShow);
    refreshInspectorRelinkButtonState();
}

projectname::AppCommandResult MainComponent::dispatchAppCommand(std::string_view commandId)
{
    projectname::AppCommandDispatcher dispatcher;

    auto registerHandler = [&dispatcher](std::string_view id, projectname::AppCommandDispatcher::Handler handler)
    {
        static_cast<void>(dispatcher.registerHandler(toCommandString(id), std::move(handler)));
    };

    registerHandler(projectname::AppCommandIds::transportPlay,
                    [this]()
                    {
                        startPlayback();
                        return projectname::AppCommandResult::handled();
                    });
    registerHandler(projectname::AppCommandIds::transportStop,
                    [this]()
                    {
                        if (timelinePlaybackPreparationJob_ != nullptr)
                            cancelTimelinePlaybackPreparation();

                        session_.stop();
                        audioService_.setTestToneEnabled(session_.shouldPlayGeneratedTone());
                        return projectname::AppCommandResult::handled();
                    });
    registerHandler(projectname::AppCommandIds::projectNew,
                    [this]()
                    {
                        newProject();
                        return projectname::AppCommandResult::handled();
                    });
    registerHandler(projectname::AppCommandIds::projectSave,
                    [this]()
                    {
                        saveProject();
                        return projectname::AppCommandResult::handled();
                    });
    registerHandler(projectname::AppCommandIds::projectSaveAs,
                    [this]()
                    {
                        saveProjectAs();
                        return projectname::AppCommandResult::handled();
                    });
    registerHandler(projectname::AppCommandIds::projectOpen,
                    [this]()
                    {
                        openProject();
                        return projectname::AppCommandResult::handled();
                    });
    registerHandler(projectname::AppCommandIds::editUndo,
                    [this]()
                    {
                        return undoImportedClipEdit();
                    });
    registerHandler(projectname::AppCommandIds::editRedo,
                    [this]()
                    {
                        return redoImportedClipEdit();
                    });
    registerHandler(projectname::AppCommandIds::audioImport,
                    [this]()
                    {
                        importAudio();
                        return projectname::AppCommandResult::handled();
                    });
    registerHandler(projectname::AppCommandIds::audioImportCancel,
                    [this]()
                    {
                        cancelAudioImport();
                        return projectname::AppCommandResult::handled();
                    });
    registerHandler(projectname::AppCommandIds::timelinePreparationCancel,
                    [this]()
                    {
                        cancelTimelinePlaybackPreparation();
                        return projectname::AppCommandResult::handled();
                    });
    registerHandler(projectname::AppCommandIds::audioSettingsShow,
                    [this]()
                    {
                        showAudioSettings();
                        return projectname::AppCommandResult::handled();
                    });

    auto result = dispatcher.dispatch(buildAppCommandRegistry(), commandId);
    handleAppCommandResult(result);
    return result;
}

void MainComponent::handleAppCommandResult(const projectname::AppCommandResult& result)
{
    if (result.status == projectname::AppCommandResultStatus::handled)
        return;

    if (!result.message.empty())
        setStatus(juce::String(result.message));
}

void MainComponent::updateTransportLabels()
{
    const auto& transport = session_.getTransport();
    const auto signature = transport.getTimeSignature();
    signatureLabel_.setText(juce::String(signature.numerator) + "/" + juce::String(signature.denominator),
                            juce::dontSendNotification);
    positionLabel_.setText(formatPosition(transport.getPositionBeats(), signature),
                           juce::dontSendNotification);

    const auto summary = audioService_.getDeviceSummary();
    if (statusText_.isNotEmpty())
    {
        statusLabel_.setText(statusText_, juce::dontSendNotification);
    }
    else if (summary.isOpen)
    {
        statusLabel_.setText(summary.name + " | " + juce::String(static_cast<int>(summary.sampleRate))
                                 + " Hz | " + juce::String(summary.bufferSizeSamples) + " samples",
                             juce::dontSendNotification);
    }
}

void MainComponent::startPlayback()
{
    const auto summary = audioService_.getDeviceSummary();
    const auto sampleRateHz = summary.isOpen && summary.sampleRate > 0.0 ? summary.sampleRate : 44100.0;

    std::string error;
    const auto voiceWindowFrameCount = makeTimelineVoiceWindowFrameCount(summary, sampleRateHz);
    auto voicePlayback = session_.playCachedTimelineVoiceWindow(sampleRateHz,
                                                                voiceWindowFrameCount,
                                                                error);
    if (voicePlayback.status == projectname::TimelineVoicePlaybackPreparationStatus::voiceWindowReady)
    {
        schedulePreparedTimelineVoicePlayback(std::move(voicePlayback));
        return;
    }

    if (voicePlayback.status == projectname::TimelineVoicePlaybackPreparationStatus::backgroundPreparationRequired)
    {
        startTimelinePlaybackPreparation(sampleRateHz, voiceWindowFrameCount);
        return;
    }

    auto playback = session_.playFromCachedTimeline(sampleRateHz, error);
    if (playback.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady)
    {
        schedulePreparedTimelinePlayback(std::move(playback));
        return;
    }

    if (playback.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired)
    {
        startTimelinePlaybackPreparation(sampleRateHz, voiceWindowFrameCount);
        return;
    }

    audioService_.setTestToneEnabled(session_.shouldPlayGeneratedTone());
    if (!error.empty())
        setStatus("Playing generated tone - " + juce::String(error));
}

void MainComponent::schedulePreparedTimelinePlayback(projectname::TimelinePlaybackPreparation playback)
{
    if (!playback.activation.has_value()
        || !playback.clip.has_value()
        || playback.preparedSamples == nullptr)
    {
        session_.play();
        audioService_.setTestToneEnabled(session_.shouldPlayGeneratedTone());
        setStatus("Playing generated tone - timeline clip was not ready");
        return;
    }

    const auto summary = audioService_.getDeviceSummary();
    const auto clipName = juce::String(playback.clip->clipName);
    const auto importedSampleRate = playback.preparedClip.sampleRateHz;
    audioService_.playPreparedMonoClipFromTimeline(std::move(playback.preparedSamples),
                                                   *playback.activation,
                                                   playback.transportTimelineSample);

    auto status = "Playing " + clipName + " from timeline";
    if (playback.usedCachedBuffer)
        status += " (cached)";

    if (summary.isOpen && importedSampleRate > 0.0 && std::abs(summary.sampleRate - importedSampleRate) > 1.0)
        status += " - sample-rate conversion is not implemented yet";

    setStatus(status);
}

void MainComponent::schedulePreparedTimelineVoicePlayback(
    projectname::TimelineVoicePlaybackPreparation playback)
{
    if (playback.preparedBuffers.empty() || playback.schedule.voices.empty())
    {
        session_.play();
        audioService_.setTestToneEnabled(session_.shouldPlayGeneratedTone());
        setStatus("Playing generated tone - timeline voice window was not ready");
        return;
    }

    const auto voiceCount = playback.schedule.voices.size();
    audioService_.playPreparedTrackVoiceSchedule(std::move(playback.preparedBuffers),
                                                 std::move(playback.schedule));

    auto status = "Playing timeline voice window ("
        + juce::String(static_cast<int>(voiceCount))
        + (voiceCount == 1 ? " voice)" : " voices)");
    if (playback.usedCachedBuffers)
        status += " (cached)";

    if (!playback.sampleRateMismatches.empty())
    {
        status += playback.sampleRateMismatches.size() == 1
            ? " - sample-rate mismatch; resampling is not implemented yet"
            : " - sample-rate mismatches; resampling is not implemented yet";
    }

    setStatus(status);
}

void MainComponent::startTimelinePlaybackPreparation(double outputSampleRateHz,
                                                     std::int64_t minimumRenderFrameCount)
{
    if (timelinePlaybackPreparationJob_ != nullptr)
    {
        setStatus("Timeline playback preparation is already running");
        return;
    }

    projectname::BackgroundTimelinePlaybackPreparationRequest request;
    request.project = session_.getProject();
    request.packageDirectory = getCurrentProjectPackagePath();
    request.outputSampleRateHz = outputSampleRateHz;
    request.minimumRenderFrameCount = minimumRenderFrameCount;

    timelinePlaybackPreparationJob_ =
        std::make_unique<projectname::BackgroundTimelinePlaybackPreparationJob>(std::move(request));
    timelinePlaybackPreparationJob_->start();
    canCancelTimelinePlaybackPreparation_ = true;
    refreshAppCommandEnabledState();
    audioService_.stopPlayback();
    setStatus("Preparing timeline audio - 5%");
}

void MainComponent::showProjectMenu()
{
    const auto registry = buildAppCommandRegistry();

    juce::PopupMenu menu;
    menu.addItem(1, "New...", registry.isEnabled(projectname::AppCommandIds::projectNew));
    menu.addItem(2, "Save", registry.isEnabled(projectname::AppCommandIds::projectSave));
    menu.addItem(3, "Save As...", registry.isEnabled(projectname::AppCommandIds::projectSaveAs));
    menu.addSeparator();
    menu.addItem(4, "Open...", registry.isEnabled(projectname::AppCommandIds::projectOpen));

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&projectButton_),
                       [safeThis](int result) mutable
                       {
                           if (safeThis == nullptr)
                               return;

                           switch (result)
                           {
                               case 1:
                                   safeThis->dispatchAppCommand(projectname::AppCommandIds::projectNew);
                                   break;
                               case 2:
                                   safeThis->dispatchAppCommand(projectname::AppCommandIds::projectSave);
                                   break;
                               case 3:
                                   safeThis->dispatchAppCommand(projectname::AppCommandIds::projectSaveAs);
                                   break;
                               case 4:
                                   safeThis->dispatchAppCommand(projectname::AppCommandIds::projectOpen);
                                   break;
                               default:
                                   break;
                           }
                       });
}

void MainComponent::newProject()
{
    if (hasProjectChooserOpen())
    {
        setStatus("Finish the current project chooser first");
        return;
    }

    const auto initialPackage = toJuceFile(getCurrentProjectPackagePath());
    projectNewChooser_ = std::make_unique<juce::FileChooser>("Create a Rabbington Studio project package",
                                                             initialPackage,
                                                             "*.project",
                                                             true,
                                                             false,
                                                             this);
    refreshAppCommandEnabledState();

    const auto chooserFlags =
        juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    projectNewChooser_->launchAsync(chooserFlags,
                                    [safeThis](const juce::FileChooser& chooser) mutable
                                    {
                                        if (safeThis != nullptr)
                                            safeThis->handleProjectNewResult(chooser);
                                    });
}

void MainComponent::saveProject()
{
    std::string error;
    const auto packagePath = getCurrentProjectPackagePath();

    if (session_.saveProjectPackage(packagePath, error))
        setStatus("Saved project package: " + juce::String(packagePath.string()));
    else
        setStatus("Save failed: " + juce::String(error));

    refreshInspectorPanel();
    refreshMixerControls();
    requestPackageMediaMaintenanceRefresh();
    updateTransportLabels();
}

void MainComponent::saveProjectAs()
{
    if (hasProjectChooserOpen())
    {
        setStatus("Finish the current project chooser first");
        return;
    }

    const auto initialPackage = toJuceFile(getCurrentProjectPackagePath());
    projectSaveAsChooser_ = std::make_unique<juce::FileChooser>("Save Rabbington Studio project as",
                                                                initialPackage,
                                                                "*.project",
                                                                true,
                                                                false,
                                                                this);
    refreshAppCommandEnabledState();

    const auto chooserFlags =
        juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    projectSaveAsChooser_->launchAsync(chooserFlags,
                                       [safeThis](const juce::FileChooser& chooser) mutable
                                       {
                                           if (safeThis != nullptr)
                                               safeThis->handleProjectSaveAsResult(chooser);
                                       });
}

void MainComponent::openProject()
{
    if (hasProjectChooserOpen())
    {
        setStatus("Finish the current project chooser first");
        return;
    }

    const auto initialPackage = toJuceFile(getCurrentProjectPackagePath());
    projectOpenChooser_ = std::make_unique<juce::FileChooser>("Open a Rabbington Studio project package",
                                                              initialPackage,
                                                              "*.project",
                                                              true,
                                                              false,
                                                              this);
    refreshAppCommandEnabledState();

    const auto chooserFlags =
        juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectDirectories
        | juce::FileBrowserComponent::canSelectFiles;
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    projectOpenChooser_->launchAsync(chooserFlags,
                                     [safeThis](const juce::FileChooser& chooser) mutable
                                     {
                                         if (safeThis != nullptr)
                                             safeThis->handleProjectOpenResult(chooser);
                                     });
}

void MainComponent::handleProjectNewResult(const juce::FileChooser& chooser)
{
    const auto selectedFile = chooser.getResult();
    projectNewChooser_.reset();

    if (selectedFile == juce::File {})
    {
        setStatus("New project cancelled");
        refreshAppCommandEnabledState();
        return;
    }

    const auto packagePath = projectPackagePathFromChooserResult(selectedFile, true);
    if (projectManifestExists(packagePath))
    {
        setStatus("New project cancelled: package already contains a manifest");
        refreshAppCommandEnabledState();
        return;
    }

    auto project = projectname::ProjectModel::createDefault();
    project.setName(projectNameFromPackagePath(packagePath));

    std::string error;
    if (!project.savePackage(packagePath, error))
    {
        setStatus("New project failed: " + juce::String(error));
        refreshAppCommandEnabledState();
        return;
    }

    session_.stop();
    session_.replaceProject(std::move(project));
    currentProjectPackagePath_ = packagePath;
    selectedPackageMediaCleanupId_.clear();
    refreshAfterProjectPackageChange("Created project package: " + juce::String(packagePath.string()));
}

void MainComponent::handleProjectSaveAsResult(const juce::FileChooser& chooser)
{
    const auto selectedFile = chooser.getResult();
    projectSaveAsChooser_.reset();

    if (selectedFile == juce::File {})
    {
        setStatus("Save As cancelled");
        refreshAppCommandEnabledState();
        return;
    }

    const auto packagePath = projectPackagePathFromChooserResult(selectedFile, true);

    std::string error;
    if (session_.saveProjectPackage(packagePath, error))
    {
        currentProjectPackagePath_ = packagePath;
        selectedPackageMediaCleanupId_.clear();
        refreshAfterProjectPackageChange("Saved project package as: " + juce::String(packagePath.string()));
    }
    else
    {
        setStatus("Save As failed: " + juce::String(error));
        refreshAppCommandEnabledState();
    }
}

void MainComponent::handleProjectOpenResult(const juce::FileChooser& chooser)
{
    const auto selectedFile = chooser.getResult();
    projectOpenChooser_.reset();

    if (selectedFile == juce::File {})
    {
        setStatus("Open cancelled");
        refreshAppCommandEnabledState();
        return;
    }

    const auto packagePath = projectPackagePathFromChooserResult(selectedFile, false);

    std::string error;
    auto project = projectname::ProjectModel::loadPackage(packagePath, error);
    if (!project.has_value())
    {
        setStatus("Open failed: " + juce::String(error));
        refreshAppCommandEnabledState();
        updateTransportLabels();
        return;
    }

    session_.stop();
    session_.replaceProject(std::move(*project));
    currentProjectPackagePath_ = packagePath;
    selectedPackageMediaCleanupId_.clear();
    refreshAfterProjectPackageChange("Opened project package: " + juce::String(packagePath.string()));
}

void MainComponent::refreshAfterProjectPackageChange(juce::String status)
{
    tempoSlider_.setValue(session_.getTransport().getTempoBpm(), juce::dontSendNotification);
    audioService_.setTestToneEnabled(session_.shouldPlayGeneratedTone());
    hasPackageMediaMaintenanceSnapshot_ = false;
    packageMediaMaintenanceViewModel_ = {};
    refreshWorkspaceTimelineLane();
    refreshInspectorPanel();
    refreshMixerControls();
    requestPackageMediaMaintenanceRefresh();
    refreshAppCommandEnabledState();
    setStatus(std::move(status));
    updateTransportLabels();
}

projectname::AppCommandResult MainComponent::undoImportedClipEdit()
{
    std::string error;
    if (!session_.undoImportedClipEdit(error))
    {
        refreshAppCommandEnabledState();
        return projectname::AppCommandResult::failed("Undo failed: " + error);
    }

    refreshWorkspaceTimelineLane();
    refreshInspectorPanel();
    requestPackageMediaMaintenanceRefresh();
    refreshAppCommandEnabledState();
    return projectname::AppCommandResult::handledWithStatus("Undid imported clip edit");
}

projectname::AppCommandResult MainComponent::redoImportedClipEdit()
{
    std::string error;
    if (!session_.redoImportedClipEdit(error))
    {
        refreshAppCommandEnabledState();
        return projectname::AppCommandResult::failed("Redo failed: " + error);
    }

    refreshWorkspaceTimelineLane();
    refreshInspectorPanel();
    requestPackageMediaMaintenanceRefresh();
    refreshAppCommandEnabledState();
    return projectname::AppCommandResult::handledWithStatus("Redid imported clip edit");
}

void MainComponent::importAudio()
{
    if (audioImportJob_ != nullptr)
    {
        setStatus("Audio import is already running");
        return;
    }

    if (audioImportChooser_ != nullptr)
    {
        setStatus("Audio import chooser is already open");
        return;
    }

    if (mediaRelinkChooser_ != nullptr)
    {
        setStatus("Finish the media relink chooser before importing audio");
        return;
    }

    if (mediaRelinkPreparationJob_ != nullptr)
    {
        setStatus("Finish or cancel media relink preparation before importing audio");
        return;
    }

    const auto initialDirectory = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    audioImportChooser_ = std::make_unique<juce::FileChooser>("Import a PCM16 WAV file",
                                                              initialDirectory,
                                                              "*.wav",
                                                              true,
                                                              false,
                                                              this);
    refreshAppCommandEnabledState();

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    audioImportChooser_->launchAsync(chooserFlags,
                                     [safeThis](const juce::FileChooser& chooser) mutable
                                     {
                                         if (safeThis != nullptr)
                                             safeThis->handleAudioImportResult(chooser);
                                     });
}

void MainComponent::relinkSelectedClipMedia()
{
    if (!inspectorEditDraft_.canEdit()
        || inspectorEditDraft_.getClipId() != session_.getSelectedClipId())
    {
        setStatus("Select an imported audio clip before relinking media");
        refreshInspectorPanel();
        return;
    }

    if (mediaRelinkChooser_ != nullptr)
    {
        setStatus("Media relink chooser is already open");
        return;
    }

    if (audioImportChooser_ != nullptr || audioImportJob_ != nullptr)
    {
        setStatus("Finish the current audio import before relinking media");
        return;
    }

    if (mediaRelinkPreparationJob_ != nullptr)
    {
        setStatus("Media relink preparation is already running");
        return;
    }

    if (timelinePlaybackPreparationJob_ != nullptr)
    {
        setStatus("Finish or cancel timeline audio preparation before relinking media");
        return;
    }

    const auto initialDirectory = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    mediaRelinkChooser_ = std::make_unique<juce::FileChooser>("Relink selected clip to a PCM16 WAV file",
                                                              initialDirectory,
                                                              "*.wav",
                                                              true,
                                                              false,
                                                              this);
    refreshInspectorRelinkButtonState();
    refreshAppCommandEnabledState();

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    mediaRelinkChooser_->launchAsync(chooserFlags,
                                     [safeThis](const juce::FileChooser& chooser) mutable
                                     {
                                         if (safeThis != nullptr)
                                             safeThis->handleMediaRelinkResult(chooser);
                                     });
}

void MainComponent::cancelAudioImport()
{
    if (audioImportJob_ == nullptr)
    {
        setStatus("No audio import is running");
        canCancelAudioImport_ = false;
        refreshAppCommandEnabledState();
        return;
    }

    audioImportJob_->requestCancel();
    canCancelAudioImport_ = false;
    refreshAppCommandEnabledState();
    setStatus("Cancelling import before package write if possible");
}

void MainComponent::cancelMediaRelinkPreparation()
{
    if (mediaRelinkPreparationJob_ == nullptr)
    {
        setStatus("No media relink preparation is running");
        canCancelMediaRelinkPreparation_ = false;
        refreshInspectorRelinkButtonState();
        refreshAppCommandEnabledState();
        return;
    }

    mediaRelinkPreparationJob_->requestCancel();
    canCancelMediaRelinkPreparation_ = false;
    refreshInspectorRelinkButtonState();
    refreshAppCommandEnabledState();
    setStatus("Cancelling media relink preparation");
}

void MainComponent::cancelTimelinePlaybackPreparation()
{
    if (timelinePlaybackPreparationJob_ == nullptr)
    {
        setStatus("No timeline preparation is running");
        canCancelTimelinePlaybackPreparation_ = false;
        refreshAppCommandEnabledState();
        return;
    }

    timelinePlaybackPreparationJob_->requestCancel();
    canCancelTimelinePlaybackPreparation_ = false;
    refreshAppCommandEnabledState();
    setStatus("Cancelling timeline audio preparation");
}

void MainComponent::handleAudioImportResult(const juce::FileChooser& chooser)
{
    const auto selectedFile = chooser.getResult();
    if (!selectedFile.existsAsFile())
    {
        setStatus("Import cancelled");
        audioImportChooser_.reset();
        refreshAppCommandEnabledState();
        return;
    }

    auto projectSnapshot = session_.getProject();

    projectname::BackgroundAudioImportRequest request;
    request.project = std::move(projectSnapshot);
    request.packageDirectory = getCurrentProjectPackagePath();
    request.sourceWavPath = std::filesystem::path(selectedFile.getFullPathName().toStdString());

    audioImportJob_ = std::make_unique<projectname::BackgroundAudioImportJob>(std::move(request));
    audioImportJob_->start();

    canCancelAudioImport_ = true;
    setStatus("Importing " + selectedFile.getFileName());
    audioImportChooser_.reset();
    refreshAppCommandEnabledState();
    updateTransportLabels();
}

void MainComponent::handleMediaRelinkResult(const juce::FileChooser& chooser)
{
    const auto selectedFile = chooser.getResult();
    mediaRelinkChooser_.reset();

    if (!selectedFile.existsAsFile())
    {
        setStatus("Media relink cancelled");
        refreshInspectorRelinkButtonState();
        refreshAppCommandEnabledState();
        return;
    }

    if (!inspectorEditDraft_.canEdit()
        || inspectorEditDraft_.getClipId() != session_.getSelectedClipId())
    {
        setStatus("Media relink failed: selected clip changed");
        refreshInspectorPanel();
        refreshAppCommandEnabledState();
        return;
    }

    projectname::BackgroundMediaRelinkPreparationRequest request;
    request.project = session_.getProject();
    request.packageDirectory = getCurrentProjectPackagePath();
    request.sourceWavPath = std::filesystem::path(selectedFile.getFullPathName().toStdString());
    request.selectedClipId = session_.getSelectedClipId();

    mediaRelinkPreparationJob_ =
        std::make_unique<projectname::BackgroundMediaRelinkPreparationJob>(std::move(request));
    mediaRelinkPreparationJob_->start();
    canCancelMediaRelinkPreparation_ = true;
    refreshInspectorRelinkButtonState();
    refreshAppCommandEnabledState();
    setStatus("Preparing media relink for " + selectedFile.getFileName());
}

void MainComponent::pollAudioImportJob()
{
    if (audioImportJob_ == nullptr)
        return;

    updateAudioImportProgress(audioImportJob_->getProgress());

    if (!audioImportJob_->isReady())
        return;

    auto result = audioImportJob_->waitForResult();
    audioImportJob_.reset();
    canCancelAudioImport_ = false;
    refreshAppCommandEnabledState();
    applyCompletedAudioImport(std::move(result));
}

void MainComponent::pollMediaRelinkPreparationJob()
{
    if (mediaRelinkPreparationJob_ == nullptr)
        return;

    updateMediaRelinkPreparationProgress(mediaRelinkPreparationJob_->getProgress());

    if (!mediaRelinkPreparationJob_->isReady())
        return;

    auto result = mediaRelinkPreparationJob_->waitForResult();
    mediaRelinkPreparationJob_.reset();
    canCancelMediaRelinkPreparation_ = false;
    refreshInspectorRelinkButtonState();
    refreshAppCommandEnabledState();
    applyCompletedMediaRelinkPreparation(std::move(result));
}

void MainComponent::applyCompletedMediaRelinkPreparation(
    projectname::BackgroundMediaRelinkPreparationResult result)
{
    if (result.cancelled)
    {
        setStatus("Media relink cancelled");
        refreshInspectorPanel();
        requestPackageMediaMaintenanceRefresh();
        return;
    }

    if (!result.preparation.has_value())
    {
        setStatus("Media relink failed: " + juce::String(result.error));
        refreshInspectorPanel();
        refreshAppCommandEnabledState();
        requestPackageMediaMaintenanceRefresh();
        return;
    }

    auto preparation = std::move(*result.preparation);
    const auto relinkedPath = preparation.relativePath;
    const auto relinkedSampleRate = preparation.preparedClip.sampleRateHz;
    auto commit = projectname::commitPreparedImportedClipMediaRelink(session_, preparation);
    if (commit.status != projectname::ImportedClipMediaRelinkCommitStatus::committed)
    {
        setStatus("Media relink failed: " + juce::String(commit.error));
        refreshWorkspaceTimelineLane();
        refreshInspectorPanel();
        refreshAppCommandEnabledState();
        requestPackageMediaMaintenanceRefresh();
        return;
    }

    const auto* relinkedClip = findProjectClipById(session_.getProject(), preparation.clipId);
    auto preparedSamples = relinkedClip != nullptr
        ? session_.cacheImportedTimelineClip(*relinkedClip, std::move(preparation.preparedClip))
        : nullptr;
    const auto refreshedPlaybackCache = preparedSamples != nullptr;
    if (refreshedPlaybackCache)
        audioService_.playPreparedMonoClip(std::move(preparedSamples));

    refreshWorkspaceTimelineLane();
    refreshInspectorPanel();
    refreshAppCommandEnabledState();
    requestPackageMediaMaintenanceRefresh();
    updateTransportLabels();

    auto status = "Relinked selected clip to " + juce::String(relinkedPath);
    const auto summary = audioService_.getDeviceSummary();
    if (summary.isOpen && relinkedSampleRate > 0.0 && std::abs(summary.sampleRate - relinkedSampleRate) > 1.0)
        status += " - sample-rate conversion is not implemented yet";
    if (!refreshedPlaybackCache)
        status += " - playback cache was not refreshed";

    setStatus(status);
}

void MainComponent::pollTimelinePlaybackPreparationJob()
{
    if (timelinePlaybackPreparationJob_ == nullptr)
        return;

    const auto progress = timelinePlaybackPreparationJob_->getProgress();
    updateTimelinePlaybackPreparationProgress(progress);

    if (!timelinePlaybackPreparationJob_->isReady())
        return;

    auto result = timelinePlaybackPreparationJob_->waitForResult();
    timelinePlaybackPreparationJob_.reset();
    canCancelTimelinePlaybackPreparation_ = false;
    refreshAppCommandEnabledState();
    applyCompletedTimelinePlaybackPreparation(std::move(result));
}

void MainComponent::updateAudioImportProgress(const projectname::BackgroundAudioImportProgress& progress)
{
    const auto canCancelBeforeCommit =
        progress.phase == projectname::BackgroundAudioImportPhase::pending
        || progress.phase == projectname::BackgroundAudioImportPhase::decoding
        || progress.phase == projectname::BackgroundAudioImportPhase::readyToCommit
        || progress.phase == projectname::BackgroundAudioImportPhase::copying;
    canCancelAudioImport_ = canCancelBeforeCommit && !progress.cancelRequested;
    refreshAppCommandEnabledState();

    if (progress.cancelRequested && canCancelBeforeCommit)
    {
        setStatus("Cancelling import before package write - " + juce::String(progress.percent) + "%");
        return;
    }

    switch (progress.phase)
    {
        case projectname::BackgroundAudioImportPhase::pending:
            setStatus("Preparing audio import");
            break;

        case projectname::BackgroundAudioImportPhase::decoding:
        {
            if (progress.framesTotal > 0)
            {
                setStatus("Decoding audio - " + juce::String(progress.percent) + "% ("
                          + juce::String(static_cast<double>(progress.framesProcessed), 0)
                          + "/"
                          + juce::String(static_cast<double>(progress.framesTotal), 0)
                          + " frames)");
            }
            else
            {
                setStatus("Decoding audio - " + juce::String(progress.percent) + "%");
            }
            break;
        }

        case projectname::BackgroundAudioImportPhase::readyToCommit:
            setStatus("Ready to save imported audio - " + juce::String(progress.percent) + "%");
            break;

        case projectname::BackgroundAudioImportPhase::copying:
        {
            const auto copiedKiB = static_cast<double>(progress.bytesProcessed) / 1024.0;
            const auto totalKiB = static_cast<double>(progress.bytesTotal) / 1024.0;
            setStatus("Copying audio - " + juce::String(progress.percent) + "% ("
                      + juce::String(copiedKiB, 1) + "/" + juce::String(totalKiB, 1) + " KiB)");
            break;
        }

        case projectname::BackgroundAudioImportPhase::committing:
            setStatus("Saving imported audio - " + juce::String(progress.percent) + "%");
            break;

        case projectname::BackgroundAudioImportPhase::completed:
        case projectname::BackgroundAudioImportPhase::failed:
        case projectname::BackgroundAudioImportPhase::cancelled:
            break;
    }
}

void MainComponent::updateMediaRelinkPreparationProgress(
    const projectname::BackgroundMediaRelinkPreparationProgress& progress)
{
    const auto canCancel =
        progress.phase == projectname::BackgroundMediaRelinkPreparationPhase::pending
        || progress.phase == projectname::BackgroundMediaRelinkPreparationPhase::decoding
        || progress.phase == projectname::BackgroundMediaRelinkPreparationPhase::copying
        || progress.phase == projectname::BackgroundMediaRelinkPreparationPhase::analysing;
    canCancelMediaRelinkPreparation_ = canCancel && !progress.cancelRequested;
    refreshInspectorRelinkButtonState();
    refreshAppCommandEnabledState();

    auto status = formatMediaRelinkPreparationPhase(progress.phase)
        + " - " + juce::String(progress.percent) + "%";

    if (progress.cancelRequested && canCancel)
        status = "Cancelling media relink preparation - " + juce::String(progress.percent) + "%";

    if (progress.phase == projectname::BackgroundMediaRelinkPreparationPhase::decoding
        && progress.framesTotal > 0)
    {
        status += " ("
            + juce::String(static_cast<double>(progress.framesProcessed), 0)
            + "/"
            + juce::String(static_cast<double>(progress.framesTotal), 0)
            + " frames)";
    }
    else if (progress.phase == projectname::BackgroundMediaRelinkPreparationPhase::copying
             && progress.bytesTotal > 0)
    {
        const auto copiedKiB = static_cast<double>(progress.bytesProcessed) / 1024.0;
        const auto totalKiB = static_cast<double>(progress.bytesTotal) / 1024.0;
        status += " ("
            + juce::String(copiedKiB, 1)
            + "/"
            + juce::String(totalKiB, 1)
            + " KiB)";
    }

    setStatus(status);
}

void MainComponent::updateTimelinePlaybackPreparationProgress(
    const projectname::BackgroundTimelinePlaybackPreparationProgress& progress)
{
    const auto canCancel =
        progress.phase == projectname::BackgroundTimelinePlaybackPreparationPhase::pending
        || progress.phase == projectname::BackgroundTimelinePlaybackPreparationPhase::planning
        || progress.phase == projectname::BackgroundTimelinePlaybackPreparationPhase::decoding;
    canCancelTimelinePlaybackPreparation_ = canCancel && !progress.cancelRequested;
    refreshAppCommandEnabledState();

    auto status = formatTimelinePreparationPhase(progress.phase)
        + " - " + juce::String(progress.percent) + "%";

    if (progress.cancelRequested && canCancel)
        status = "Cancelling timeline audio preparation - " + juce::String(progress.percent) + "%";

    if (progress.phase == projectname::BackgroundTimelinePlaybackPreparationPhase::decoding
        && progress.framesTotal > 0)
    {
        status += " ("
            + juce::String(static_cast<double>(progress.framesProcessed), 0)
            + "/"
            + juce::String(static_cast<double>(progress.framesTotal), 0)
            + " frames)";
    }

    setStatus(status);
}

void MainComponent::applyCompletedAudioImport(projectname::BackgroundAudioImportResult result)
{
    if (result.cancelled)
    {
        setStatus("Import cancelled");
        requestPackageMediaMaintenanceRefresh();
        return;
    }

    if (!result.import.has_value())
    {
        setStatus("Import failed: " + juce::String(result.error));
        requestPackageMediaMaintenanceRefresh();
        return;
    }

    auto imported = std::move(*result.import);
    const auto importedPath = imported.clip.relativePath;
    const auto importedSampleRate = imported.preparedClip.sampleRateHz;
    const auto summary = audioService_.getDeviceSummary();

    session_.replaceProject(std::move(result.project));
    std::string selectionError;
    [[maybe_unused]] const auto selectedImportedClip =
        session_.selectImportedAudioClip(imported.clip.id, selectionError);
    refreshWorkspaceTimelineLane();
    refreshInspectorPanel();
    refreshMixerControls();
    requestPackageMediaMaintenanceRefresh();
    auto preparedSamples = session_.cacheImportedTimelineClip(imported.clip, std::move(imported.preparedClip));
    if (preparedSamples != nullptr)
        audioService_.playPreparedMonoClip(std::move(preparedSamples));

    auto status = "Imported " + juce::String(importedPath) + " into project audio";
    if (summary.isOpen && importedSampleRate > 0.0 && std::abs(summary.sampleRate - importedSampleRate) > 1.0)
        status += " - sample-rate conversion is not implemented yet";

    setStatus(status);
}

void MainComponent::applyCompletedTimelinePlaybackPreparation(
    projectname::BackgroundTimelinePlaybackPreparationResult result)
{
    const auto summary = audioService_.getDeviceSummary();
    const auto sampleRateHz = summary.isOpen && summary.sampleRate > 0.0 ? summary.sampleRate : 44100.0;
    auto completion =
        projectname::completeBackgroundTimelinePlaybackPreparation(session_, std::move(result), sampleRateHz);

    if (completion.status == projectname::TimelinePlaybackPreparationCompletionStatus::scheduledVoiceWindow)
    {
        schedulePreparedTimelineVoicePlayback(std::move(completion.voicePlayback));
        return;
    }

    if (completion.status == projectname::TimelinePlaybackPreparationCompletionStatus::scheduledImportedClip)
    {
        schedulePreparedTimelinePlayback(std::move(completion.playback));
        return;
    }

    if (completion.status == projectname::TimelinePlaybackPreparationCompletionStatus::cancelled)
    {
        setStatus("Timeline playback preparation cancelled");
        return;
    }

    if (completion.status == projectname::TimelinePlaybackPreparationCompletionStatus::staleResult)
    {
        setStatus("Timeline playback preparation was stale");
        return;
    }

    audioService_.setTestToneEnabled(session_.shouldPlayGeneratedTone());
    setStatus("Playing generated tone - " + juce::String(completion.message));
}

bool MainComponent::hasActivePackageFileWork() const
{
    return audioImportJob_ != nullptr
        || mediaRelinkPreparationJob_ != nullptr
        || timelinePlaybackPreparationJob_ != nullptr
        || packageMediaCleanupJob_ != nullptr;
}

void MainComponent::pollPackageMediaCleanupJob()
{
    if (packageMediaCleanupJob_ == nullptr)
        return;

    updatePackageMediaCleanupProgress(packageMediaCleanupJob_->getProgress());

    if (!packageMediaCleanupJob_->isReady())
        return;

    auto result = packageMediaCleanupJob_->waitForResult();
    packageMediaCleanupJob_.reset();
    refreshAppCommandEnabledState();
    applyCompletedPackageMediaCleanup(std::move(result));
}

void MainComponent::updatePackageMediaCleanupProgress(
    const projectname::BackgroundPackageMediaCleanupProgress& progress)
{
    auto status = formatPackageMediaCleanupPhase(progress.phase)
        + " - " + juce::String(progress.percent) + "%";

    if (progress.cancelRequested
        && progress.phase != projectname::BackgroundPackageMediaCleanupPhase::completed
        && progress.phase != projectname::BackgroundPackageMediaCleanupPhase::failed)
    {
        status = "Cancelling package media restore - " + juce::String(progress.percent) + "%";
    }

    setStatus(status);
}

void MainComponent::applyCompletedPackageMediaCleanup(
    projectname::BackgroundPackageMediaCleanupResult result)
{
    if (result.operation == projectname::BackgroundPackageMediaCleanupOperation::quarantine
        && result.quarantine.status == projectname::PackageMediaQuarantineCommandStatus::completed
        && result.quarantine.restoreManifest.has_value())
    {
        selectedPackageMediaCleanupId_ = result.quarantine.restoreManifest->cleanupId;
    }

    requestPackageMediaMaintenanceRefresh();
    refreshWorkspaceTimelineLane();
    refreshInspectorPanel();
    refreshBrowserPanel();

    if (result.cancelled)
    {
        setStatus("Package media restore cancelled");
        return;
    }

    if (result.operation != projectname::BackgroundPackageMediaCleanupOperation::restore)
    {
        if (!result.error.empty())
        {
            setStatus("Package media cleanup failed: " + juce::String(result.error));
            return;
        }

        if (result.quarantine.status == projectname::PackageMediaQuarantineCommandStatus::completed)
        {
            auto movedCount = std::size_t {};
            if (result.quarantine.restoreManifest.has_value())
                movedCount = result.quarantine.restoreManifest->movedEntries.size();

            auto status = "Moved "
                + juce::String(static_cast<int>(movedCount))
                + " package media item";
            if (movedCount != 1)
                status += "s";
            status += " to media trash";
            setStatus(status);
        }
        else
        {
            setStatus("Package media cleanup failed");
        }
        return;
    }

    if (result.restore.status == projectname::PackageMediaQuarantineRestoreCommandStatus::restored)
    {
        auto status = "Restored "
            + juce::String(static_cast<int>(result.restore.restoredCount))
            + " package media item";
        if (result.restore.restoredCount != 1)
            status += "s";
        setStatus(status);
        return;
    }

    auto failure = result.error.empty()
        ? juce::String("Package media restore failed")
        : juce::String("Package media restore failed: ") + juce::String(result.error);
    setStatus(failure);
}

void MainComponent::requestPackageMediaMaintenanceRefresh()
{
    if (packageMediaMaintenanceScan_.valid())
    {
        if (packageMediaMaintenanceScan_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            applyPackageMediaMaintenanceScanResult(packageMediaMaintenanceScan_.get());
        }
        else
        {
            packageMediaMaintenanceRefreshPending_ = true;
            refreshBrowserPanel();
            return;
        }
    }

    packageMediaMaintenanceRefreshPending_ = false;
    const auto generation = ++packageMediaMaintenanceScanGeneration_;
    const auto packageDirectory = getCurrentProjectPackagePath();
    const auto selectedCleanupId = selectedPackageMediaCleanupId_;
    const auto packageWorkInProgress = hasActivePackageFileWork();

    packageMediaMaintenanceScan_ = std::async(std::launch::async,
                                              runPackageMediaMaintenanceScan,
                                              packageDirectory,
                                              selectedCleanupId,
                                              packageWorkInProgress,
                                              generation);
    refreshBrowserPanel();
}

void MainComponent::pollPackageMediaMaintenanceScan()
{
    if (!packageMediaMaintenanceScan_.valid()
        || packageMediaMaintenanceScan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        return;
    }

    const auto refreshAgain = packageMediaMaintenanceRefreshPending_;
    packageMediaMaintenanceRefreshPending_ = false;
    applyPackageMediaMaintenanceScanResult(packageMediaMaintenanceScan_.get());

    if (refreshAgain)
        requestPackageMediaMaintenanceRefresh();
}

void MainComponent::applyPackageMediaMaintenanceScanResult(PackageMediaMaintenanceScanResult result)
{
    if (result.generation != packageMediaMaintenanceScanGeneration_)
        return;

    packageMediaMaintenanceViewModel_ = std::move(result.viewModel);
    hasPackageMediaMaintenanceSnapshot_ = true;
    selectedPackageMediaCleanupId_ = packageMediaMaintenanceViewModel_.selectedCleanupId;
    refreshBrowserPanel();
}

void MainComponent::refreshBrowserPanel()
{
    const auto scanRunning =
        packageMediaMaintenanceScan_.valid()
        && packageMediaMaintenanceScan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    const auto modelRows = projectname::buildPackageMediaMaintenanceBrowserRows(
        packageMediaMaintenanceViewModel_,
        { hasPackageMediaMaintenanceSnapshot_, scanRunning, 2, hasActivePackageFileWork() });

    auto cleanupEnabled = modelRows.cleanupAction.enabled && !scanRunning && !hasActivePackageFileWork();
    auto cleanupTooltip = juce::String("Move cleanup candidates to media trash");
    if (!modelRows.cleanupAction.disabledReason.empty())
        cleanupTooltip = juce::String(modelRows.cleanupAction.disabledReason);
    if (scanRunning)
    {
        cleanupEnabled = false;
        cleanupTooltip = "Waiting for package media scan";
    }
    else if (packageMediaCleanupJob_ != nullptr)
    {
        cleanupEnabled = false;
        cleanupTooltip = "Package media cleanup is already running";
    }
    else if (hasActivePackageFileWork())
    {
        cleanupEnabled = false;
        cleanupTooltip = "Package files are busy";
    }

    auto restoreEnabled = modelRows.restoreAction.enabled && !scanRunning && !hasActivePackageFileWork();
    auto restoreTooltip = juce::String("Restore selected cleanup batch");
    if (!modelRows.restoreAction.disabledReason.empty())
        restoreTooltip = juce::String(modelRows.restoreAction.disabledReason);
    if (scanRunning)
    {
        restoreEnabled = false;
        restoreTooltip = "Waiting for package media scan";
    }
    else if (packageMediaCleanupJob_ != nullptr)
    {
        restoreEnabled = false;
        restoreTooltip = "Package media restore is already running";
    }
    else if (hasActivePackageFileWork())
    {
        restoreEnabled = false;
        restoreTooltip = "Package files are busy";
    }

    browserPanel_.setSubtitle(scanRunning
                                  ? "Project assets and media scan running"
                                  : "Project assets and media maintenance");
    browserPanel_.setRows(makePackageMediaMaintenancePanelRows(modelRows));
    browserPanel_.setSecondaryPanelAction(modelRows.cleanupAction.visible
                                              ? juce::String(modelRows.cleanupAction.text)
                                              : juce::String(),
                                          cleanupEnabled,
                                          cleanupTooltip,
                                          [this]()
                                          {
                                              startPackageMediaCleanup();
                                          });
    browserPanel_.setPanelAction(modelRows.restoreAction.visible
                                     ? juce::String(modelRows.restoreAction.text)
                                     : juce::String(),
                                 restoreEnabled,
                                 restoreTooltip,
                                 [this]()
                                 {
                                     startPackageMediaRestore();
                                 });
}

void MainComponent::selectPackageMediaCleanupBatch(std::string cleanupId)
{
    if (!hasPackageMediaMaintenanceSnapshot_ || packageMediaMaintenanceViewModel_.batches.empty())
    {
        setStatus("No cleanup batches to select");
        return;
    }

    const auto requestedCleanupId = cleanupId;
    packageMediaMaintenanceViewModel_ =
        projectname::selectPackageMediaMaintenanceBatch(std::move(packageMediaMaintenanceViewModel_),
                                                        std::move(cleanupId));
    selectedPackageMediaCleanupId_ = packageMediaMaintenanceViewModel_.selectedCleanupId;
    refreshBrowserPanel();

    if (selectedPackageMediaCleanupId_.empty())
    {
        setStatus("No cleanup batch selected");
        return;
    }

    const auto prefix = selectedPackageMediaCleanupId_ == requestedCleanupId
        ? juce::String("Selected cleanup batch: ")
        : juce::String("Selected newest cleanup batch: ");
    setStatus(prefix + juce::String(selectedPackageMediaCleanupId_));
}

void MainComponent::selectAdjacentPackageMediaCleanupBatch(
    projectname::PackageMediaMaintenanceBrowserSelectionDirection direction)
{
    if (!hasPackageMediaMaintenanceSnapshot_ || packageMediaMaintenanceViewModel_.batches.empty())
    {
        setStatus("No cleanup batches to select");
        return;
    }

    auto cleanupId = projectname::selectAdjacentPackageMediaCleanupId(packageMediaMaintenanceViewModel_,
                                                                      direction);
    if (cleanupId.empty())
    {
        setStatus("No cleanup batches to select");
        return;
    }

    selectPackageMediaCleanupBatch(std::move(cleanupId));
}

void MainComponent::startPackageMediaCleanup()
{
    if (packageMediaCleanupJob_ != nullptr)
    {
        setStatus("Package media cleanup is already running");
        return;
    }

    if (!hasPackageMediaMaintenanceSnapshot_)
    {
        setStatus("Package media cleanup unavailable: waiting for scan");
        refreshBrowserPanel();
        return;
    }

    if (!packageMediaMaintenanceViewModel_.cleanupReviewAvailable)
    {
        const auto modelRows = projectname::buildPackageMediaMaintenanceBrowserRows(
            packageMediaMaintenanceViewModel_,
            { true, false, 2, hasActivePackageFileWork() });
        auto reason = modelRows.cleanupAction.disabledReason.empty()
            ? juce::String("cleanup is unavailable")
            : juce::String(modelRows.cleanupAction.disabledReason);
        setStatus("Package media cleanup unavailable: " + reason);
        refreshBrowserPanel();
        return;
    }

    if (hasActivePackageFileWork())
    {
        setStatus("Package media cleanup unavailable: package files are busy");
        refreshBrowserPanel();
        return;
    }

    const auto timestamp = makePackageMediaCleanupTimestamp();
    projectname::BackgroundPackageMediaCleanupRequest request;
    request.operation = projectname::BackgroundPackageMediaCleanupOperation::quarantine;
    request.packageDirectory = getCurrentProjectPackagePath();
    request.cleanupId = timestamp.cleanupId;
    request.createdAtUtc = timestamp.createdAtUtc;
    request.packageDisplayPath = getCurrentProjectPackagePath().filename().string();
    request.manifestMarker = "rabbington-studio-ui";
    request.packageWorkInProgress = false;

    selectedPackageMediaCleanupId_ = request.cleanupId;
    packageMediaCleanupJob_ =
        std::make_unique<projectname::BackgroundPackageMediaCleanupJob>(std::move(request));
    packageMediaCleanupJob_->start();
    refreshAppCommandEnabledState();
    refreshBrowserPanel();
    setStatus("Cleaning package media candidates");
}

void MainComponent::startPackageMediaRestore()
{
    if (packageMediaCleanupJob_ != nullptr)
    {
        setStatus("Package media restore is already running");
        return;
    }

    if (!hasPackageMediaMaintenanceSnapshot_)
    {
        setStatus("Package media restore unavailable: waiting for scan");
        refreshBrowserPanel();
        return;
    }

    if (!packageMediaMaintenanceViewModel_.hasSelectedBatch)
    {
        setStatus("Package media restore unavailable: select a cleanup batch");
        refreshBrowserPanel();
        return;
    }

    if (!packageMediaMaintenanceViewModel_.restoreActionEnabled)
    {
        auto reason = packageMediaMaintenanceViewModel_.restoreUnavailableReason.empty()
            ? juce::String("selected cleanup batch cannot be restored")
            : juce::String(packageMediaMaintenanceViewModel_.restoreUnavailableReason);
        setStatus("Package media restore unavailable: " + reason);
        refreshBrowserPanel();
        return;
    }

    if (hasActivePackageFileWork())
    {
        setStatus("Package media restore unavailable: package files are busy");
        refreshBrowserPanel();
        return;
    }

    const auto selectedIndex =
        static_cast<std::size_t>(packageMediaMaintenanceViewModel_.selectedBatchIndex);
    if (selectedIndex >= packageMediaMaintenanceViewModel_.batches.size())
    {
        setStatus("Package media restore unavailable: selected batch is stale");
        refreshBrowserPanel();
        return;
    }

    const auto selectedBatch = packageMediaMaintenanceViewModel_.batches[selectedIndex];
    if (selectedBatch.manifestPath.empty())
    {
        setStatus("Package media restore unavailable: restore manifest path is missing");
        refreshBrowserPanel();
        return;
    }

    projectname::BackgroundPackageMediaCleanupRequest request;
    request.operation = projectname::BackgroundPackageMediaCleanupOperation::restore;
    request.packageDirectory = getCurrentProjectPackagePath();
    request.restoreManifestPath = selectedBatch.manifestPath;

    packageMediaCleanupJob_ =
        std::make_unique<projectname::BackgroundPackageMediaCleanupJob>(std::move(request));
    packageMediaCleanupJob_->start();
    refreshAppCommandEnabledState();
    refreshBrowserPanel();
    setStatus("Restoring cleanup batch: " + juce::String(selectedBatch.cleanupId));
}

void MainComponent::refreshWorkspaceTimelineLane()
{
    projectname::TimelineClipLaneOptions options;
    const auto& viewport = session_.getTimelineViewport();
    options.viewStartBeats = viewport.viewStartBeats;
    options.beatsPerPixel = viewport.beatsPerPixel;
    options.viewportWidthPixels = workspacePanel_.getTimelineClipViewportWidthPixels();

    workspacePanel_.setSubtitle(juce::String(projectname::formatTimelineViewportIndicator(viewport)));
    workspacePanel_.setTimelineClipLane(projectname::buildImportedAudioTimelineClipLane(session_.getProject(),
                                                                                        getCurrentProjectPackagePath(),
                                                                                        options));
}

void MainComponent::refreshInspectorPanel()
{
    const auto summary = audioService_.getDeviceSummary();
    const auto outputSampleRate = currentOutputSampleRate(summary);
    lastInspectorOutputSampleRateHz_ = outputSampleRate;

    auto inspector = projectname::buildFirstImportedAudioClipInspector(session_.getProject(),
                                                                       getCurrentProjectPackagePath(),
                                                                       outputSampleRate);
    inspectorPanel_.setSubtitle(
        inspector.status == projectname::ImportedClipInspectorStatus::noImportedAudio
            ? "Project overview"
            : (inspector.usingSelectedClip ? "Selected imported audio clip" : "First imported audio clip"));
    inspectorPanel_.setLines(makeImportedClipInspectorLines(inspector));
    refreshInspectorStartBeatControl(inspector);
}

void MainComponent::refreshInspectorStartBeatControl(const projectname::ImportedClipInspectorState& inspector)
{
    inspectorEditDraft_ = projectname::ImportedClipInspectorEditDraft::fromInspectorState(inspector);
    const auto editable = inspectorEditDraft_.canEdit();

    if (!editable)
    {
        inspectorStartBeatEditor_.setText({}, juce::dontSendNotification);
        refreshInspectorRelinkButtonState();
        return;
    }

    inspectorStartBeatEditor_.setText(juce::String(inspectorEditDraft_.getStartBeatText()),
                                      juce::dontSendNotification);
    refreshInspectorRelinkButtonState();
}

void MainComponent::refreshInspectorRelinkButtonState()
{
    const auto wasRelinkVisible = inspectorRelinkButton_.isVisible();
    const auto wasCancelVisible = inspectorCancelRelinkButton_.isVisible();
    const auto wasStartBeatVisible = inspectorStartBeatEditor_.isVisible();

    const auto editable = inspectorEditDraft_.canEdit();
    const auto relinkRunning = mediaRelinkPreparationJob_ != nullptr;
    const auto showStartBeatEdit = editable && !relinkRunning;
    const auto relinkEnabled = editable
        && audioImportChooser_ == nullptr
        && mediaRelinkChooser_ == nullptr
        && audioImportJob_ == nullptr
        && mediaRelinkPreparationJob_ == nullptr
        && timelinePlaybackPreparationJob_ == nullptr
        && packageMediaCleanupJob_ == nullptr;

    inspectorStartBeatLabel_.setVisible(showStartBeatEdit);
    inspectorStartBeatEditor_.setVisible(showStartBeatEdit);
    inspectorStartBeatEditor_.setEnabled(showStartBeatEdit);
    inspectorRelinkButton_.setVisible(editable);
    inspectorRelinkButton_.setEnabled(relinkEnabled);
    inspectorCancelRelinkButton_.setVisible(relinkRunning);
    inspectorCancelRelinkButton_.setEnabled(relinkRunning && canCancelMediaRelinkPreparation_);

    if (wasRelinkVisible != inspectorRelinkButton_.isVisible()
        || wasCancelVisible != inspectorCancelRelinkButton_.isVisible()
        || wasStartBeatVisible != inspectorStartBeatEditor_.isVisible())
    {
        resized();
        inspectorPanel_.repaint();
    }
}

juce::Rectangle<int> MainComponent::getInspectorStartBeatRowBounds() const
{
    auto content = inspectorPanel_.getBounds().reduced(16);
    content.removeFromTop(24);
    content.removeFromTop(22);
    content.removeFromTop(10);

    for (auto skippedRows = 0; skippedRows < 2; ++skippedRows)
    {
        if (content.getHeight() < 76)
            return {};

        content.removeFromTop(28);
        content.removeFromTop(6);
    }

    return content.getHeight() >= 28 ? content.removeFromTop(28) : juce::Rectangle<int> {};
}

void MainComponent::commitInspectorStartBeatEdit()
{
    if (!inspectorStartBeatEditor_.isVisible())
        return;

    inspectorEditDraft_.setStartBeatText(inspectorStartBeatEditor_.getText().toStdString());

    std::string error;
    const auto commit = inspectorEditDraft_.makeStartBeatCommit(error);
    if (!commit.has_value())
    {
        setStatus("Start beat edit failed: " + juce::String(error));
        inspectorStartBeatEditor_.grabKeyboardFocus();
        inspectorStartBeatEditor_.selectAll();
        return;
    }

    if (!session_.setImportedAudioClipStartBeats(commit->clipId, commit->startBeats, error))
    {
        setStatus("Start beat edit failed: " + juce::String(error));
        refreshInspectorPanel();
        return;
    }

    refreshWorkspaceTimelineLane();
    refreshInspectorPanel();
    refreshAppCommandEnabledState();
    setStatus("Set selected clip start beat: " + juce::String(commit->startBeats, 2));
}

void MainComponent::cancelInspectorStartBeatEdit()
{
    if (!inspectorStartBeatEditor_.isVisible())
        return;

    inspectorEditDraft_.cancelStartBeatEdit();
    inspectorStartBeatEditor_.setText(juce::String(inspectorEditDraft_.getStartBeatText()),
                                      juce::dontSendNotification);
    setStatus("Cancelled selected clip start beat edit");
}

void MainComponent::selectTimelineClip(std::string clipId)
{
    std::string error;
    if (!session_.selectImportedAudioClip(clipId, error))
    {
        setStatus("Clip selection failed: " + juce::String(error));
        return;
    }

    refreshWorkspaceTimelineLane();
    refreshInspectorPanel();
    setStatus("Selected clip: " + juce::String(clipId));
}

void MainComponent::selectAdjacentTimelineClip(projectname::ImportedAudioClipSelectionDirection direction)
{
    std::string error;
    if (!session_.selectAdjacentImportedAudioClip(direction, error))
    {
        setStatus("Clip keyboard selection failed: " + juce::String(error));
        return;
    }

    refreshWorkspaceTimelineLane();
    refreshInspectorPanel();

    const auto label = direction == projectname::ImportedAudioClipSelectionDirection::next
        ? juce::String("Selected next clip: ")
        : juce::String("Selected previous clip: ");
    setStatus(label + juce::String(session_.getSelectedClipId()));
}

void MainComponent::panTimelineViewport(double deltaBeats)
{
    session_.nudgeTimelineViewStartBeats(deltaBeats);
    refreshWorkspaceTimelineLane();

    const auto& viewport = session_.getTimelineViewport();
    setStatus("Timeline start: beat " + juce::String(viewport.viewStartBeats, 2));
}

void MainComponent::zoomTimelineViewport(double multiplier)
{
    session_.scaleTimelineBeatsPerPixel(multiplier);
    refreshWorkspaceTimelineLane();

    const auto& viewport = session_.getTimelineViewport();
    setStatus("Timeline zoom: " + juce::String(viewport.beatsPerPixel, 4) + " beats/pixel");
}

void MainComponent::resetTimelineViewportStart()
{
    session_.setTimelineViewStartBeats(0.0);
    refreshWorkspaceTimelineLane();
    setStatus("Timeline start: beat 0.00");
}

void MainComponent::fitTimelineViewportToImportedClips()
{
    const auto fit = projectname::fitTimelineViewportToImportedAudioClips(
        session_.getProject(),
        workspacePanel_.getTimelineClipViewportWidthPixels());

    if (!fit.has_value())
    {
        setStatus("No imported audio clips to fit in the timeline.");
        return;
    }

    session_.setTimelineViewport(fit->viewStartBeats, fit->beatsPerPixel);
    refreshWorkspaceTimelineLane();
    setStatus(juce::String("Fit imported clips - ")
              + juce::String(projectname::formatTimelineViewportIndicator(*fit)));
}

void MainComponent::centerTimelineViewportOnSelectedClip()
{
    const auto centered = projectname::centerTimelineViewportOnSelectedImportedAudioClip(
        session_.getProject(),
        session_.getTimelineViewport(),
        workspacePanel_.getTimelineClipViewportWidthPixels());

    if (!centered.has_value())
    {
        setStatus("No selected imported audio clip to center in the timeline.");
        return;
    }

    session_.setTimelineViewport(centered->viewStartBeats, centered->beatsPerPixel);
    refreshWorkspaceTimelineLane();
    setStatus(juce::String("Centered selected clip - ")
              + juce::String(projectname::formatTimelineViewportIndicator(*centered)));
}

void MainComponent::refreshMixerControls()
{
    const auto* track = findFirstAudioTrack(session_.getProject());
    refreshingMixerControls_ = true;

    const auto hasTrack = track != nullptr;
    mixerTrackLabel_.setText(hasTrack ? juce::String(track->name) : juce::String("No audio track"),
                             juce::dontSendNotification);
    trackVolumeSlider_.setValue(hasTrack ? static_cast<double>(track->volume) : 1.0,
                                juce::dontSendNotification);
    trackPanSlider_.setValue(hasTrack ? static_cast<double>(track->pan) : 0.0,
                             juce::dontSendNotification);
    muteToggle_.setToggleState(hasTrack && track->muted, juce::dontSendNotification);
    soloToggle_.setToggleState(hasTrack && track->solo, juce::dontSendNotification);

    trackVolumeSlider_.setEnabled(hasTrack);
    trackPanSlider_.setEnabled(hasTrack);
    muteToggle_.setEnabled(hasTrack);
    soloToggle_.setEnabled(hasTrack);

    refreshingMixerControls_ = false;
}

void MainComponent::refreshDevicePanel(bool force)
{
    const auto summary = audioService_.getDeviceSummary();

    projectname::AudioSetupStatusRequest request;
    request.firstRunPromptDismissed = audioSetupPromptDismissed_;
    request.outputDeviceOpen = summary.isOpen;
    request.outputChannelCount = summary.outputChannels;
    request.sampleRateHz = summary.sampleRate;
    request.bufferSizeSamples = summary.bufferSizeSamples;
    request.outputDeviceName = summary.name.toStdString();
    request.initializationError = audioSetupInitializationError_.toStdString();

    const auto model = projectname::buildAudioSetupStatusViewModel(request);

    std::ostringstream signature;
    signature << static_cast<int>(model.kind) << '|'
              << model.subtitle << '|'
              << model.setupActionVisible << '|'
              << model.setupActionEnabled << '|'
              << model.dismissActionVisible << '|'
              << audioSetupPromptDismissed_;
    for (const auto& line : model.lines)
        signature << '|' << line;

    const auto nextSignature = signature.str();
    if (!force && nextSignature == lastAudioSetupStatusSignature_)
        return;

    lastAudioSetupStatusSignature_ = nextSignature;
    devicePanel_.setSubtitle(juce::String(model.subtitle));
    devicePanel_.setLines(makeStringArray(model.lines));
    devicePanel_.setPanelAction(model.setupActionVisible ? juce::String(model.setupActionLabel) : juce::String {},
                                model.setupActionEnabled,
                                juce::String(model.setupActionTooltip),
                                [this]()
                                {
                                    dispatchAppCommand(projectname::AppCommandIds::audioSettingsShow);
                                });
    devicePanel_.setSecondaryPanelAction(model.dismissActionVisible ? juce::String(model.dismissActionLabel) : juce::String {},
                                         true,
                                         juce::String(model.dismissActionTooltip),
                                         [this]()
                                         {
                                             dismissAudioSetupPrompt();
                                         });
}

void MainComponent::applyMixerControlChange()
{
    if (refreshingMixerControls_)
        return;

    const auto* track = findFirstAudioTrack(session_.getProject());
    if (track == nullptr)
    {
        refreshMixerControls();
        setStatus("No audio track is available for mixing");
        return;
    }

    const auto trackId = track->id;
    const auto trackName = track->name;

    std::string error;
    if (!session_.setTrackMixState(trackId,
                                   static_cast<float>(trackVolumeSlider_.getValue()),
                                   static_cast<float>(trackPanSlider_.getValue()),
                                   muteToggle_.getToggleState(),
                                   soloToggle_.getToggleState(),
                                   error))
    {
        refreshMixerControls();
        setStatus("Mix update failed: " + juce::String(error));
        return;
    }

    refreshMixerControls();
    setStatus("Updated mix: " + juce::String(trackName));
}

void MainComponent::showAudioSettings()
{
    audioSetupPromptDismissed_ = true;
    refreshDevicePanel(true);

    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(audioService_.getDeviceManager(),
                                                                         0,
                                                                         2,
                                                                         0,
                                                                         2,
                                                                         true,
                                                                         true,
                                                                         true,
                                                                         false);
    selector->setSize(620, 460);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Audio and MIDI Setup";
    options.dialogBackgroundColour = panelBackground;
    options.content.setOwned(selector.release());
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
    setStatus("Audio/MIDI setup opened - press Play to test output");
}

void MainComponent::dismissAudioSetupPrompt()
{
    audioSetupPromptDismissed_ = true;
    refreshDevicePanel(true);
    setStatus("Audio setup reminder dismissed");
}

void MainComponent::setStatus(juce::String status)
{
    statusText_ = std::move(status);
    statusLabel_.setText(statusText_, juce::dontSendNotification);
}

std::filesystem::path MainComponent::getDefaultProjectPackagePath() const
{
    const auto documentsDirectory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    return std::filesystem::path(documentsDirectory.getChildFile(projectname::demoProjectPackageName)
                                     .getFullPathName()
                                     .toStdString());
}

const std::filesystem::path& MainComponent::getCurrentProjectPackagePath() const noexcept
{
    return currentProjectPackagePath_;
}

bool MainComponent::hasProjectChooserOpen() const noexcept
{
    return projectNewChooser_ != nullptr
        || projectSaveAsChooser_ != nullptr
        || projectOpenChooser_ != nullptr;
}
