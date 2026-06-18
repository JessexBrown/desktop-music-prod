// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "AppSession.h"
#include "ProjectModel.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace projectname
{
struct BackgroundTimelinePlaybackPreparationRequest
{
    ProjectModel project;
    std::filesystem::path packageDirectory;
    double outputSampleRateHz = 44100.0;
    std::int64_t minimumRenderFrameCount = 0;
};

struct BackgroundPreparedTimelineClip
{
    TimelinePlaybackClipPlan clip;
    PreparedMonoAudioClip preparedClip;
    std::shared_ptr<const std::vector<float>> preparedSamples;
    std::filesystem::path audioPath;
};

struct BackgroundTimelinePlaybackPreparationResult
{
    TimelinePlaybackPreparation preparation;
    TimelineVoicePlaybackPreparation voicePreparation;
    std::vector<BackgroundPreparedTimelineClip> preparedClips;
    std::int64_t minimumRenderFrameCount = 0;
    std::string error;
    bool cancelled = false;
};

enum class BackgroundTimelinePlaybackPreparationPhase
{
    pending,
    planning,
    decoding,
    completed,
    failed,
    cancelled,
};

struct BackgroundTimelinePlaybackPreparationProgress
{
    BackgroundTimelinePlaybackPreparationPhase phase = BackgroundTimelinePlaybackPreparationPhase::pending;
    int percent = 0;
    std::uintmax_t framesProcessed = 0;
    std::uintmax_t framesTotal = 0;
    bool cancelRequested = false;
};

class BackgroundTimelinePlaybackPreparationJob
{
public:
    explicit BackgroundTimelinePlaybackPreparationJob(BackgroundTimelinePlaybackPreparationRequest request);
    ~BackgroundTimelinePlaybackPreparationJob();

    BackgroundTimelinePlaybackPreparationJob(const BackgroundTimelinePlaybackPreparationJob&) = delete;
    BackgroundTimelinePlaybackPreparationJob& operator=(const BackgroundTimelinePlaybackPreparationJob&) = delete;

    void start();
    void requestCancel() noexcept;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool hasStarted() const noexcept;
    [[nodiscard]] BackgroundTimelinePlaybackPreparationProgress getProgress() const noexcept;
    [[nodiscard]] BackgroundTimelinePlaybackPreparationResult waitForResult();

private:
    [[nodiscard]] static BackgroundTimelinePlaybackPreparationResult run(
        BackgroundTimelinePlaybackPreparationRequest request,
        std::shared_ptr<std::atomic_bool> cancelRequested,
        std::shared_ptr<std::atomic_int> phase,
        std::shared_ptr<std::atomic_int> progressPercent,
        std::shared_ptr<std::atomic<std::uintmax_t>> framesProcessed,
        std::shared_ptr<std::atomic<std::uintmax_t>> framesTotal);

    BackgroundTimelinePlaybackPreparationRequest request_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::shared_ptr<std::atomic_int> phase_;
    std::shared_ptr<std::atomic_int> progressPercent_;
    std::shared_ptr<std::atomic<std::uintmax_t>> framesProcessed_;
    std::shared_ptr<std::atomic<std::uintmax_t>> framesTotal_;
    mutable std::future<BackgroundTimelinePlaybackPreparationResult> future_;
    bool started_ = false;
};
} // namespace projectname
