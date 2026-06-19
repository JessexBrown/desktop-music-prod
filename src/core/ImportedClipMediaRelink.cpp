// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ImportedClipMediaRelink.h"

#include "WaveformSummary.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
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

    return sanitized.empty() ? std::string("relinked-audio") : sanitized;
}

[[nodiscard]] bool isImportedAudioClip(const ProjectClip& clip) noexcept
{
    return clip.type == "audio-file";
}

[[nodiscard]] const ProjectClip* findImportedAudioClipById(const ProjectModel& project,
                                                           const std::string& clipId) noexcept
{
    if (clipId.empty())
        return nullptr;

    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (clip.id == clipId && isImportedAudioClip(clip))
                return &clip;
        }
    }

    return nullptr;
}

[[nodiscard]] bool isCancelRequested(const ImportedClipMediaRelinkPreparationRequest& request) noexcept
{
    return request.cancelRequested != nullptr
        && request.cancelRequested->load(std::memory_order_acquire);
}

void reportProgress(const ImportedClipMediaRelinkPreparationRequest& request,
                    ImportedClipMediaRelinkPreparationStage stage,
                    std::uintmax_t bytesCopied,
                    std::uintmax_t totalBytes)
{
    if (!request.progressCallback)
        return;

    ImportedClipMediaRelinkProgress progress;
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
        progress.percent = stage == ImportedClipMediaRelinkPreparationStage::prepared ? 100 : 0;
    }

    request.progressCallback(progress);
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

