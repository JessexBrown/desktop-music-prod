// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectPackageSaveAsPolicy.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>

namespace projectname
{
enum class BackgroundSaveAsPackageCopyPhase
{
    pending,
    planning,
    preflight,
    copying,
    completed,
    failed,
    cancelled,
};

struct BackgroundSaveAsPackageCopyRequest
{
    ProjectModel project;
    std::filesystem::path sourcePackageDirectory;
    std::filesystem::path targetPackageDirectory;
};

struct BackgroundSaveAsPackageCopyProgress
{
    BackgroundSaveAsPackageCopyPhase phase =
        BackgroundSaveAsPackageCopyPhase::pending;
    int percent = 0;
    std::size_t filesCopied = 0;
    std::size_t filesTotal = 0;
    std::uintmax_t bytesCopied = 0;
    std::uintmax_t bytesTotal = 0;
    bool cancelRequested = false;
};

struct BackgroundSaveAsPackageCopyResult
{
    std::filesystem::path sourcePackageDirectory;
    std::filesystem::path targetPackageDirectory;
    ProjectPackageSaveAsCopyResult copy;
    std::string error;
    bool cancelled = false;
};

class BackgroundSaveAsPackageCopyJob
{
public:
    explicit BackgroundSaveAsPackageCopyJob(BackgroundSaveAsPackageCopyRequest request);
    ~BackgroundSaveAsPackageCopyJob();

    BackgroundSaveAsPackageCopyJob(const BackgroundSaveAsPackageCopyJob&) = delete;
    BackgroundSaveAsPackageCopyJob& operator=(const BackgroundSaveAsPackageCopyJob&) = delete;

    void start();
    void requestCancel() noexcept;
    [[nodiscard]] bool isReady() const;
    [[nodiscard]] bool hasStarted() const noexcept;
    [[nodiscard]] BackgroundSaveAsPackageCopyProgress getProgress() const noexcept;
    [[nodiscard]] BackgroundSaveAsPackageCopyResult waitForResult();

private:
    [[nodiscard]] static BackgroundSaveAsPackageCopyResult run(
        BackgroundSaveAsPackageCopyRequest request,
        std::shared_ptr<std::atomic_bool> cancelRequested,
        std::shared_ptr<std::atomic_int> phase,
        std::shared_ptr<std::atomic_int> progressPercent,
        std::shared_ptr<std::atomic<std::size_t>> filesCopied,
        std::shared_ptr<std::atomic<std::size_t>> filesTotal,
        std::shared_ptr<std::atomic<std::uintmax_t>> bytesCopied,
        std::shared_ptr<std::atomic<std::uintmax_t>> bytesTotal);

    BackgroundSaveAsPackageCopyRequest request_;
    std::shared_ptr<std::atomic_bool> cancelRequested_;
    std::shared_ptr<std::atomic_int> phase_;
    std::shared_ptr<std::atomic_int> progressPercent_;
    std::shared_ptr<std::atomic<std::size_t>> filesCopied_;
    std::shared_ptr<std::atomic<std::size_t>> filesTotal_;
    std::shared_ptr<std::atomic<std::uintmax_t>> bytesCopied_;
    std::shared_ptr<std::atomic<std::uintmax_t>> bytesTotal_;
    mutable std::future<BackgroundSaveAsPackageCopyResult> future_;
    bool started_ = false;
};
} // namespace projectname
