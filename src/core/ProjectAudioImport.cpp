// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ProjectAudioImport.h"
#include "WaveformSummary.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace projectname
{
namespace
{
[[nodiscard]] bool isAsciiAlphaNumeric(unsigned char character)
{
    return std::isalnum(character) != 0;
}

[[nodiscard]] std::string sanitizeFileStem(std::string stem)
{
    std::string sanitized;
    sanitized.reserve(stem.size());
    auto previousWasSeparator = false;

    for (const auto character : stem)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (isAsciiAlphaNumeric(byte) || character == '_' || character == '-')
        {
            sanitized.push_back(character);
            previousWasSeparator = false;
        }
        else if (!previousWasSeparator && !sanitized.empty())
        {
            sanitized.push_back('-');
            previousWasSeparator = true;
        }
    }

    while (!sanitized.empty() && sanitized.back() == '-')
        sanitized.pop_back();

    if (sanitized.empty())
        return "imported-audio";

    return sanitized;
}

[[nodiscard]] std::filesystem::path chooseUniqueAudioPath(
    const std::filesystem::path& audioDirectory,
    const std::filesystem::path& sourcePath)
{
    const auto stem = sanitizeFileStem(sourcePath.stem().string());
    const auto extension = sourcePath.extension().empty() ? std::string(".wav") : sourcePath.extension().string();

    auto candidate = audioDirectory / (stem + extension);
    auto suffix = 2;
    while (std::filesystem::exists(candidate))
    {
        candidate = audioDirectory / (stem + "-" + std::to_string(suffix) + extension);
        ++suffix;
    }

    return candidate;
}

[[nodiscard]] std::filesystem::path chooseUniqueWaveformSummaryPath(
    const std::filesystem::path& analysisDirectory,
    const std::filesystem::path& audioPath)
{
    const auto stem = sanitizeFileStem(audioPath.stem().string());

    auto candidate = analysisDirectory / (stem + ".waveform.json");
    auto suffix = 2;
    while (std::filesystem::exists(candidate))
    {
        candidate = analysisDirectory / (stem + "-" + std::to_string(suffix) + ".waveform.json");
        ++suffix;
    }

    return candidate;
}

[[nodiscard]] std::string makeClipId(const ProjectModel& project)
{
    auto clipCount = 0;
    for (const auto& track : project.getTracks())
        clipCount += static_cast<int>(track.clips.size());

    return "clip-imported-audio-" + std::to_string(clipCount + 1);
}

[[nodiscard]] bool trackIdExists(const ProjectModel& project, const std::string& trackId)
{
    return std::any_of(project.getTracks().begin(),
                       project.getTracks().end(),
                       [&trackId](const ProjectTrack& track)
                       {
                           return track.id == trackId;
                       });
}

[[nodiscard]] std::string makeImportedAudioTrackId(const ProjectModel& project)
{
    auto index = 1;
    while (true)
    {
        auto candidate = "track-imported-audio-" + std::to_string(index);
        if (!trackIdExists(project, candidate))
            return candidate;

        ++index;
    }
}

[[nodiscard]] std::string findOrCreateImportTrack(ProjectModel& project)
{
    for (const auto& track : project.getTracks())
    {
        if (track.type == "audio")
            return track.id;
    }

    ProjectTrack track;
    track.id = makeImportedAudioTrackId(project);
    track.name = "Imported Audio";
    track.type = "audio";
    project.addTrack(std::move(track));
    return project.getTracks().back().id;
}

[[nodiscard]] double calculateLengthBeats(const ProjectModel& project, const PreparedMonoAudioClip& preparedClip)
{
    if (preparedClip.sampleRateHz <= 0.0 || preparedClip.frameCount <= 0)
        return 0.0;

    const auto seconds = static_cast<double>(preparedClip.frameCount) / preparedClip.sampleRateHz;
    const auto beats = seconds * (project.getTransport().getTempoBpm() / 60.0);
    if (!std::isfinite(beats) || beats <= 0.0)
        return 0.0;

    return beats;
}

[[nodiscard]] bool validateRequestedStartBeats(const ProjectAudioImportOptions& options, std::string& error)
{
    if (!options.requestedStartBeats.has_value())
        return true;

    if (!std::isfinite(*options.requestedStartBeats) || *options.requestedStartBeats < 0.0)
    {
        error = "Requested clip start beat must be a finite non-negative value.";
        return false;
    }

    return true;
}

[[nodiscard]] double calculateNextAvailableClipStartBeats(const ProjectModel& project,
                                                          const std::string& trackId) noexcept
{
    for (const auto& track : project.getTracks())
    {
        if (track.id != trackId)
            continue;

        auto endBeats = 0.0;
        for (const auto& clip : track.clips)
        {
            if (!std::isfinite(clip.startBeats)
                || !std::isfinite(clip.lengthBeats)
                || clip.lengthBeats <= 0.0)
            {
                continue;
            }

            endBeats = std::max(endBeats, clip.startBeats + clip.lengthBeats);
        }

        return endBeats;
    }

    return 0.0;
}

[[nodiscard]] std::string makeClipName(const std::filesystem::path& sourcePath)
{
    const auto stem = sourcePath.stem().string();
    return stem.empty() ? std::string("Imported Audio") : stem;
}

[[nodiscard]] bool isCancelRequested(const ProjectAudioImportOptions& options) noexcept
{
    return options.cancelRequested != nullptr
        && options.cancelRequested->load(std::memory_order_acquire);
}

void reportProgress(const ProjectAudioImportOptions& options,
                    ProjectAudioImportStage stage,
                    std::uintmax_t bytesCopied,
                    std::uintmax_t totalBytes)
{
    if (!options.progressCallback)
        return;

    ProjectAudioImportProgress progress;
    progress.stage = stage;
    progress.bytesCopied = bytesCopied;
    progress.totalBytes = totalBytes;
    if (totalBytes > 0)
    {
        const auto percent = (bytesCopied * 100U) / totalBytes;
        progress.percent = static_cast<int>(std::min<std::uintmax_t>(percent, 100U));
    }
    else
    {
        progress.percent = stage == ProjectAudioImportStage::completed ? 100 : 0;
    }

    options.progressCallback(progress);
}

[[nodiscard]] std::filesystem::path chooseStagingDirectory(const std::filesystem::path& packageDirectory,
                                                           const std::filesystem::path& finalAudioPath)
{
    const auto stagingRoot = packageDirectory / ".projectname-staging";
    const auto stem = sanitizeFileStem(finalAudioPath.stem().string());

    auto candidate = stagingRoot / ("audio-import-" + stem);
    auto suffix = 2;
    while (std::filesystem::exists(candidate))
    {
        candidate = stagingRoot / ("audio-import-" + stem + "-" + std::to_string(suffix));
        ++suffix;
    }

    return candidate;
}

void cleanupStagingDirectory(const std::filesystem::path& stagingDirectory) noexcept
{
    std::error_code ignored;
    std::filesystem::remove_all(stagingDirectory, ignored);

    const auto stagingRoot = stagingDirectory.parent_path();
    if (!stagingRoot.empty() && std::filesystem::is_empty(stagingRoot, ignored))
        std::filesystem::remove(stagingRoot, ignored);
}

[[nodiscard]] bool copyFileToStaging(const std::filesystem::path& sourcePath,
                                     const std::filesystem::path& stagedPath,
                                     const ProjectAudioImportOptions& options,
                                     std::string& error)
{
    if (isCancelRequested(options))
    {
        error = "Audio import was cancelled before staged copy.";
        return false;
    }

    std::error_code filesystemError;
    const auto totalBytes = std::filesystem::file_size(sourcePath, filesystemError);
    if (filesystemError)
    {
        error = "Could not inspect imported audio file size: " + filesystemError.message();
        return false;
    }

    std::ifstream input(sourcePath, std::ios::binary);
    if (!input)
    {
        error = "Could not open imported audio for staged copy.";
        return false;
    }

    std::ofstream output(stagedPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "Could not open staged audio copy for writing.";
        return false;
    }

    const auto configuredChunkBytes = std::max<std::size_t>(options.copyChunkBytes, 1U);
    std::vector<char> buffer(std::min<std::size_t>(configuredChunkBytes, 1024U * 1024U));

    std::uintmax_t bytesCopied = 0;
    reportProgress(options, ProjectAudioImportStage::copying, bytesCopied, totalBytes);

    while (input)
    {
        if (isCancelRequested(options))
        {
            error = "Audio import was cancelled during staged copy.";
            return false;
        }

        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = input.gcount();
        if (bytesRead <= 0)
            break;

        output.write(buffer.data(), bytesRead);
        if (!output)
        {
            error = "Staged imported audio write failed.";
            return false;
        }

        bytesCopied += static_cast<std::uintmax_t>(bytesRead);
        reportProgress(options, ProjectAudioImportStage::copying, bytesCopied, totalBytes);
    }

    if (!input.eof())
    {
        error = "Staged imported audio read failed.";
        return false;
    }

    output.close();
    if (!output)
    {
        error = "Staged imported audio finalization failed.";
        return false;
    }

    if (isCancelRequested(options))
    {
        error = "Audio import was cancelled after staged copy.";
        return false;
    }

    return true;
}
} // namespace

