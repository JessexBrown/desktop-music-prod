// SPDX-License-Identifier: AGPL-3.0-or-later

#include "WavAudioImporter.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace projectname
{
namespace
{
using FourCc = std::array<char, 4>;

[[nodiscard]] bool readExact(std::ifstream& file, char* destination, std::streamsize byteCount)
{
    return static_cast<bool>(file.read(destination, byteCount));
}

[[nodiscard]] bool readFourCc(std::ifstream& file, FourCc& id)
{
    return readExact(file, id.data(), static_cast<std::streamsize>(id.size()));
}

[[nodiscard]] bool fourCcEquals(const FourCc& id, const char (&literal)[5])
{
    return id[0] == literal[0] && id[1] == literal[1] && id[2] == literal[2] && id[3] == literal[3];
}

[[nodiscard]] std::uint16_t readLittleEndianUInt16(const unsigned char* bytes)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0])
        | (static_cast<std::uint16_t>(bytes[1]) << 8));
}

[[nodiscard]] std::uint32_t readLittleEndianUInt32(const unsigned char* bytes)
{
    return static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8)
        | (static_cast<std::uint32_t>(bytes[2]) << 16)
        | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

[[nodiscard]] bool readLittleEndianUInt32(std::ifstream& file, std::uint32_t& value)
{
    std::array<unsigned char, 4> bytes {};
    if (!readExact(file, reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return false;

    value = readLittleEndianUInt32(bytes.data());
    return true;
}

[[nodiscard]] bool skipBytes(std::ifstream& file, std::uint32_t byteCount)
{
    file.seekg(static_cast<std::streamoff>(byteCount), std::ios::cur);
    return static_cast<bool>(file);
}

[[nodiscard]] bool skipChunkPayload(std::ifstream& file, std::uint32_t chunkSize)
{
    if (chunkSize == std::numeric_limits<std::uint32_t>::max())
        return false;

    const auto paddedSize = chunkSize + (chunkSize & 1U);
    return skipBytes(file, paddedSize);
}

[[nodiscard]] float pcm16ToFloat(std::int16_t value)
{
    if (value == std::numeric_limits<std::int16_t>::min())
        return -1.0f;

    return static_cast<float>(value) / 32767.0f;
}

[[nodiscard]] std::int16_t readSignedLittleEndianInt16(const unsigned char* bytes)
{
    const auto value = readLittleEndianUInt16(bytes);
    if (value >= 0x8000U)
        return static_cast<std::int16_t>(static_cast<int>(value) - 0x10000);

    return static_cast<std::int16_t>(value);
}

[[nodiscard]] bool isCancelRequested(const WavDecodeOptions& options) noexcept
{
    return options.cancelRequested != nullptr
        && options.cancelRequested->load(std::memory_order_acquire);
}

void reportDecodeProgress(const WavDecodeOptions& options,
                          std::uintmax_t framesDecoded,
                          std::uintmax_t totalFrames)
{
    if (!options.progressCallback)
        return;

    WavDecodeProgress progress;
    progress.framesDecoded = framesDecoded;
    progress.totalFrames = totalFrames;
    if (totalFrames > 0)
    {
        const auto percent = (framesDecoded * 100U) / totalFrames;
        progress.percent = static_cast<int>(std::min<std::uintmax_t>(percent, 100U));
    }

    options.progressCallback(progress);
}

struct WavFormat
{
    std::uint16_t channelCount = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bitsPerSample = 0;
};

[[nodiscard]] bool parseFormatChunk(std::ifstream& file, std::uint32_t chunkSize, WavFormat& format, std::string& error)
{
    if (chunkSize < 16)
    {
        error = "Unsupported WAV: fmt chunk is too small.";
        return false;
    }

    std::array<unsigned char, 16> bytes {};
    if (!readExact(file, reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    {
        error = "Could not read WAV fmt chunk.";
        return false;
    }

    const auto audioFormat = readLittleEndianUInt16(bytes.data());
    format.channelCount = readLittleEndianUInt16(bytes.data() + 2);
    format.sampleRate = readLittleEndianUInt32(bytes.data() + 4);
    format.blockAlign = readLittleEndianUInt16(bytes.data() + 12);
    format.bitsPerSample = readLittleEndianUInt16(bytes.data() + 14);

    const auto remainingBytes = chunkSize - static_cast<std::uint32_t>(bytes.size());
    if (remainingBytes > 0 && !skipBytes(file, remainingBytes))
    {
        error = "Could not skip WAV fmt extension bytes.";
        return false;
    }

    if ((chunkSize & 1U) != 0 && !skipBytes(file, 1))
    {
        error = "Could not skip WAV fmt padding byte.";
        return false;
    }

    if (audioFormat != 1)
    {
        error = "Unsupported WAV: only PCM format is supported in this importer.";
        return false;
    }

    if (format.channelCount == 0 || format.channelCount > 64)
    {
        error = "Unsupported WAV: channel count is invalid.";
        return false;
    }

    if (format.sampleRate == 0)
    {
        error = "Unsupported WAV: sample rate is invalid.";
        return false;
    }

    if (format.bitsPerSample != 16)
    {
        error = "Unsupported WAV: only 16-bit PCM is supported in this importer.";
        return false;
    }

    const auto expectedBlockAlign = static_cast<std::uint16_t>(format.channelCount * (format.bitsPerSample / 8));
    if (format.blockAlign != expectedBlockAlign)
    {
        error = "Unsupported WAV: block alignment does not match the PCM format.";
        return false;
    }

    return true;
}

[[nodiscard]] std::optional<PreparedMonoAudioClip> decodePcm16DataChunk(
    std::ifstream& file,
    std::uint32_t chunkSize,
    const WavFormat& format,
    const WavDecodeOptions& options,
    std::string& error)
{
    if (format.blockAlign == 0 || (chunkSize % format.blockAlign) != 0)
    {
        error = "Unsupported WAV: data chunk size does not align to whole frames.";
        return std::nullopt;
    }

    const auto frameCount = chunkSize / format.blockAlign;
    reportDecodeProgress(options, 0U, frameCount);

    PreparedMonoAudioClip clip;
    clip.sampleRateHz = static_cast<double>(format.sampleRate);
    clip.sourceChannelCount = static_cast<int>(format.channelCount);
    clip.frameCount = static_cast<std::int64_t>(frameCount);
    clip.samples.reserve(static_cast<std::size_t>(frameCount));

    std::array<unsigned char, 2> sampleBytes {};
    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
    {
        if (isCancelRequested(options))
        {
            error = "WAV decode was cancelled.";
            return std::nullopt;
        }

        auto monoSample = 0.0f;
        for (std::uint16_t channel = 0; channel < format.channelCount; ++channel)
        {
            if (!readExact(file, reinterpret_cast<char*>(sampleBytes.data()), static_cast<std::streamsize>(sampleBytes.size())))
            {
                error = "Could not read WAV sample data.";
                return std::nullopt;
            }

            monoSample += pcm16ToFloat(readSignedLittleEndianInt16(sampleBytes.data()));
        }

        monoSample /= static_cast<float>(format.channelCount);
        clip.samples.push_back(std::clamp(monoSample, -1.0f, 1.0f));
        reportDecodeProgress(options, static_cast<std::uintmax_t>(frame) + 1U, frameCount);
    }

    if (isCancelRequested(options))
    {
        error = "WAV decode was cancelled.";
        return std::nullopt;
    }

    if ((chunkSize & 1U) != 0 && !skipBytes(file, 1))
    {
        error = "Could not skip WAV data padding byte.";
        return std::nullopt;
    }

    return clip;
}
} // namespace

std::optional<PreparedMonoAudioClip> loadPcm16WavAsPreparedMonoClip(
    const std::filesystem::path& path,
    const WavDecodeOptions& options,
    std::string& error)
{
    error.clear();

    if (isCancelRequested(options))
    {
        error = "WAV decode was cancelled before reading.";
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "Could not open WAV file.";
        return std::nullopt;
    }

    FourCc riffId {};
    std::uint32_t riffSize = 0;
    FourCc waveId {};
    if (!readFourCc(file, riffId) || !readLittleEndianUInt32(file, riffSize) || !readFourCc(file, waveId))
    {
        error = "Could not read WAV header.";
        return std::nullopt;
    }

    if (!fourCcEquals(riffId, "RIFF") || !fourCcEquals(waveId, "WAVE"))
    {
        error = "Unsupported WAV: expected RIFF/WAVE header.";
        return std::nullopt;
    }

    WavFormat format;
    auto hasFormat = false;

    while (file)
    {
        FourCc chunkId {};
        if (!readFourCc(file, chunkId))
            break;

        std::uint32_t chunkSize = 0;
        if (!readLittleEndianUInt32(file, chunkSize))
        {
            error = "Could not read WAV chunk size.";
            return std::nullopt;
        }

        if (fourCcEquals(chunkId, "fmt "))
        {
            if (!parseFormatChunk(file, chunkSize, format, error))
                return std::nullopt;

            hasFormat = true;
            continue;
        }

        if (fourCcEquals(chunkId, "data"))
        {
            if (!hasFormat)
            {
                error = "Unsupported WAV: data chunk appears before fmt chunk.";
                return std::nullopt;
            }

            auto decoded = decodePcm16DataChunk(file, chunkSize, format, options, error);
            if (!decoded.has_value())
                return std::nullopt;

            if (decoded->samples.empty())
            {
                error = "Unsupported WAV: data chunk is empty.";
                return std::nullopt;
            }

            return decoded;
        }

        if (!skipChunkPayload(file, chunkSize))
        {
            error = "Could not skip unsupported WAV chunk.";
            return std::nullopt;
        }
    }

    error = hasFormat ? "Unsupported WAV: missing data chunk." : "Unsupported WAV: missing fmt chunk.";
    return std::nullopt;
}

std::optional<PreparedMonoAudioClip> loadPcm16WavAsPreparedMonoClip(
    const std::filesystem::path& path,
    std::string& error)
{
    WavDecodeOptions options;
    return loadPcm16WavAsPreparedMonoClip(path, options, error);
}
} // namespace projectname
