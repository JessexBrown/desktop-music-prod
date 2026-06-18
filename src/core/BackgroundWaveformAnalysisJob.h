// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectModel.h"
#include "WaveformAnalysisRegenerator.h"

#include <atomic>
#include <filesystem>
#include <future>
#include <memory>
#include <string>

namespace projectname
{
struct BackgroundWaveformAnalysisRequest
{
    ProjectModel project;
    std::filesystem::path packageDirectory;
};

struct BackgroundWaveformAnalysisResult
{
    WaveformRegenerationResult regeneration;
    std::string error;
    bool cancelled = false;
};

class BackgroundWaveformAnalysisJob
{
public:
    explicit BackgroundWaveformAnalysisJob(BackgroundWaveformAnalysisRequest request);
    ~BackgroundWaveformAnalysisJob();

    BackgroundWaveformAnalysisJob(const BackgroundWaveformAnalysisJob&) = delete;
    BackgroundWaveformAnalysisJob& operator=(const BackgroundWaveformAnalysisJob&) = delete;

    void start();
    void requestCancel() noexcept;
    [[nodiscard]] bool hasStarted() const noexcept;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] BackgroundWaveformAnalysisResult waitForResult();

private:
    [[nodiscard]] static BackgroundWaveformAnalysisResult run(BackgroundWaveformAnalysisRequest request,
                                                              std::shared_ptr<std::atomic_bool> cancelRequested);

    BackgroundWaveformAnalysisRequest request_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    mutable std::future<BackgroundWaveformAnalysisResult> future_;
    bool started_ = false;
};
} // namespace projectname
