#include "MainComponent.h"

#include "core/AppCommandRegistry.h"
#include "core/WorkspaceCommandRouter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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
} // namespace

WorkspacePanel::WorkspacePanel(juce::String title, juce::String subtitle, juce::StringArray lines)
    : title_(std::move(title)),
      subtitle_(std::move(subtitle)),
      lines_(std::move(lines))
{
    setWantsKeyboardFocus(true);

    configureButton(panLeftViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(resetViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(fitClipsViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(centerSelectedViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(zoomOutViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(zoomInViewportButton_, juce::Colour(0xffc6ccd5));
    configureButton(panRightViewportButton_, juce::Colour(0xffc6ccd5));

    panLeftViewportButton_.setTooltip("Pan timeline view left");
    resetViewportButton_.setTooltip("Reset timeline view to the project start");
    fitClipsViewportButton_.setTooltip("Fit imported audio clips in the timeline view");
    centerSelectedViewportButton_.setTooltip("Center the selected imported clip in the timeline view");
    zoomOutViewportButton_.setTooltip("Zoom timeline view out");
    zoomInViewportButton_.setTooltip("Zoom timeline view in");
    panRightViewportButton_.setTooltip("Pan timeline view right");

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

    addChildComponent(panLeftViewportButton_);
    addChildComponent(resetViewportButton_);
    addChildComponent(fitClipsViewportButton_);
    addChildComponent(centerSelectedViewportButton_);
    addChildComponent(zoomOutViewportButton_);
    addChildComponent(zoomInViewportButton_);
    addChildComponent(panRightViewportButton_);
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
    graphics.drawFittedText(subtitle_, subtitleArea, juce::Justification::centredLeft, 1);

    content.removeFromTop(10);
    graphics.setFont(juce::FontOptions(13.0f));

    for (const auto& line : lines_)
    {
        if (content.getHeight() < 76)
            break;

        graphics.setColour(juce::Colour(0xff252830));
        const auto row = content.removeFromTop(28);
        graphics.fillRoundedRectangle(row.toFloat(), 4.0f);
        graphics.setColour(textSecondary);
        graphics.drawFittedText(line, row.reduced(10, 0), juce::Justification::centredLeft, 1);
        content.removeFromTop(6);
    }

    if (content.getHeight() >= 54)
        paintTimelineClipLane(graphics, content);
}

juce::Rectangle<int> WorkspacePanel::getTimelineContentBounds() const
{
    auto content = getLocalBounds().reduced(16);
    content.removeFromTop(24);
    content.removeFromTop(22);
    content.removeFromTop(10);

    for (const auto& line : lines_)
    {
        juce::ignoreUnused(line);
        if (content.getHeight() < 76)
            break;

        content.removeFromTop(28);
        content.removeFromTop(6);
    }

    return content.getHeight() >= 54 ? content : juce::Rectangle<int> {};
}

bool WorkspacePanel::shouldPaintKeyboardFocus() const
{
    return (previousTimelineClipRequested_ || nextTimelineClipRequested_
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

    if (!visible)
        return;

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

void WorkspacePanel::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();

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
    lines_ = std::move(lines);
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
    configureControls();
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

    const auto error = audioService_.initialiseDefaultDevice();
    refreshInspectorPanel();
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
    if (timelinePlaybackPreparationJob_ != nullptr)
        timelinePlaybackPreparationJob_->requestCancel();

    audioImportJob_.reset();
    timelinePlaybackPreparationJob_.reset();
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
    openButton_.setBounds(topBar.removeFromRight(76).reduced(0, 10));
    topBar.removeFromRight(8);
    saveButton_.setBounds(topBar.removeFromRight(76).reduced(0, 10));
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
    else if (button == &saveButton_)
    {
        dispatchAppCommand(projectname::AppCommandIds::projectSave);
    }
    else if (button == &openButton_)
    {
        dispatchAppCommand(projectname::AppCommandIds::projectOpen);
    }
    else if (button == &importButton_)
    {
        dispatchAppCommand(projectname::AppCommandIds::audioImport);
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
    pollTimelinePlaybackPreparationJob();
    session_.advanceSeconds(1.0 / 30.0);
    updateTransportLabels();

    const auto outputSampleRate = currentOutputSampleRate(audioService_.getDeviceSummary());
    if (std::abs(outputSampleRate - lastInspectorOutputSampleRateHz_) > 1.0)
        refreshInspectorPanel();
}

void MainComponent::configureControls()
{
    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(saveButton_);
    addAndMakeVisible(openButton_);
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

    configureButton(playButton_, accent);
    configureButton(stopButton_, accentWarm);
    configureButton(saveButton_, juce::Colour(0xffc6ccd5));
    configureButton(openButton_, juce::Colour(0xffc6ccd5));
    configureButton(importButton_, juce::Colour(0xffc6ccd5));
    configureButton(cancelImportButton_, accentWarm);
    configureButton(cancelTimelinePreparationButton_, accentWarm);
    configureButton(audioButton_, juce::Colour(0xffc6ccd5));

    playButton_.addListener(this);
    stopButton_.addListener(this);
    saveButton_.addListener(this);
    openButton_.addListener(this);
    importButton_.addListener(this);
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
    statusLabel_.setColour(juce::Label::textColourId, textSecondary);

    tempoLabel_.setText("Tempo", juce::dontSendNotification);
    trackVolumeLabel_.setText("Volume", juce::dontSendNotification);
    trackPanLabel_.setText("Pan", juce::dontSendNotification);

    refreshAppCommandEnabledState();
}

projectname::AppCommandRegistry MainComponent::buildAppCommandRegistry() const
{
    projectname::AppCommandAvailability availability;
    availability.canImportAudio = audioImportChooser_ == nullptr && audioImportJob_ == nullptr;
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
    setButtonEnabledFromCommand(saveButton_, registry, projectname::AppCommandIds::projectSave);
    setButtonEnabledFromCommand(openButton_, registry, projectname::AppCommandIds::projectOpen);
    setButtonEnabledFromCommand(importButton_, registry, projectname::AppCommandIds::audioImport);
    setButtonEnabledFromCommand(cancelImportButton_, registry, projectname::AppCommandIds::audioImportCancel);
    setButtonEnabledFromCommand(cancelTimelinePreparationButton_,
                                registry,
                                projectname::AppCommandIds::timelinePreparationCancel);
    setButtonEnabledFromCommand(audioButton_, registry, projectname::AppCommandIds::audioSettingsShow);
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
    registerHandler(projectname::AppCommandIds::projectSave,
                    [this]()
                    {
                        saveProject();
                        return projectname::AppCommandResult::handled();
                    });
    registerHandler(projectname::AppCommandIds::projectOpen,
                    [this]()
                    {
                        openProject();
                        return projectname::AppCommandResult::handled();
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
    request.packageDirectory = getDefaultProjectPackagePath();
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

void MainComponent::saveProject()
{
    session_.getProject().setName("ProjectName Demo");

    std::string error;
    const auto packagePath = getDefaultProjectPackagePath();

    if (session_.saveProjectPackage(packagePath, error))
        setStatus("Saved project package: " + juce::String(packagePath.string()));
    else
        setStatus("Save failed: " + juce::String(error));

    refreshInspectorPanel();
    refreshMixerControls();
    updateTransportLabels();
}

void MainComponent::openProject()
{
    std::string error;
    const auto packagePath = getDefaultProjectPackagePath();

    if (session_.loadProjectPackage(packagePath, error))
    {
        tempoSlider_.setValue(session_.getTransport().getTempoBpm(), juce::dontSendNotification);
        audioService_.setTestToneEnabled(session_.shouldPlayGeneratedTone());
        refreshWorkspaceTimelineLane();
        refreshInspectorPanel();
        refreshMixerControls();
        setStatus("Opened project package: " + juce::String(packagePath.string()));
    }
    else
    {
        setStatus("Open failed: " + juce::String(error));
    }

    updateTransportLabels();
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
    projectSnapshot.setName("ProjectName Demo");

    projectname::BackgroundAudioImportRequest request;
    request.project = std::move(projectSnapshot);
    request.packageDirectory = getDefaultProjectPackagePath();
    request.sourceWavPath = std::filesystem::path(selectedFile.getFullPathName().toStdString());

    audioImportJob_ = std::make_unique<projectname::BackgroundAudioImportJob>(std::move(request));
    audioImportJob_->start();

    canCancelAudioImport_ = true;
    setStatus("Importing " + selectedFile.getFileName());
    audioImportChooser_.reset();
    refreshAppCommandEnabledState();
    updateTransportLabels();
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
        return;
    }

    if (!result.import.has_value())
    {
        setStatus("Import failed: " + juce::String(result.error));
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

void MainComponent::refreshWorkspaceTimelineLane()
{
    projectname::TimelineClipLaneOptions options;
    const auto& viewport = session_.getTimelineViewport();
    options.viewStartBeats = viewport.viewStartBeats;
    options.beatsPerPixel = viewport.beatsPerPixel;
    options.viewportWidthPixels = workspacePanel_.getTimelineClipViewportWidthPixels();

    workspacePanel_.setSubtitle(juce::String(projectname::formatTimelineViewportIndicator(viewport)));
    workspacePanel_.setTimelineClipLane(projectname::buildImportedAudioTimelineClipLane(session_.getProject(),
                                                                                        getDefaultProjectPackagePath(),
                                                                                        options));
}

void MainComponent::refreshInspectorPanel()
{
    const auto summary = audioService_.getDeviceSummary();
    const auto outputSampleRate = currentOutputSampleRate(summary);
    lastInspectorOutputSampleRateHz_ = outputSampleRate;

    auto inspector = projectname::buildFirstImportedAudioClipInspector(session_.getProject(),
                                                                       getDefaultProjectPackagePath(),
                                                                       outputSampleRate);
    inspectorPanel_.setSubtitle(
        inspector.status == projectname::ImportedClipInspectorStatus::noImportedAudio
            ? "Project overview"
            : (inspector.usingSelectedClip ? "Selected imported audio clip" : "First imported audio clip"));
    inspectorPanel_.setLines(makeImportedClipInspectorLines(inspector));
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
}

void MainComponent::setStatus(juce::String status)
{
    statusText_ = std::move(status);
    statusLabel_.setText(statusText_, juce::dontSendNotification);
}

std::filesystem::path MainComponent::getDefaultProjectPackagePath() const
{
    const auto documentsDirectory = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    return std::filesystem::path(documentsDirectory.getChildFile("ProjectName Demo.project")
                                     .getFullPathName()
                                     .toStdString());
}