[[nodiscard]] std::filesystem::path chooseStagingDirectory(
    const std::filesystem::path& packageDirectory)
{
    const auto stagingRoot = packageDirectory / ".projectname-staging";

    auto candidate = stagingRoot / "media-relink";
    auto suffix = 2;
    while (std::filesystem::exists(candidate))
    {
        candidate = stagingRoot / ("media-relink-" + std::to_string(suffix));
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

[[nodiscard]] ImportedClipMediaRelinkPreparationResult makePreparationError(
    ImportedClipMediaRelinkPreparationStatus status,
    std::string error)
{
    ImportedClipMediaRelinkPreparationResult result;
    result.status = status;
    result.error = std::move(error);
    return result;
}

[[nodiscard]] ImportedClipMediaRelinkCommitResult makeCommitError(
    ImportedClipMediaRelinkCommitStatus status,
    std::string error)
{
    ImportedClipMediaRelinkCommitResult result;
    result.status = status;
    result.error = std::move(error);
    return result;
}

[[nodiscard]] double calculateLengthBeats(const ProjectModel& project,
                                          const PreparedMonoAudioClip& preparedClip) noexcept
{
    if (preparedClip.sampleRateHz <= 0.0 || preparedClip.frameCount <= 0)
        return 0.0;

    const auto seconds = static_cast<double>(preparedClip.frameCount) / preparedClip.sampleRateHz;
    const auto beats = seconds * (project.getTransport().getTempoBpm() / 60.0);
    return std::isfinite(beats) && beats > 0.0 ? beats : 0.0;
}

[[nodiscard]] bool copyFileToStaging(const std::filesystem::path& sourcePath,
                                     const std::filesystem::path& stagedPath,
                                     const ImportedClipMediaRelinkPreparationRequest& request,
                                     std::string& error)
{
    if (isCancelRequested(request))
    {
        error = "Media relink was cancelled before staged copy.";
        return false;
    }

    std::error_code filesystemError;
    const auto totalBytes = std::filesystem::file_size(sourcePath, filesystemError);
    if (filesystemError)
    {
        error = "Could not inspect relink audio file size: " + filesystemError.message();
        return false;
    }

    std::ifstream input(sourcePath, std::ios::binary);
    if (!input)
    {
        error = "Could not open relink audio for staged copy.";
        return false;
    }

    std::ofstream output(stagedPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        error = "Could not open staged relink audio copy for writing.";
        return false;
    }

    const auto configuredChunkBytes = std::max<std::size_t>(request.copyChunkBytes, 1U);
    std::vector<char> buffer(std::min<std::size_t>(configuredChunkBytes, 1024U * 1024U));

    std::uintmax_t bytesCopied = 0;
    reportProgress(request, ImportedClipMediaRelinkPreparationStage::copying, bytesCopied, totalBytes);

    while (input)
    {
        if (isCancelRequested(request))
        {
            error = "Media relink was cancelled during staged copy.";
            return false;
        }

        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = input.gcount();
        if (bytesRead <= 0)
            break;

        output.write(buffer.data(), bytesRead);
        if (!output)
        {
            error = "Staged relink audio write failed.";
            return false;
        }

        bytesCopied += static_cast<std::uintmax_t>(bytesRead);
        reportProgress(request, ImportedClipMediaRelinkPreparationStage::copying, bytesCopied, totalBytes);
    }

    if (!input.eof())
    {
        error = "Staged relink audio read failed.";
        return false;
    }

    output.close();
    if (!output)
    {
        error = "Staged relink audio finalization failed.";
        return false;
    }

    if (isCancelRequested(request))
    {
        error = "Media relink was cancelled after staged copy.";
        return false;
    }

    return true;
}

[[nodiscard]] bool commitStagedRelinkFiles(const ImportedClipMediaRelinkPreparation& preparation,
                                           std::string& error)
{
    std::error_code filesystemError;
    std::filesystem::create_directories(preparation.finalAudioPath.parent_path(), filesystemError);
    if (filesystemError)
    {
        error = "Could not create project audio folder for relink: " + filesystemError.message();
        return false;
    }

    std::filesystem::create_directories(preparation.finalAnalysisPath.parent_path(), filesystemError);
    if (filesystemError)
    {
        error = "Could not create project analysis folder for relink: " + filesystemError.message();
        return false;
    }

    if (std::filesystem::exists(preparation.finalAudioPath, filesystemError))
    {
        error = "Relink audio target already exists.";
        return false;
    }

    filesystemError.clear();
    if (std::filesystem::exists(preparation.finalAnalysisPath, filesystemError))
    {
        error = "Relink analysis target already exists.";
        return false;
    }

    filesystemError.clear();
    std::filesystem::rename(preparation.stagedAudioPath, preparation.finalAudioPath, filesystemError);
    if (filesystemError)
    {
        error = "Could not commit relink audio into project package: " + filesystemError.message();
        return false;
    }

    std::filesystem::rename(preparation.stagedAnalysisPath, preparation.finalAnalysisPath, filesystemError);
    if (filesystemError)
    {
        error = "Could not commit relink waveform summary into project package: " + filesystemError.message();
        std::filesystem::remove(preparation.finalAudioPath, filesystemError);
        return false;
    }

    return true;
}

void cleanupCommittedRelinkFiles(const ImportedClipMediaRelinkPreparation& preparation) noexcept
{
    std::error_code ignored;
    std::filesystem::remove(preparation.finalAudioPath, ignored);
    std::filesystem::remove(preparation.finalAnalysisPath, ignored);
}
} // namespace

ImportedClipMediaRelinkPreparationResult prepareImportedClipMediaRelink(
    ImportedClipMediaRelinkPreparationRequest request)
{
    if (request.selectedClipId.empty())
    {
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::invalidSelection,
                                    "Selected imported clip id is required for media relink.");
    }

    if (request.project.getSelectedClipId() != request.selectedClipId)
    {
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::invalidSelection,
                                    "Media relink requires the selected imported clip to remain selected.");
    }

    if (findImportedAudioClipById(request.project, request.selectedClipId) == nullptr)
    {
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::invalidSelection,
                                    "Selected imported audio clip was not found for media relink.");
    }

    if (std::filesystem::is_regular_file(request.packageDirectory))
    {
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::packageError,
                                    "Project package path points to a file.");
    }

    if (isCancelRequested(request))
    {
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::cancelled,
                                    "Media relink was cancelled before it started.");
    }

    reportProgress(request, ImportedClipMediaRelinkPreparationStage::decoding, 0, 0);

    std::string error;
    WavDecodeOptions decodeOptions;
    decodeOptions.cancelRequested = request.cancelRequested;
    decodeOptions.progressCallback = request.decodeProgressCallback;
    auto preparedClip = loadPcm16WavAsPreparedMonoClip(request.sourceWavPath, decodeOptions, error);
    if (!preparedClip.has_value())
    {
        const auto cancelled = isCancelRequested(request);
        return makePreparationError(
            cancelled ? ImportedClipMediaRelinkPreparationStatus::cancelled
                      : ImportedClipMediaRelinkPreparationStatus::decodeFailed,
            error.empty() ? "Could not decode relink WAV source." : error);
    }

    if (isCancelRequested(request))
    {
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::cancelled,
                                    "Media relink was cancelled after decode.");
    }

    const auto lengthBeats = calculateLengthBeats(request.project, *preparedClip);
    if (!std::isfinite(lengthBeats) || lengthBeats <= 0.0)
    {
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::decodeFailed,
                                    "Relink WAV source produced invalid clip length.");
    }

    const auto finalAudioPath = chooseUniqueAudioPath(request.packageDirectory / "audio",
                                                     request.sourceWavPath);
    const auto finalAnalysisPath = chooseUniqueWaveformSummaryPath(request.packageDirectory / "analysis",
                                                                   finalAudioPath);
    const auto stagingDirectory = chooseStagingDirectory(request.packageDirectory);

    std::error_code filesystemError;
    std::filesystem::create_directories(stagingDirectory, filesystemError);
    if (filesystemError)
    {
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::packageError,
                                    "Could not create media relink staging folder: "
                                        + filesystemError.message());
    }

    const auto stagedAudioPath = stagingDirectory / finalAudioPath.filename();
    if (!copyFileToStaging(request.sourceWavPath, stagedAudioPath, request, error))
    {
        cleanupStagingDirectory(stagingDirectory);
        return makePreparationError(
            isCancelRequested(request) ? ImportedClipMediaRelinkPreparationStatus::cancelled
                                       : ImportedClipMediaRelinkPreparationStatus::packageError,
            error);
    }

    if (isCancelRequested(request))
    {
        cleanupStagingDirectory(stagingDirectory);
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::cancelled,
                                    "Media relink was cancelled before waveform analysis.");
    }

    reportProgress(request, ImportedClipMediaRelinkPreparationStage::analysing, 0, 0);
    const auto stagedAnalysisPath = stagingDirectory / finalAnalysisPath.filename();
    auto waveformSummary = buildWaveformSummary(*preparedClip);
    if (!saveWaveformSummary(waveformSummary, stagedAnalysisPath, error))
    {
        cleanupStagingDirectory(stagingDirectory);
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::packageError,
                                    error);
    }

    if (isCancelRequested(request))
    {
        cleanupStagingDirectory(stagingDirectory);
        return makePreparationError(ImportedClipMediaRelinkPreparationStatus::cancelled,
                                    "Media relink was cancelled after waveform analysis.");
    }

    reportProgress(request, ImportedClipMediaRelinkPreparationStage::prepared, 0, 0);

    ImportedClipMediaRelinkPreparation preparation;
    preparation.clipId = request.selectedClipId;
    preparation.relativePath = (std::filesystem::path("audio") / finalAudioPath.filename()).generic_string();
    preparation.analysisPath = (std::filesystem::path("analysis") / finalAnalysisPath.filename()).generic_string();
    preparation.lengthBeats = lengthBeats;
    preparation.preparedClip = std::move(*preparedClip);
    preparation.stagingDirectory = stagingDirectory;
    preparation.stagedAudioPath = stagedAudioPath;
    preparation.stagedAnalysisPath = stagedAnalysisPath;
    preparation.finalAudioPath = finalAudioPath;
    preparation.finalAnalysisPath = finalAnalysisPath;

    ImportedClipMediaRelinkPreparationResult result;
    result.status = ImportedClipMediaRelinkPreparationStatus::prepared;
    result.preparation = std::move(preparation);
    return result;
}

