// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AudioEngineStub.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <memory>
#include <utility>

namespace projectname
{
void AudioEngineStub::prepare(double sampleRate) noexcept
{
    if (std::isfinite(sampleRate) && sampleRate > 0.0)
        sampleRate_ = sampleRate;

    toneRenderer_.prepare(sampleRate);
}

void AudioEngineStub::reset() noexcept
{
    toneRenderer_.reset();
}

void AudioEngineStub::startGeneratedTone() noexcept
{
    playbackMode_ = PlaybackMode::continuousTone;
}

void AudioEngineStub::startGeneratedClip(double lengthSeconds) noexcept
{
    if (!std::isfinite(lengthSeconds) || lengthSeconds <= 0.0)
    {
        stop();
        return;
    }

    const auto clippedLengthSeconds = std::clamp(lengthSeconds, 0.001, 600.0);
    generatedClipLengthSamples_ = static_cast<std::int64_t>(std::llround(clippedLengthSeconds * sampleRate_));
    generatedClipPositionSamples_ = 0;
    scheduledClipLocalStartOffsetSamples_ = 0;
    scheduledClipHasStarted_ = false;
    toneRenderer_.reset();
    playbackMode_ = PlaybackMode::generatedClip;
}

void AudioEngineStub::startScheduledGeneratedClip(std::int64_t timelineStartSample, std::int64_t lengthSamples) noexcept
{
    if (lengthSamples <= 0)
    {
        stop();
        return;
    }

    scheduledClipStartSample_ = std::max<std::int64_t>(0, timelineStartSample);
    scheduledClipLocalStartOffsetSamples_ = 0;
    generatedClipLengthSamples_ = lengthSamples;
    generatedClipPositionSamples_ = 0;
    scheduledClipHasStarted_ = false;
    playbackMode_ = PlaybackMode::scheduledGeneratedClip;
}

void AudioEngineStub::setPreparedMonoClipSamples(std::vector<float> monoSamples)
{
    for (auto& sample : monoSamples)
    {
        if (!std::isfinite(sample))
            sample = 0.0f;
        else
            sample = std::clamp(sample, -1.0f, 1.0f);
    }

    preparedMonoClipSamples_ = std::make_shared<const std::vector<float>>(std::move(monoSamples));
}

void AudioEngineStub::setPreparedMonoClipSamples(std::shared_ptr<const std::vector<float>> monoSamples) noexcept
{
    preparedMonoClipSamples_ = std::move(monoSamples);
}

void AudioEngineStub::setPreparedTrackVoiceBuffers(std::vector<PreparedTrackVoiceBuffer> buffers)
{
    preparedTrackVoiceBuffers_ = std::move(buffers);
}

void AudioEngineStub::startScheduledPreparedMonoClip(std::int64_t timelineStartSample) noexcept
{
    startScheduledPreparedMonoClip(timelineStartSample,
                                   0,
                                   getPreparedClipLengthSamples());
}

void AudioEngineStub::startScheduledPreparedMonoClip(std::int64_t timelineStartSample,
                                                     std::int64_t clipLocalStartOffsetSamples,
                                                     std::int64_t clipLengthSamples) noexcept
{
    if (preparedMonoClipSamples_ == nullptr || preparedMonoClipSamples_->empty())
    {
        stop();
        return;
    }

    scheduledClipStartSample_ = std::max<std::int64_t>(0, timelineStartSample);
    const auto preparedLengthSamples = static_cast<std::int64_t>(preparedMonoClipSamples_->size());
    generatedClipLengthSamples_ = std::min(preparedLengthSamples, clipLengthSamples);
    scheduledClipLocalStartOffsetSamples_ = std::max<std::int64_t>(0, clipLocalStartOffsetSamples);

    if (generatedClipLengthSamples_ <= 0
        || scheduledClipLocalStartOffsetSamples_ >= generatedClipLengthSamples_)
    {
        stop();
        return;
    }

    generatedClipPositionSamples_ = scheduledClipLocalStartOffsetSamples_;
    scheduledClipHasStarted_ = false;
    playbackMode_ = PlaybackMode::scheduledPreparedMonoClip;
}

void AudioEngineStub::startPreparedVoiceSchedule(TrackVoiceSchedule schedule)
{
    if (schedule.frameCount <= 0 || schedule.voices.empty())
    {
        stop();
        return;
    }

    preparedVoiceSchedule_ = std::move(schedule);
    timelinePositionSamples_ = preparedVoiceSchedule_.renderTimelineStartSample;
    playbackMode_ = PlaybackMode::preparedVoiceSchedule;
}

void AudioEngineStub::stop() noexcept
{
    playbackMode_ = PlaybackMode::stopped;
    scheduledClipHasStarted_ = false;
}

void AudioEngineStub::setGeneratedToneEnabled(bool shouldEnable) noexcept
{
    if (shouldEnable)
        startGeneratedTone();
    else
        stop();
}

bool AudioEngineStub::isGeneratedToneEnabled() const noexcept
{
    return playbackMode_ == PlaybackMode::continuousTone;
}

bool AudioEngineStub::isGeneratedClipPlaying() const noexcept
{
    return playbackMode_ == PlaybackMode::generatedClip
        || playbackMode_ == PlaybackMode::scheduledGeneratedClip
        || playbackMode_ == PlaybackMode::scheduledPreparedMonoClip
        || playbackMode_ == PlaybackMode::preparedVoiceSchedule;
}

std::int64_t AudioEngineStub::getGeneratedClipPositionSamples() const noexcept
{
    return generatedClipPositionSamples_;
}

std::int64_t AudioEngineStub::getPreparedClipLengthSamples() const noexcept
{
    return preparedMonoClipSamples_ == nullptr
        ? 0
        : static_cast<std::int64_t>(preparedMonoClipSamples_->size());
}

void AudioEngineStub::setTimelinePositionSamples(std::int64_t timelinePositionSamples) noexcept
{
    timelinePositionSamples_ = std::max<std::int64_t>(0, timelinePositionSamples);
}

std::int64_t AudioEngineStub::getTimelinePositionSamples() const noexcept
{
    return timelinePositionSamples_;
}

void AudioEngineStub::setGeneratedToneFrequencyHz(double frequencyHz) noexcept
{
    toneRenderer_.setFrequencyHz(frequencyHz);
}

double AudioEngineStub::getGeneratedToneFrequencyHz() const noexcept
{
    return toneRenderer_.getFrequencyHz();
}

void AudioEngineStub::setGeneratedToneGain(float gain) noexcept
{
    toneRenderer_.setGain(gain);
}

float AudioEngineStub::getGeneratedToneGain() const noexcept
{
    return toneRenderer_.getGain();
}

float AudioEngineStub::renderSample() noexcept
{
    if (playbackMode_ == PlaybackMode::stopped)
        return 0.0f;

    if (playbackMode_ == PlaybackMode::preparedVoiceSchedule)
        return 0.0f;

    if (playbackMode_ == PlaybackMode::continuousTone)
        return toneRenderer_.renderSample();

    if (playbackMode_ == PlaybackMode::generatedClip)
    {
        if (generatedClipPositionSamples_ >= generatedClipLengthSamples_)
        {
            stop();
            return 0.0f;
        }

        const auto sample = toneRenderer_.renderSample();
        ++generatedClipPositionSamples_;
        if (generatedClipPositionSamples_ >= generatedClipLengthSamples_)
            stop();

        return sample;
    }

    if (timelinePositionSamples_ < scheduledClipStartSample_)
    {
        ++timelinePositionSamples_;
        return 0.0f;
    }

    if (!scheduledClipHasStarted_)
    {
        if (playbackMode_ == PlaybackMode::scheduledGeneratedClip)
            toneRenderer_.reset();

        scheduledClipHasStarted_ = true;
        generatedClipPositionSamples_ = scheduledClipLocalStartOffsetSamples_
            + (timelinePositionSamples_ - scheduledClipStartSample_);
    }

    if (generatedClipPositionSamples_ >= generatedClipLengthSamples_)
    {
        stop();
        ++timelinePositionSamples_;
        return 0.0f;
    }

    const auto sample = playbackMode_ == PlaybackMode::scheduledPreparedMonoClip
        ? (*preparedMonoClipSamples_)[static_cast<std::size_t>(generatedClipPositionSamples_)]
        : toneRenderer_.renderSample();
    ++generatedClipPositionSamples_;
    ++timelinePositionSamples_;
    if (generatedClipPositionSamples_ >= generatedClipLengthSamples_)
        stop();

    return sample;
}

void AudioEngineStub::render(float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0 || left == nullptr)
        return;

