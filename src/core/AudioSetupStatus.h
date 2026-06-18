// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace projectname
{
enum class AudioSetupStatusKind
{
    firstRun,
    ready,
    unavailable,
    initializationFailed,
};

struct AudioSetupStatusRequest
{
    bool firstRunPromptDismissed = false;
    bool outputDeviceOpen = false;
    int outputChannelCount = 0;
    double sampleRateHz = 0.0;
    int bufferSizeSamples = 0;
    std::string outputDeviceName;
    std::string initializationError;
};

struct AudioSetupStatusViewModel
{
    AudioSetupStatusKind kind = AudioSetupStatusKind::unavailable;
    std::string subtitle;
    std::vector<std::string> lines;
    bool setupActionVisible = true;
    bool setupActionEnabled = true;
    std::string setupActionLabel = "Setup";
    std::string setupActionTooltip = "Open Audio/MIDI setup";
    bool dismissActionVisible = false;
    std::string dismissActionLabel = "Dismiss";
    std::string dismissActionTooltip = "Hide the first-run audio setup reminder";
    bool needsAttention = true;
};

[[nodiscard]] AudioSetupStatusViewModel buildAudioSetupStatusViewModel(
    const AudioSetupStatusRequest& request);
} // namespace projectname