std::optional<ProjectAudioImportResult> commitPreparedAudioImportToProjectPackage(
    ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& sourceAudioPath,
    PreparedMonoAudioClip preparedClip,
    const ProjectAudioImportOptions& options,
    std::string& error)
{
    error.clear();

    std::error_code filesystemError;
    if (std::filesystem::is_regular_file(packageDirectory, filesystemError))
    {
        error = "Project package path points to a file.";
        return std::nullopt;
    }

    if (isCancelRequested(options))
    {
        error = "Audio import was cancelled before package commit.";
        return std::nullopt;
    }

    if (!validateRequestedStartBeats(options, error))
        return std::nullopt;

    const auto copiedAudioPath = chooseUniqueAudioPath(packageDirectory / "audio", sourceAudioPath);
    const auto waveformSummaryPath = chooseUniqueWaveformSummaryPath(packageDirectory / "analysis", copiedAudioPath);
    const auto stagingDirectory = chooseStagingDirectory(packageDirectory, copiedAudioPath);
    std::filesystem::create_directories(stagingDirectory, filesystemError);
    if (filesystemError)
    {
        error = "Could not create audio import staging folder: " + filesystemError.message();
        return std::nullopt;
    }

    const auto stagedAudioPath = stagingDirectory / copiedAudioPath.filename();
    if (!copyFileToStaging(sourceAudioPath, stagedAudioPath, options, error))
    {
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    if (isCancelRequested(options))
    {
        error = "Audio import was cancelled before waveform analysis.";
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    const auto stagedWaveformSummaryPath = stagingDirectory / waveformSummaryPath.filename();
    auto waveformSummary = buildWaveformSummary(preparedClip);
    if (!saveWaveformSummary(waveformSummary, stagedWaveformSummaryPath, error))
    {
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    if (isCancelRequested(options))
    {
        error = "Audio import was cancelled after waveform analysis.";
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    std::error_code stagedSizeError;
    const auto stagedBytes = std::filesystem::file_size(stagedAudioPath, stagedSizeError);
    const auto committedBytes = stagedSizeError ? 0U : stagedBytes;
    reportProgress(options,
                   ProjectAudioImportStage::committing,
                   committedBytes,
                   committedBytes);

    const auto audioDirectory = packageDirectory / "audio";
    const auto analysisDirectory = packageDirectory / "analysis";
    std::filesystem::create_directories(audioDirectory, filesystemError);
    if (filesystemError)
    {
        error = "Could not create project audio folder: " + filesystemError.message();
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    std::filesystem::create_directories(analysisDirectory, filesystemError);
    if (filesystemError)
    {
        error = "Could not create project analysis folder: " + filesystemError.message();
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    if (isCancelRequested(options))
    {
        error = "Audio import was cancelled before project package mutation.";
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    std::filesystem::rename(stagedAudioPath, copiedAudioPath, filesystemError);
    if (filesystemError)
    {
        error = "Could not commit imported audio into project package: " + filesystemError.message();
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    std::filesystem::rename(stagedWaveformSummaryPath, waveformSummaryPath, filesystemError);
    if (filesystemError)
    {
        error = "Could not commit waveform summary into project package: " + filesystemError.message();
        std::filesystem::remove(copiedAudioPath, filesystemError);
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    ProjectModel updatedProject = project;
    const auto trackId = findOrCreateImportTrack(updatedProject);
    const auto clipStartBeats = options.requestedStartBeats.value_or(
        calculateNextAvailableClipStartBeats(updatedProject, trackId));

    ProjectClip clip;
    clip.id = makeClipId(updatedProject);
    clip.name = makeClipName(sourceAudioPath);
    clip.type = "audio-file";
    clip.relativePath = (std::filesystem::path("audio") / copiedAudioPath.filename()).generic_string();
    clip.analysisPath = (std::filesystem::path("analysis") / waveformSummaryPath.filename()).generic_string();
    clip.startBeats = clipStartBeats;
    clip.lengthBeats = calculateLengthBeats(updatedProject, preparedClip);

    if (!updatedProject.addClipToTrack(trackId, clip))
    {
        error = "Could not attach imported audio clip to project track.";
        std::filesystem::remove(copiedAudioPath, filesystemError);
        std::filesystem::remove(waveformSummaryPath, filesystemError);
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    if (!updatedProject.savePackage(packageDirectory, error))
    {
        std::filesystem::remove(copiedAudioPath, filesystemError);
        std::filesystem::remove(waveformSummaryPath, filesystemError);
        cleanupStagingDirectory(stagingDirectory);
        return std::nullopt;
    }

    project = std::move(updatedProject);
    cleanupStagingDirectory(stagingDirectory);
    reportProgress(options, ProjectAudioImportStage::completed, committedBytes, committedBytes);

    ProjectAudioImportResult result;
    result.clip = std::move(clip);
    result.preparedClip = std::move(preparedClip);
    result.copiedAudioPath = copiedAudioPath;
    result.waveformSummaryPath = waveformSummaryPath;
    return result;
}

std::optional<ProjectAudioImportResult> importPcm16WavIntoProjectPackage(
    ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& sourceWavPath,
    const ProjectAudioImportOptions& options,
    std::string& error)
{
    error.clear();

    WavDecodeOptions decodeOptions;
    decodeOptions.cancelRequested = options.cancelRequested;
    decodeOptions.progressCallback = options.decodeProgressCallback;
    auto preparedClip = loadPcm16WavAsPreparedMonoClip(sourceWavPath, decodeOptions, error);
    if (!preparedClip.has_value())
        return std::nullopt;

    return commitPreparedAudioImportToProjectPackage(project,
                                                     packageDirectory,
                                                     sourceWavPath,
                                                     std::move(*preparedClip),
                                                     options,
                                                     error);
}

std::optional<ProjectAudioImportResult> importPcm16WavIntoProjectPackage(
    ProjectModel& project,
    const std::filesystem::path& packageDirectory,
    const std::filesystem::path& sourceWavPath,
    std::string& error)
{
    ProjectAudioImportOptions options;
    return importPcm16WavIntoProjectPackage(project, packageDirectory, sourceWavPath, options, error);
}
} // namespace projectname