    if (playbackMode_ == PlaybackMode::preparedVoiceSchedule)
    {
        renderPreparedVoiceSchedule(left, right, numSamples);
        return;
    }

    for (int index = 0; index < numSamples; ++index)
    {
        const auto sample = renderSample();
        left[index] = sample;

        if (right != nullptr)
            right[index] = sample;
    }
}

void AudioEngineStub::render(float* const* outputChannelData, int numOutputChannels, int numSamples) noexcept
{
    if (outputChannelData == nullptr || numOutputChannels <= 0 || numSamples <= 0)
        return;

    if (playbackMode_ == PlaybackMode::preparedVoiceSchedule)
    {
        auto* left = outputChannelData[0];
        auto* right = numOutputChannels > 1 ? outputChannelData[1] : nullptr;
        renderPreparedVoiceSchedule(left, right, numSamples);

        for (int channel = 2; channel < numOutputChannels; ++channel)
        {
            if (outputChannelData[channel] != nullptr)
                std::fill(outputChannelData[channel], outputChannelData[channel] + numSamples, 0.0f);
        }
        return;
    }

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const auto sample = renderSample();

        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            if (outputChannelData[channel] != nullptr)
                outputChannelData[channel][sampleIndex] = sample;
        }
    }
}

const std::vector<float>* AudioEngineStub::findPreparedVoiceSamples(const std::string& clipId) const noexcept
{
    const auto match = std::find_if(preparedTrackVoiceBuffers_.begin(),
                                    preparedTrackVoiceBuffers_.end(),
                                    [&clipId](const PreparedTrackVoiceBuffer& buffer)
                                    {
                                        return buffer.clipId == clipId;
                                    });

    if (match == preparedTrackVoiceBuffers_.end() || match->samples == nullptr)
        return nullptr;

    return match->samples.get();
}

