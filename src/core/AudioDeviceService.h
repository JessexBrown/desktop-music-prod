// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "AudioEngineStub.h"
#include "TimelinePlaybackPlan.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

namespace projectname
{
struct AudioDeviceSummary
{
    juce::String type;
    juce::String name;
    double sampleRate = 0.0;
    int bufferSizeSamples = 0;
    int inputChannels = 0;
    int outputChannels = 0;
    bool isOpen = false;
};

class AudioDeviceService final : private juce::AudioIODeviceCallback
{
public:
    AudioDeviceService() = default;
    ~AudioDeviceService() override;

    juce::String initialiseDefaultDevice();
    void shutdown();

    void setTestToneEnabled(bool shouldEnable);
    [[nodiscard]] bool isTestToneEnabled() const noexcept;
    void playPreparedMonoClip(std::vector<float> monoSamples);
    void playPreparedMonoClip(std::shared_ptr<const std::vector<float>> monoSamples);
    void playPreparedMonoClipFromTimeline(std::vector<float> monoSamples,
                                          const TimelinePlaybackActivation& activation,
                                          std::int64_t transportTimelineSample);
    void playPreparedMonoClipFromTimeline(std::shared_ptr<const std::vector<float>> monoSamples,
                                          const TimelinePlaybackActivation& activation,
                                          std::int64_t transportTimelineSample);
    void playPreparedTrackVoiceSchedule(std::vector<PreparedTrackVoiceBuffer> buffers,
                                        TrackVoiceSchedule schedule);
    void stopPlayback();

    [[nodiscard]] AudioDeviceSummary getDeviceSummary() const;

    [[nodiscard]] juce::AudioDeviceManager& getDeviceManager() noexcept;
    [[nodiscard]] const juce::AudioDeviceManager& getDeviceManager() const noexcept;

private:
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

    juce::AudioDeviceManager deviceManager_;
    AudioEngineStub audioEngine_;
    std::atomic_bool testToneEnabled_ { false };
    bool audioCallbackRegistered_ = false;
};
} // namespace projectname
