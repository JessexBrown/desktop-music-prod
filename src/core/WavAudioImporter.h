// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace projectname
{
struct PreparedMonoAudioClip
{
    double sampleRateHz = 0.0;
    int sourceChannelCount = 0;
    std::int64_t frameCount = 0;
    std::vector<float> samples;
};

struct WavDecodeProgress
{
    std::uintmax_t framesDecoded = 0;
    std::uintmax_t totalFrames = 0;
    int percent = 0;
};

struct WavDecodeOptions
{
    std::atomic_bool* cancelRequested = nullptr;
    std::function<void(const WavDecodeProgress&)> progressCallback;
};

[[nodiscard]] std::optional<PreparedMonoAudioClip> loadPcm16WavAsPreparedMonoClip(
    const std::filesystem::path& path,
    const WavDecodeOptions& options,
    std::string& error);

[[nodiscard]] std::optional<PreparedMonoAudioClip> loadPcm16WavAsPreparedMonoClip(
    const std::filesystem::path& path,
    std::string& error);
} // namespace projectname