void AudioEngineStub::renderPreparedVoiceSchedule(float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0 || left == nullptr)
        return;

    for (int index = 0; index < numSamples; ++index)
    {
        auto mixLeft = 0.0f;
        auto mixRight = 0.0f;
        const auto renderOffset = timelinePositionSamples_ - preparedVoiceSchedule_.renderTimelineStartSample;

        if (renderOffset >= 0 && renderOffset < preparedVoiceSchedule_.frameCount)
        {
            for (const auto& voice : preparedVoiceSchedule_.voices)
            {
                if (renderOffset < voice.renderStartOffsetSamples
                    || renderOffset >= voice.renderStartOffsetSamples + voice.frameCount)
                {
                    continue;
                }

                const auto* samples = findPreparedVoiceSamples(voice.clipId);
                if (samples == nullptr)
                    continue;

                const auto sampleIndex = voice.clipLocalStartOffsetSamples
                    + (renderOffset - voice.renderStartOffsetSamples);
                if (sampleIndex < 0 || sampleIndex >= static_cast<std::int64_t>(samples->size()))
                    continue;

                auto sample = (*samples)[static_cast<std::size_t>(sampleIndex)];
                if (!std::isfinite(sample))
                    sample = 0.0f;

                mixLeft += sample * voice.gainLeft;
                mixRight += sample * voice.gainRight;
            }
        }

        left[index] = std::clamp(mixLeft, -1.0f, 1.0f);
        if (right != nullptr)
            right[index] = std::clamp(mixRight, -1.0f, 1.0f);

        ++timelinePositionSamples_;
        if (timelinePositionSamples_ >= preparedVoiceSchedule_.renderTimelineStartSample
                + preparedVoiceSchedule_.frameCount)
        {
            stop();
        }
    }
}

void AudioEngineStub::renderInterleavedInt16(std::int16_t* outputSamples, int frameCount, int channelCount) noexcept
{
    if (outputSamples == nullptr || frameCount <= 0 || channelCount <= 0)
        return;

    for (int frame = 0; frame < frameCount; ++frame)
    {
        const auto sample = static_cast<std::int16_t>(
            std::clamp(renderSample(), -1.0f, 1.0f) * 32767.0f);

        for (int channel = 0; channel < channelCount; ++channel)
            outputSamples[(frame * channelCount) + channel] = sample;
    }
}
} // namespace projectname
