// SPDX-License-Identifier: AGPL-3.0-or-later

#include "BackgroundWaveformAnalysisJob.h"

#include <chrono>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] bool isFailure(WaveformRegenerationStatus status) noexcept
{
    return status == WaveformRegenerationStatus::invalidPackagePath
        || status == WaveformRegenerationStatus::missingAnalysisPath
        || status == WaveformRegenerationStatus::missingAudio
        || status == WaveformRegenerationStatus::decodeFailed
        || status == WaveformRegenerationStatus::saveFailed;
}
} // namespace

BackgroundWaveformAnalysisJob::BackgroundWaveformAnalysisJob(BackgroundWaveformAnalysisRequest request)
    : request_(std::move(request)),
      cancelRequested_(std::make_shared<std::atomic_bool>(false))
{
}

BackgroundWaveformAnalysisJob::~BackgroundWaveformAnalysisJob()
{
    requestCancel();
    if (future_.valid())
        future_.wait();
}

void BackgroundWaveformAnalysisJob::start()
{
    if (started_)
        return;

    started_ = true;
    future_ = std::async(std::launch::async,
                         &BackgroundWaveformAnalysisJob::run,
                         std::move(request_),
                         cancelRequested_);
}

void BackgroundWaveformAnalysisJob::requestCancel() noexcept
{
    cancelRequested_->store(true, std::memory_order_release);
}

bool BackgroundWaveformAnalysisJob::hasStarted() const noexcept
{
    return started_;
}

bool BackgroundWaveformAnalysisJob::isReady() const
{
    if (!future_.valid())
        return false;

    return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

BackgroundWaveformAnalysisResult BackgroundWaveformAnalysisJob::waitForResult()
{
    if (!started_)
        start();

    return future_.get();
}

BackgroundWaveformAnalysisResult BackgroundWaveformAnalysisJob::run(
    BackgroundWaveformAnalysisRequest request,
    std::shared_ptr<std::atomic_bool> cancelRequested)
{
    WaveformRegenerationOptions options;
    options.cancelRequested = cancelRequested.get();
    auto regeneration = regenerateFirstImportedAudioWaveformAnalysis(request.project,
                                                                     request.packageDirectory,
                                                                     options);

    BackgroundWaveformAnalysisResult result;
    result.regeneration = std::move(regeneration);
    result.cancelled = result.regeneration.status == WaveformRegenerationStatus::cancelled;
    if (result.cancelled || isFailure(result.regeneration.status))
        result.error = result.regeneration.error;

    return result;
}
} // namespace projectname
