// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "AppSession.h"
#include "ImportedClipInspectorEditDraft.h"
#include "ProjectModel.h"
#include "WavAudioImporter.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace projectname
{
enum class ImportedClipMediaRelinkPreparationStage
{
    preparing,
    decoding,
    copying,
    analysing,
    prepared,
};

struct ImportedClipMediaRelinkProgress
{
    ImportedClipMediaRelinkPreparationStage stage =
        ImportedClipMediaRelinkPreparationStage::preparing;
    std::uintmax_t bytesCopied = 0;
    std::uintmax_t totalBytes = 0;
    int percent = 0;
};

struct ImportedClipMediaRelinkPreparationRequest
{
    ProjectModel project;
    std::filesystem::path packageDirectory;
    std::filesystem::path sourceWavPath;
    std::string selectedClipId;
    std::atomic_bool* cancelRequested = nullptr;
    std::function<void(const WavDecodeProgress&)> decodeProgressCallback;
    std::function<void(const ImportedClipMediaRelinkProgress&)> progressCallback;
    std::size_t copyChunkBytes = 64U * 1024U;
};

struct ImportedClipMediaRelinkPreparation
{
    std::string clipId;
    std::string relativePath;
    std::string analysisPath;
    double lengthBeats = 0.0;
    PreparedMonoAudioClip preparedClip;
    std::filesystem::path stagingDirectory;
    std::filesystem::path stagedAudioPath;
    std::filesystem::path stagedAnalysisPath;
    std::filesystem::path finalAudioPath;
    std::filesystem::path finalAnalysisPath;
};

enum class ImportedClipMediaRelinkPreparationStatus
{
    prepared,
    invalidSelection,
    decodeFailed,
    cancelled,
    packageError,
};

struct ImportedClipMediaRelinkPreparationResult
{
    ImportedClipMediaRelinkPreparationStatus status =
        ImportedClipMediaRelinkPreparationStatus::packageError;
    std::optional<ImportedClipMediaRelinkPreparation> preparation;
    std::string error;
};

enum class ImportedClipMediaRelinkCommitStatus
{
    committed,
    staleSelection,
    packageError,
    sessionError,
};

struct ImportedClipMediaRelinkCommitResult
{
    ImportedClipMediaRelinkCommitStatus status =
        ImportedClipMediaRelinkCommitStatus::packageError;
    std::optional<ImportedClipMediaRelinkCommit> commit;
    std::string error;
};

[[nodiscard]] ImportedClipMediaRelinkPreparationResult prepareImportedClipMediaRelink(
    ImportedClipMediaRelinkPreparationRequest request);

void discardPreparedImportedClipMediaRelink(
    const ImportedClipMediaRelinkPreparation& preparation) noexcept;

[[nodiscard]] ImportedClipMediaRelinkCommitResult commitPreparedImportedClipMediaRelink(
    AppSession& session,
    const ImportedClipMediaRelinkPreparation& preparation);
} // namespace projectname
