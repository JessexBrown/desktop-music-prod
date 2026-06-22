// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AudioSetupStatus.h"

#include <cmath>
#include <sstream>

namespace projectname
{
namespace
{
[[nodiscard]] bool hasOpenOutput(const AudioSetupStatusRequest& request) noexcept
{
    return request.outputDeviceOpen && request.outputChannelCount > 0;
}

[[nodiscard]] std::string formatDeviceName(const std::string& name)
{
    return name.empty() ? std::string("Default output") : name;
}

[[nodiscard]] std::string formatSampleRate(double sampleRateHz)
{
    if (!std::isfinite(sampleRateHz) || sampleRateHz <= 0.0)
        return "Sample rate unavailable";

    std::ostringstream text;
    text << static_cast<int>(std::llround(sampleRateHz)) << " Hz";
    return text.str();
}

[[nodiscard]] std::string formatBufferSize(int bufferSizeSamples)
{
    if (bufferSizeSamples <= 0)
        return "Buffer unavailable";

    return std::to_string(bufferSizeSamples) + " samples";
}

[[nodiscard]] std::string formatOutputChannels(int outputChannelCount)
{
    if (outputChannelCount <= 0)
        return "No output channels";

    return std::to_string(outputChannelCount) + (outputChannelCount == 1 ? " output channel" : " output channels");
}

void addOutputDetails(AudioSetupStatusViewModel& model, const AudioSetupStatusRequest& request)
{
    model.lines.push_back("Output: " + formatDeviceName(request.outputDeviceName));
    model.lines.push_back("Format: " + formatSampleRate(request.sampleRateHz)
                          + " / " + formatBufferSize(request.bufferSizeSamples));
    model.lines.push_back(formatOutputChannels(request.outputChannelCount));
}

void addSettingsWarning(AudioSetupStatusViewModel& model, const AudioSetupStatusRequest& request)
{
    if (!request.settingsLoadError.empty())
    {
        model.lines.push_back("Settings ignored: " + request.settingsLoadError);
        model.needsAttention = true;
    }
}
} // namespace

AudioSetupStatusViewModel buildAudioSetupStatusViewModel(const AudioSetupStatusRequest& request)
{
    AudioSetupStatusViewModel model;

    if (!hasOpenOutput(request) && !request.initializationError.empty())
    {
        model.kind = AudioSetupStatusKind::initializationFailed;
        model.subtitle = "Audio setup needs attention";
        model.lines.push_back("Default output did not open");
        model.lines.push_back("Error: " + request.initializationError);
        model.lines.push_back("Choose another output device");
        model.needsAttention = true;
        addSettingsWarning(model, request);
        return model;
    }

    if (!hasOpenOutput(request))
    {
        model.kind = AudioSetupStatusKind::unavailable;
        model.subtitle = "Audio output unavailable";
        model.lines.push_back("No output device is open");
        model.lines.push_back("Choose an output before recording");
        model.lines.push_back(formatOutputChannels(request.outputChannelCount));
        model.needsAttention = true;
        addSettingsWarning(model, request);
        return model;
    }

    if (!request.firstRunPromptDismissed)
    {
        model.kind = AudioSetupStatusKind::firstRun;
        model.subtitle = "Confirm Audio/MIDI setup";
        addOutputDetails(model, request);
        model.lines.push_back("Press Play to check output");
        model.dismissActionVisible = true;
        model.needsAttention = false;
        addSettingsWarning(model, request);
        return model;
    }

    model.kind = AudioSetupStatusKind::ready;
    model.subtitle = "Audio output ready";
    addOutputDetails(model, request);
    model.lines.push_back("Generated tone path ready");
    model.needsAttention = false;
    addSettingsWarning(model, request);
    return model;
}
} // namespace projectname
