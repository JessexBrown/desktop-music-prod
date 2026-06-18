// SPDX-License-Identifier: AGPL-3.0-or-later

#include "WaveformSummary.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>

namespace projectname
{
namespace
{
constexpr int waveformSummaryVersion = 1;

[[nodiscard]] float sanitizeSample(float sample) noexcept
{
    if (!std::isfinite(sample))
        return 0.0f;

    return std::clamp(sample, -1.0f, 1.0f);
}

[[nodiscard]] nlohmann::json makeBucketJson(const WaveformBucket& bucket)
{
    return {
        { "peak", bucket.peak },
        { "rms", bucket.rms },
    };
}

[[nodiscard]] std::optional<WaveformBucket> readBucket(const nlohmann::json& json, std::string& error)
{
    if (!json.is_object())
    {
        error = "Waveform summary bucket is not an object.";
        return std::nullopt;
    }

    WaveformBucket bucket;
    bucket.peak = json.value("peak", 0.0f);
    bucket.rms = json.value("rms", 0.0f);
    if (!std::isfinite(bucket.peak) || !std::isfinite(bucket.rms) || bucket.peak < 0.0f || bucket.rms < 0.0f)
    {
        error = "Waveform summary bucket values are invalid.";
        return std::nullopt;
    }

    bucket.peak = std::clamp(bucket.peak, 0.0f, 1.0f);
    bucket.rms = std::clamp(bucket.rms, 0.0f, 1.0f);
    return bucket;
}
} // namespace

WaveformSummary buildWaveformSummary(const PreparedMonoAudioClip& clip, std::size_t targetBucketCount)
{
    WaveformSummary summary;
    summary.sampleRateHz = clip.sampleRateHz;
    summary.frameCount = clip.frameCount;

    if (clip.samples.empty() || targetBucketCount == 0)
        return summary;

    const auto bucketCount = std::min(targetBucketCount, clip.samples.size());
    const auto framesPerBucket = (clip.samples.size() + bucketCount - 1U) / bucketCount;
    summary.sourceFramesPerBucket = static_cast<std::int64_t>(framesPerBucket);
    summary.buckets.reserve(bucketCount);

    for (std::size_t bucketIndex = 0; bucketIndex < bucketCount; ++bucketIndex)
    {
        const auto begin = bucketIndex * framesPerBucket;
        const auto end = std::min(begin + framesPerBucket, clip.samples.size());
        if (begin >= end)
            break;

        auto peak = 0.0f;
        auto sumSquares = 0.0;
        for (auto sampleIndex = begin; sampleIndex < end; ++sampleIndex)
        {
            const auto sample = sanitizeSample(clip.samples[sampleIndex]);
            const auto magnitude = std::abs(sample);
            peak = std::max(peak, magnitude);
            sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
        }

        WaveformBucket bucket;
        bucket.peak = peak;
        bucket.rms = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(end - begin)));
        summary.buckets.push_back(bucket);
    }

    return summary;
}

bool saveWaveformSummary(const WaveformSummary& summary,
                         const std::filesystem::path& path,
                         std::string& error)
{
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError)
    {
        error = "Could not create waveform analysis folder: " + filesystemError.message();
        return false;
    }

    auto buckets = nlohmann::json::array();
    for (const auto& bucket : summary.buckets)
        buckets.push_back(makeBucketJson(bucket));

    const auto json = nlohmann::json {
        { "formatVersion", waveformSummaryVersion },
        { "kind", "projectname.waveform-summary" },
        { "sampleRateHz", summary.sampleRateHz },
        { "frameCount", summary.frameCount },
        { "sourceFramesPerBucket", summary.sourceFramesPerBucket },
        { "buckets", buckets },
    };

    std::ofstream file(path, std::ios::trunc);
    if (!file)
    {
        error = "Could not write waveform summary.";
        return false;
    }

    file << json.dump(2) << '\n';
    if (!file)
    {
        error = "Waveform summary write failed.";
        return false;
    }

    error.clear();
    return true;
}

std::optional<WaveformSummary> loadWaveformSummary(const std::filesystem::path& path, std::string& error)
{
    std::ifstream file(path);
    if (!file)
    {
        error = "Waveform summary could not be opened.";
        return std::nullopt;
    }

    try
    {
        const auto json = nlohmann::json::parse(file);
        if (!json.is_object())
        {
            error = "Waveform summary is not a JSON object.";
            return std::nullopt;
        }

        if (json.value("formatVersion", 0) != waveformSummaryVersion)
        {
            error = "Unsupported waveform summary version.";
            return std::nullopt;
        }

        WaveformSummary summary;
        summary.sampleRateHz = json.value("sampleRateHz", 0.0);
        summary.frameCount = json.value("frameCount", 0);
        summary.sourceFramesPerBucket = json.value("sourceFramesPerBucket", 0);
        if (summary.sampleRateHz < 0.0 || summary.frameCount < 0 || summary.sourceFramesPerBucket < 0)
        {
            error = "Waveform summary metadata is invalid.";
            return std::nullopt;
        }

        const auto buckets = json.find("buckets");
        if (buckets == json.end() || !buckets->is_array())
        {
            error = "Waveform summary buckets must be an array.";
            return std::nullopt;
        }

        for (const auto& bucketJson : *buckets)
        {
            auto bucket = readBucket(bucketJson, error);
            if (!bucket.has_value())
                return std::nullopt;

            summary.buckets.push_back(*bucket);
        }

        error.clear();
        return summary;
    }
    catch (const nlohmann::json::exception& exception)
    {
        error = std::string("Waveform summary is not valid JSON: ") + exception.what();
        return std::nullopt;
    }
}
} // namespace projectname
