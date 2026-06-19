// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AudioDeviceService.h"

#include <utility>

namespace projectname
{
AudioDeviceService::~AudioDeviceService()
{
    shutdown();
}

juce::String AudioDeviceService::initialiseDefaultDevice()
{
    auto error = deviceManager_.initialise(0, 2, nullptr, true);

    if (!audioCallbackRegistered_)
    {
        deviceManager_.addAudioCallback(this);
        audioCallbackRegistered_ = true;
    }

    return error;
}

void AudioDeviceService::shutdown()
{
    testToneEnabled_.store(false, std::memory_order_release);

    if (audioCallbackRegistered_)
    {
        deviceManager_.removeAudioCallback(this);
        audioCallbackRegistered_ = false;
    }

    deviceManager_.closeAudioDevice();
}

void AudioDeviceService::setTestToneEnabled(bool shouldEnable)
{
    testToneEnabled_.store(shouldEnable, std::memory_order_release);

    if (!shouldEnable)
        stopPlayback();
}

bool AudioDeviceService::isTestToneEnabled() const noexcept
{
    return testToneEnabled_.load(std::memory_order_acquire);
}

void AudioDeviceService::playPreparedMonoClip(std::vector<float> monoSamples)
{
    const auto shouldRestoreCallback = audioCallbackRegistered_;
    if (shouldRestoreCallback)
    {
        deviceManager_.removeAudioCallback(this);
        audioCallbackRegistered_ = false;
    }

    testToneEnabled_.store(false, std::memory_order_release);
    audioEngine_.stop();
    audioEngine_.setTimelinePositionSamples(0);
    audioEngine_.setPreparedMonoClipSamples(std::move(monoSamples));
    audioEngine_.startScheduledPreparedMonoClip(0);

    if (shouldRestoreCallback)
    {
        deviceManager_.addAudioCallback(this);
        audioCallbackRegistered_ = true;
    }
}

void AudioDeviceService::playPreparedMonoClip(std::shared_ptr<const std::vector<float>> monoSamples)
{
    const auto shouldRestoreCallback = audioCallbackRegistered_;
    if (shouldRestoreCallback)
    {
        deviceManager_.removeAudioCallback(this);
        audioCallbackRegistered_ = false;
    }

    testToneEnabled_.store(false, std::memory_order_release);
    audioEngine_.stop();
    audioEngine_.setTimelinePositionSamples(0);
    audioEngine_.setPreparedMonoClipSamples(std::move(monoSamples));
    audioEngine_.startScheduledPreparedMonoClip(0);

    if (shouldRestoreCallback)
    {
        deviceManager_.addAudioCallback(this);
        audioCallbackRegistered_ = true;
    }
}

void AudioDeviceService::playPreparedMonoClipFromTimeline(std::vector<float> monoSamples,
                                                          const TimelinePlaybackActivation& activation,
                                                          std::int64_t transportTimelineSample)
{
    const auto shouldRestoreCallback = audioCallbackRegistered_;
    if (shouldRestoreCallback)
    {
        deviceManager_.removeAudioCallback(this);
        audioCallbackRegistered_ = false;
    }

    testToneEnabled_.store(false, std::memory_order_release);
    audioEngine_.stop();
    audioEngine_.setTimelinePositionSamples(transportTimelineSample);
    audioEngine_.setPreparedMonoClipSamples(std::move(monoSamples));
    audioEngine_.startScheduledPreparedMonoClip(activation.timelinePlaybackStartSample,
                                               activation.clipLocalStartOffsetSamples,
                                               activation.clipLengthSamples);

    if (shouldRestoreCallback)
    {
        deviceManager_.addAudioCallback(this);
        audioCallbackRegistered_ = true;
    }
}

void AudioDeviceService::playPreparedMonoClipFromTimeline(std::shared_ptr<const std::vector<float>> monoSamples,
                                                          const TimelinePlaybackActivation& activation,
                                                          std::int64_t transportTimelineSample)
{
    const auto shouldRestoreCallback = audioCallbackRegistered_;
    if (shouldRestoreCallback)
    {
        deviceManager_.removeAudioCallback(this);
        audioCallbackRegistered_ = false;
    }

    testToneEnabled_.store(false, std::memory_order_release);
    audioEngine_.stop();
    audioEngine_.setTimelinePositionSamples(transportTimelineSample);
    audioEngine_.setPreparedMonoClipSamples(std::move(monoSamples));
    audioEngine_.startScheduledPreparedMonoClip(activation.timelinePlaybackStartSample,
                                               activation.clipLocalStartOffsetSamples,
                                               activation.clipLengthSamples);

    if (shouldRestoreCallback)
    {
        deviceManager_.addAudioCallback(this);
        audioCallbackRegistered_ = true;
    }
}

void AudioDeviceService::playPreparedTrackVoiceSchedule(std::vector<PreparedTrackVoiceBuffer> buffers,
                                                        TrackVoiceSchedule schedule)
{
    const auto shouldRestoreCallback = audioCallbackRegistered_;
    if (shouldRestoreCallback)
    {
        deviceManager_.removeAudioCallback(this);
        audioCallbackRegistered_ = false;
    }

    testToneEnabled_.store(false, std::memory_order_release);
    audioEngine_.stop();
    audioEngine_.setPreparedTrackVoiceBuffers(std::move(buffers));
    audioEngine_.startPreparedVoiceSchedule(std::move(schedule));

    if (shouldRestoreCallback)
    {
        deviceManager_.addAudioCallback(this);
        audioCallbackRegistered_ = true;
    }
}

void AudioDeviceService::stopPlayback()
{
    const auto shouldRestoreCallback = audioCallbackRegistered_;
    if (shouldRestoreCallback)
    {
        deviceManager_.removeAudioCallback(this);
        audioCallbackRegistered_ = false;
    }

    audioEngine_.stop();

    if (shouldRestoreCallback)
    {
        deviceManager_.addAudioCallback(this);
        audioCallbackRegistered_ = true;
    }
}

AudioDeviceSummary AudioDeviceService::getDeviceSummary() const
{
    AudioDeviceSummary summary;

    if (auto* device = deviceManager_.getCurrentAudioDevice())
    {
        summary.type = device->getTypeName();
        summary.name = device->getName();
        summary.sampleRate = device->getCurrentSampleRate();
        summary.bufferSizeSamples = device->getCurrentBufferSizeSamples();
        summary.inputChannels = device->getActiveInputChannels().countNumberOfSetBits();
        summary.outputChannels = device->getActiveOutputChannels().countNumberOfSetBits();
        summary.isOpen = true;
    }

    return summary;
}

juce::AudioDeviceManager& AudioDeviceService::getDeviceManager() noexcept
{
    return deviceManager_;
}

const juce::AudioDeviceManager& AudioDeviceService::getDeviceManager() const noexcept
{
    return deviceManager_;
}

void AudioDeviceService::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr)
        audioEngine_.prepare(device->getCurrentSampleRate());
}

void AudioDeviceService::audioDeviceStopped()
{
    audioEngine_.stop();
    audioEngine_.reset();
}

void AudioDeviceService::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                          int numInputChannels,
                                                          float* const* outputChannelData,
                                                          int numOutputChannels,
                                                          int numSamples,
                                                          const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(inputChannelData, numInputChannels, context);

    if (outputChannelData == nullptr || numSamples <= 0)
        return;

    if (testToneEnabled_.load(std::memory_order_acquire))
        audioEngine_.startGeneratedTone();

    audioEngine_.render(outputChannelData, numOutputChannels, numSamples);
}
} // namespace projectname
