// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ToneRenderer.h"
#include "TrackVoiceSchedule.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace projectname
{
class AudioEngineStub
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void startGeneratedTone() noexcept;
    void startGeneratedClip(double lengthSeconds) noexcept;
    void startScheduledGeneratedClip(std::int64_t timelineStartSample, std::int64_t lengthSamples) noexcept;
    void setPreparedMonoClipSamples(std::vector<float> monoSamples);
    void setPreparedMonoClipSamples(std::shared_ptr<const std::vector<float>> monoSamples) noexcept;
    void setPreparedTrackVoiceBuffers(std::vector<PreparedTrackVoiceBuffer> buffers);
    void startScheduledPreparedMonoClip(std::int64_t timelineStartSample) noexcept;
    void startScheduledPreparedMonoClip(std::int64_t timelineStartSample,
                                        std::int64_t clipLocalStartOffsetSamples,
                                        std::int64_t clipLengthSamples) noexcept;
    void startPreparedVoiceSchedule(TrackVoiceSchedule schedule);
    void stop() noexcept;
    void setGeneratedToneEnabled(bool shouldEnable) noexcept;
    [[nodiscard]] bool isGeneratedToneEnabled() const noexcept;
    [[nodiscard]] bool isGeneratedClipPlaying() const noexcept;
    [[nodiscard]] std::int64_t getGeneratedClipPositionSamples() const noexcept;
    [[nodiscard]] std::int64_t getPreparedClipLengthSamples() const noexcept;
    void setTimelinePositionSamples(std::int64_t timelinePositionSamples) noexcept;
    [[nodiscard]] std::int64_t getTimelinePositionSamples() const noexcept;

    void setGeneratedToneFrequencyHz(double frequencyHz) noexcept;
    [[nodiscard]] double getGeneratedToneFrequencyHz() const noexcept;

    void setGeneratedToneGain(float gain) noexcept;
    [[nodiscard]] float getGeneratedToneGain() const noexcept;

    [[nodiscard]] float renderSample() noexcept;
    void render(float* left, float* right, int numSamples) noexcept;
    void render(float* const* outputChannelData, int numOutputChannels, int numSamples) noexcept;
    void renderInterleavedInt16(std::int16_t* outputSamples, int frameCount, int channelCount) noexcept;

private:
    enum class PlaybackMode
    {
        stopped,
        continuousTone,
        generatedClip,
        scheduledGeneratedClip,
        scheduledPreparedMonoClip,
        preparedVoiceSchedule,
    };

    [[nodiscard]] const std::vector<float>* findPreparedVoiceSamples(const std::string& clipId) const noexcept;
    void renderPreparedVoiceSchedule(float* left, float* right, int numSamples) noexcept;

    ToneRenderer toneRenderer_;
    double sampleRate_ = 44100.0;
    PlaybackMode playbackMode_ = PlaybackMode::stopped;
    std::int64_t generatedClipPositionSamples_ = 0;
    std::int64_t generatedClipLengthSamples_ = 0;
    std::int64_t timelinePositionSamples_ = 0;
    std::int64_t scheduledClipStartSample_ = 0;
    std::int64_t scheduledClipLocalStartOffsetSamples_ = 0;
    bool scheduledClipHasStarted_ = false;
    std::shared_ptr<const std::vector<float>> preparedMonoClipSamples_;
    std::vector<PreparedTrackVoiceBuffer> preparedTrackVoiceBuffers_;
    TrackVoiceSchedule preparedVoiceSchedule_;
};
} // namespace projectname