void discardPreparedImportedClipMediaRelink(
    const ImportedClipMediaRelinkPreparation& preparation) noexcept
{
    cleanupStagingDirectory(preparation.stagingDirectory);
}

ImportedClipMediaRelinkCommitResult commitPreparedImportedClipMediaRelink(
    AppSession& session,
    const ImportedClipMediaRelinkPreparation& preparation)
{
    if (session.getSelectedClipId() != preparation.clipId
        || findImportedAudioClipById(session.getProject(), preparation.clipId) == nullptr)
    {
        discardPreparedImportedClipMediaRelink(preparation);
        return makeCommitError(ImportedClipMediaRelinkCommitStatus::staleSelection,
                               "Selected imported audio clip changed before media relink commit.");
    }

    std::string error;
    if (!commitStagedRelinkFiles(preparation, error))
    {
        discardPreparedImportedClipMediaRelink(preparation);
        return makeCommitError(ImportedClipMediaRelinkCommitStatus::packageError, error);
    }

    if (!session.replaceImportedAudioClipMedia(preparation.clipId,
                                               preparation.relativePath,
                                               preparation.analysisPath,
                                               preparation.lengthBeats,
                                               error))
    {
        cleanupCommittedRelinkFiles(preparation);
        discardPreparedImportedClipMediaRelink(preparation);
        return makeCommitError(ImportedClipMediaRelinkCommitStatus::sessionError,
                               error.empty() ? "Media relink session commit failed." : error);
    }

    discardPreparedImportedClipMediaRelink(preparation);

    ImportedClipMediaRelinkCommitResult result;
    result.status = ImportedClipMediaRelinkCommitStatus::committed;
    result.commit = ImportedClipMediaRelinkCommit {
        preparation.clipId,
        preparation.relativePath,
        preparation.analysisPath,
        preparation.lengthBeats,
    };
    return result;
}
} // namespace projectname
