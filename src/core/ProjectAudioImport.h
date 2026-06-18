// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectModel.h"
#include "WavAudioImporter.h"

#include <filesystem>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace projectname
{
struct ProjectAudioImportResult
{
    ProjectClip clip;
    PreparedMonoAudioClip preparedClip;
    std::filesystem::path copiedAudioPath;
    std::filesystem::path waveformSummaryPath;
};

enum class ProjectAudioImportStage
{
    preparing,
    copying,
    committing,
    completed
};

struct ProjectAudioImportProgress
{
    ProjectAudioImportStage stage = ProjectAudioImportStage::preparing;
    std::uintmax_t bytesCopied = 0;
    std::uintmax_t totalBytes = 0;
    int percent = 0;
};

struct ProjectAudioImportOptions
{
    std::atomic_bool* cancelRequested = nullptr;
    std::optional<double> requestedStartBeats;
    std::function<void(const WavDecodeProgress&)> decodeProgressCallback;
    std::function<void(const ProjectAudioImportProgress&)> progressCallback;
    std::size_t copyChunkBytes = 64U * 1024U;
};

[[nodiscard]] std::optional<ProjectAudioImportResult> commitPreparedAudioImportToProjectPackage(
    ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& sourceAudioPath,
    PreparedMonoAudioClip preparedClip,
    const ProjectAudioImportOptions& options,
    std::string& error);

[[nodiscard]] std::optional<ProjectAudioImportResult> importPcm16WavIntoProjectPackage(
    ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& sourceWavPath,
    const ProjectAudioImportOptions& options,
    std::string& error);

[[nodiscard]] std::optional<ProjectAudioImportResult> importPcm16WavIntoProjectPackage(
    ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& sourceWavPath,
    std::string& error);
} // namespace projectname
