// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "WavAudioImporter.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace projectname
{
struct WaveformBucket
{
    float peak = 0.0f;
    float rms = 0.0f;

    [[nodiscard]] bool operator==(const WaveformBucket& other) const = default;
};

struct WaveformSummary
{
    double sampleRateHz = 0.0;
    std::int64_t frameCount = 0;
    std::int64_t sourceFramesPerBucket = 0;
    std::vector<WaveformBucket> buckets;

    [[nodiscard]] bool operator==(const WaveformSummary& other) const = default;
};

[[nodiscard]] WaveformSummary buildWaveformSummary(const PreparedMonoAudioClip& clip,
                                                   std::size_t targetBucketCount = 128);

[[nodiscard]] bool saveWaveformSummary(const WaveformSummary& summary,
                                       const std::filesystem::path& path,
                                       std::string& error);

[[nodiscard]] std::optional<WaveformSummary> loadWaveformSummary(const std::filesystem::path& path,
                                                                 std::string& error);
} // namespace projectname
