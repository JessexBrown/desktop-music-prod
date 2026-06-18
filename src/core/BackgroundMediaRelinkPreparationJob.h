// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ImportedClipMediaRelink.h"
#include "ProjectModel.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>

namespace projectname
{
struct BackgroundMediaRelinkPreparationRequest
{
    ProjectModel project;
    std::filesystem::path packageDirectory;
    std::filesystem::path sourceWavPath;
    std::string selectedClipId;
    std::size_t copyChunkBytes = 64U * 1024U;
};

struct BackgroundMediaRelinkPreparationResult
{
    std::optional<ImportedClipMediaRelinkPreparation> preparation;
    ImportedClipMediaRelinkPreparationStatus status =
        ImportedClipMediaRelinkPreparationStatus::packageError;
    std::string error;
    bool cancelled = false;
};

enum class BackgroundMediaRelinkPreparationPhase
{
    pending,
    decoding,
    copying,
    analysing,
    completed,
    failed,
    cancelled,
};

struct BackgroundMediaRelinkPreparationProgress
{
    BackgroundMediaRelinkPreparationPhase phase =
        BackgroundMediaRelinkPreparationPhase::pending;
    int percent = 0;
    std::uintmax_t framesProcessed = 0;
    std::uintmax_t framesTotal = 0;
    std::uintmax_t bytesProcessed = 0;
    std::uintmax_t bytesTotal = 0;
    bool cancelRequested = false;
};

class BackgroundMediaRelinkPreparationJob
{
public:
    explicit BackgroundMediaRelinkPreparationJob(BackgroundMediaRelinkPreparationRequest request);
    ~BackgroundMediaRelinkPreparationJob();

    BackgroundMediaRelinkPreparationJob(const BackgroundMediaRelinkPreparationJob&) = delete;
    BackgroundMediaRelinkPreparationJob& operator=(const BackgroundMediaRelinkPreparationJob&) = delete;

    void start();
    void requestCancel() noexcept;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool hasStarted() const noexcept;
    [[nodiscard]] BackgroundMediaRelinkPreparationProgress getProgress() const noexcept;
    [[nodiscard]] BackgroundMediaRelinkPreparationResult waitForResult();

private:
    [[nodiscard]] static BackgroundMediaRelinkPreparationResult run(
        BackgroundMediaRelinkPreparationRequest request,
        std::shared_ptr<std::atomic_bool> cancelRequested,
        std::shared_ptr<std::atomic_int> phase,
        std::shared_ptr<std::atomic_int> progressPercent,
        std::shared_ptr<std::atomic<std::uintmax_t>> framesProcessed,
        std::shared_ptr<std::atomic<std::uintmax_t>> framesTotal,
        std::shared_ptr<std::atomic<std::uintmax_t>> bytesProcessed,
        std::shared_ptr<std::atomic<std::uintmax_t>> bytesTotal);

    BackgroundMediaRelinkPreparationRequest request_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::shared_ptr<std::atomic_int> phase_;
    std::shared_ptr<std::atomic_int> progressPercent_;
    std::shared_ptr<std::atomic<std::uintmax_t>> framesProcessed_;
    std::shared_ptr<std::atomic<std::uintmax_t>> framesTotal_;
    std::shared_ptr<std::atomic<std::uintmax_t>> bytesProcessed_;
    std::shared_ptr<std::atomic<std::uintmax_t>> bytesTotal_;
    mutable std::future<BackgroundMediaRelinkPreparationResult> future_;
    bool started_ = false;
};
} // namespace projectname
