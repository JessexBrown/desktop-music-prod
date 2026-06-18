// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectAudioImport.h"
#include "ProjectModel.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>

namespace projectname
{
struct BackgroundAudioImportRequest
{
    ProjectModel project;
    std::filesystem::path packageDirectory;
    std::filesystem::path sourceWavPath;
    std::optional<double> requestedStartBeats;
};

struct BackgroundAudioImportResult
{
    ProjectModel project;
    std::optional<ProjectAudioImportResult> import;
    std::string error;
    bool cancelled = false;
};

enum class BackgroundAudioImportPhase
{
    pending,
    decoding,
    readyToCommit,
    copying,
    committing,
    completed,
    failed,
    cancelled
};

struct BackgroundAudioImportProgress
{
    BackgroundAudioImportPhase phase = BackgroundAudioImportPhase::pending;
    int percent = 0;
    std::uintmax_t framesProcessed = 0;
    std::uintmax_t framesTotal = 0;
    std::uintmax_t bytesProcessed = 0;
    std::uintmax_t bytesTotal = 0;
    bool cancelRequested = false;
};

class BackgroundAudioImportJob
{
public:
    explicit BackgroundAudioImportJob(BackgroundAudioImportRequest request);
    ~BackgroundAudioImportJob();

    BackgroundAudioImportJob(const BackgroundAudioImportJob&) = delete;
    BackgroundAudioImportJob& operator=(const BackgroundAudioImportJob&) = delete;

    void start();
    void requestCancel() noexcept;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool hasStarted() const noexcept;
    [[nodiscard]] BackgroundAudioImportProgress getProgress() const noexcept;
    [[nodiscard]] BackgroundAudioImportResult waitForResult();

private:
    [[nodiscard]] static BackgroundAudioImportResult run(BackgroundAudioImportRequest request,
                                                         std::shared_ptr<std::atomic_bool> cancelRequested,
                                                         std::shared_ptr<std::atomic_int> phase,
                                                         std::shared_ptr<std::atomic_int> progressPercent,
                                                         std::shared_ptr<std::atomic<std::uintmax_t>> framesProcessed,
                                                         std::shared_ptr<std::atomic<std::uintmax_t>> framesTotal,
                                                         std::shared_ptr<std::atomic<std::uintmax_t>> bytesProcessed,
                                                         std::shared_ptr<std::atomic<std::uintmax_t>> bytesTotal);

    BackgroundAudioImportRequest request_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::shared_ptr<std::atomic_int> phase_;
    std::shared_ptr<std::atomic_int> progressPercent_;
    std::shared_ptr<std::atomic<std::uintmax_t>> framesProcessed_;
    std::shared_ptr<std::atomic<std::uintmax_t>> framesTotal_;
    std::shared_ptr<std::atomic<std::uintmax_t>> bytesProcessed_;
    std::shared_ptr<std::atomic<std::uintmax_t>> bytesTotal_;
    mutable std::future<BackgroundAudioImportResult> future_;
    bool started_ = false;
};
} // namespace projectname
