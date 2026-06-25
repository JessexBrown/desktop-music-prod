// SPDX-License-Identifier: AGPL-3.0-or-later

#include "core/AppSession.h"
#include "core/AppCommandRegistry.h"
#include "core/AppSettings.h"
#include "core/AudioSetupStatus.h"
#include "core/AudioEngineStub.h"
#include "core/BackgroundAudioImportJob.h"
#include "core/BackgroundMediaRelinkPreparationJob.h"
#include "core/BackgroundPackageMediaCleanupJob.h"
#include "core/BackgroundSaveAsPackageCopyJob.h"
#include "core/BackgroundTimelinePlaybackPreparationJob.h"
#include "core/BackgroundWaveformAnalysisJob.h"
#include "core/ImportedClipInspectorEditDraft.h"
#include "core/ImportedClipMediaRelink.h"
#include "core/ImportedClipInspector.h"
#include "core/ImportedMediaPackageInventory.h"
#include "core/PackageMediaCleanupBatchDiscovery.h"
#include "core/PackageMediaCleanupStatus.h"
#include "core/PackageMediaMaintenanceBrowserRows.h"
#include "core/PackageMediaMaintenanceViewModel.h"
#include "core/PackageMediaQuarantineCommand.h"
#include "core/PackageMediaQuarantinePreflightPlan.h"
#include "core/PackageMediaQuarantineRestoreManifest.h"
#include "core/PackageMediaRestoreEntrySelection.h"
#include "core/ProductIdentity.h"
#include "core/ProjectAudioImport.h"
#include "core/ProjectModel.h"
#include "core/ProjectPackageSaveAsPolicy.h"
#include "core/ProjectPackageSaveAsRetry.h"
#include "core/TimelineClipLane.h"
#include "core/TimelinePlaybackPlan.h"
#include "core/TimelinePlaybackPreparationCompletion.h"
#include "core/ToneRenderer.h"
#include "core/TrackVoiceSchedule.h"
#include "core/TransportState.h"
#include "core/WaveformAnalysisRegenerator.h"
#include "core/WaveformSummary.h"
#include "core/WaveformThumbnail.h"
#include "core/WorkspaceCommandRouter.h"
#include "core/WavAudioImporter.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect(bool condition, const std::string& message)
{
    expect(condition, message.c_str());
}

std::string makeTemporaryPathSuffix()
{
    static std::atomic_uint64_t sequence { 0 };

    const auto counter = sequence.fetch_add(1, std::memory_order_relaxed);
    const auto randomValue = std::random_device {}();
    return std::to_string(counter) + "-" + std::to_string(randomValue);
}

std::filesystem::path makeTemporaryRootPath()
{
    auto root = std::filesystem::temp_directory_path();

    std::error_code filesystemError;
    auto canonicalRoot = std::filesystem::weakly_canonical(root, filesystemError);
    if (!filesystemError && !canonicalRoot.empty())
        root = std::move(canonicalRoot);

    return root;
}

std::filesystem::path makeTemporaryPackagePath(const std::string& prefix)
{
    return makeTemporaryRootPath() / (prefix + "-" + makeTemporaryPathSuffix() + ".project");
}

std::filesystem::path makeTemporaryAudioPath(const std::string& prefix)
{
    return makeTemporaryRootPath() / (prefix + "-" + makeTemporaryPathSuffix() + ".wav");
}

std::filesystem::path makeTemporarySettingsPath(const std::string& prefix)
{
    return makeTemporaryRootPath() / (prefix + "-" + makeTemporaryPathSuffix() + ".json");
}

void writeManifestText(const std::filesystem::path& package, const std::string& manifestText)
{
    std::filesystem::create_directories(package);
    std::ofstream manifest(package / "manifest.json", std::ios::trunc);
    manifest << manifestText;
}

void writeTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    file << text;
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    std::string text;
    std::string line;

    while (std::getline(file, line))
        text += line;

    return text;
}

const projectname::ProjectPackageSaveAsFolderPolicy* findSaveAsFolderPolicy(
    const projectname::ProjectPackageSaveAsPlan& plan,
    const std::string& folderName)
{
    const auto match = std::find_if(plan.folders.begin(),
                                    plan.folders.end(),
                                    [&folderName](const projectname::ProjectPackageSaveAsFolderPolicy& folder)
                                    {
                                        return folder.folderName == folderName;
                                    });

    return match == plan.folders.end() ? nullptr : &*match;
}

const projectname::ProjectPackageSaveAsReference* findSaveAsReference(
    const projectname::ProjectPackageSaveAsPlan& plan,
    const std::string& path)
{
    const auto match = std::find_if(plan.references.begin(),
                                    plan.references.end(),
                                    [&path](const projectname::ProjectPackageSaveAsReference& reference)
                                    {
                                        return reference.path == path;
                                    });

    return match == plan.references.end() ? nullptr : &*match;
}

const projectname::ProjectClip* findClipById(const projectname::ProjectModel& project, const std::string& clipId)
{
    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (clip.id == clipId)
                return &clip;
        }
    }

    return nullptr;
}

bool hasInventoryReferenceSource(const std::vector<projectname::ImportedMediaPackageReferenceSource>& sources,
                                 projectname::ImportedMediaPackageReferenceSource source)
{
    return std::find(sources.begin(), sources.end(), source) != sources.end();
}

const projectname::ImportedMediaPackageAsset* findInventoryAsset(
    const projectname::ImportedMediaPackageInventory& inventory,
    projectname::ImportedMediaPackageAssetKind kind,
    const std::string& relativePath)
{
    for (const auto& asset : inventory.assets)
    {
        if (asset.kind == kind && asset.relativePath == relativePath)
            return &asset;
    }

    return nullptr;
}

const projectname::ImportedMediaPackageMissingReference* findInventoryMissingReference(
    const projectname::ImportedMediaPackageInventory& inventory,
    projectname::ImportedMediaPackageAssetKind kind,
    const std::string& relativePath)
{
    for (const auto& reference : inventory.missingReferences)
    {
        if (reference.kind == kind && reference.relativePath == relativePath)
            return &reference;
    }

    return nullptr;
}

bool hasUnsafeInventoryReference(const projectname::ImportedMediaPackageInventory& inventory,
                                 projectname::ImportedMediaPackageAssetKind kind,
                                 const std::string& relativePath)
{
    for (const auto& reference : inventory.unsafeReferences)
    {
        if (reference.kind == kind && reference.relativePath == relativePath)
            return true;
    }

    return false;
}

projectname::PackageMediaQuarantineRestoreManifest makeTestQuarantineRestoreManifest(
    const std::string& cleanupId)
{
    projectname::PackageMediaQuarantineRestoreManifest manifest;
    manifest.cleanupId = cleanupId;
    manifest.createdAtUtc = "2026-06-18T18-30-00Z";
    manifest.packageDisplayPath = "Display Only.project";
    manifest.inventorySummary = "1 audio, 1 analysis, 1 skipped";
    manifest.manifestMarker = "manifest-save-1";

    projectname::PackageMediaQuarantineMovedEntry audio;
    audio.kind = projectname::PackageMediaQuarantineEntryKind::audio;
    audio.originalRelativePath = "audio/orphan.wav";
    audio.quarantineRelativePath = "backups/media-trash/" + cleanupId + "/audio/orphan.wav";
    audio.byteSize = 1234;
    audio.contentHash = "sha256:abc";
    manifest.movedEntries.push_back(std::move(audio));

    projectname::PackageMediaQuarantineMovedEntry analysis;
    analysis.kind = projectname::PackageMediaQuarantineEntryKind::analysis;
    analysis.originalRelativePath = "analysis/orphan.waveform.json";
    analysis.quarantineRelativePath =
        "backups/media-trash/" + cleanupId + "/analysis/orphan.waveform.json";
    manifest.movedEntries.push_back(std::move(analysis));

    projectname::PackageMediaQuarantineSkippedEntry skipped;
    skipped.kind = projectname::PackageMediaQuarantineEntryKind::audio;
    skipped.originalRelativePath = "audio/current.wav";
    skipped.reason = "current-manifest-reference";
    skipped.detail = "Protected by current manifest";
    manifest.skippedEntries.push_back(std::move(skipped));

    return manifest;
}

const projectname::PackageMediaQuarantineMovedEntry* findQuarantineMovedEntry(
    const projectname::PackageMediaQuarantineRestoreManifest& manifest,
    projectname::PackageMediaQuarantineEntryKind kind,
    const std::string& originalPath)
{
    for (const auto& entry : manifest.movedEntries)
    {
        if (entry.kind == kind && entry.originalRelativePath == originalPath)
            return &entry;
    }

    return nullptr;
}

const projectname::PackageMediaQuarantineSkippedEntry* findQuarantineSkippedEntry(
    const projectname::PackageMediaQuarantineRestoreManifest& manifest,
    const std::string& originalPath)
{
    for (const auto& entry : manifest.skippedEntries)
    {
        if (entry.originalRelativePath == originalPath)
            return &entry;
    }

    return nullptr;
}

const projectname::PackageMediaRestoreEntrySelectionItem* findRestoreSelectionEntry(
    const projectname::PackageMediaRestoreEntrySelection& selection,
    const std::string& originalPath)
{
    for (const auto& entry : selection.entries)
    {
        if (entry.originalRelativePath == originalPath)
            return &entry;
    }

    return nullptr;
}

bool hasCleanupBatchDiscoveryIssue(
    const projectname::PackageMediaCleanupBatchDiscoveryResult& result,
    projectname::PackageMediaCleanupBatchDiscoveryIssueKind kind,
    const std::string& cleanupId)
{
    for (const auto& issue : result.issues)
    {
        if (issue.kind == kind && issue.cleanupId == cleanupId)
            return true;
    }

    return false;
}

const projectname::PackageMediaMaintenanceBrowserRow* findMaintenanceBrowserRow(
    const projectname::PackageMediaMaintenanceBrowserRows& rows,
    projectname::PackageMediaMaintenanceBrowserRowKind kind)
{
    for (const auto& row : rows.rows)
    {
        if (row.kind == kind)
            return &row;
    }

    return nullptr;
}

const projectname::PackageMediaMaintenanceBrowserRow* findMaintenanceBrowserBatchRow(
    const projectname::PackageMediaMaintenanceBrowserRows& rows,
    const std::string& cleanupId)
{
    for (const auto& row : rows.rows)
    {
        if (row.kind == projectname::PackageMediaMaintenanceBrowserRowKind::batch
            && row.cleanupId == cleanupId)
        {
            return &row;
        }
    }

    return nullptr;
}

const projectname::PackageMediaMaintenanceBrowserRow* findMaintenanceBrowserRestoreEntryRow(
    const projectname::PackageMediaMaintenanceBrowserRows& rows,
    const std::string& originalRelativePath)
{
    for (const auto& row : rows.rows)
    {
        if (row.kind == projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath
            && row.restoreOriginalRelativePath == originalRelativePath)
        {
            return &row;
        }
    }

    return nullptr;
}

std::size_t countMaintenanceBrowserRows(
    const projectname::PackageMediaMaintenanceBrowserRows& rows,
    projectname::PackageMediaMaintenanceBrowserRowKind kind)
{
    return static_cast<std::size_t>(
        std::count_if(rows.rows.begin(),
                      rows.rows.end(),
                      [kind](const projectname::PackageMediaMaintenanceBrowserRow& row)
                      {
                          return row.kind == kind;
                      }));
}

bool hasMaintenanceBrowserRowText(
    const projectname::PackageMediaMaintenanceBrowserRows& rows,
    projectname::PackageMediaMaintenanceBrowserRowKind kind,
    const std::string& needle)
{
    for (const auto& row : rows.rows)
    {
        if (row.kind == kind && row.text.find(needle) != std::string::npos)
            return true;
    }

    return false;
}

bool hasMaintenanceBrowserDetailAction(
    const projectname::PackageMediaMaintenanceBrowserRow& row,
    projectname::PackageMediaMaintenanceDetailActionKind kind,
    const std::string& value)
{
    return std::any_of(row.detailActions.begin(),
                       row.detailActions.end(),
                       [kind, &value](const projectname::PackageMediaMaintenanceDetailAction& action)
                       {
                           return action.kind == kind && action.value == value;
                       });
}

bool hasMaintenanceBrowserFocusedDetailAction(
    const projectname::PackageMediaMaintenanceBrowserRows& rows,
    projectname::PackageMediaMaintenanceDetailActionKind kind,
    const std::string& value)
{
    return std::any_of(rows.focusedDetailActions.begin(),
                       rows.focusedDetailActions.end(),
                       [kind, &value](const projectname::PackageMediaMaintenanceDetailAction& action)
                       {
                           return action.kind == kind && action.value == value;
                       });
}

std::size_t countSelectableMaintenanceBrowserRows(
    const projectname::PackageMediaMaintenanceBrowserRows& rows)
{
    return static_cast<std::size_t>(
        std::count_if(rows.rows.begin(),
                      rows.rows.end(),
                      [](const projectname::PackageMediaMaintenanceBrowserRow& row)
                      {
                          return row.selectable;
                      }));
}

void saveTestCleanupBatchManifest(
    const std::filesystem::path& package,
    const std::string& cleanupId,
    const std::string& createdAtUtc,
    projectname::PackageMediaQuarantineManifestState state)
{
    auto manifest = makeTestQuarantineRestoreManifest(cleanupId);
    manifest.createdAtUtc = createdAtUtc;
    manifest.state = state;

    if (state == projectname::PackageMediaQuarantineManifestState::restored)
    {
        for (auto& entry : manifest.movedEntries)
            entry.restored = true;
    }
    else if (state == projectname::PackageMediaQuarantineManifestState::restoreConflict)
    {
        manifest.movedEntries.front().restoreConflict = true;
    }
    else if (state == projectname::PackageMediaQuarantineManifestState::partialFailure)
    {
        manifest.error = "Restore batch needs review.";
        manifest.movedEntries.front().error = "Quarantine path is missing.";
    }

    const auto manifestPath =
        package / "backups" / "media-trash" / cleanupId / "restore-manifest.json";
    std::filesystem::create_directories(manifestPath.parent_path());
    std::string error;
    expect(projectname::savePackageMediaQuarantineRestoreManifest(manifest, manifestPath, error),
           "Test cleanup batch manifest saves");
}

projectname::ImportedMediaPackageInventory makePreflightInventoryWithCandidates(
    const std::filesystem::path& package)
{
    writeTextFile(package / "audio" / "orphan.wav", "orphan-audio");
    writeTextFile(package / "analysis" / "orphan.waveform.json", "{}");
    writeTextFile(package / ".projectname-staging" / "audio-import-a" / "staged.wav", "staged");

    projectname::ImportedMediaPackageInventory inventory;

    projectname::ImportedMediaPackageAsset audio;
    audio.kind = projectname::ImportedMediaPackageAssetKind::audio;
    audio.relativePath = "audio/orphan.wav";
    audio.absolutePath = package / "audio" / "orphan.wav";
    audio.unreferencedCandidate = true;
    inventory.assets.push_back(std::move(audio));

    projectname::ImportedMediaPackageAsset analysis;
    analysis.kind = projectname::ImportedMediaPackageAssetKind::analysis;
    analysis.relativePath = "analysis/orphan.waveform.json";
    analysis.absolutePath = package / "analysis" / "orphan.waveform.json";
    analysis.unreferencedCandidate = true;
    inventory.assets.push_back(std::move(analysis));

    projectname::ImportedMediaPackageAsset protectedAudio;
    protectedAudio.kind = projectname::ImportedMediaPackageAssetKind::audio;
    protectedAudio.relativePath = "audio/current.wav";
    protectedAudio.absolutePath = package / "audio" / "current.wav";
    protectedAudio.referenceSources.push_back(
        projectname::ImportedMediaPackageReferenceSource::currentManifest);
    protectedAudio.unreferencedCandidate = false;
    inventory.assets.push_back(std::move(protectedAudio));

    projectname::ImportedMediaPackageStagingDirectory staging;
    staging.relativePath = ".projectname-staging/audio-import-a";
    staging.absolutePath = package / ".projectname-staging" / "audio-import-a";
    staging.staleCandidate = true;
    inventory.stagingDirectories.push_back(std::move(staging));

    return inventory;
}

projectname::PackageMediaQuarantinePreflightRequest makePreflightRequest(
    projectname::ImportedMediaPackageInventory inventory,
    std::string cleanupId = "2026-06-18T19-00-00Z-test")
{
    projectname::PackageMediaQuarantinePreflightRequest request;
    request.inventory = std::move(inventory);
    request.cleanupId = std::move(cleanupId);
    request.createdAtUtc = "2026-06-18T19-00-00Z";
    request.packageDisplayPath = "Display Only.project";
    request.manifestMarker = "manifest-save-2";
    return request;
}

void writePackageMediaCleanupJobFixture(const std::filesystem::path& package)
{
    writeManifestText(package, R"({ "tracks": [] })");
    writeTextFile(package / "audio" / "orphan.wav", "orphan-audio");
    writeTextFile(package / "analysis" / "orphan.waveform.json", "{}");
    writeTextFile(package / ".projectname-staging" / "audio-import-a" / "staged.wav", "staged");
}

projectname::BackgroundPackageMediaCleanupRequest makeBackgroundCleanupQuarantineRequest(
    const std::filesystem::path& package,
    std::string cleanupId = "2026-06-18T22-00-00Z-test")
{
    projectname::BackgroundPackageMediaCleanupRequest request;
    request.operation = projectname::BackgroundPackageMediaCleanupOperation::quarantine;
    request.packageDirectory = package;
    request.cleanupId = std::move(cleanupId);
    request.createdAtUtc = "2026-06-18T22-00-00Z";
    request.packageDisplayPath = "Display Only.project";
    request.manifestMarker = "manifest-save-3";
    return request;
}

projectname::ProjectModel makeProjectWithImportedTimelineClip(double startBeats, double lengthBeats)
{
    auto project = projectname::ProjectModel::createDefault();
    project.getTransport().setTempoBpm(120.0);

    projectname::ProjectTrack track;
    track.id = "track-imported-playback";
    track.name = "Imported Playback";
    track.type = "audio";

    projectname::ProjectClip clip;
    clip.id = "clip-imported-playback";
    clip.name = "Timeline Clip";
    clip.type = "audio-file";
    clip.relativePath = "audio/timeline-clip.wav";
    clip.analysisPath = "analysis/timeline-clip.waveform.json";
    clip.startBeats = startBeats;
    clip.lengthBeats = lengthBeats;
    track.clips.push_back(clip);

    project.addTrack(std::move(track));
    return project;
}

projectname::ImportedClipInspectorState makeImportedClipInspectorEditState(bool usingSelectedClip)
{
    projectname::ImportedClipInspectorState state;
    state.status = projectname::ImportedClipInspectorStatus::ready;
    state.trackId = "track-imported-playback";
    state.trackName = "Imported Playback";
    state.clipId = "clip-imported-playback";
    state.clipName = "Timeline Clip";
    state.relativePath = "audio/timeline-clip.wav";
    state.analysisPath = "analysis/timeline-clip.waveform.json";
    state.startBeats = 4.0;
    state.lengthBeats = 2.0;
    state.usingSelectedClip = usingSelectedClip;
    return state;
}

projectname::ProjectClip makeImportedTimelineCacheClip(int index, double startBeats)
{
    projectname::ProjectClip clip;
    clip.id = "clip-cache-" + std::to_string(index);
    clip.name = "Cache Clip " + std::to_string(index);
    clip.type = "audio-file";
    clip.relativePath = "audio/cache-" + std::to_string(index) + ".wav";
    clip.analysisPath = "analysis/cache-" + std::to_string(index) + ".waveform.json";
    clip.startBeats = startBeats;
    clip.lengthBeats = 1.0;
    return clip;
}

projectname::PreparedMonoAudioClip makePreparedMonoCacheClip(std::vector<float> samples,
                                                             double sampleRateHz = 10.0)
{
    projectname::PreparedMonoAudioClip preparedClip;
    preparedClip.sampleRateHz = sampleRateHz;
    preparedClip.sourceChannelCount = 1;
    preparedClip.frameCount = static_cast<std::int64_t>(samples.size());
    preparedClip.samples = std::move(samples);
    return preparedClip;
}

projectname::BackgroundTimelinePlaybackPreparationResult makeReadyTimelinePreparationResult(
    const projectname::ProjectClip& clip,
    std::vector<float> samples)
{
    projectname::BackgroundTimelinePlaybackPreparationResult result;
    result.preparation.status = projectname::TimelinePlaybackPreparationStatus::importedClipReady;

    projectname::TimelinePlaybackClipPlan clipPlan;
    clipPlan.trackId = "track-imported-playback";
    clipPlan.clipId = clip.id;
    clipPlan.clipName = clip.name;
    clipPlan.relativePath = clip.relativePath;
    clipPlan.startBeats = clip.startBeats;
    clipPlan.lengthBeats = clip.lengthBeats;
    clipPlan.timelineStartSample = 10;
    clipPlan.timelineLengthSamples = 5;
    clipPlan.timelineEndSample = 15;
    result.preparation.clip = clipPlan;

    projectname::TimelinePlaybackActivation activation;
    activation.clipIndex = 0;
    activation.timelinePlaybackStartSample = 10;
    activation.clipLocalStartOffsetSamples = 0;
    activation.clipLengthSamples = 5;
    result.preparation.activation = activation;

    result.preparation.preparedClip = makePreparedMonoCacheClip(samples);
    result.preparation.preparedSamples =
        std::make_shared<const std::vector<float>>(result.preparation.preparedClip.samples);
    result.preparation.preparedClip.samples.clear();
    result.preparation.transportTimelineSample = 10;
    result.preparation.message = "Prepared imported timeline clip.";
    return result;
}

void writeFourCc(std::ofstream& file, const char (&id)[5])
{
    file.write(id, 4);
}

void writeUInt16LittleEndian(std::ofstream& file, std::uint16_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
    };
    file.write(bytes, 2);
}

void writeUInt32LittleEndian(std::ofstream& file, std::uint32_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU),
        static_cast<char>((value >> 16U) & 0xffU),
        static_cast<char>((value >> 24U) & 0xffU),
    };
    file.write(bytes, 4);
}

void writePcm16Wav(
    const std::filesystem::path& path,
    std::uint32_t sampleRate,
    std::uint16_t channelCount,
    const std::vector<std::int16_t>& interleavedSamples)
{
    const auto dataByteCount = static_cast<std::uint32_t>(interleavedSamples.size() * sizeof(std::int16_t));
    const auto blockAlign = static_cast<std::uint16_t>(channelCount * sizeof(std::int16_t));
    const auto byteRate = sampleRate * blockAlign;
    const auto riffPayloadByteCount = 4U + (8U + 16U) + (8U + dataByteCount);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    writeFourCc(file, "RIFF");
    writeUInt32LittleEndian(file, riffPayloadByteCount);
    writeFourCc(file, "WAVE");
    writeFourCc(file, "fmt ");
    writeUInt32LittleEndian(file, 16);
    writeUInt16LittleEndian(file, 1);
    writeUInt16LittleEndian(file, channelCount);
    writeUInt32LittleEndian(file, sampleRate);
    writeUInt32LittleEndian(file, byteRate);
    writeUInt16LittleEndian(file, blockAlign);
    writeUInt16LittleEndian(file, 16);
    writeFourCc(file, "data");
    writeUInt32LittleEndian(file, dataByteCount);

    for (const auto sample : interleavedSamples)
        writeUInt16LittleEndian(file, static_cast<std::uint16_t>(sample));
}

void transportStateAdvancesOnlyWhilePlaying()
{
    projectname::TransportState transport;

    expect(!transport.isPlaying(), "Transport starts stopped");
    expect(transport.getTempoBpm() == 120.0, "Default tempo is 120 BPM");

    transport.advanceSeconds(1.0);
    expect(transport.getPositionBeats() == 0.0, "Stopped transport does not advance");

    transport.play();
    transport.advanceSamples(48000, 48000.0);
    expect(std::abs(transport.getPositionBeats() - 2.0) < 0.0001, "Transport advances by tempo");

    transport.stop();
    transport.advanceSeconds(1.0);
    expect(std::abs(transport.getPositionBeats() - 2.0) < 0.0001, "Stopped transport stays put");

    transport.setTempoBpm(999.0);
    expect(transport.getTempoBpm() == projectname::TransportState::maxTempoBpm, "Tempo clamps high");

    transport.setTempoBpm(-1.0);
    expect(transport.getTempoBpm() == projectname::TransportState::minTempoBpm, "Tempo clamps low");

    expect(transport.setTimeSignature(7, 8), "Valid time signature accepted");
    expect(!transport.setTimeSignature(0, 3), "Invalid time signature rejected");
    expect(transport.getTimeSignature() == projectname::TimeSignature { 7, 8 },
           "Rejected signature leaves previous value");
}

void projectManifestRoundTrips()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Round Trip Test");
    project.getTransport().setTempoBpm(142.0);
    expect(project.getTransport().setTimeSignature(7, 8), "Project test signature accepted");
    project.getTransport().setPositionBeats(13.5);

    const auto package = makeTemporaryPackagePath("projectname-test");

    std::string error;
    expect(project.savePackage(package, error), "Project package saves");
    expect(error.empty(), "Save leaves error empty");
    expect(std::filesystem::is_regular_file(package / "manifest.json"), "Manifest exists");
    const auto manifestText = readTextFile(package / "manifest.json");
    const auto expectedApplication = std::string("\"application\": \"") + projectname::productName + "\"";
    expect(manifestText.find(expectedApplication) != std::string::npos,
           "Project manifest records the Rabbington Studio application marker");
    expect(!std::filesystem::exists(package / "manifest.json.tmp"), "Temporary manifest is cleaned up after save");
    expect(std::filesystem::is_directory(package / "audio"), "Audio asset folder exists");
    expect(std::filesystem::is_directory(package / "samples"), "Samples asset folder exists");
    expect(std::filesystem::is_directory(package / "presets"), "Presets asset folder exists");
    expect(std::filesystem::is_directory(package / "analysis"), "Analysis asset folder exists");
    expect(std::filesystem::is_directory(package / "backups"), "Backups asset folder exists");

    expect(!project.getTracks().empty(), "Default project has a track");
    expect(!project.getTracks().empty() && !project.getTracks().front().devices.empty(),
           "Default project has a device-chain placeholder");
    expect(!project.getTracks().empty()
               && !project.getTracks().front().devices.empty()
               && project.getTracks().front().devices.front().type == "builtin/generated-tone-source",
           "Default device-chain placeholder uses an original built-in type");

    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(loaded.has_value(), "Project package loads");
    expect(loaded.has_value() && *loaded == project, "Loaded project equals saved project");

    expect(std::filesystem::remove_all(package) > 0, "Temporary project package deleted");
}

void appSettingsRoundTripsAudioSetupPreferences()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Rabbington Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;
    settings.audioSetup.preferredOutput.juceDeviceStateXml =
        R"(<DEVICESETUP deviceType="Windows Audio" audioOutputDeviceName="Rabbington Output"/>)";

    const auto settingsPath = makeTemporarySettingsPath("projectname-app-settings-test");

    std::string error;
    expect(projectname::saveAppSettings(settings, settingsPath, error),
           "App settings save writes settings file");
    expect(error.empty(), "App settings save leaves error empty");

    const auto settingsText = readTextFile(settingsPath);
    expect(settingsText.find("\"settingsVersion\": 1") != std::string::npos,
           "App settings file records schema version");
    expect(settingsText.find("Rabbington Output") != std::string::npos,
           "App settings file records readable output device name");

    const auto loaded = projectname::loadAppSettings(settingsPath, error);
    expect(loaded.has_value(), "App settings load succeeds");
    expect(loaded.has_value() && *loaded == settings, "App settings round trip through JSON");

    expect(std::filesystem::remove(settingsPath), "Temporary app settings file deleted");
}

void appSettingsCommitFailureRemovesTemporaryFile()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Occupied Settings Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;

    const auto settingsPath = makeTemporarySettingsPath("projectname-app-settings-commit-failure-test");
    auto temporaryPath = settingsPath;
    temporaryPath += ".tmp";
    const auto occupiedMarkerPath = settingsPath / "occupied-marker.txt";
    writeTextFile(occupiedMarkerPath, "occupied settings path");

    std::string error;
    expect(!projectname::saveAppSettings(settings, settingsPath, error),
           "App settings save reports commit failure when settings path is occupied");
    expect(error.find("Could not commit app settings file") != std::string::npos,
           "App settings commit failure error is human-readable");
    expect(!std::filesystem::exists(temporaryPath),
           "App settings commit failure removes temporary settings file");
    expect(std::filesystem::is_directory(settingsPath),
           "App settings commit failure leaves occupied settings path as a directory");
    expect(readTextFile(occupiedMarkerPath) == "occupied settings path",
           "App settings commit failure preserves occupied settings path contents");

    expect(std::filesystem::remove_all(settingsPath) > 0,
           "Temporary occupied app settings path deleted");
}

void appSettingsTemporaryWriteFailureKeepsExistingSettings()
{
    projectname::AppSettings originalSettings;
    originalSettings.audioSetup.firstRunPromptDismissed = true;
    originalSettings.audioSetup.preferredOutput.hasOutputDevice = true;
    originalSettings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    originalSettings.audioSetup.preferredOutput.deviceName = "Original Settings Output";
    originalSettings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    originalSettings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    originalSettings.audioSetup.preferredOutput.outputChannelCount = 2;

    const auto settingsPath = makeTemporarySettingsPath("projectname-app-settings-temp-write-failure-test");
    auto temporaryPath = settingsPath;
    temporaryPath += ".tmp";

    std::string error;
    expect(projectname::saveAppSettings(originalSettings, settingsPath, error),
           "Initial app settings save succeeds before temp write failure fixture");

    const auto originalSettingsText = readTextFile(settingsPath);
    expect(originalSettingsText.find("Original Settings Output") != std::string::npos,
           "Temp write failure fixture starts with original settings file");

    const auto occupiedTemporaryMarkerPath = temporaryPath / "occupied-marker.txt";
    writeTextFile(occupiedTemporaryMarkerPath, "occupied temporary settings path");

    projectname::AppSettings replacementSettings = originalSettings;
    replacementSettings.audioSetup.preferredOutput.deviceName = "Replacement Settings Output";
    replacementSettings.audioSetup.preferredOutput.sampleRateHz = 96000.0;

    expect(!projectname::saveAppSettings(replacementSettings, settingsPath, error),
           "App settings save reports temporary write failure when temp path is occupied");
    expect(error.find("Could not open temporary app settings file") != std::string::npos,
           "App settings temporary write failure error is human-readable");
    expect(readTextFile(settingsPath) == originalSettingsText,
           "App settings temporary write failure leaves existing settings unchanged");
    expect(std::filesystem::is_directory(temporaryPath),
           "App settings temporary write failure leaves occupied temp path as a directory");
    expect(readTextFile(occupiedTemporaryMarkerPath) == "occupied temporary settings path",
           "App settings temporary write failure preserves occupied temp path contents");

    const auto loaded = projectname::loadAppSettings(settingsPath, error);
    expect(loaded.has_value() && *loaded == originalSettings,
           "App settings temporary write failure leaves loadable original settings");

    expect(std::filesystem::remove_all(temporaryPath) > 0,
           "Temporary occupied app settings temp path deleted");
    expect(std::filesystem::remove(settingsPath),
           "Temporary original app settings file deleted");
}

void appSettingsSaveRemovesStaleTemporarySymlinkWithoutFollowingIt()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Temporary Symlink Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;

    const auto settingsPath =
        makeTemporarySettingsPath("projectname-app-settings-temp-symlink-cleanup-test");
    auto temporaryPath = settingsPath;
    temporaryPath += ".tmp";
    const auto temporarySymlinkTargetPath =
        makeTemporarySettingsPath("projectname-app-settings-temp-symlink-target-test");
    writeTextFile(temporarySymlinkTargetPath, "temporary symlink target sentinel");

    std::error_code symlinkError;
    std::filesystem::create_symlink(temporarySymlinkTargetPath, temporaryPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(temporarySymlinkTargetPath);
        return;
    }

    std::string error;
    expect(projectname::saveAppSettings(settings, settingsPath, error),
           "App settings save succeeds after removing a stale temporary symlink");
    expect(error.empty(),
           "App settings stale temporary symlink cleanup leaves error empty");
    expect(std::filesystem::is_regular_file(settingsPath),
           "App settings stale temporary symlink cleanup writes a real settings file");
    expect(!std::filesystem::is_symlink(std::filesystem::symlink_status(settingsPath)),
           "App settings stale temporary symlink cleanup does not commit a settings symlink");
    expect(!std::filesystem::exists(temporaryPath),
           "App settings stale temporary symlink cleanup removes the temporary link");
    expect(readTextFile(temporarySymlinkTargetPath) == "temporary symlink target sentinel",
           "App settings stale temporary symlink cleanup preserves the symlink target");

    const auto loaded = projectname::loadAppSettings(settingsPath, error);
    expect(loaded.has_value() && *loaded == settings,
           "App settings stale temporary symlink cleanup commits loadable settings");

    expect(std::filesystem::remove(settingsPath),
           "Temporary app settings file after temp-symlink cleanup deleted");
    expect(std::filesystem::remove(temporarySymlinkTargetPath),
           "Temporary app settings temp-symlink target deleted");
}

void appSettingsSaveRemovesStaleBrokenTemporarySymlinkWithoutFollowingIt()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Broken Temporary Symlink Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;

    const auto settingsPath =
        makeTemporarySettingsPath("projectname-app-settings-broken-temp-symlink-cleanup-test");
    auto temporaryPath = settingsPath;
    temporaryPath += ".tmp";
    const auto brokenTemporarySymlinkTargetPath =
        makeTemporarySettingsPath("projectname-app-settings-broken-temp-symlink-target-test");

    std::error_code symlinkError;
    std::filesystem::create_symlink(brokenTemporarySymlinkTargetPath, temporaryPath, symlinkError);
    if (symlinkError)
        return;

    std::string error = "stale broken temporary settings symlink cleanup error";
    expect(projectname::saveAppSettings(settings, settingsPath, error),
           "App settings save succeeds after removing a stale broken temporary symlink");
    expect(error.empty(),
           "App settings stale broken temporary symlink cleanup leaves error empty");
    expect(std::filesystem::is_regular_file(settingsPath),
           "App settings stale broken temporary symlink cleanup writes a real settings file");
    expect(!std::filesystem::is_symlink(std::filesystem::symlink_status(settingsPath)),
           "App settings stale broken temporary symlink cleanup does not commit a settings symlink");
    expect(!std::filesystem::exists(temporaryPath),
           "App settings stale broken temporary symlink cleanup removes the temporary link");
    expect(!std::filesystem::exists(brokenTemporarySymlinkTargetPath),
           "App settings stale broken temporary symlink cleanup does not create the missing target");

    const auto loaded = projectname::loadAppSettings(settingsPath, error);
    expect(loaded.has_value() && *loaded == settings,
           "App settings stale broken temporary symlink cleanup commits loadable settings");

    expect(std::filesystem::remove(settingsPath),
           "Temporary app settings file after broken temp-symlink cleanup deleted");
}

void appSettingsDirectoryCreationFailurePreservesOccupiedParentPath()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Blocked Parent Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;

    const auto occupiedParentPath =
        makeTemporarySettingsPath("projectname-app-settings-directory-failure-parent");
    const auto settingsPath = occupiedParentPath / projectname::appSettingsFileName;
    const auto temporaryPath = occupiedParentPath / "settings.json.tmp";
    writeTextFile(occupiedParentPath, "occupied app settings parent path");

    std::string error;
    expect(!projectname::saveAppSettings(settings, settingsPath, error),
           "App settings save reports directory creation failure when parent path is occupied");
    expect(error.find("Could not create app settings directory") != std::string::npos,
           "App settings directory creation failure error is human-readable");
    expect(std::filesystem::is_regular_file(occupiedParentPath),
           "App settings directory creation failure leaves occupied parent as a file");
    expect(readTextFile(occupiedParentPath) == "occupied app settings parent path",
           "App settings directory creation failure preserves occupied parent path contents");
    expect(!std::filesystem::exists(settingsPath),
           "App settings directory creation failure does not create settings file below occupied parent");
    expect(!std::filesystem::exists(temporaryPath),
           "App settings directory creation failure does not create temporary settings file below occupied parent");

    expect(std::filesystem::remove(occupiedParentPath),
           "Temporary occupied app settings parent path deleted");
}

void appSettingsSaveSymlinkPathFailureLeavesTargetsUntouched()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Symlink Save Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;

    const auto settingsSymlinkPath =
        makeTemporarySettingsPath("projectname-app-settings-save-symlink-link-test");
    auto settingsSymlinkTemporaryPath = settingsSymlinkPath;
    settingsSymlinkTemporaryPath += ".tmp";
    const auto settingsSymlinkTargetPath =
        makeTemporarySettingsPath("projectname-app-settings-save-symlink-target-test");
    writeTextFile(settingsSymlinkTargetPath, "linked settings target");

    const auto linkedParentTarget =
        makeTemporaryPackagePath("projectname-app-settings-save-parent-symlink-target-test");
    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-app-settings-save-parent-symlink-link-test");
    const auto linkedParentSentinelPath = linkedParentTarget / "sentinel.txt";
    const auto linkedParentSettingsPath = parentSymlink / projectname::appSettingsFileName;
    const auto linkedParentTargetSettingsPath = linkedParentTarget / projectname::appSettingsFileName;
    const auto linkedParentTargetTemporaryPath = linkedParentTarget / "settings.json.tmp";
    writeTextFile(linkedParentSentinelPath, "linked settings parent target");

    std::error_code symlinkError;
    std::filesystem::create_symlink(settingsSymlinkTargetPath, settingsSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(settingsSymlinkTargetPath);
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    symlinkError.clear();
    std::filesystem::create_directory_symlink(linkedParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(settingsSymlinkPath);
        std::filesystem::remove(settingsSymlinkTargetPath);
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    std::string error;
    expect(!projectname::saveAppSettings(settings, settingsSymlinkPath, error),
           "App settings save rejects a symlink settings file path");
    expect(error.find("App settings path") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "App settings save symlink file failure error is human-readable");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(settingsSymlinkPath)),
           "App settings save symlink file failure leaves the symlink unchanged");
    expect(readTextFile(settingsSymlinkTargetPath) == "linked settings target",
           "App settings save symlink file failure preserves the linked target contents");
    expect(!std::filesystem::exists(settingsSymlinkTemporaryPath),
           "App settings save symlink file failure does not create a temporary settings file");

    error.clear();
    expect(!projectname::saveAppSettings(settings, linkedParentSettingsPath, error),
           "App settings save rejects an intermediate parent symlink");
    expect(error.find("App settings path") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "App settings save parent symlink failure error is human-readable");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "App settings save parent symlink failure leaves the parent symlink unchanged");
    expect(readTextFile(linkedParentSentinelPath) == "linked settings parent target",
           "App settings save parent symlink failure preserves the linked target sentinel");
    expect(!std::filesystem::exists(linkedParentTargetSettingsPath),
           "App settings save parent symlink failure does not write settings through the symlink");
    expect(!std::filesystem::exists(linkedParentTargetTemporaryPath),
           "App settings save parent symlink failure does not write temporary settings through the symlink");

    expect(std::filesystem::remove(settingsSymlinkPath),
           "Temporary app settings save symlink deleted");
    expect(std::filesystem::remove(parentSymlink),
           "Temporary app settings save parent symlink deleted");
    expect(std::filesystem::remove(settingsSymlinkTargetPath),
           "Temporary app settings save symlink target deleted");
    expect(std::filesystem::remove_all(linkedParentTarget) > 0,
           "Temporary app settings save parent symlink target deleted");
}

void appSettingsSaveBrokenSymlinkPathFailureLeavesTargetsUntouched()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Broken Symlink Save Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;
    const auto originalSettings = settings;

    const auto settingsSymlinkPath =
        makeTemporarySettingsPath("projectname-app-settings-save-broken-symlink-link-test");
    auto settingsSymlinkTemporaryPath = settingsSymlinkPath;
    settingsSymlinkTemporaryPath += ".tmp";
    const auto brokenSettingsTargetPath =
        makeTemporarySettingsPath("projectname-app-settings-save-broken-symlink-target-test");

    std::error_code symlinkError;
    std::filesystem::create_symlink(brokenSettingsTargetPath, settingsSymlinkPath, symlinkError);
    if (symlinkError)
        return;

    std::string error = "stale broken symlink settings save error";
    expect(!projectname::saveAppSettings(settings, settingsSymlinkPath, error),
           "App settings save rejects a broken symlink settings file path");
    expect(error.find("App settings path") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "App settings broken-symlink save failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "App settings broken-symlink save failure is not reported as a missing path");
    expect(settings == originalSettings,
           "App settings broken-symlink save failure leaves caller settings unchanged");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(settingsSymlinkPath)),
           "App settings broken-symlink save failure leaves the symlink unchanged");
    expect(!std::filesystem::exists(brokenSettingsTargetPath),
           "App settings broken-symlink save failure does not create the missing target");
    expect(!std::filesystem::exists(settingsSymlinkTemporaryPath),
           "App settings broken-symlink save failure does not create a temporary settings file");

    expect(std::filesystem::remove(settingsSymlinkPath),
           "Temporary broken app settings save symlink deleted");
}

void appSettingsSaveBrokenParentSymlinkFailureLeavesTargetsUntouched()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Broken Parent Symlink Save Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;
    const auto originalSettings = settings;

    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-app-settings-save-broken-parent-symlink-link-test");
    const auto missingParentTarget =
        makeTemporaryPackagePath("projectname-app-settings-save-broken-parent-symlink-target-test");
    const auto settingsPath = parentSymlink / projectname::appSettingsFileName;
    const auto temporaryPath = parentSymlink / "settings.json.tmp";
    const auto targetSettingsPath = missingParentTarget / projectname::appSettingsFileName;
    const auto targetTemporaryPath = missingParentTarget / "settings.json.tmp";

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
        return;

    std::string error = "stale broken parent symlink settings save error";
    expect(!projectname::saveAppSettings(settings, settingsPath, error),
           "App settings save rejects a broken intermediate parent symlink");
    expect(error.find("App settings path") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "App settings broken parent-symlink save failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "App settings broken parent-symlink save failure is not reported as a missing path");
    expect(settings == originalSettings,
           "App settings broken parent-symlink save failure leaves caller settings unchanged");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "App settings broken parent-symlink save failure leaves the parent symlink unchanged");
    expect(!std::filesystem::exists(missingParentTarget),
           "App settings broken parent-symlink save failure does not create the missing target");
    expect(!std::filesystem::exists(settingsPath),
           "App settings broken parent-symlink save failure does not create settings through the link");
    expect(!std::filesystem::exists(temporaryPath),
           "App settings broken parent-symlink save failure does not create temporary settings through the link");
    expect(!std::filesystem::exists(targetSettingsPath),
           "App settings broken parent-symlink save failure does not write target settings");
    expect(!std::filesystem::exists(targetTemporaryPath),
           "App settings broken parent-symlink save failure does not write target temporary settings");

    expect(std::filesystem::remove(parentSymlink),
           "Temporary broken app settings parent symlink deleted");
}

void appSettingsEmptyPathFailsBeforeFilesystemWork()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Empty Path Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;
    const auto originalSettings = settings;

    const auto previousCurrentPath = std::filesystem::current_path();
    const auto scratchDirectory =
        makeTemporaryPackagePath("projectname-app-settings-empty-path-cwd-test");
    std::filesystem::create_directories(scratchDirectory);
    std::filesystem::current_path(scratchDirectory);

    const auto unexpectedTemporaryPath = scratchDirectory / ".tmp";
    std::string error;
    expect(!projectname::saveAppSettings(settings, {}, error),
           "App settings save rejects an empty settings path");
    expect(error.find("App settings path is empty") != std::string::npos,
           "Empty app settings path failure error is human-readable");
    expect(settings == originalSettings,
           "Empty app settings path failure does not mutate the settings model");
    expect(!std::filesystem::exists(unexpectedTemporaryPath),
           "Empty app settings path failure does not create a cwd temporary settings file");
    expect(std::filesystem::is_empty(scratchDirectory),
           "Empty app settings path failure does not create directories or files in the scratch cwd");

    std::filesystem::current_path(previousCurrentPath);
    expect(std::filesystem::remove_all(scratchDirectory) > 0,
           "Temporary empty-path app settings cwd deleted");
}

void appSettingsLoadDirectoryPathKeepsCallerFallbackSettings()
{
    projectname::AppSettings fallbackSettings;
    fallbackSettings.audioSetup.firstRunPromptDismissed = true;
    fallbackSettings.audioSetup.preferredOutput.hasOutputDevice = true;
    fallbackSettings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    fallbackSettings.audioSetup.preferredOutput.deviceName = "Fallback Output";
    fallbackSettings.audioSetup.preferredOutput.sampleRateHz = 44100.0;
    fallbackSettings.audioSetup.preferredOutput.bufferSizeSamples = 512;
    fallbackSettings.audioSetup.preferredOutput.outputChannelCount = 2;
    fallbackSettings.audioSetup.preferredOutput.juceDeviceStateXml =
        R"(<DEVICESETUP deviceType="Windows Audio" audioOutputDeviceName="Fallback Output"/>)";
    const auto originalFallbackSettings = fallbackSettings;

    const auto settingsPath =
        makeTemporarySettingsPath("projectname-app-settings-load-directory-test");
    const auto nestedSettingsPath = settingsPath / projectname::appSettingsFileName;
    writeTextFile(nestedSettingsPath, "{ malformed nested app settings");

    std::string error = "stale load error";
    const auto loaded = projectname::loadAppSettings(settingsPath, error);
    expect(!loaded.has_value(),
           "App settings load treats a directory settings path as no persisted settings");
    expect(error.empty(),
           "Directory app settings load leaves error empty like a missing settings file");
    expect(error.find("parse") == std::string::npos
               && error.find("Unsupported app settings version") == std::string::npos,
           "Directory app settings load does not parse JSON below the occupied settings path");

    if (loaded.has_value())
        fallbackSettings = *loaded;

    expect(fallbackSettings == originalFallbackSettings,
           "Directory app settings load leaves caller fallback settings unchanged");
    expect(std::filesystem::is_directory(settingsPath),
           "Directory app settings load leaves occupied settings path unchanged");
    expect(readTextFile(nestedSettingsPath) == "{ malformed nested app settings",
           "Directory app settings load preserves nested settings directory contents");

    expect(std::filesystem::remove_all(settingsPath) > 0,
           "Temporary directory app settings path deleted");
}

void appSettingsLoadSymlinkPathKeepsCallerFallbackSettings()
{
    projectname::AppSettings fallbackSettings;
    fallbackSettings.audioSetup.firstRunPromptDismissed = true;
    fallbackSettings.audioSetup.preferredOutput.hasOutputDevice = true;
    fallbackSettings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    fallbackSettings.audioSetup.preferredOutput.deviceName = "Fallback Output";
    fallbackSettings.audioSetup.preferredOutput.sampleRateHz = 44100.0;
    fallbackSettings.audioSetup.preferredOutput.bufferSizeSamples = 512;
    fallbackSettings.audioSetup.preferredOutput.outputChannelCount = 2;
    fallbackSettings.audioSetup.preferredOutput.juceDeviceStateXml =
        R"(<DEVICESETUP deviceType="Windows Audio" audioOutputDeviceName="Fallback Output"/>)";
    const auto originalFallbackSettings = fallbackSettings;

    projectname::AppSettings linkedSettings;
    linkedSettings.audioSetup.firstRunPromptDismissed = true;
    linkedSettings.audioSetup.preferredOutput.hasOutputDevice = true;
    linkedSettings.audioSetup.preferredOutput.deviceType = "Linked Audio";
    linkedSettings.audioSetup.preferredOutput.deviceName = "Linked Output";
    linkedSettings.audioSetup.preferredOutput.sampleRateHz = 96000.0;
    linkedSettings.audioSetup.preferredOutput.bufferSizeSamples = 128;
    linkedSettings.audioSetup.preferredOutput.outputChannelCount = 8;

    const auto settingsSymlinkPath =
        makeTemporarySettingsPath("projectname-app-settings-load-symlink-test");
    const auto linkedSettingsTargetPath =
        makeTemporarySettingsPath("projectname-app-settings-load-symlink-target-test");
    const auto linkedSettingsText = projectname::makeAppSettingsJson(linkedSettings).dump();
    writeTextFile(linkedSettingsTargetPath, linkedSettingsText);

    std::error_code symlinkError;
    std::filesystem::create_symlink(linkedSettingsTargetPath, settingsSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(linkedSettingsTargetPath);
        return;
    }

    std::string error = "stale symlink settings load error";
    const auto loaded = projectname::loadAppSettings(settingsSymlinkPath, error);
    expect(!loaded.has_value(),
           "App settings load rejects a symlink settings path");
    expect(error.find("App settings") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "App settings symlink load failure error is human-readable");
    expect(error.find("parse") == std::string::npos
               && error.find("Unsupported app settings version") == std::string::npos,
           "App settings symlink load failure does not parse JSON through the symlink");

    if (loaded.has_value())
        fallbackSettings = *loaded;

    expect(fallbackSettings == originalFallbackSettings,
           "App settings symlink load leaves caller fallback settings unchanged");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(settingsSymlinkPath)),
           "App settings symlink load leaves the settings symlink unchanged");
    expect(readTextFile(linkedSettingsTargetPath) == linkedSettingsText,
           "App settings symlink load preserves the symlink target contents");
    expect(!std::filesystem::exists(settingsSymlinkPath.string() + ".tmp"),
           "App settings symlink load does not create a temporary settings file");

    expect(std::filesystem::remove(settingsSymlinkPath),
           "Temporary app settings symlink deleted");
    expect(std::filesystem::remove(linkedSettingsTargetPath),
           "Temporary app settings symlink target deleted");
}

void appSettingsLoadBrokenSymlinkPathKeepsCallerFallbackSettings()
{
    projectname::AppSettings fallbackSettings;
    fallbackSettings.audioSetup.firstRunPromptDismissed = true;
    fallbackSettings.audioSetup.preferredOutput.hasOutputDevice = true;
    fallbackSettings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    fallbackSettings.audioSetup.preferredOutput.deviceName = "Fallback Broken Link Output";
    fallbackSettings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    fallbackSettings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    fallbackSettings.audioSetup.preferredOutput.outputChannelCount = 2;
    fallbackSettings.audioSetup.preferredOutput.juceDeviceStateXml =
        R"(<DEVICESETUP deviceType="Windows Audio" audioOutputDeviceName="Fallback Broken Link Output"/>)";
    const auto originalFallbackSettings = fallbackSettings;

    const auto settingsSymlinkPath =
        makeTemporarySettingsPath("projectname-app-settings-load-broken-symlink-test");
    const auto brokenSettingsTargetPath =
        makeTemporarySettingsPath("projectname-app-settings-load-broken-symlink-target-test");

    std::error_code symlinkError;
    std::filesystem::create_symlink(brokenSettingsTargetPath, settingsSymlinkPath, symlinkError);
    if (symlinkError)
        return;

    std::string error = "stale broken symlink settings load error";
    const auto loaded = projectname::loadAppSettings(settingsSymlinkPath, error);
    expect(!loaded.has_value(),
           "App settings load rejects a broken symlink settings path");
    expect(error.find("App settings") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "App settings broken-symlink load failure error is human-readable");
    expect(error.find("No such") == std::string::npos
               && error.find("not found") == std::string::npos,
           "App settings broken-symlink load failure is not reported as missing settings");

    if (loaded.has_value())
        fallbackSettings = *loaded;

    expect(fallbackSettings == originalFallbackSettings,
           "App settings broken-symlink load leaves caller fallback settings unchanged");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(settingsSymlinkPath)),
           "App settings broken-symlink load leaves the settings symlink unchanged");
    expect(!std::filesystem::exists(brokenSettingsTargetPath),
           "App settings broken-symlink load does not create the missing target");
    expect(!std::filesystem::exists(settingsSymlinkPath.string() + ".tmp"),
           "App settings broken-symlink load does not create a temporary settings file");

    expect(std::filesystem::remove(settingsSymlinkPath),
           "Temporary broken app settings symlink deleted");
}

void appSettingsLoadBrokenParentSymlinkPathKeepsCallerFallbackSettings()
{
    projectname::AppSettings fallbackSettings;
    fallbackSettings.audioSetup.firstRunPromptDismissed = true;
    fallbackSettings.audioSetup.preferredOutput.hasOutputDevice = true;
    fallbackSettings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    fallbackSettings.audioSetup.preferredOutput.deviceName = "Fallback Broken Parent Link Output";
    fallbackSettings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    fallbackSettings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    fallbackSettings.audioSetup.preferredOutput.outputChannelCount = 2;
    fallbackSettings.audioSetup.preferredOutput.juceDeviceStateXml =
        R"(<DEVICESETUP deviceType="Windows Audio" audioOutputDeviceName="Fallback Broken Parent Link Output"/>)";
    const auto originalFallbackSettings = fallbackSettings;

    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-app-settings-load-broken-parent-symlink-link-test");
    const auto missingParentTarget =
        makeTemporaryPackagePath("projectname-app-settings-load-broken-parent-symlink-target-test");
    const auto settingsPath = parentSymlink / projectname::appSettingsFileName;
    const auto temporaryPath = parentSymlink / "settings.json.tmp";
    const auto targetSettingsPath = missingParentTarget / projectname::appSettingsFileName;
    const auto targetTemporaryPath = missingParentTarget / "settings.json.tmp";

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
        return;

    std::string error = "stale broken parent symlink settings load error";
    const auto loaded = projectname::loadAppSettings(settingsPath, error);
    expect(!loaded.has_value(),
           "App settings load rejects a broken intermediate parent symlink");
    expect(error.find("App settings path") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "App settings broken parent-symlink load failure error is human-readable");
    expect(error.find("No such") == std::string::npos
               && error.find("not found") == std::string::npos,
           "App settings broken parent-symlink load failure is not reported as missing settings");

    if (loaded.has_value())
        fallbackSettings = *loaded;

    expect(fallbackSettings == originalFallbackSettings,
           "App settings broken parent-symlink load leaves caller fallback settings unchanged");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "App settings broken parent-symlink load leaves the parent symlink unchanged");
    expect(!std::filesystem::exists(missingParentTarget),
           "App settings broken parent-symlink load does not create the missing target");
    expect(!std::filesystem::exists(settingsPath),
           "App settings broken parent-symlink load does not create settings through the link");
    expect(!std::filesystem::exists(temporaryPath),
           "App settings broken parent-symlink load does not create temporary settings through the link");
    expect(!std::filesystem::exists(targetSettingsPath),
           "App settings broken parent-symlink load does not write target settings");
    expect(!std::filesystem::exists(targetTemporaryPath),
           "App settings broken parent-symlink load does not write target temporary settings");

    expect(std::filesystem::remove(parentSymlink),
           "Temporary broken app settings load parent symlink deleted");
}

void appSettingsLoadLinkedParentSymlinkPathKeepsCallerFallbackSettings()
{
    projectname::AppSettings fallbackSettings;
    fallbackSettings.audioSetup.firstRunPromptDismissed = true;
    fallbackSettings.audioSetup.preferredOutput.hasOutputDevice = true;
    fallbackSettings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    fallbackSettings.audioSetup.preferredOutput.deviceName = "Fallback Linked Parent Output";
    fallbackSettings.audioSetup.preferredOutput.sampleRateHz = 44100.0;
    fallbackSettings.audioSetup.preferredOutput.bufferSizeSamples = 512;
    fallbackSettings.audioSetup.preferredOutput.outputChannelCount = 2;
    fallbackSettings.audioSetup.preferredOutput.juceDeviceStateXml =
        R"(<DEVICESETUP deviceType="Windows Audio" audioOutputDeviceName="Fallback Linked Parent Output"/>)";
    const auto originalFallbackSettings = fallbackSettings;

    projectname::AppSettings linkedSettings;
    linkedSettings.audioSetup.firstRunPromptDismissed = true;
    linkedSettings.audioSetup.preferredOutput.hasOutputDevice = true;
    linkedSettings.audioSetup.preferredOutput.deviceType = "Linked Parent Audio";
    linkedSettings.audioSetup.preferredOutput.deviceName = "Linked Parent Output";
    linkedSettings.audioSetup.preferredOutput.sampleRateHz = 96000.0;
    linkedSettings.audioSetup.preferredOutput.bufferSizeSamples = 128;
    linkedSettings.audioSetup.preferredOutput.outputChannelCount = 8;

    const auto linkedParentTarget =
        makeTemporaryPackagePath("projectname-app-settings-load-parent-symlink-target-test");
    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-app-settings-load-parent-symlink-link-test");
    const auto settingsPath = parentSymlink / projectname::appSettingsFileName;
    const auto temporaryPath = parentSymlink / "settings.json.tmp";
    const auto targetSettingsPath = linkedParentTarget / projectname::appSettingsFileName;
    const auto targetTemporaryPath = linkedParentTarget / "settings.json.tmp";
    const auto linkedSettingsText = projectname::makeAppSettingsJson(linkedSettings).dump();
    writeTextFile(targetSettingsPath, linkedSettingsText);

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    std::string error = "stale linked parent symlink settings load error";
    const auto loaded = projectname::loadAppSettings(settingsPath, error);
    expect(!loaded.has_value(),
           "App settings load rejects a linked intermediate parent symlink");
    expect(error.find("App settings path") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "App settings linked parent-symlink load failure error is human-readable");
    expect(error.find("parse") == std::string::npos
               && error.find("Unsupported app settings version") == std::string::npos,
           "App settings linked parent-symlink load failure does not parse JSON through the symlink");

    if (loaded.has_value())
        fallbackSettings = *loaded;

    expect(fallbackSettings == originalFallbackSettings,
           "App settings linked parent-symlink load leaves caller fallback settings unchanged");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "App settings linked parent-symlink load leaves the parent symlink unchanged");
    expect(readTextFile(targetSettingsPath) == linkedSettingsText,
           "App settings linked parent-symlink load preserves the linked target settings");
    expect(!std::filesystem::exists(temporaryPath),
           "App settings linked parent-symlink load does not create a temporary settings file through the link");
    expect(!std::filesystem::exists(targetTemporaryPath),
           "App settings linked parent-symlink load does not write target temporary settings");

    expect(std::filesystem::remove(parentSymlink),
           "Temporary linked app settings load parent symlink deleted");
    expect(std::filesystem::remove_all(linkedParentTarget) > 0,
           "Temporary linked app settings load parent target deleted");
}

void appSettingsLoadsAudioSetupDefaultsFromMinimalJson()
{
    std::string error;
    const auto loaded = projectname::parseAppSettingsJson(
        nlohmann::json { { "settingsVersion", projectname::appSettingsSchemaVersion } },
        error);

    expect(loaded.has_value(), "Minimal app settings JSON loads");
    expect(error.empty(), "Minimal app settings JSON leaves error empty");
    expect(loaded.has_value() && !loaded->audioSetup.firstRunPromptDismissed,
           "Minimal app settings defaults setup prompt to visible");
    expect(loaded.has_value() && !loaded->audioSetup.preferredOutput.hasOutputDevice,
           "Minimal app settings defaults preferred output to unset");
}

void appSettingsRejectsUnsupportedVersion()
{
    std::string error;
    const auto loaded = projectname::parseAppSettingsJson(
        nlohmann::json { { "settingsVersion", projectname::appSettingsSchemaVersion + 1 } },
        error);

    expect(!loaded.has_value(), "App settings reject unsupported future version");
    expect(error.find("Unsupported app settings version") != std::string::npos,
           "Unsupported app settings version reports readable error");
}

void appSettingsResetClearsAudioSetupPreferences()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Rabbington Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;
    settings.audioSetup.preferredOutput.juceDeviceStateXml =
        R"(<DEVICESETUP deviceType="Windows Audio" audioOutputDeviceName="Rabbington Output"/>)";

    projectname::resetAudioSetupPreferences(settings);

    expect(settings.settingsVersion == projectname::appSettingsSchemaVersion,
           "Audio setup reset preserves app settings schema version");
    expect(!settings.audioSetup.firstRunPromptDismissed,
           "Audio setup reset clears first-run dismissal");
    expect(settings.audioSetup.preferredOutput == projectname::AudioOutputPreference {},
           "Audio setup reset clears preferred output intent");

    const auto settingsPath = makeTemporarySettingsPath("projectname-app-settings-reset-test");
    std::string error;
    expect(projectname::saveAppSettings(settings, settingsPath, error),
           "Reset app settings save succeeds");

    const auto loaded = projectname::loadAppSettings(settingsPath, error);
    expect(loaded.has_value(), "Reset app settings load succeeds");
    expect(loaded.has_value() && !loaded->audioSetup.firstRunPromptDismissed,
           "Reset app settings file keeps first-run prompt visible");
    expect(loaded.has_value() && loaded->audioSetup.preferredOutput == projectname::AudioOutputPreference {},
           "Reset app settings file keeps preferred output unset");

    expect(std::filesystem::remove(settingsPath), "Temporary reset app settings file deleted");
}

void projectPackageSaveAsPolicyBlocksManifestOnlyWhenPackageAssetsNeedCopy()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-imported";
    imported.name = "Save As Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/take.wav";
    imported.analysisPath = "analysis/take.waveform.json";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported), "Save As policy test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-target-test");
    writeTextFile(sourcePackage / imported.relativePath, "audio");
    writeTextFile(sourcePackage / imported.analysisPath, "{}");
    writeTextFile(sourcePackage / "samples" / "one-shot.wav", "sample");
    writeTextFile(sourcePackage / "presets" / "starter.json", "{}");
    writeTextFile(sourcePackage / "backups" / "manifest.previous.json", "{}");

    const auto plan = projectname::buildProjectPackageSaveAsPlan(project, sourcePackage, targetPackage);

    expect(!plan.samePackage, "Save As policy detects a different target package");
    expect(plan.requiresPackageAssetCopy, "Save As policy requires package asset copy for local media");
    expect(!plan.canSaveManifestOnly, "Save As policy blocks manifest-only relocation when package assets exist");
    expect(plan.warning.find("package asset copy") != std::string::npos,
           "Save As policy records a package relocation warning");

    const auto* audioFolder = findSaveAsFolderPolicy(plan, "audio");
    expect(audioFolder != nullptr
               && audioFolder->action == projectname::ProjectPackageSaveAsFolderAction::cloneContents
               && audioFolder->containsSourceContent,
           "Save As policy plans to clone audio folder contents");

    const auto* samplesFolder = findSaveAsFolderPolicy(plan, "samples");
    expect(samplesFolder != nullptr
               && samplesFolder->action == projectname::ProjectPackageSaveAsFolderAction::cloneContents
               && samplesFolder->containsSourceContent,
           "Save As policy plans to clone samples folder contents");

    const auto* backupsFolder = findSaveAsFolderPolicy(plan, "backups");
    expect(backupsFolder != nullptr
               && backupsFolder->action == projectname::ProjectPackageSaveAsFolderAction::startFresh
               && backupsFolder->containsSourceContent,
           "Save As policy starts a fresh backup history in the target package");

    const auto* audioReference = findSaveAsReference(plan, imported.relativePath);
    expect(audioReference != nullptr
               && audioReference->action == projectname::ProjectPackageSaveAsReferenceAction::clonePackageAsset
               && audioReference->existsInSourcePackage,
           "Save As policy marks existing imported audio for package copy");

    const auto* analysisReference = findSaveAsReference(plan, imported.analysisPath);
    expect(analysisReference != nullptr
               && analysisReference->action == projectname::ProjectPackageSaveAsReferenceAction::clonePackageAsset
               && analysisReference->existsInSourcePackage,
           "Save As policy marks existing imported analysis for package copy");

    expect(std::filesystem::remove_all(sourcePackage) > 0, "Temporary Save As source package deleted");
    std::filesystem::remove_all(targetPackage);
}

void projectPackageSaveAsPolicyAllowsSamePackageManifestSave()
{
    auto project = projectname::ProjectModel::createDefault();

    const auto package = makeTemporaryPackagePath("projectname-save-as-same-test");
    writeTextFile(package / "audio" / "existing.wav", "audio");

    const auto plan = projectname::buildProjectPackageSaveAsPlan(project, package, package);

    expect(plan.samePackage, "Save As policy detects same-package target");
    expect(!plan.requiresPackageAssetCopy, "Same-package save does not require package asset copy");
    expect(plan.canSaveManifestOnly, "Same-package save can write manifest only");
    expect(projectname::describeProjectPackageSaveAsPlan(plan).find("normal save is safe") != std::string::npos,
           "Same-package Save As policy has a clear status description");

    expect(std::filesystem::remove_all(package) > 0, "Temporary same-package Save As package deleted");
}

void projectPackageSaveAsPolicyPreservesExternalReferences()
{
    auto project = projectname::ProjectModel::createDefault();

    const auto externalAudio = makeTemporaryAudioPath("projectname-save-as-external-test");
    writeTextFile(externalAudio, "external");

    projectname::ProjectClip imported;
    imported.id = "save-as-external";
    imported.name = "External Reference";
    imported.type = "audio-file";
    imported.relativePath = externalAudio.string();
    imported.analysisPath = "../outside.waveform.json";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported), "Save As policy test adds external clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-external-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-external-target-test");
    const auto plan = projectname::buildProjectPackageSaveAsPlan(project, sourcePackage, targetPackage);

    expect(!plan.requiresPackageAssetCopy, "External Save As references do not require package copy");
    expect(plan.canSaveManifestOnly, "External Save As references can be preserved in the manifest");

    const auto* audioReference = findSaveAsReference(plan, imported.relativePath);
    expect(audioReference != nullptr
               && audioReference->action
                   == projectname::ProjectPackageSaveAsReferenceAction::preserveExternalReference,
           "Save As policy preserves absolute external audio reference");

    const auto* analysisReference = findSaveAsReference(plan, imported.analysisPath);
    expect(analysisReference != nullptr
               && analysisReference->action == projectname::ProjectPackageSaveAsReferenceAction::reportUnsafeReference,
           "Save As policy reports unsafe analysis reference without copying it");

    expect(plan.warning.find("preserve external") != std::string::npos,
           "Save As policy warns that external references remain explicit");

    expect(std::filesystem::remove(externalAudio), "Temporary external Save As audio deleted");
    std::filesystem::remove_all(sourcePackage);
    std::filesystem::remove_all(targetPackage);
}

void projectPackageSaveAsCopyCommandCopiesPackageAssetsAndStartsFreshBackups()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-copy-imported";
    imported.name = "Save As Copy Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/take.wav";
    imported.analysisPath = "analysis/take.waveform.json";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported), "Save As copy command test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-copy-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-copy-target-test");
    writeTextFile(sourcePackage / imported.relativePath, "audio");
    writeTextFile(sourcePackage / imported.analysisPath, "{}");
    writeTextFile(sourcePackage / "samples" / "one-shot.wav", "sample");
    writeTextFile(sourcePackage / "presets" / "starter.json", "{}");
    writeTextFile(sourcePackage / "backups" / "manifest.previous.json", "{}");

    const auto result = projectname::copyProjectPackageAssetsForSaveAs({ project, sourcePackage, targetPackage });

    expect(result.status == projectname::ProjectPackageSaveAsCopyStatus::completed,
           "Save As copy command completes for package-local assets");
    expect(result.plan.requiresPackageAssetCopy,
           "Save As copy command preserves the preflight copy requirement");
    expect(result.copiedFileCount == 4,
           "Save As copy command copies audio, analysis, samples, and presets files");
    expect(std::filesystem::exists(targetPackage / imported.relativePath),
           "Save As copy command copies imported audio into the target package");
    expect(std::filesystem::exists(targetPackage / imported.analysisPath),
           "Save As copy command copies imported analysis into the target package");
    expect(std::filesystem::exists(targetPackage / "samples" / "one-shot.wav"),
           "Save As copy command copies package samples into the target package");
    expect(std::filesystem::exists(targetPackage / "presets" / "starter.json"),
           "Save As copy command copies package presets into the target package");
    expect(!std::filesystem::exists(targetPackage / "backups" / "manifest.previous.json"),
           "Save As copy command starts target backups fresh");

    expect(std::filesystem::remove_all(sourcePackage) > 0,
           "Temporary Save As copy source package deleted");
    expect(std::filesystem::remove_all(targetPackage) > 0,
           "Temporary Save As copy target package deleted");
}

void projectPackageSaveAsCopyCommandRejectsTargetConflictsBeforeCopy()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-conflict-imported";
    imported.name = "Save As Conflict Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/take.wav";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported), "Save As conflict test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-conflict-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-conflict-target-test");
    writeTextFile(sourcePackage / imported.relativePath, "new audio");
    writeTextFile(sourcePackage / "samples" / "one-shot.wav", "sample");
    writeTextFile(targetPackage / imported.relativePath, "existing audio");

    const auto result = projectname::copyProjectPackageAssetsForSaveAs({ project, sourcePackage, targetPackage });

    expect(result.status == projectname::ProjectPackageSaveAsCopyStatus::targetConflict,
           "Save As copy command rejects occupied target files");
    expect(readTextFile(targetPackage / imported.relativePath) == "existing audio",
           "Save As copy command leaves conflicting target files untouched");
    expect(!std::filesystem::exists(targetPackage / "samples" / "one-shot.wav"),
           "Save As copy command does not partially copy after preflight conflict");

    expect(std::filesystem::remove_all(sourcePackage) > 0,
           "Temporary Save As conflict source package deleted");
    expect(std::filesystem::remove_all(targetPackage) > 0,
           "Temporary Save As conflict target package deleted");
}

void projectPackageSaveAsCopyCommandRejectsTargetSymlinksBeforeCopy()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-target-symlink-imported";
    imported.name = "Save As Target Symlink Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/take.wav";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported),
           "Save As target symlink test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-target-symlink-source-test");
    const auto targetPackageSymlink =
        makeTemporaryPackagePath("projectname-save-as-target-package-symlink-link-test");
    const auto linkedTargetPackage =
        makeTemporaryPackagePath("projectname-save-as-target-package-symlink-target-test");
    const auto linkedTargetSentinel = linkedTargetPackage / "sentinel.txt";
    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-save-as-target-parent-symlink-link-test");
    const auto linkedParentTarget =
        makeTemporaryPackagePath("projectname-save-as-target-parent-symlink-target-test");
    const auto linkedParentSentinel = linkedParentTarget / "sentinel.txt";
    const auto nestedTargetPackage = parentSymlink / "Nested Target.project";

    writeTextFile(sourcePackage / imported.relativePath, "new audio");
    writeTextFile(sourcePackage / "samples" / "one-shot.wav", "sample");
    writeTextFile(linkedTargetSentinel, "target package sentinel");
    writeTextFile(linkedParentSentinel, "target parent sentinel");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedTargetPackage, targetPackageSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(sourcePackage);
        std::filesystem::remove_all(linkedTargetPackage);
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    symlinkError.clear();
    std::filesystem::create_directory_symlink(linkedParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(targetPackageSymlink);
        std::filesystem::remove_all(sourcePackage);
        std::filesystem::remove_all(linkedTargetPackage);
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    const auto targetPackageResult =
        projectname::copyProjectPackageAssetsForSaveAs({ project, sourcePackage, targetPackageSymlink });

    expect(targetPackageResult.status == projectname::ProjectPackageSaveAsCopyStatus::copyFailed,
           "Save As copy command rejects a symlink target package");
    expect(targetPackageResult.error.find("Target directory path is a symlink") != std::string::npos,
           "Save As target-package symlink failure error is human-readable");
    expect(targetPackageResult.plan.requiresPackageAssetCopy,
           "Save As target-package symlink failure preserves the copy plan");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(targetPackageSymlink)),
           "Save As target-package symlink failure leaves the target package symlink unchanged");
    expect(readTextFile(linkedTargetSentinel) == "target package sentinel",
           "Save As target-package symlink failure preserves the linked target sentinel");
    expect(!std::filesystem::exists(linkedTargetPackage / imported.relativePath),
           "Save As target-package symlink failure does not copy audio through the symlink");
    expect(!std::filesystem::exists(linkedTargetPackage / "samples" / "one-shot.wav"),
           "Save As target-package symlink failure does not copy samples through the symlink");

    const auto parentSymlinkResult =
        projectname::copyProjectPackageAssetsForSaveAs({ project, sourcePackage, nestedTargetPackage });

    expect(parentSymlinkResult.status == projectname::ProjectPackageSaveAsCopyStatus::copyFailed,
           "Save As copy command rejects a symlink target parent");
    expect(parentSymlinkResult.error.find("Target directory path is a symlink") != std::string::npos,
           "Save As target-parent symlink failure error is human-readable");
    expect(parentSymlinkResult.plan.requiresPackageAssetCopy,
           "Save As target-parent symlink failure preserves the copy plan");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Save As target-parent symlink failure leaves the parent symlink unchanged");
    expect(readTextFile(linkedParentSentinel) == "target parent sentinel",
           "Save As target-parent symlink failure preserves the linked parent sentinel");
    expect(!std::filesystem::exists(linkedParentTarget / "Nested Target.project"),
           "Save As target-parent symlink failure does not create a package through the symlink");
    expect(!std::filesystem::exists(linkedParentTarget / "Nested Target.project" / imported.relativePath),
           "Save As target-parent symlink failure does not copy audio through the symlink");

    expect(std::filesystem::remove(targetPackageSymlink),
           "Temporary Save As target-package symlink deleted");
    expect(std::filesystem::remove(parentSymlink),
           "Temporary Save As target-parent symlink deleted");
    expect(std::filesystem::remove_all(sourcePackage) > 0,
           "Temporary Save As target-symlink source package deleted");
    expect(std::filesystem::remove_all(linkedTargetPackage) > 0,
           "Temporary Save As target-package symlink target deleted");
    expect(std::filesystem::remove_all(linkedParentTarget) > 0,
           "Temporary Save As target-parent symlink target deleted");
}

void projectPackageSaveAsCopyCommandRejectsBrokenTargetSymlinksBeforeCopy()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-broken-target-symlink-imported";
    imported.name = "Save As Broken Target Symlink Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/take.wav";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported),
           "Save As broken target symlink test adds imported clip");

    const auto sourcePackage =
        makeTemporaryPackagePath("projectname-save-as-broken-target-symlink-source-test");
    const auto targetPackageSymlink =
        makeTemporaryPackagePath("projectname-save-as-broken-target-package-symlink-link-test");
    const auto missingTargetPackage =
        makeTemporaryPackagePath("projectname-save-as-broken-target-package-symlink-target-test");
    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-save-as-broken-target-parent-symlink-link-test");
    const auto missingParentTarget =
        makeTemporaryPackagePath("projectname-save-as-broken-target-parent-symlink-target-test");
    const auto nestedTargetPackage = parentSymlink / "Nested Target.project";

    writeTextFile(sourcePackage / imported.relativePath, "new audio");
    writeTextFile(sourcePackage / "samples" / "one-shot.wav", "sample");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingTargetPackage,
                                             targetPackageSymlink,
                                             symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(sourcePackage);
        return;
    }

    symlinkError.clear();
    std::filesystem::create_directory_symlink(missingParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(targetPackageSymlink);
        std::filesystem::remove_all(sourcePackage);
        return;
    }

    const auto targetPackageResult =
        projectname::copyProjectPackageAssetsForSaveAs({ project, sourcePackage, targetPackageSymlink });

    expect(targetPackageResult.status == projectname::ProjectPackageSaveAsCopyStatus::copyFailed,
           "Save As copy command rejects a broken symlink target package");
    expect(targetPackageResult.error.find("Target directory path is a symlink") != std::string::npos,
           "Save As broken target-package symlink failure error is human-readable");
    expect(targetPackageResult.plan.requiresPackageAssetCopy,
           "Save As broken target-package symlink failure preserves the copy plan");
    expect(targetPackageResult.createdPaths.empty(),
           "Save As broken target-package symlink failure creates no target paths");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(targetPackageSymlink)),
           "Save As broken target-package symlink failure leaves the target package symlink unchanged");
    expect(!std::filesystem::exists(missingTargetPackage),
           "Save As broken target-package symlink failure does not create the missing target");
    expect(!std::filesystem::exists(missingTargetPackage / imported.relativePath),
           "Save As broken target-package symlink failure does not copy audio through the broken symlink");
    expect(!std::filesystem::exists(missingTargetPackage / "samples" / "one-shot.wav"),
           "Save As broken target-package symlink failure does not copy samples through the broken symlink");

    const auto parentSymlinkResult =
        projectname::copyProjectPackageAssetsForSaveAs({ project, sourcePackage, nestedTargetPackage });

    expect(parentSymlinkResult.status == projectname::ProjectPackageSaveAsCopyStatus::copyFailed,
           "Save As copy command rejects a broken symlink target parent");
    expect(parentSymlinkResult.error.find("Target directory path is a symlink") != std::string::npos,
           "Save As broken target-parent symlink failure error is human-readable");
    expect(parentSymlinkResult.plan.requiresPackageAssetCopy,
           "Save As broken target-parent symlink failure preserves the copy plan");
    expect(parentSymlinkResult.createdPaths.empty(),
           "Save As broken target-parent symlink failure creates no target paths");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Save As broken target-parent symlink failure leaves the parent symlink unchanged");
    expect(!std::filesystem::exists(missingParentTarget),
           "Save As broken target-parent symlink failure does not create the missing parent target");
    expect(!std::filesystem::exists(missingParentTarget / "Nested Target.project"),
           "Save As broken target-parent symlink failure does not create a package through the broken symlink");
    expect(!std::filesystem::exists(missingParentTarget / "Nested Target.project" / imported.relativePath),
           "Save As broken target-parent symlink failure does not copy audio through the broken symlink");

    expect(std::filesystem::remove(targetPackageSymlink),
           "Temporary Save As broken target-package symlink deleted");
    expect(std::filesystem::remove(parentSymlink),
           "Temporary Save As broken target-parent symlink deleted");
    expect(std::filesystem::remove_all(sourcePackage) > 0,
           "Temporary Save As broken target-symlink source package deleted");
    std::filesystem::remove_all(missingTargetPackage);
    std::filesystem::remove_all(missingParentTarget);
}

void projectPackageSaveAsCopyCommandRejectsSourceSymlinksBeforeCopy()
{
    auto folderSymlinkProject = projectname::ProjectModel::createDefault();

    projectname::ProjectClip folderSymlinkClip;
    folderSymlinkClip.id = "save-as-source-folder-symlink-imported";
    folderSymlinkClip.name = "Save As Source Folder Symlink Imported";
    folderSymlinkClip.type = "audio-file";
    folderSymlinkClip.relativePath = "audio/take.wav";
    folderSymlinkClip.lengthBeats = 4.0;
    expect(folderSymlinkProject.addClipToTrack("track-1", folderSymlinkClip),
           "Save As source-folder symlink test adds imported clip");

    const auto folderSymlinkSourcePackage =
        makeTemporaryPackagePath("projectname-save-as-source-folder-symlink-source-test");
    const auto folderSymlinkTargetPackage =
        makeTemporaryPackagePath("projectname-save-as-source-folder-symlink-target-test");
    const auto linkedAudioFolderTarget =
        makeTemporaryPackagePath("projectname-save-as-source-folder-symlink-audio-target-test");
    const auto linkedAudioFolderSentinel = linkedAudioFolderTarget / "sentinel.txt";
    writeTextFile(linkedAudioFolderTarget / "take.wav", "linked folder audio");
    writeTextFile(linkedAudioFolderSentinel, "linked folder sentinel");
    std::filesystem::create_directories(folderSymlinkSourcePackage);

    auto entrySymlinkProject = projectname::ProjectModel::createDefault();

    projectname::ProjectClip entrySymlinkClip;
    entrySymlinkClip.id = "save-as-source-entry-symlink-imported";
    entrySymlinkClip.name = "Save As Source Entry Symlink Imported";
    entrySymlinkClip.type = "audio-file";
    entrySymlinkClip.relativePath = "audio/take.wav";
    entrySymlinkClip.lengthBeats = 4.0;
    expect(entrySymlinkProject.addClipToTrack("track-1", entrySymlinkClip),
           "Save As source-entry symlink test adds imported clip");

    const auto entrySymlinkSourcePackage =
        makeTemporaryPackagePath("projectname-save-as-source-entry-symlink-source-test");
    const auto entrySymlinkTargetPackage =
        makeTemporaryPackagePath("projectname-save-as-source-entry-symlink-target-test");
    const auto linkedAudioEntryTarget =
        makeTemporaryAudioPath("projectname-save-as-source-entry-symlink-target-test");
    const auto sourceEntrySymlink = entrySymlinkSourcePackage / entrySymlinkClip.relativePath;
    writeTextFile(linkedAudioEntryTarget, "linked entry audio");
    std::filesystem::create_directories(sourceEntrySymlink.parent_path());

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedAudioFolderTarget,
                                             folderSymlinkSourcePackage / "audio",
                                             symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(folderSymlinkSourcePackage);
        std::filesystem::remove_all(linkedAudioFolderTarget);
        std::filesystem::remove_all(entrySymlinkSourcePackage);
        std::filesystem::remove(linkedAudioEntryTarget);
        return;
    }

    symlinkError.clear();
    std::filesystem::create_symlink(linkedAudioEntryTarget, sourceEntrySymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(folderSymlinkSourcePackage / "audio");
        std::filesystem::remove_all(folderSymlinkSourcePackage);
        std::filesystem::remove_all(linkedAudioFolderTarget);
        std::filesystem::remove_all(entrySymlinkSourcePackage);
        std::filesystem::remove(linkedAudioEntryTarget);
        return;
    }

    const auto folderSymlinkResult = projectname::copyProjectPackageAssetsForSaveAs(
        { folderSymlinkProject, folderSymlinkSourcePackage, folderSymlinkTargetPackage });

    expect(folderSymlinkResult.status == projectname::ProjectPackageSaveAsCopyStatus::unsupportedSourceEntry,
           "Save As copy command rejects a symlink source package folder");
    expect(folderSymlinkResult.error.find("Source package folder is a symlink") != std::string::npos,
           "Save As source-folder symlink failure error is human-readable");
    expect(folderSymlinkResult.plan.requiresPackageAssetCopy,
           "Save As source-folder symlink failure preserves the copy plan");
    expect(std::filesystem::is_symlink(
               std::filesystem::symlink_status(folderSymlinkSourcePackage / "audio")),
           "Save As source-folder symlink failure leaves the source symlink unchanged");
    expect(readTextFile(linkedAudioFolderSentinel) == "linked folder sentinel",
           "Save As source-folder symlink failure preserves the linked folder target");
    expect(!std::filesystem::exists(folderSymlinkTargetPackage),
           "Save As source-folder symlink failure does not mutate the target package");

    const auto entrySymlinkResult = projectname::copyProjectPackageAssetsForSaveAs(
        { entrySymlinkProject, entrySymlinkSourcePackage, entrySymlinkTargetPackage });

    expect(entrySymlinkResult.status == projectname::ProjectPackageSaveAsCopyStatus::unsupportedSourceEntry,
           "Save As copy command rejects a symlink source package asset entry");
    expect(entrySymlinkResult.error.find("Source package entry is a symlink") != std::string::npos,
           "Save As source-entry symlink failure error is human-readable");
    expect(entrySymlinkResult.plan.requiresPackageAssetCopy,
           "Save As source-entry symlink failure preserves the copy plan");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(sourceEntrySymlink)),
           "Save As source-entry symlink failure leaves the source symlink unchanged");
    expect(readTextFile(linkedAudioEntryTarget) == "linked entry audio",
           "Save As source-entry symlink failure preserves the linked entry target");
    expect(!std::filesystem::exists(entrySymlinkTargetPackage),
           "Save As source-entry symlink failure does not mutate the target package");

    expect(std::filesystem::remove(folderSymlinkSourcePackage / "audio"),
           "Temporary Save As source-folder symlink deleted");
    expect(std::filesystem::remove(sourceEntrySymlink),
           "Temporary Save As source-entry symlink deleted");
    expect(std::filesystem::remove_all(folderSymlinkSourcePackage) > 0,
           "Temporary Save As source-folder symlink source package deleted");
    expect(std::filesystem::remove_all(linkedAudioFolderTarget) > 0,
           "Temporary Save As source-folder symlink target folder deleted");
    expect(std::filesystem::remove_all(entrySymlinkSourcePackage) > 0,
           "Temporary Save As source-entry symlink source package deleted");
    expect(std::filesystem::remove(linkedAudioEntryTarget),
           "Temporary Save As source-entry symlink target file deleted");
    std::filesystem::remove_all(folderSymlinkTargetPackage);
    std::filesystem::remove_all(entrySymlinkTargetPackage);
}

void projectPackageSaveAsCopyCommandRejectsBrokenSourceSymlinksBeforeCopy()
{
    auto folderSymlinkProject = projectname::ProjectModel::createDefault();

    projectname::ProjectClip folderSymlinkClip;
    folderSymlinkClip.id = "save-as-broken-source-folder-symlink-imported";
    folderSymlinkClip.name = "Save As Broken Source Folder Symlink Imported";
    folderSymlinkClip.type = "audio-file";
    folderSymlinkClip.relativePath = "audio/take.wav";
    folderSymlinkClip.lengthBeats = 4.0;
    expect(folderSymlinkProject.addClipToTrack("track-1", folderSymlinkClip),
           "Save As broken source-folder symlink test adds imported clip");

    const auto folderSymlinkSourcePackage =
        makeTemporaryPackagePath("projectname-save-as-broken-source-folder-symlink-source-test");
    const auto folderSymlinkTargetPackage =
        makeTemporaryPackagePath("projectname-save-as-broken-source-folder-symlink-target-test");
    const auto missingAudioFolderTarget =
        makeTemporaryPackagePath("projectname-save-as-broken-source-folder-symlink-target-test");
    std::filesystem::create_directories(folderSymlinkSourcePackage);

    auto entrySymlinkProject = projectname::ProjectModel::createDefault();

    projectname::ProjectClip entrySymlinkClip;
    entrySymlinkClip.id = "save-as-broken-source-entry-symlink-imported";
    entrySymlinkClip.name = "Save As Broken Source Entry Symlink Imported";
    entrySymlinkClip.type = "audio-file";
    entrySymlinkClip.relativePath = "audio/take.wav";
    entrySymlinkClip.lengthBeats = 4.0;
    expect(entrySymlinkProject.addClipToTrack("track-1", entrySymlinkClip),
           "Save As broken source-entry symlink test adds imported clip");

    const auto entrySymlinkSourcePackage =
        makeTemporaryPackagePath("projectname-save-as-broken-source-entry-symlink-source-test");
    const auto entrySymlinkTargetPackage =
        makeTemporaryPackagePath("projectname-save-as-broken-source-entry-symlink-target-test");
    const auto missingAudioEntryTarget =
        makeTemporaryAudioPath("projectname-save-as-broken-source-entry-symlink-target-test");
    const auto sourceEntrySymlink = entrySymlinkSourcePackage / entrySymlinkClip.relativePath;
    std::filesystem::create_directories(sourceEntrySymlink.parent_path());

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingAudioFolderTarget,
                                             folderSymlinkSourcePackage / "audio",
                                             symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(folderSymlinkSourcePackage);
        std::filesystem::remove_all(entrySymlinkSourcePackage);
        return;
    }

    symlinkError.clear();
    std::filesystem::create_symlink(missingAudioEntryTarget, sourceEntrySymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(folderSymlinkSourcePackage / "audio");
        std::filesystem::remove_all(folderSymlinkSourcePackage);
        std::filesystem::remove_all(entrySymlinkSourcePackage);
        std::filesystem::remove_all(folderSymlinkTargetPackage);
        std::filesystem::remove_all(entrySymlinkTargetPackage);
        std::filesystem::remove_all(missingAudioFolderTarget);
        std::filesystem::remove(missingAudioEntryTarget);
        return;
    }

    const auto folderSymlinkResult = projectname::copyProjectPackageAssetsForSaveAs(
        { folderSymlinkProject, folderSymlinkSourcePackage, folderSymlinkTargetPackage });

    expect(folderSymlinkResult.status == projectname::ProjectPackageSaveAsCopyStatus::unsupportedSourceEntry,
           "Save As copy command rejects a broken symlink source package folder");
    expect(folderSymlinkResult.error.find("Source package folder is a symlink") != std::string::npos,
           "Save As broken source-folder symlink failure error is human-readable");
    expect(folderSymlinkResult.plan.requiresPackageAssetCopy,
           "Save As broken source-folder symlink failure preserves the copy plan");
    expect(folderSymlinkResult.createdPaths.empty(),
           "Save As broken source-folder symlink failure creates no target paths");
    expect(std::filesystem::is_symlink(
               std::filesystem::symlink_status(folderSymlinkSourcePackage / "audio")),
           "Save As broken source-folder symlink failure leaves the source symlink unchanged");
    expect(!std::filesystem::exists(missingAudioFolderTarget),
           "Save As broken source-folder symlink failure does not create the missing target");
    expect(!std::filesystem::exists(folderSymlinkTargetPackage),
           "Save As broken source-folder symlink failure does not mutate the target package");

    const auto entrySymlinkResult = projectname::copyProjectPackageAssetsForSaveAs(
        { entrySymlinkProject, entrySymlinkSourcePackage, entrySymlinkTargetPackage });

    expect(entrySymlinkResult.status == projectname::ProjectPackageSaveAsCopyStatus::unsupportedSourceEntry,
           "Save As copy command rejects a broken symlink source package asset entry");
    expect(entrySymlinkResult.error.find("Source package entry is a symlink") != std::string::npos,
           "Save As broken source-entry symlink failure error is human-readable");
    expect(entrySymlinkResult.plan.requiresPackageAssetCopy,
           "Save As broken source-entry symlink failure preserves the copy plan");
    expect(entrySymlinkResult.createdPaths.empty(),
           "Save As broken source-entry symlink failure creates no target paths");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(sourceEntrySymlink)),
           "Save As broken source-entry symlink failure leaves the source symlink unchanged");
    expect(!std::filesystem::exists(missingAudioEntryTarget),
           "Save As broken source-entry symlink failure does not create the missing target");
    expect(!std::filesystem::exists(entrySymlinkTargetPackage),
           "Save As broken source-entry symlink failure does not mutate the target package");

    expect(std::filesystem::remove(folderSymlinkSourcePackage / "audio"),
           "Temporary Save As broken source-folder symlink deleted");
    expect(std::filesystem::remove(sourceEntrySymlink),
           "Temporary Save As broken source-entry symlink deleted");
    expect(std::filesystem::remove_all(folderSymlinkSourcePackage) > 0,
           "Temporary Save As broken source-folder symlink source package deleted");
    expect(std::filesystem::remove_all(entrySymlinkSourcePackage) > 0,
           "Temporary Save As broken source-entry symlink source package deleted");
    std::filesystem::remove_all(folderSymlinkTargetPackage);
    std::filesystem::remove_all(entrySymlinkTargetPackage);
    std::filesystem::remove_all(missingAudioFolderTarget);
    std::filesystem::remove(missingAudioEntryTarget);
}

void projectPackageSaveAsCopyCommandRejectsTargetInsideSource()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-nested-imported";
    imported.name = "Save As Nested Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/take.wav";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported), "Save As nested target test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-nested-source-test");
    const auto targetPackage = sourcePackage / "nested.project";
    writeTextFile(sourcePackage / imported.relativePath, "audio");

    const auto result = projectname::copyProjectPackageAssetsForSaveAs({ project, sourcePackage, targetPackage });

    expect(result.status == projectname::ProjectPackageSaveAsCopyStatus::invalidRequest,
           "Save As copy command rejects a target package inside the source package");
    expect(!std::filesystem::exists(targetPackage),
           "Save As copy command does not create a nested target package");

    expect(std::filesystem::remove_all(sourcePackage) > 0,
           "Temporary Save As nested source package deleted");
}

void projectPackageSaveAsCopyCommandCancelsAndRollsBackPartialTarget()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-cancel-imported";
    imported.name = "Save As Cancel Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/large.wav";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported), "Save As cancel test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-cancel-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-cancel-target-test");
    writeTextFile(sourcePackage / imported.relativePath, std::string(200000, 'x'));

    std::atomic_bool cancelRequested { false };
    auto sawCopyProgress = false;

    projectname::ProjectPackageSaveAsCopyRequest request;
    request.project = project;
    request.sourcePackageDirectory = sourcePackage;
    request.targetPackageDirectory = targetPackage;
    request.cancelRequested = &cancelRequested;
    request.progressCallback =
        [&cancelRequested, &sawCopyProgress](const projectname::ProjectPackageSaveAsCopyProgress& progress)
        {
            if (progress.stage == projectname::ProjectPackageSaveAsCopyProgressStage::copying
                && progress.bytesCopied > 0)
            {
                sawCopyProgress = true;
                cancelRequested.store(true, std::memory_order_release);
            }
        };

    const auto result = projectname::copyProjectPackageAssetsForSaveAs(std::move(request));

    expect(sawCopyProgress, "Save As copy command reports chunk progress before cancellation");
    expect(result.status == projectname::ProjectPackageSaveAsCopyStatus::cancelled,
           "Save As copy command reports cancellation");
    expect(!std::filesystem::exists(targetPackage / imported.relativePath),
           "Save As copy command rolls back partially copied target files");

    expect(std::filesystem::remove_all(sourcePackage) > 0,
           "Temporary Save As cancel source package deleted");
    std::filesystem::remove_all(targetPackage);
}

void backgroundSaveAsPackageCopyJobCopiesAssetsAndReportsProgress()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-background-imported";
    imported.name = "Save As Background Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/background.wav";
    imported.analysisPath = "analysis/background.waveform.json";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported), "Background Save As test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-background-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-background-target-test");
    writeTextFile(sourcePackage / imported.relativePath, "audio");
    writeTextFile(sourcePackage / imported.analysisPath, "{}");

    projectname::BackgroundSaveAsPackageCopyRequest request;
    request.project = project;
    request.sourcePackageDirectory = sourcePackage;
    request.targetPackageDirectory = targetPackage;

    projectname::BackgroundSaveAsPackageCopyJob job(std::move(request));
    job.start();
    auto result = job.waitForResult();
    const auto progress = job.getProgress();

    expect(!result.cancelled, "Background Save As copy job is not cancelled");
    expect(result.copy.status == projectname::ProjectPackageSaveAsCopyStatus::completed,
           "Background Save As copy job completes");
    expect(progress.phase == projectname::BackgroundSaveAsPackageCopyPhase::completed,
           "Background Save As copy progress reports completed");
    expect(progress.percent == 100, "Background Save As copy progress reaches 100 percent");
    expect(progress.filesTotal == 2 && progress.filesCopied == 2,
           "Background Save As copy progress counts copied files");
    expect(std::filesystem::exists(targetPackage / imported.relativePath),
           "Background Save As copy job copies audio");
    expect(std::filesystem::exists(targetPackage / imported.analysisPath),
           "Background Save As copy job copies analysis");

    expect(std::filesystem::remove_all(sourcePackage) > 0,
           "Temporary background Save As source package deleted");
    expect(std::filesystem::remove_all(targetPackage) > 0,
           "Temporary background Save As target package deleted");
}

void backgroundSaveAsPackageCopyJobRejectsLinkedTargetSymlinks()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-background-linked-target-symlink-imported";
    imported.name = "Save As Background Linked Target Symlink Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/take.wav";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported),
           "Background Save As linked target symlink test adds imported clip");

    const auto sourcePackage =
        makeTemporaryPackagePath("projectname-save-as-background-linked-target-symlink-source-test");
    const auto targetPackageSymlink =
        makeTemporaryPackagePath("projectname-save-as-background-linked-target-package-symlink-link-test");
    const auto linkedTargetPackage =
        makeTemporaryPackagePath("projectname-save-as-background-linked-target-package-symlink-target-test");
    const auto linkedTargetSentinel = linkedTargetPackage / "sentinel.txt";
    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-save-as-background-linked-target-parent-symlink-link-test");
    const auto linkedParentTarget =
        makeTemporaryPackagePath("projectname-save-as-background-linked-target-parent-symlink-target-test");
    const auto linkedParentSentinel = linkedParentTarget / "sentinel.txt";
    const auto nestedTargetPackage = parentSymlink / "Nested Target.project";

    writeTextFile(sourcePackage / imported.relativePath, "new audio");
    writeTextFile(sourcePackage / "samples" / "one-shot.wav", "sample");
    writeTextFile(linkedTargetSentinel, "target package sentinel");
    writeTextFile(linkedParentSentinel, "target parent sentinel");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedTargetPackage,
                                             targetPackageSymlink,
                                             symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(sourcePackage);
        std::filesystem::remove_all(linkedTargetPackage);
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    symlinkError.clear();
    std::filesystem::create_directory_symlink(linkedParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(targetPackageSymlink);
        std::filesystem::remove_all(sourcePackage);
        std::filesystem::remove_all(linkedTargetPackage);
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    projectname::BackgroundSaveAsPackageCopyRequest targetPackageRequest;
    targetPackageRequest.project = project;
    targetPackageRequest.sourcePackageDirectory = sourcePackage;
    targetPackageRequest.targetPackageDirectory = targetPackageSymlink;

    projectname::BackgroundSaveAsPackageCopyJob targetPackageJob(std::move(targetPackageRequest));
    targetPackageJob.start();
    const auto targetPackageResult = targetPackageJob.waitForResult();
    const auto targetPackageProgress = targetPackageJob.getProgress();

    expect(!targetPackageResult.cancelled,
           "Background Save As linked target-package symlink job is not cancelled");
    expect(targetPackageResult.copy.status == projectname::ProjectPackageSaveAsCopyStatus::copyFailed,
           "Background Save As copy job rejects a linked symlink target package");
    expect(targetPackageResult.error.find("Target directory path is a symlink") != std::string::npos,
           "Background Save As linked target-package symlink failure error is human-readable");
    expect(targetPackageProgress.phase == projectname::BackgroundSaveAsPackageCopyPhase::failed,
           "Background Save As linked target-package symlink progress reports failed");
    expect(targetPackageProgress.percent == 100,
           "Background Save As linked target-package symlink progress reaches failure percent");
    expect(targetPackageResult.copy.plan.requiresPackageAssetCopy,
           "Background Save As linked target-package symlink failure preserves the copy plan");
    expect(targetPackageResult.copy.createdPaths.empty(),
           "Background Save As linked target-package symlink failure creates no target paths");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(targetPackageSymlink)),
           "Background Save As linked target-package symlink failure leaves the symlink unchanged");
    expect(readTextFile(linkedTargetSentinel) == "target package sentinel",
           "Background Save As linked target-package symlink failure preserves the linked target sentinel");
    expect(!std::filesystem::exists(linkedTargetPackage / imported.relativePath),
           "Background Save As linked target-package symlink failure does not copy audio through the link");
    expect(!std::filesystem::exists(linkedTargetPackage / "samples" / "one-shot.wav"),
           "Background Save As linked target-package symlink failure does not copy samples through the link");

    projectname::BackgroundSaveAsPackageCopyRequest parentRequest;
    parentRequest.project = project;
    parentRequest.sourcePackageDirectory = sourcePackage;
    parentRequest.targetPackageDirectory = nestedTargetPackage;

    projectname::BackgroundSaveAsPackageCopyJob parentJob(std::move(parentRequest));
    parentJob.start();
    const auto parentResult = parentJob.waitForResult();
    const auto parentProgress = parentJob.getProgress();

    expect(!parentResult.cancelled,
           "Background Save As linked target-parent symlink job is not cancelled");
    expect(parentResult.copy.status == projectname::ProjectPackageSaveAsCopyStatus::copyFailed,
           "Background Save As copy job rejects a linked symlink target parent");
    expect(parentResult.error.find("Target directory path is a symlink") != std::string::npos,
           "Background Save As linked target-parent symlink failure error is human-readable");
    expect(parentProgress.phase == projectname::BackgroundSaveAsPackageCopyPhase::failed,
           "Background Save As linked target-parent symlink progress reports failed");
    expect(parentProgress.percent == 100,
           "Background Save As linked target-parent symlink progress reaches failure percent");
    expect(parentResult.copy.plan.requiresPackageAssetCopy,
           "Background Save As linked target-parent symlink failure preserves the copy plan");
    expect(parentResult.copy.createdPaths.empty(),
           "Background Save As linked target-parent symlink failure creates no target paths");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Background Save As linked target-parent symlink failure leaves the parent symlink unchanged");
    expect(readTextFile(linkedParentSentinel) == "target parent sentinel",
           "Background Save As linked target-parent symlink failure preserves the linked parent sentinel");
    expect(!std::filesystem::exists(linkedParentTarget / "Nested Target.project"),
           "Background Save As linked target-parent symlink failure does not create a package through the link");
    expect(!std::filesystem::exists(linkedParentTarget / "Nested Target.project" / imported.relativePath),
           "Background Save As linked target-parent symlink failure does not copy audio through the link");

    expect(std::filesystem::remove(targetPackageSymlink),
           "Temporary background Save As linked target-package symlink deleted");
    expect(std::filesystem::remove(parentSymlink),
           "Temporary background Save As linked target-parent symlink deleted");
    expect(std::filesystem::remove_all(sourcePackage) > 0,
           "Temporary background Save As linked target-symlink source package deleted");
    expect(std::filesystem::remove_all(linkedTargetPackage) > 0,
           "Temporary background Save As linked target-package symlink target deleted");
    expect(std::filesystem::remove_all(linkedParentTarget) > 0,
           "Temporary background Save As linked target-parent symlink target deleted");
}

void backgroundSaveAsPackageCopyJobRejectsBrokenTargetSymlinks()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-background-broken-target-symlink-imported";
    imported.name = "Save As Background Broken Target Symlink Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/take.wav";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported),
           "Background Save As broken target symlink test adds imported clip");

    const auto sourcePackage =
        makeTemporaryPackagePath("projectname-save-as-background-broken-target-symlink-source-test");
    const auto targetPackageSymlink =
        makeTemporaryPackagePath("projectname-save-as-background-broken-target-package-symlink-link-test");
    const auto missingTargetPackage =
        makeTemporaryPackagePath("projectname-save-as-background-broken-target-package-symlink-target-test");
    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-save-as-background-broken-target-parent-symlink-link-test");
    const auto missingParentTarget =
        makeTemporaryPackagePath("projectname-save-as-background-broken-target-parent-symlink-target-test");
    const auto nestedTargetPackage = parentSymlink / "Nested Target.project";

    writeTextFile(sourcePackage / imported.relativePath, "new audio");
    writeTextFile(sourcePackage / "samples" / "one-shot.wav", "sample");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingTargetPackage,
                                             targetPackageSymlink,
                                             symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(sourcePackage);
        return;
    }

    symlinkError.clear();
    std::filesystem::create_directory_symlink(missingParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(targetPackageSymlink);
        std::filesystem::remove_all(sourcePackage);
        return;
    }

    projectname::BackgroundSaveAsPackageCopyRequest targetPackageRequest;
    targetPackageRequest.project = project;
    targetPackageRequest.sourcePackageDirectory = sourcePackage;
    targetPackageRequest.targetPackageDirectory = targetPackageSymlink;

    projectname::BackgroundSaveAsPackageCopyJob targetPackageJob(std::move(targetPackageRequest));
    targetPackageJob.start();
    const auto targetPackageResult = targetPackageJob.waitForResult();
    const auto targetPackageProgress = targetPackageJob.getProgress();

    expect(!targetPackageResult.cancelled,
           "Background Save As broken target-package symlink job is not cancelled");
    expect(targetPackageResult.copy.status == projectname::ProjectPackageSaveAsCopyStatus::copyFailed,
           "Background Save As copy job rejects a broken symlink target package");
    expect(targetPackageResult.error.find("Target directory path is a symlink") != std::string::npos,
           "Background Save As broken target-package symlink failure error is human-readable");
    expect(targetPackageProgress.phase == projectname::BackgroundSaveAsPackageCopyPhase::failed,
           "Background Save As broken target-package symlink progress reports failed");
    expect(targetPackageProgress.percent == 100,
           "Background Save As broken target-package symlink progress reaches failure percent");
    expect(targetPackageResult.copy.plan.requiresPackageAssetCopy,
           "Background Save As broken target-package symlink failure preserves the copy plan");
    expect(targetPackageResult.copy.createdPaths.empty(),
           "Background Save As broken target-package symlink failure creates no target paths");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(targetPackageSymlink)),
           "Background Save As broken target-package symlink failure leaves the symlink unchanged");
    expect(!std::filesystem::exists(missingTargetPackage),
           "Background Save As broken target-package symlink failure does not create the missing target");
    expect(!std::filesystem::exists(missingTargetPackage / imported.relativePath),
           "Background Save As broken target-package symlink failure does not copy audio through the broken link");
    expect(!std::filesystem::exists(missingTargetPackage / "samples" / "one-shot.wav"),
           "Background Save As broken target-package symlink failure does not copy samples through the broken link");

    projectname::BackgroundSaveAsPackageCopyRequest parentRequest;
    parentRequest.project = project;
    parentRequest.sourcePackageDirectory = sourcePackage;
    parentRequest.targetPackageDirectory = nestedTargetPackage;

    projectname::BackgroundSaveAsPackageCopyJob parentJob(std::move(parentRequest));
    parentJob.start();
    const auto parentResult = parentJob.waitForResult();
    const auto parentProgress = parentJob.getProgress();

    expect(!parentResult.cancelled,
           "Background Save As broken target-parent symlink job is not cancelled");
    expect(parentResult.copy.status == projectname::ProjectPackageSaveAsCopyStatus::copyFailed,
           "Background Save As copy job rejects a broken symlink target parent");
    expect(parentResult.error.find("Target directory path is a symlink") != std::string::npos,
           "Background Save As broken target-parent symlink failure error is human-readable");
    expect(parentProgress.phase == projectname::BackgroundSaveAsPackageCopyPhase::failed,
           "Background Save As broken target-parent symlink progress reports failed");
    expect(parentProgress.percent == 100,
           "Background Save As broken target-parent symlink progress reaches failure percent");
    expect(parentResult.copy.plan.requiresPackageAssetCopy,
           "Background Save As broken target-parent symlink failure preserves the copy plan");
    expect(parentResult.copy.createdPaths.empty(),
           "Background Save As broken target-parent symlink failure creates no target paths");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Background Save As broken target-parent symlink failure leaves the parent symlink unchanged");
    expect(!std::filesystem::exists(missingParentTarget),
           "Background Save As broken target-parent symlink failure does not create the missing parent target");
    expect(!std::filesystem::exists(missingParentTarget / "Nested Target.project"),
           "Background Save As broken target-parent symlink failure does not create a package through the broken link");
    expect(!std::filesystem::exists(missingParentTarget / "Nested Target.project" / imported.relativePath),
           "Background Save As broken target-parent symlink failure does not copy audio through the broken link");

    expect(std::filesystem::remove(targetPackageSymlink),
           "Temporary background Save As broken target-package symlink deleted");
    expect(std::filesystem::remove(parentSymlink),
           "Temporary background Save As broken target-parent symlink deleted");
    expect(std::filesystem::remove_all(sourcePackage) > 0,
           "Temporary background Save As broken target-symlink source package deleted");
    std::filesystem::remove_all(missingTargetPackage);
    std::filesystem::remove_all(missingParentTarget);
}

void backgroundSaveAsPackageCopyJobRejectsBrokenSourceSymlinks()
{
    auto folderSymlinkProject = projectname::ProjectModel::createDefault();

    projectname::ProjectClip folderSymlinkClip;
    folderSymlinkClip.id = "save-as-background-broken-source-folder-symlink-imported";
    folderSymlinkClip.name = "Save As Background Broken Source Folder Symlink Imported";
    folderSymlinkClip.type = "audio-file";
    folderSymlinkClip.relativePath = "audio/take.wav";
    folderSymlinkClip.lengthBeats = 4.0;
    expect(folderSymlinkProject.addClipToTrack("track-1", folderSymlinkClip),
           "Background Save As broken source-folder symlink test adds imported clip");

    const auto folderSymlinkSourcePackage =
        makeTemporaryPackagePath("projectname-save-as-background-broken-source-folder-symlink-source-test");
    const auto folderSymlinkTargetPackage =
        makeTemporaryPackagePath("projectname-save-as-background-broken-source-folder-symlink-target-test");
    const auto missingAudioFolderTarget =
        makeTemporaryPackagePath("projectname-save-as-background-broken-source-folder-symlink-missing-target-test");
    std::filesystem::create_directories(folderSymlinkSourcePackage);

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingAudioFolderTarget,
                                             folderSymlinkSourcePackage / "audio",
                                             symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(folderSymlinkSourcePackage);
        return;
    }

    projectname::BackgroundSaveAsPackageCopyRequest folderRequest;
    folderRequest.project = folderSymlinkProject;
    folderRequest.sourcePackageDirectory = folderSymlinkSourcePackage;
    folderRequest.targetPackageDirectory = folderSymlinkTargetPackage;

    projectname::BackgroundSaveAsPackageCopyJob folderJob(std::move(folderRequest));
    folderJob.start();
    const auto folderResult = folderJob.waitForResult();
    const auto folderProgress = folderJob.getProgress();

    expect(!folderResult.cancelled,
           "Background Save As broken source-folder symlink job is not cancelled");
    expect(folderResult.copy.status == projectname::ProjectPackageSaveAsCopyStatus::unsupportedSourceEntry,
           "Background Save As copy job rejects a broken source package folder symlink");
    expect(folderResult.error.find("Source package folder is a symlink") != std::string::npos,
           "Background Save As broken source-folder symlink failure error is human-readable");
    expect(folderProgress.phase == projectname::BackgroundSaveAsPackageCopyPhase::failed,
           "Background Save As broken source-folder symlink progress reports failed");
    expect(folderProgress.percent == 100,
           "Background Save As broken source-folder symlink progress reaches failure percent");
    expect(folderResult.copy.plan.requiresPackageAssetCopy,
           "Background Save As broken source-folder symlink failure preserves the copy plan");
    expect(folderResult.copy.createdPaths.empty(),
           "Background Save As broken source-folder symlink failure creates no target paths");
    expect(std::filesystem::is_symlink(
               std::filesystem::symlink_status(folderSymlinkSourcePackage / "audio")),
           "Background Save As broken source-folder symlink failure leaves the source symlink unchanged");
    expect(!std::filesystem::exists(missingAudioFolderTarget),
           "Background Save As broken source-folder symlink failure does not create the missing target");
    expect(!std::filesystem::exists(folderSymlinkTargetPackage),
           "Background Save As broken source-folder symlink failure does not mutate the target package");

    auto entrySymlinkProject = projectname::ProjectModel::createDefault();

    projectname::ProjectClip entrySymlinkClip;
    entrySymlinkClip.id = "save-as-background-broken-source-entry-symlink-imported";
    entrySymlinkClip.name = "Save As Background Broken Source Entry Symlink Imported";
    entrySymlinkClip.type = "audio-file";
    entrySymlinkClip.relativePath = "audio/take.wav";
    entrySymlinkClip.lengthBeats = 4.0;
    expect(entrySymlinkProject.addClipToTrack("track-1", entrySymlinkClip),
           "Background Save As broken source-entry symlink test adds imported clip");

    const auto entrySymlinkSourcePackage =
        makeTemporaryPackagePath("projectname-save-as-background-broken-source-entry-symlink-source-test");
    const auto entrySymlinkTargetPackage =
        makeTemporaryPackagePath("projectname-save-as-background-broken-source-entry-symlink-target-test");
    const auto missingAudioEntryTarget =
        makeTemporaryAudioPath("projectname-save-as-background-broken-source-entry-symlink-missing-target-test");
    const auto sourceEntrySymlink = entrySymlinkSourcePackage / entrySymlinkClip.relativePath;
    std::filesystem::create_directories(sourceEntrySymlink.parent_path());

    symlinkError.clear();
    std::filesystem::create_symlink(missingAudioEntryTarget, sourceEntrySymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(folderSymlinkSourcePackage / "audio");
        std::filesystem::remove_all(folderSymlinkSourcePackage);
        std::filesystem::remove_all(entrySymlinkSourcePackage);
        return;
    }

    projectname::BackgroundSaveAsPackageCopyRequest entryRequest;
    entryRequest.project = entrySymlinkProject;
    entryRequest.sourcePackageDirectory = entrySymlinkSourcePackage;
    entryRequest.targetPackageDirectory = entrySymlinkTargetPackage;

    projectname::BackgroundSaveAsPackageCopyJob entryJob(std::move(entryRequest));
    entryJob.start();
    const auto entryResult = entryJob.waitForResult();
    const auto entryProgress = entryJob.getProgress();

    expect(!entryResult.cancelled,
           "Background Save As broken source-entry symlink job is not cancelled");
    expect(entryResult.copy.status == projectname::ProjectPackageSaveAsCopyStatus::unsupportedSourceEntry,
           "Background Save As copy job rejects a broken source package asset entry symlink");
    expect(entryResult.error.find("Source package entry is a symlink") != std::string::npos,
           "Background Save As broken source-entry symlink failure error is human-readable");
    expect(entryProgress.phase == projectname::BackgroundSaveAsPackageCopyPhase::failed,
           "Background Save As broken source-entry symlink progress reports failed");
    expect(entryProgress.percent == 100,
           "Background Save As broken source-entry symlink progress reaches failure percent");
    expect(entryResult.copy.plan.requiresPackageAssetCopy,
           "Background Save As broken source-entry symlink failure preserves the copy plan");
    expect(entryResult.copy.createdPaths.empty(),
           "Background Save As broken source-entry symlink failure creates no target paths");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(sourceEntrySymlink)),
           "Background Save As broken source-entry symlink failure leaves the source symlink unchanged");
    expect(!std::filesystem::exists(missingAudioEntryTarget),
           "Background Save As broken source-entry symlink failure does not create the missing target");
    expect(!std::filesystem::exists(entrySymlinkTargetPackage),
           "Background Save As broken source-entry symlink failure does not mutate the target package");

    expect(std::filesystem::remove(folderSymlinkSourcePackage / "audio"),
           "Temporary background Save As broken source-folder symlink deleted");
    expect(std::filesystem::remove(sourceEntrySymlink),
           "Temporary background Save As broken source-entry symlink deleted");
    expect(std::filesystem::remove_all(folderSymlinkSourcePackage) > 0,
           "Temporary background Save As broken source-folder package deleted");
    expect(std::filesystem::remove_all(entrySymlinkSourcePackage) > 0,
           "Temporary background Save As broken source-entry package deleted");
    std::filesystem::remove_all(folderSymlinkTargetPackage);
    std::filesystem::remove_all(entrySymlinkTargetPackage);
    std::filesystem::remove_all(missingAudioFolderTarget);
    std::filesystem::remove(missingAudioEntryTarget);
}

void backgroundSaveAsPackageCopyJobCancelsBeforeStart()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-background-cancel-imported";
    imported.name = "Save As Background Cancel Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/background-cancel.wav";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported), "Background Save As cancel test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-background-cancel-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-background-cancel-target-test");
    writeTextFile(sourcePackage / imported.relativePath, "audio");

    projectname::BackgroundSaveAsPackageCopyRequest request;
    request.project = project;
    request.sourcePackageDirectory = sourcePackage;
    request.targetPackageDirectory = targetPackage;

    projectname::BackgroundSaveAsPackageCopyJob job(std::move(request));
    job.requestCancel();
    const auto requestedProgress = job.getProgress();
    expect(requestedProgress.cancelRequested,
           "Background Save As copy progress records cancellation request before start");
    expect(requestedProgress.phase == projectname::BackgroundSaveAsPackageCopyPhase::cancelled,
           "Background Save As copy pre-start progress reports cancelled");

    job.start();
    auto result = job.waitForResult();
    const auto cancelledProgress = job.getProgress();

    expect(result.cancelled, "Background Save As copy job reports cancellation");
    expect(result.copy.status == projectname::ProjectPackageSaveAsCopyStatus::cancelled,
           "Background Save As copy command result reports cancellation");
    expect(cancelledProgress.phase == projectname::BackgroundSaveAsPackageCopyPhase::cancelled,
           "Background Save As copy progress stays cancelled");
    expect(!std::filesystem::exists(targetPackage),
           "Background Save As copy cancellation does not create the target package");

    expect(std::filesystem::remove_all(sourcePackage) > 0,
           "Temporary background Save As cancel source package deleted");
}

void projectPackageSaveAsRetryPreflightAllowsManifestOnlyRetryAndStaleTempCleanup()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-retry-imported";
    imported.name = "Save As Retry Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/retry.wav";
    imported.analysisPath = "analysis/retry.waveform.json";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported), "Save As retry test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-retry-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-retry-target-test");
    writeTextFile(targetPackage / imported.relativePath, "copied audio");
    writeTextFile(targetPackage / imported.analysisPath, "{}");
    writeTextFile(targetPackage / "manifest.json.tmp", "stale temporary manifest");

    projectname::ProjectPackageSaveAsRetryState recovery;
    recovery.sourcePackageDirectory = sourcePackage;
    recovery.targetPackageDirectory = targetPackage;
    recovery.projectSnapshot = project;
    recovery.copiedFileCount = 2;
    recovery.copiedBytes = 16;

    const auto preflight = projectname::preflightProjectPackageSaveAsRetry(&recovery,
                                                                           project,
                                                                           sourcePackage);
    expect(preflight.status == projectname::ProjectPackageSaveAsRetryPreflightStatus::ready,
           "Save As retry preflight allows missing final manifest with copied target assets: "
               + preflight.message);
    expect(std::filesystem::exists(targetPackage / "manifest.json.tmp"),
           "Save As retry preflight leaves stale temporary manifest for staged writer cleanup");

    std::string error;
    expect(project.savePackage(targetPackage, error),
           "Save As retry manifest-only staged save succeeds");
    expect(std::filesystem::is_regular_file(targetPackage / "manifest.json"),
           "Save As retry manifest-only staged save writes final manifest");
    expect(!std::filesystem::exists(targetPackage / "manifest.json.tmp"),
           "Save As retry staged writer removes stale temporary manifest");

    expect(std::filesystem::remove_all(targetPackage) > 0,
           "Temporary Save As retry target package deleted");
}

void projectPackageSaveAsRetryPreflightRejectsTargetManifestConflicts()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-retry-conflict-imported";
    imported.name = "Save As Retry Conflict Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/conflict.wav";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported),
           "Save As retry conflict test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-retry-conflict-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-retry-conflict-target-test");
    writeTextFile(targetPackage / imported.relativePath, "copied audio");
    writeTextFile(targetPackage / "manifest.json", "existing manifest");

    projectname::ProjectPackageSaveAsRetryState recovery;
    recovery.sourcePackageDirectory = sourcePackage;
    recovery.targetPackageDirectory = targetPackage;
    recovery.projectSnapshot = project;
    recovery.copiedFileCount = 1;

    const auto regularConflict = projectname::preflightProjectPackageSaveAsRetry(&recovery,
                                                                                 project,
                                                                                 sourcePackage);
    expect(regularConflict.status
               == projectname::ProjectPackageSaveAsRetryPreflightStatus::targetManifestExists,
           "Save As retry preflight rejects existing regular manifests");
    expect(readTextFile(targetPackage / "manifest.json") == "existing manifest",
           "Save As retry preflight leaves existing target manifest unchanged");

    const auto directoryTarget = makeTemporaryPackagePath("projectname-save-as-retry-directory-test");
    writeTextFile(directoryTarget / imported.relativePath, "copied audio");
    std::filesystem::create_directories(directoryTarget / "manifest.json");
    recovery.targetPackageDirectory = directoryTarget;

    const auto directoryConflict = projectname::preflightProjectPackageSaveAsRetry(&recovery,
                                                                                   project,
                                                                                   sourcePackage);
    expect(directoryConflict.status
               == projectname::ProjectPackageSaveAsRetryPreflightStatus::targetManifestConflict,
           "Save As retry preflight rejects non-regular manifest path conflicts");

    std::filesystem::remove_all(targetPackage);
    std::filesystem::remove_all(directoryTarget);
}

void projectPackageSaveAsRetryPreflightRejectsTargetManifestSymlinkConflicts()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-retry-symlink-imported";
    imported.name = "Save As Retry Symlink Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/symlink.wav";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported),
           "Save As retry symlink conflict test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-retry-symlink-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-retry-symlink-target-test");
    const auto manifestSymlinkPath = targetPackage / "manifest.json";
    const auto symlinkTargetPath = targetPackage / "linked-manifest-target.json";
    writeTextFile(targetPackage / imported.relativePath, "copied audio");
    writeTextFile(symlinkTargetPath, "external manifest target");

    std::error_code symlinkError;
    std::filesystem::create_symlink(symlinkTargetPath, manifestSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(targetPackage);
        return;
    }

    projectname::ProjectPackageSaveAsRetryState recovery;
    recovery.sourcePackageDirectory = sourcePackage;
    recovery.targetPackageDirectory = targetPackage;
    recovery.projectSnapshot = project;
    recovery.copiedFileCount = 1;

    const auto symlinkConflict = projectname::preflightProjectPackageSaveAsRetry(&recovery,
                                                                                 project,
                                                                                 sourcePackage);
    expect(symlinkConflict.status
               == projectname::ProjectPackageSaveAsRetryPreflightStatus::targetManifestConflict,
           "Save As retry preflight rejects manifest symlink conflicts");
    expect(symlinkConflict.message.find("target manifest path is occupied by a non-regular entry")
               != std::string::npos,
           "Save As retry manifest symlink conflict error is human-readable");
    expect(symlinkConflict.path == manifestSymlinkPath,
           "Save As retry manifest symlink conflict reports the symlink path");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(manifestSymlinkPath)),
           "Save As retry preflight leaves the manifest symlink unchanged");
    expect(readTextFile(symlinkTargetPath) == "external manifest target",
           "Save As retry preflight does not overwrite or remove the manifest symlink target");
    expect(readTextFile(targetPackage / imported.relativePath) == "copied audio",
           "Save As retry preflight does not recopy or mutate copied target assets");
    expect(!std::filesystem::exists(targetPackage / "manifest.json.tmp"),
           "Save As retry manifest symlink conflict does not create a temporary manifest");

    std::filesystem::remove_all(targetPackage);
}

void projectPackageSaveAsRetryPreflightRejectsCopiedAssetSymlinks()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-retry-asset-symlink-imported";
    imported.name = "Save As Retry Asset Symlink Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/linked-asset.wav";
    imported.analysisPath = "analysis/linked-asset.waveform.json";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported),
           "Save As retry asset symlink test adds imported clip");

    const auto sourcePackage =
        makeTemporaryPackagePath("projectname-save-as-retry-asset-symlink-source-test");
    const auto targetPackage =
        makeTemporaryPackagePath("projectname-save-as-retry-asset-symlink-target-test");
    const auto assetSymlinkPath = targetPackage / imported.relativePath;
    const auto symlinkTargetPath = targetPackage / "linked-audio-target.wav";
    writeTextFile(sourcePackage / imported.relativePath, "source audio should not be recopied");
    writeTextFile(targetPackage / imported.analysisPath, "{}");
    writeTextFile(symlinkTargetPath, "external audio target");
    std::filesystem::create_directories(assetSymlinkPath.parent_path());

    std::error_code symlinkError;
    std::filesystem::create_symlink(symlinkTargetPath, assetSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(sourcePackage);
        std::filesystem::remove_all(targetPackage);
        return;
    }

    projectname::ProjectPackageSaveAsRetryState recovery;
    recovery.sourcePackageDirectory = sourcePackage;
    recovery.targetPackageDirectory = targetPackage;
    recovery.projectSnapshot = project;
    recovery.copiedFileCount = 2;
    recovery.copiedBytes = 18;

    const auto symlinkAsset = projectname::preflightProjectPackageSaveAsRetry(&recovery,
                                                                              project,
                                                                              sourcePackage);
    expect(symlinkAsset.status
               == projectname::ProjectPackageSaveAsRetryPreflightStatus::missingPackageAsset,
           "Save As retry preflight rejects copied asset symlink conflicts");
    expect(symlinkAsset.message.find("copied target package asset is a symlink")
               != std::string::npos,
           "Save As retry asset symlink failure error is human-readable");
    expect(symlinkAsset.path == assetSymlinkPath,
           "Save As retry asset symlink failure reports the symlink path");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(assetSymlinkPath)),
           "Save As retry preflight leaves the copied asset symlink unchanged");
    expect(readTextFile(symlinkTargetPath) == "external audio target",
           "Save As retry preflight does not overwrite or remove the asset symlink target");
    expect(readTextFile(targetPackage / imported.analysisPath) == "{}",
           "Save As retry preflight does not recopy or mutate other copied target assets");
    expect(!std::filesystem::exists(targetPackage / "manifest.json"),
           "Save As retry asset symlink failure does not write a final manifest");
    expect(!std::filesystem::exists(targetPackage / "manifest.json.tmp"),
           "Save As retry asset symlink failure does not create a temporary manifest");

    std::filesystem::remove_all(sourcePackage);
    std::filesystem::remove_all(targetPackage);
}

void projectPackageSaveAsRetryPreflightRejectsMissingCopiedAssetsAndStaleState()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "save-as-retry-missing-imported";
    imported.name = "Save As Retry Missing Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/missing.wav";
    imported.analysisPath = "analysis/missing.waveform.json";
    imported.lengthBeats = 4.0;
    expect(project.addClipToTrack("track-1", imported),
           "Save As retry missing test adds imported clip");

    const auto sourcePackage = makeTemporaryPackagePath("projectname-save-as-retry-missing-source-test");
    const auto targetPackage = makeTemporaryPackagePath("projectname-save-as-retry-missing-target-test");
    writeTextFile(targetPackage / imported.analysisPath, "{}");

    projectname::ProjectPackageSaveAsRetryState recovery;
    recovery.sourcePackageDirectory = sourcePackage;
    recovery.targetPackageDirectory = targetPackage;
    recovery.projectSnapshot = project;
    recovery.copiedFileCount = 2;

    const auto missingAsset = projectname::preflightProjectPackageSaveAsRetry(&recovery,
                                                                              project,
                                                                              sourcePackage);
    expect(missingAsset.status == projectname::ProjectPackageSaveAsRetryPreflightStatus::missingPackageAsset,
           "Save As retry preflight rejects missing copied target assets: "
               + missingAsset.message);
    expect(!std::filesystem::exists(targetPackage / "manifest.json"),
           "Save As retry preflight does not write a manifest when copied assets are missing");

    auto changedProject = project;
    changedProject.setName("Changed After Failed Save As");
    const auto changedState = projectname::preflightProjectPackageSaveAsRetry(&recovery,
                                                                              changedProject,
                                                                              sourcePackage);
    expect(changedState.status == projectname::ProjectPackageSaveAsRetryPreflightStatus::projectChanged,
           "Save As retry preflight rejects mutated project state");

    recovery.copiedFileCount = 0;
    const auto noCopiedAssets = projectname::preflightProjectPackageSaveAsRetry(&recovery,
                                                                                project,
                                                                                sourcePackage);
    expect(noCopiedAssets.status == projectname::ProjectPackageSaveAsRetryPreflightStatus::noCopiedAssets,
           "Save As retry preflight rejects failed Save As state without copied assets");

    std::filesystem::remove_all(targetPackage);
}

void projectLoopRegionValidatesAndRoundTrips()
{
    auto project = projectname::ProjectModel::createDefault();
    expect(!project.getLoopRegion().enabled, "Default project loop region starts disabled");

    std::string error;
    expect(project.setLoopRegion(8.0, 4.0, error), "Project accepts valid loop region");
    expect(project.getLoopRegion().enabled, "Project loop region enables after set");
    expect(std::abs(project.getLoopRegion().startBeats - 8.0) < 0.0001,
           "Project loop region stores start beat");
    expect(std::abs(project.getLoopRegion().lengthBeats - 4.0) < 0.0001,
           "Project loop region stores length beats");

    expect(!project.setLoopRegion(-1.0, 4.0, error), "Project rejects negative loop start");
    expect(!project.setLoopRegion(4.0, 0.0, error), "Project rejects zero loop length");
    expect(!project.setLoopRegion(std::numeric_limits<double>::quiet_NaN(), 4.0, error),
           "Project rejects non-finite loop start");
    expect(std::abs(project.getLoopRegion().startBeats - 8.0) < 0.0001
               && std::abs(project.getLoopRegion().lengthBeats - 4.0) < 0.0001,
           "Rejected loop region leaves previous loop unchanged");

    const auto package = makeTemporaryPackagePath("projectname-loop-region-test");
    expect(project.savePackage(package, error), "Loop region project saves");

    const auto manifestText = readTextFile(package / "manifest.json");
    expect(manifestText.find("\"loopRegion\"") != std::string::npos,
           "Project manifest persists loop region object");
    expect(manifestText.find("\"enabled\": true") != std::string::npos,
           "Project manifest persists enabled loop region");

    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(loaded.has_value(), "Loop region project loads");
    expect(loaded.has_value() && *loaded == project, "Loop region round trips through manifest");

    project.clearLoopRegion();
    expect(!project.getLoopRegion().enabled, "Project clears loop region");
    expect(project.getLoopRegion().startBeats == 0.0 && project.getLoopRegion().lengthBeats == 0.0,
           "Cleared loop region resets beats");
    expect(project.savePackage(package, error), "Cleared loop region project saves");
    loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(loaded.has_value() && !loaded->getLoopRegion().enabled,
           "Cleared loop region round trips as disabled");

    expect(std::filesystem::remove_all(package) > 0, "Temporary loop region package deleted");
}

void projectImportedClipSelectionValidatesAndRoundTrips()
{
    auto project = projectname::ProjectModel::createDefault();
    expect(project.getSelectedClipId().empty(), "Default project starts with no selected clip");

    projectname::ProjectClip imported;
    imported.id = "clip-selection-imported";
    imported.name = "Selection Imported";
    imported.type = "audio-file";
    imported.relativePath = "audio/selection.wav";
    imported.analysisPath = "analysis/selection.waveform.json";
    imported.startBeats = 4.0;
    imported.lengthBeats = 2.0;
    expect(project.addClipToTrack("track-1", imported), "Project selection test adds imported clip");

    std::string error;
    expect(project.selectImportedAudioClip(imported.id, error),
           "Project selects imported audio clip");
    expect(project.getSelectedClipId() == imported.id,
           "Project stores selected imported clip id");

    expect(!project.selectImportedAudioClip("clip-1", error),
           "Project rejects generated clip selection");
    expect(project.getSelectedClipId() == imported.id,
           "Rejected generated selection leaves previous selection");
    expect(!project.selectImportedAudioClip("missing-clip", error),
           "Project rejects missing imported clip selection");
    expect(project.getSelectedClipId() == imported.id,
           "Rejected missing selection leaves previous selection");

    const auto package = makeTemporaryPackagePath("projectname-clip-selection-test");
    expect(project.savePackage(package, error), "Selected clip project package saves");
    const auto manifestText = readTextFile(package / "manifest.json");
    expect(manifestText.find("\"selection\"") != std::string::npos,
           "Project manifest persists selection object");
    expect(manifestText.find(imported.id) != std::string::npos,
           "Project manifest persists selected clip id");

    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(loaded.has_value() && *loaded == project,
           "Selected clip round trips through manifest");
    expect(loaded.has_value() && loaded->getSelectedClipId() == imported.id,
           "Loaded project restores selected clip id");

    project.clearSelectedClip();
    expect(project.getSelectedClipId().empty(), "Project clears selected clip");

    expect(std::filesystem::remove_all(package) > 0, "Temporary selected clip package deleted");
}

void projectTrackMixStateRoundTripsAndLoadsLegacyDefaults()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Track Mix State Test");

    projectname::ProjectTrack mixedTrack;
    mixedTrack.id = "track-mix-round-trip";
    mixedTrack.name = "Mixed Track";
    mixedTrack.type = "audio";
    mixedTrack.volume = 0.625f;
    mixedTrack.pan = -0.25f;
    mixedTrack.muted = true;
    mixedTrack.solo = true;
    project.addTrack(mixedTrack);

    const auto package = makeTemporaryPackagePath("projectname-track-mix-test");
    std::string error;
    expect(project.savePackage(package, error), "Track mix project package saves");

    const auto manifestText = readTextFile(package / "manifest.json");
    expect(manifestText.find("\"volume\": 0.625") != std::string::npos,
           "Project manifest persists track volume");
    expect(manifestText.find("\"pan\": -0.25") != std::string::npos,
           "Project manifest persists track pan");
    expect(manifestText.find("\"muted\": true") != std::string::npos,
           "Project manifest persists track mute state");
    expect(manifestText.find("\"solo\": true") != std::string::npos,
           "Project manifest persists track solo state");

    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(loaded.has_value() && *loaded == project,
           "Track mix state round trips through manifest");

    expect(std::filesystem::remove_all(package) > 0, "Temporary track mix package deleted");

    const auto legacyPackage = makeTemporaryPackagePath("projectname-track-mix-legacy-test");
    writeManifestText(legacyPackage, R"({
  "manifestVersion": 1,
  "name": "Legacy Mix Defaults",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "positionBeats": 0
  },
  "tracks": [
    {
      "id": "track-legacy-mix",
      "name": "Legacy Mix",
      "type": "audio",
      "clips": []
    }
  ]
})");

    auto legacy = projectname::ProjectModel::loadPackage(legacyPackage, error);
    expect(legacy.has_value(), "Legacy track mix manifest loads");
    expect(legacy.has_value() && std::abs(legacy->getTracks().front().volume - 1.0f) < 0.0001f,
           "Legacy track mix defaults volume to unity");
    expect(legacy.has_value() && std::abs(legacy->getTracks().front().pan) < 0.0001f,
           "Legacy track mix defaults pan to center");
    expect(legacy.has_value() && !legacy->getTracks().front().muted,
           "Legacy track mix defaults mute off");
    expect(legacy.has_value() && !legacy->getTracks().front().solo,
           "Legacy track mix defaults solo off");

    expect(std::filesystem::remove_all(legacyPackage) > 0, "Temporary legacy track mix package deleted");
}

void projectSavePackagePathFileFailureLeavesOccupiedPathUntouched()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Package Path File Failure Test");
    project.getTransport().setTempoBpm(99.0);

    const auto occupiedPackagePath =
        makeTemporaryPackagePath("projectname-save-package-file-failure-test");
    writeTextFile(occupiedPackagePath, "occupied project package path");

    std::string error;
    expect(!project.savePackage(occupiedPackagePath, error),
           "Project save reports package path file failure");
    expect(error.find("Project package path points to a file") != std::string::npos,
           "Package path file failure error is human-readable");
    expect(std::filesystem::is_regular_file(occupiedPackagePath),
           "Package path file failure leaves occupied package path as a file");
    expect(readTextFile(occupiedPackagePath) == "occupied project package path",
           "Package path file failure preserves occupied package file contents");
    expect(!std::filesystem::exists(occupiedPackagePath / "manifest.json"),
           "Package path file failure does not create a manifest below occupied path");
    expect(!std::filesystem::exists(occupiedPackagePath / "manifest.json.tmp"),
           "Package path file failure does not create a temporary manifest below occupied path");
    expect(!std::filesystem::exists(occupiedPackagePath / "audio"),
           "Package path file failure does not create an audio asset folder");
    expect(!std::filesystem::exists(occupiedPackagePath / "samples"),
           "Package path file failure does not create a samples asset folder");
    expect(!std::filesystem::exists(occupiedPackagePath / "presets"),
           "Package path file failure does not create a presets asset folder");
    expect(!std::filesystem::exists(occupiedPackagePath / "analysis"),
           "Package path file failure does not create an analysis asset folder");
    expect(!std::filesystem::exists(occupiedPackagePath / "backups"),
           "Package path file failure does not create a backups asset folder");

    expect(std::filesystem::remove(occupiedPackagePath),
           "Temporary occupied project package path file deleted");
}

void projectSavePackageDirectoryCreationFailureLeavesOccupiedParentUntouched()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Package Directory Creation Failure Test");
    project.getTransport().setTempoBpm(102.0);

    const auto occupiedParentPath =
        makeTemporaryPackagePath("projectname-save-package-parent-file-failure-test");
    const auto targetPackage = occupiedParentPath / "Nested Target.project";
    writeTextFile(occupiedParentPath, "occupied intermediate package parent");

    std::string error;
    expect(!project.savePackage(targetPackage, error),
           "Project save reports package directory creation failure");
    expect(error.find("Could not create project package directory") != std::string::npos,
           "Package directory creation failure error is human-readable");
    expect(std::filesystem::is_regular_file(occupiedParentPath),
           "Package directory creation failure leaves occupied parent as a file");
    expect(readTextFile(occupiedParentPath) == "occupied intermediate package parent",
           "Package directory creation failure preserves occupied parent contents");
    expect(!std::filesystem::exists(targetPackage),
           "Package directory creation failure does not create the target package directory");
    expect(!std::filesystem::exists(targetPackage / "manifest.json"),
           "Package directory creation failure does not create a manifest below occupied parent");
    expect(!std::filesystem::exists(targetPackage / "manifest.json.tmp"),
           "Package directory creation failure does not create a temporary manifest below occupied parent");
    expect(!std::filesystem::exists(targetPackage / "audio"),
           "Package directory creation failure does not create an audio asset folder");
    expect(!std::filesystem::exists(targetPackage / "samples"),
           "Package directory creation failure does not create a samples asset folder");
    expect(!std::filesystem::exists(targetPackage / "presets"),
           "Package directory creation failure does not create a presets asset folder");
    expect(!std::filesystem::exists(targetPackage / "analysis"),
           "Package directory creation failure does not create an analysis asset folder");
    expect(!std::filesystem::exists(targetPackage / "backups"),
           "Package directory creation failure does not create a backups asset folder");

    expect(std::filesystem::remove(occupiedParentPath),
           "Temporary occupied package parent path file deleted");
}

void projectSavePackageSymlinkPathFailureLeavesTargetUntouched()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Package Symlink Failure Test");
    project.getTransport().setTempoBpm(108.0);

    const auto linkedPackageTarget =
        makeTemporaryPackagePath("projectname-save-package-symlink-target-test");
    const auto packageSymlink =
        makeTemporaryPackagePath("projectname-save-package-symlink-link-test");
    const auto linkedPackageSentinel = linkedPackageTarget / "sentinel.txt";
    writeTextFile(linkedPackageSentinel, "linked package target sentinel");

    const auto linkedParentTarget =
        makeTemporaryPackagePath("projectname-save-package-parent-symlink-target-test");
    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-save-package-parent-symlink-link-test");
    const auto linkedParentSentinel = linkedParentTarget / "sentinel.txt";
    const auto nestedTargetPackage = parentSymlink / "Nested Target.project";
    const auto nestedTargetThroughLink = linkedParentTarget / "Nested Target.project";
    writeTextFile(linkedParentSentinel, "linked parent target sentinel");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedPackageTarget, packageSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(linkedPackageTarget);
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    symlinkError.clear();
    std::filesystem::create_directory_symlink(linkedParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(packageSymlink);
        std::filesystem::remove_all(linkedPackageTarget);
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    std::string error;
    expect(!project.savePackage(packageSymlink, error),
           "Project save rejects a package path that is a directory symlink");
    expect(error.find("Project package path") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "Package symlink failure error is human-readable");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(packageSymlink)),
           "Package symlink failure leaves the package symlink unchanged");
    expect(readTextFile(linkedPackageSentinel) == "linked package target sentinel",
           "Package symlink failure preserves the target sentinel");
    expect(!std::filesystem::exists(linkedPackageTarget / "manifest.json"),
           "Package symlink failure does not write a manifest through the symlink");
    expect(!std::filesystem::exists(linkedPackageTarget / "manifest.json.tmp"),
           "Package symlink failure does not write a temporary manifest through the symlink");
    expect(!std::filesystem::exists(linkedPackageTarget / "audio"),
           "Package symlink failure does not create an audio folder through the symlink");
    expect(!std::filesystem::exists(linkedPackageTarget / "samples"),
           "Package symlink failure does not create a samples folder through the symlink");
    expect(!std::filesystem::exists(linkedPackageTarget / "presets"),
           "Package symlink failure does not create a presets folder through the symlink");
    expect(!std::filesystem::exists(linkedPackageTarget / "analysis"),
           "Package symlink failure does not create an analysis folder through the symlink");
    expect(!std::filesystem::exists(linkedPackageTarget / "backups"),
           "Package symlink failure does not create a backups folder through the symlink");

    error.clear();
    expect(!project.savePackage(nestedTargetPackage, error),
           "Project save rejects an intermediate package parent symlink");
    expect(error.find("Project package path") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "Package parent symlink failure error is human-readable");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Package parent symlink failure leaves the parent symlink unchanged");
    expect(readTextFile(linkedParentSentinel) == "linked parent target sentinel",
           "Package parent symlink failure preserves the target sentinel");
    expect(!std::filesystem::exists(nestedTargetThroughLink),
           "Package parent symlink failure does not create the nested package target");
    expect(!std::filesystem::exists(nestedTargetThroughLink / "manifest.json"),
           "Package parent symlink failure does not write a manifest through the symlink");
    expect(!std::filesystem::exists(nestedTargetThroughLink / "manifest.json.tmp"),
           "Package parent symlink failure does not write a temporary manifest through the symlink");
    expect(!std::filesystem::exists(nestedTargetThroughLink / "audio"),
           "Package parent symlink failure does not create an audio folder through the symlink");
    expect(!std::filesystem::exists(nestedTargetThroughLink / "samples"),
           "Package parent symlink failure does not create a samples folder through the symlink");
    expect(!std::filesystem::exists(nestedTargetThroughLink / "presets"),
           "Package parent symlink failure does not create a presets folder through the symlink");
    expect(!std::filesystem::exists(nestedTargetThroughLink / "analysis"),
           "Package parent symlink failure does not create an analysis folder through the symlink");
    expect(!std::filesystem::exists(nestedTargetThroughLink / "backups"),
           "Package parent symlink failure does not create a backups folder through the symlink");

    expect(std::filesystem::remove(packageSymlink),
           "Temporary package path symlink deleted");
    expect(std::filesystem::remove(parentSymlink),
           "Temporary package parent symlink deleted");
    expect(std::filesystem::remove_all(linkedPackageTarget) > 0,
           "Temporary package symlink target deleted");
    expect(std::filesystem::remove_all(linkedParentTarget) > 0,
           "Temporary package parent symlink target deleted");
}

void projectSaveCreatesPreviousManifestBackup()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Before Backup");
    project.getTransport().setTempoBpm(90.0);

    const auto package = makeTemporaryPackagePath("projectname-backup-test");

    std::string error;
    expect(project.savePackage(package, error), "Initial project package save succeeds");
    expect(!std::filesystem::exists(package / "backups" / "manifest.previous.json"),
           "Initial save does not create previous manifest backup");

    project.setName("After Backup");
    project.getTransport().setTempoBpm(132.0);
    expect(project.savePackage(package, error), "Second project package save succeeds");

    const auto backupPath = package / "backups" / "manifest.previous.json";
    expect(std::filesystem::is_regular_file(backupPath), "Second save creates previous manifest backup");
    expect(!std::filesystem::exists(package / "manifest.json.tmp"),
           "Temporary manifest is cleaned up after backup save");

    const auto backupText = readTextFile(backupPath);
    expect(backupText.find("Before Backup") != std::string::npos,
           "Previous manifest backup contains old project name");
    expect(backupText.find("After Backup") == std::string::npos,
           "Previous manifest backup does not contain new project name");

    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(loaded.has_value(), "Project loads after backup save");
    expect(loaded.has_value() && *loaded == project, "Current manifest contains newest project state");

    expect(std::filesystem::remove_all(package) > 0, "Temporary backup project package deleted");
}

void projectSaveBackupFailureKeepsManifestAndRemovesTemporaryManifest()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Before Backup Failure");
    project.getTransport().setTempoBpm(95.0);

    const auto package = makeTemporaryPackagePath("projectname-save-backup-failure-test");

    std::string error;
    expect(project.savePackage(package, error), "Initial backup-failure project package save succeeds");

    const auto manifestPath = package / "manifest.json";
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto originalManifestText = readTextFile(manifestPath);
    expect(originalManifestText.find("Before Backup Failure") != std::string::npos,
           "Backup failure fixture starts with the original manifest state");

    const auto backupPath = package / "backups" / "manifest.previous.json";
    const auto backupSentinelPath = backupPath / "occupied.txt";
    writeTextFile(backupSentinelPath, "occupied backup path");

    project.setName("After Backup Failure");
    project.getTransport().setTempoBpm(150.0);

    expect(!project.savePackage(package, error), "Project save reports manifest backup failure");
    expect(error.find("Could not create project manifest backup") != std::string::npos,
           "Manifest backup failure error is human-readable");
    expect(!std::filesystem::exists(temporaryManifestPath),
           "Manifest backup failure removes the staged temporary manifest");

    const auto activeManifestText = readTextFile(manifestPath);
    expect(activeManifestText == originalManifestText,
           "Manifest backup failure leaves the active manifest unchanged");
    expect(activeManifestText.find("After Backup Failure") == std::string::npos,
           "Manifest backup failure does not commit the new project state");
    expect(std::filesystem::is_directory(backupPath),
           "Manifest backup failure leaves the occupied backup path unchanged");
    expect(readTextFile(backupSentinelPath) == "occupied backup path",
           "Manifest backup failure preserves occupied backup path contents");

    expect(std::filesystem::remove_all(package) > 0,
           "Temporary backup-failure project package deleted");
}

void projectSaveRemovesStaleTemporaryManifestSymlinkWithoutFollowingIt()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Temporary Manifest Symlink Cleanup Test");
    project.getTransport().setTempoBpm(118.0);

    const auto package = makeTemporaryPackagePath("projectname-save-temp-symlink-cleanup-test");
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto temporarySymlinkTargetPath =
        makeTemporarySettingsPath("projectname-save-temp-symlink-target-test");
    writeTextFile(temporarySymlinkTargetPath, "temporary manifest symlink target sentinel");
    std::filesystem::create_directories(package);

    std::error_code symlinkError;
    std::filesystem::create_symlink(temporarySymlinkTargetPath, temporaryManifestPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        std::filesystem::remove(temporarySymlinkTargetPath);
        return;
    }

    std::string error = "stale temporary manifest symlink cleanup error";
    expect(project.savePackage(package, error),
           "Project save succeeds after removing a stale temporary manifest symlink");
    expect(error.empty(),
           "Project save stale temporary manifest symlink cleanup leaves error empty");
    expect(std::filesystem::is_regular_file(package / "manifest.json"),
           "Project save stale temporary manifest symlink cleanup writes a real manifest");
    expect(!std::filesystem::exists(temporaryManifestPath),
           "Project save stale temporary manifest symlink cleanup removes the temporary link");
    expect(readTextFile(temporarySymlinkTargetPath) == "temporary manifest symlink target sentinel",
           "Project save stale temporary manifest symlink cleanup preserves the symlink target");
    expect(readTextFile(package / "manifest.json").find("Temporary Manifest Symlink Cleanup Test")
               != std::string::npos,
           "Project save stale temporary manifest symlink cleanup commits the requested project");

    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(loaded.has_value() && *loaded == project,
           "Project save stale temporary manifest symlink cleanup commits loadable project state");

    expect(std::filesystem::remove_all(package) > 0,
           "Temporary manifest-symlink cleanup package deleted");
    expect(std::filesystem::remove(temporarySymlinkTargetPath),
           "Temporary manifest-symlink cleanup target deleted");
}

void projectSaveRemovesStaleBrokenTemporaryManifestSymlinkWithoutFollowingIt()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Broken Temporary Manifest Symlink Cleanup Test");
    project.getTransport().setTempoBpm(121.0);

    const auto package =
        makeTemporaryPackagePath("projectname-save-broken-temp-symlink-cleanup-test");
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto brokenTemporarySymlinkTargetPath =
        package / "missing-temporary-manifest-target.json";

    std::filesystem::create_directories(package);

    std::error_code symlinkError;
    std::filesystem::create_symlink(brokenTemporarySymlinkTargetPath,
                                    temporaryManifestPath,
                                    symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        return;
    }

    std::string error = "stale broken temporary manifest symlink cleanup error";
    expect(project.savePackage(package, error),
           "Project save succeeds after removing a stale broken temporary manifest symlink");
    expect(error.empty(),
           "Project save stale broken temporary manifest symlink cleanup leaves error empty");
    expect(std::filesystem::is_regular_file(package / "manifest.json"),
           "Project save stale broken temporary manifest symlink cleanup writes a real manifest");
    expect(!std::filesystem::is_symlink(std::filesystem::symlink_status(package / "manifest.json")),
           "Project save stale broken temporary manifest symlink cleanup does not commit a manifest symlink");
    expect(!std::filesystem::exists(temporaryManifestPath),
           "Project save stale broken temporary manifest symlink cleanup removes the temporary link");
    expect(!std::filesystem::exists(brokenTemporarySymlinkTargetPath),
           "Project save stale broken temporary manifest symlink cleanup does not create the missing target");
    expect(readTextFile(package / "manifest.json")
               .find("Broken Temporary Manifest Symlink Cleanup Test") != std::string::npos,
           "Project save stale broken temporary manifest symlink cleanup commits the requested project");

    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(loaded.has_value() && *loaded == project,
           "Project save stale broken temporary manifest symlink cleanup commits loadable project state");

    expect(std::filesystem::remove_all(package) > 0,
           "Temporary broken manifest-symlink cleanup package deleted");
}

void projectSaveManifestSymlinkFailureLeavesTargetUntouched()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Before Manifest Symlink Failure");
    project.getTransport().setTempoBpm(104.0);

    const auto package = makeTemporaryPackagePath("projectname-save-manifest-symlink-test");

    std::string error;
    expect(project.savePackage(package, error), "Initial manifest-symlink project save succeeds");

    const auto manifestPath = package / "manifest.json";
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto linkedManifestTarget =
        makeTemporarySettingsPath("projectname-save-manifest-symlink-target-test");
    const auto originalManifestText = readTextFile(manifestPath);
    expect(originalManifestText.find("Before Manifest Symlink Failure") != std::string::npos,
           "Manifest symlink failure fixture starts with the original manifest state");

    expect(std::filesystem::remove(manifestPath),
           "Manifest symlink failure fixture removes the package manifest before linking");
    writeTextFile(linkedManifestTarget, originalManifestText);
    writeTextFile(temporaryManifestPath, "stale temporary manifest before symlink failure");

    std::error_code symlinkError;
    std::filesystem::create_symlink(linkedManifestTarget, manifestPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        std::filesystem::remove(linkedManifestTarget);
        return;
    }

    project.setName("After Manifest Symlink Failure");
    project.getTransport().setTempoBpm(154.0);

    error = "stale manifest symlink failure error";
    expect(!project.savePackage(package, error),
           "Project save rejects a symlink manifest path");
    expect(error.find("manifest") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "Manifest symlink save failure error is human-readable");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(manifestPath)),
           "Manifest symlink save failure leaves the manifest symlink unchanged");
    expect(readTextFile(linkedManifestTarget) == originalManifestText,
           "Manifest symlink save failure preserves the linked manifest target");
    expect(readTextFile(linkedManifestTarget).find("After Manifest Symlink Failure") == std::string::npos,
           "Manifest symlink save failure does not write the new project state through the symlink");
    expect(!std::filesystem::exists(temporaryManifestPath),
           "Manifest symlink save failure removes stale temporary manifest before rejecting");
    expect(!std::filesystem::exists(package / "backups" / "manifest.previous.json"),
           "Manifest symlink save failure happens before previous-manifest backup creation");

    expect(std::filesystem::remove(manifestPath),
           "Temporary save manifest symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary save manifest-symlink package deleted");
    expect(std::filesystem::remove(linkedManifestTarget),
           "Temporary save manifest-symlink target deleted");
}

void projectSaveBrokenManifestSymlinkFailureLeavesTargetUntouched()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Before Broken Manifest Symlink Failure");
    project.getTransport().setTempoBpm(106.0);

    const auto package =
        makeTemporaryPackagePath("projectname-save-broken-manifest-symlink-test");

    std::string error;
    expect(project.savePackage(package, error),
           "Initial broken-manifest-symlink project save succeeds");

    const auto manifestPath = package / "manifest.json";
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto brokenManifestTarget = package / "missing-manifest-target.json";
    const auto originalManifestText = readTextFile(manifestPath);
    expect(originalManifestText.find("Before Broken Manifest Symlink Failure") != std::string::npos,
           "Broken manifest symlink fixture starts with the original manifest state");

    writeTextFile(package / "audio" / "sentinel.txt", "audio sentinel");
    writeTextFile(package / "samples" / "sentinel.txt", "samples sentinel");
    writeTextFile(package / "presets" / "sentinel.txt", "presets sentinel");
    writeTextFile(package / "analysis" / "sentinel.txt", "analysis sentinel");
    writeTextFile(package / "backups" / "sentinel.txt", "backups sentinel");
    writeTextFile(temporaryManifestPath, "stale temporary manifest before broken symlink failure");

    expect(std::filesystem::remove(manifestPath),
           "Broken manifest symlink fixture removes the package manifest before linking");

    std::error_code symlinkError;
    std::filesystem::create_symlink(brokenManifestTarget, manifestPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        return;
    }

    project.setName("After Broken Manifest Symlink Failure");
    project.getTransport().setTempoBpm(156.0);

    error = "stale broken manifest symlink failure error";
    expect(!project.savePackage(package, error),
           "Project save rejects a broken symlink manifest path");
    expect(error.find("manifest") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "Broken manifest symlink save failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "Broken manifest symlink save failure is not reported as missing manifest");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(manifestPath)),
           "Broken manifest symlink save failure leaves the manifest symlink unchanged");
    expect(!std::filesystem::exists(brokenManifestTarget),
           "Broken manifest symlink save failure does not create the missing target");
    expect(!std::filesystem::exists(temporaryManifestPath),
           "Broken manifest symlink save failure removes stale temporary manifest before rejecting");
    expect(!std::filesystem::exists(package / "backups" / "manifest.previous.json"),
           "Broken manifest symlink save failure happens before previous-manifest backup creation");
    expect(readTextFile(package / "audio" / "sentinel.txt") == "audio sentinel",
           "Broken manifest symlink save failure preserves audio folder contents");
    expect(readTextFile(package / "samples" / "sentinel.txt") == "samples sentinel",
           "Broken manifest symlink save failure preserves samples folder contents");
    expect(readTextFile(package / "presets" / "sentinel.txt") == "presets sentinel",
           "Broken manifest symlink save failure preserves presets folder contents");
    expect(readTextFile(package / "analysis" / "sentinel.txt") == "analysis sentinel",
           "Broken manifest symlink save failure preserves analysis folder contents");
    expect(readTextFile(package / "backups" / "sentinel.txt") == "backups sentinel",
           "Broken manifest symlink save failure preserves backups folder contents");

    expect(std::filesystem::remove(manifestPath),
           "Temporary broken save manifest symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary broken save manifest-symlink package deleted");
}

void projectSaveTemporaryManifestOpenFailureKeepsManifestAndOccupiedPath()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Before Temporary Manifest Failure");
    project.getTransport().setTempoBpm(96.0);

    const auto package = makeTemporaryPackagePath("projectname-save-temp-open-failure-test");

    std::string error;
    expect(project.savePackage(package, error), "Initial temporary-manifest failure project save succeeds");
    const auto manifestBeforeFailure = readTextFile(package / "manifest.json");

    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto temporarySentinelPath = temporaryManifestPath / "occupied.txt";
    writeTextFile(temporarySentinelPath, "occupied temporary manifest path");

    project.setName("After Temporary Manifest Failure");
    project.getTransport().setTempoBpm(140.0);

    expect(!project.savePackage(package, error),
           "Project save reports temporary manifest open failure");
    expect(error.find("Could not write temporary project manifest") != std::string::npos,
           "Temporary manifest open failure error is human-readable");
    expect(readTextFile(package / "manifest.json") == manifestBeforeFailure,
           "Temporary manifest open failure leaves the active manifest unchanged");
    expect(manifestBeforeFailure.find("Before Temporary Manifest Failure") != std::string::npos,
           "Temporary manifest open failure baseline contains old project name");
    expect(readTextFile(package / "manifest.json").find("After Temporary Manifest Failure") == std::string::npos,
           "Temporary manifest open failure does not commit the new project state");
    expect(std::filesystem::is_directory(temporaryManifestPath),
           "Temporary manifest open failure leaves the occupied temporary path unchanged");
    expect(readTextFile(temporarySentinelPath) == "occupied temporary manifest path",
           "Temporary manifest open failure preserves occupied temporary path contents");
    expect(!std::filesystem::exists(package / "backups" / "manifest.previous.json"),
           "Temporary manifest open failure happens before previous-manifest backup creation");

    expect(std::filesystem::remove_all(package) > 0,
           "Temporary manifest-open-failure project package deleted");
}

void projectSaveFailsBeforeManifestCommitWhenAssetFolderPathIsFile()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Asset Folder Conflict Test");

    const auto package = makeTemporaryPackagePath("projectname-save-asset-folder-conflict-test");
    const auto conflictingAudioPath = package / "audio";
    writeTextFile(conflictingAudioPath, "occupied audio folder path");

    std::string error;
    expect(!project.savePackage(package, error), "Project save reports asset folder creation failure");
    expect(error.find("Could not create asset folder: audio") != std::string::npos,
           "Asset folder creation failure error is human-readable");
    expect(!std::filesystem::exists(package / "manifest.json"),
           "Asset folder creation failure does not write a project manifest");
    expect(!std::filesystem::exists(package / "manifest.json.tmp"),
           "Asset folder creation failure does not leave a temporary manifest");
    expect(std::filesystem::is_regular_file(conflictingAudioPath),
           "Asset folder creation failure leaves the occupied path unchanged");
    expect(readTextFile(conflictingAudioPath) == "occupied audio folder path",
           "Asset folder creation failure preserves occupied path contents");

    expect(std::filesystem::remove_all(package) > 0,
           "Temporary asset-folder conflict package deleted");
}

void projectSaveFailsBeforeManifestCommitWhenAssetFolderPathIsSymlink()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Asset Folder Symlink Test");

    const auto package = makeTemporaryPackagePath("projectname-save-asset-folder-symlink-test");
    const auto linkedAudioTarget =
        makeTemporaryPackagePath("projectname-save-asset-folder-symlink-target-test");
    const auto linkedAudioSentinel = linkedAudioTarget / "sentinel.txt";
    writeTextFile(linkedAudioSentinel, "linked audio target sentinel");
    std::filesystem::create_directories(package);

    const auto audioSymlinkPath = package / "audio";
    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedAudioTarget, audioSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        std::filesystem::remove_all(linkedAudioTarget);
        return;
    }

    std::string error;
    expect(!project.savePackage(package, error), "Project save reports asset folder symlink failure");
    expect(error.find("asset folder") != std::string::npos
               && error.find("symlink") != std::string::npos
               && error.find("audio") != std::string::npos,
           "Asset folder symlink failure error is human-readable");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(audioSymlinkPath)),
           "Asset folder symlink failure leaves the audio symlink unchanged");
    expect(readTextFile(linkedAudioSentinel) == "linked audio target sentinel",
           "Asset folder symlink failure preserves the linked audio target");
    expect(!std::filesystem::exists(package / "manifest.json"),
           "Asset folder symlink failure does not write a project manifest");
    expect(!std::filesystem::exists(package / "manifest.json.tmp"),
           "Asset folder symlink failure does not leave a temporary manifest");
    expect(!std::filesystem::exists(package / "samples"),
           "Asset folder symlink failure stops before creating later asset folders");
    expect(!std::filesystem::exists(linkedAudioTarget / "manifest.json"),
           "Asset folder symlink failure does not write a manifest through the link target");
    expect(!std::filesystem::exists(linkedAudioTarget / "manifest.json.tmp"),
           "Asset folder symlink failure does not write a temporary manifest through the link target");

    expect(std::filesystem::remove(audioSymlinkPath),
           "Temporary asset-folder symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary asset-folder symlink package deleted");
    expect(std::filesystem::remove_all(linkedAudioTarget) > 0,
           "Temporary linked asset-folder target deleted");
}

void projectSaveFailsBeforeManifestCommitWhenAssetFolderPathIsBrokenSymlink()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Broken Asset Folder Symlink Test");

    const auto package =
        makeTemporaryPackagePath("projectname-save-broken-asset-folder-symlink-test");
    const auto missingAudioTarget =
        makeTemporaryPackagePath("projectname-save-broken-asset-folder-symlink-target-test");
    const auto audioSymlinkPath = package / "audio";
    std::filesystem::create_directories(package);

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingAudioTarget, audioSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        return;
    }

    std::string error = "stale broken asset-folder symlink failure error";
    expect(!project.savePackage(package, error),
           "Project save rejects a broken symlink asset folder path");
    expect(error.find("asset folder") != std::string::npos
               && error.find("symlink") != std::string::npos
               && error.find("audio") != std::string::npos,
           "Broken asset-folder symlink failure error is human-readable");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(audioSymlinkPath)),
           "Broken asset-folder symlink failure leaves the audio symlink unchanged");
    expect(!std::filesystem::exists(missingAudioTarget),
           "Broken asset-folder symlink failure does not create the missing target");
    expect(!std::filesystem::exists(package / "manifest.json"),
           "Broken asset-folder symlink failure does not write a project manifest");
    expect(!std::filesystem::exists(package / "manifest.json.tmp"),
           "Broken asset-folder symlink failure does not leave a temporary manifest");
    expect(!std::filesystem::exists(package / "samples"),
           "Broken asset-folder symlink failure stops before creating later asset folders");
    expect(!std::filesystem::exists(missingAudioTarget / "manifest.json"),
           "Broken asset-folder symlink failure does not write a manifest through the link target");
    expect(!std::filesystem::exists(missingAudioTarget / "manifest.json.tmp"),
           "Broken asset-folder symlink failure does not write a temporary manifest through the link target");

    expect(std::filesystem::remove(audioSymlinkPath),
           "Temporary broken asset-folder symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary broken asset-folder symlink package deleted");
    std::filesystem::remove_all(missingAudioTarget);
}

void projectSaveRejectsBrokenLaterAssetFolderSymlinkBeforeManifestStaging()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Before Broken Later Asset Folder Symlink");

    const auto package =
        makeTemporaryPackagePath("projectname-save-broken-later-asset-folder-symlink-test");

    std::string error;
    expect(project.savePackage(package, error),
           "Initial later asset-folder symlink fixture save succeeds");

    const auto manifestPath = package / "manifest.json";
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto originalManifestText = readTextFile(manifestPath);
    expect(originalManifestText.find("Before Broken Later Asset Folder Symlink") != std::string::npos,
           "Broken later asset-folder symlink fixture starts with the original manifest state");

    writeTextFile(package / "audio" / "sentinel.txt", "audio sentinel before later symlink failure");
    writeTextFile(temporaryManifestPath,
                  "stale temporary manifest before broken later asset-folder symlink failure");

    const auto samplesSymlinkPath = package / "samples";
    const auto missingSamplesTarget =
        makeTemporaryPackagePath("projectname-save-broken-later-asset-folder-symlink-target-test");
    expect(std::filesystem::remove_all(samplesSymlinkPath) > 0,
           "Broken later asset-folder symlink fixture removes initial samples folder");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingSamplesTarget, samplesSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        return;
    }

    project.setName("After Broken Later Asset Folder Symlink");
    error = "stale broken later asset-folder symlink failure error";
    expect(!project.savePackage(package, error),
           "Project save rejects a broken later asset folder symlink path");
    expect(error.find("asset folder") != std::string::npos
               && error.find("symlink") != std::string::npos
               && error.find("samples") != std::string::npos,
           "Broken later asset-folder symlink failure error names the samples folder");
    expect(error.find("not found") == std::string::npos,
           "Broken later asset-folder symlink failure is not reported as a missing folder");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(samplesSymlinkPath)),
           "Broken later asset-folder symlink failure leaves the samples symlink unchanged");
    expect(!std::filesystem::exists(missingSamplesTarget),
           "Broken later asset-folder symlink failure does not create the missing target");
    expect(std::filesystem::is_directory(package / "audio"),
           "Broken later asset-folder symlink failure preserves the earlier audio directory");
    expect(readTextFile(package / "audio" / "sentinel.txt") == "audio sentinel before later symlink failure",
           "Broken later asset-folder symlink failure preserves earlier audio contents");
    expect(std::filesystem::is_directory(package / "presets"),
           "Broken later asset-folder symlink failure preserves later pre-existing presets directory");
    expect(std::filesystem::is_directory(package / "analysis"),
           "Broken later asset-folder symlink failure preserves later pre-existing analysis directory");
    expect(std::filesystem::is_directory(package / "backups"),
           "Broken later asset-folder symlink failure preserves later pre-existing backups directory");
    expect(readTextFile(manifestPath) == originalManifestText,
           "Broken later asset-folder symlink failure leaves the active manifest unchanged");
    expect(readTextFile(manifestPath).find("After Broken Later Asset Folder Symlink") == std::string::npos,
           "Broken later asset-folder symlink failure does not commit the new project state");
    expect(readTextFile(temporaryManifestPath)
               == "stale temporary manifest before broken later asset-folder symlink failure",
           "Broken later asset-folder symlink failure happens before temporary manifest staging");
    expect(!std::filesystem::exists(package / "backups" / "manifest.previous.json"),
           "Broken later asset-folder symlink failure happens before previous-manifest backup creation");
    expect(!std::filesystem::exists(missingSamplesTarget / "manifest.json"),
           "Broken later asset-folder symlink failure does not write target manifest through the link");
    expect(!std::filesystem::exists(missingSamplesTarget / "manifest.json.tmp"),
           "Broken later asset-folder symlink failure does not write target temporary manifest through the link");

    expect(std::filesystem::remove(samplesSymlinkPath),
           "Temporary broken later asset-folder symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary broken later asset-folder symlink package deleted");
    std::filesystem::remove_all(missingSamplesTarget);
}

void projectSaveRejectsLinkedLaterAssetFolderSymlinkBeforeManifestStaging()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Before Linked Later Asset Folder Symlink");

    const auto package =
        makeTemporaryPackagePath("projectname-save-linked-later-asset-folder-symlink-test");

    std::string error;
    expect(project.savePackage(package, error),
           "Initial linked later asset-folder symlink fixture save succeeds");

    const auto manifestPath = package / "manifest.json";
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto originalManifestText = readTextFile(manifestPath);
    expect(originalManifestText.find("Before Linked Later Asset Folder Symlink") != std::string::npos,
           "Linked later asset-folder symlink fixture starts with the original manifest state");

    writeTextFile(package / "audio" / "sentinel.txt", "audio sentinel before linked later symlink failure");
    writeTextFile(temporaryManifestPath,
                  "stale temporary manifest before linked later asset-folder symlink failure");

    const auto samplesSymlinkPath = package / "samples";
    const auto linkedSamplesTarget =
        makeTemporaryPackagePath("projectname-save-linked-later-asset-folder-symlink-target-test");
    const auto linkedSamplesSentinel = linkedSamplesTarget / "sentinel.txt";
    writeTextFile(linkedSamplesSentinel, "linked samples target sentinel");
    expect(std::filesystem::remove_all(samplesSymlinkPath) > 0,
           "Linked later asset-folder symlink fixture removes initial samples folder");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedSamplesTarget,
                                             samplesSymlinkPath,
                                             symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        std::filesystem::remove_all(linkedSamplesTarget);
        return;
    }

    project.setName("After Linked Later Asset Folder Symlink");
    error = "stale linked later asset-folder symlink failure error";
    expect(!project.savePackage(package, error),
           "Project save rejects a linked later asset folder symlink path");
    expect(error.find("asset folder") != std::string::npos
               && error.find("symlink") != std::string::npos
               && error.find("samples") != std::string::npos,
           "Linked later asset-folder symlink failure error names the samples folder");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(samplesSymlinkPath)),
           "Linked later asset-folder symlink failure leaves the samples symlink unchanged");
    expect(readTextFile(linkedSamplesSentinel) == "linked samples target sentinel",
           "Linked later asset-folder symlink failure preserves the linked target sentinel");
    expect(std::filesystem::is_directory(package / "audio"),
           "Linked later asset-folder symlink failure preserves the earlier audio directory");
    expect(readTextFile(package / "audio" / "sentinel.txt") == "audio sentinel before linked later symlink failure",
           "Linked later asset-folder symlink failure preserves earlier audio contents");
    expect(std::filesystem::is_directory(package / "presets"),
           "Linked later asset-folder symlink failure preserves later pre-existing presets directory");
    expect(std::filesystem::is_directory(package / "analysis"),
           "Linked later asset-folder symlink failure preserves later pre-existing analysis directory");
    expect(std::filesystem::is_directory(package / "backups"),
           "Linked later asset-folder symlink failure preserves later pre-existing backups directory");
    expect(readTextFile(manifestPath) == originalManifestText,
           "Linked later asset-folder symlink failure leaves the active manifest unchanged");
    expect(readTextFile(manifestPath).find("After Linked Later Asset Folder Symlink") == std::string::npos,
           "Linked later asset-folder symlink failure does not commit the new project state");
    expect(readTextFile(temporaryManifestPath)
               == "stale temporary manifest before linked later asset-folder symlink failure",
           "Linked later asset-folder symlink failure happens before temporary manifest staging");
    expect(!std::filesystem::exists(package / "backups" / "manifest.previous.json"),
           "Linked later asset-folder symlink failure happens before previous-manifest backup creation");
    expect(!std::filesystem::exists(linkedSamplesTarget / "manifest.json"),
           "Linked later asset-folder symlink failure does not write target manifest through the link");
    expect(!std::filesystem::exists(linkedSamplesTarget / "manifest.json.tmp"),
           "Linked later asset-folder symlink failure does not write target temporary manifest through the link");

    expect(std::filesystem::remove(samplesSymlinkPath),
           "Temporary linked later asset-folder symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary linked later asset-folder symlink package deleted");
    expect(std::filesystem::remove_all(linkedSamplesTarget) > 0,
           "Temporary linked later asset-folder symlink target deleted");
}

void projectSaveCommitFailureRemovesTemporaryManifest()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Commit Failure Test");

    const auto package = makeTemporaryPackagePath("projectname-save-commit-failure-test");
    const auto manifestPath = package / "manifest.json";
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto sentinelPath = manifestPath / "occupied.txt";
    writeTextFile(sentinelPath, "occupied manifest path");

    std::string error;
    expect(!project.savePackage(package, error), "Project save reports manifest commit failure");
    expect(error.find("Could not commit staged project manifest") != std::string::npos,
           "Manifest commit failure error is human-readable");
    expect(!std::filesystem::exists(temporaryManifestPath),
           "Failed project save removes temporary manifest");
    expect(std::filesystem::is_directory(manifestPath),
           "Failed project save leaves occupied manifest directory unchanged");
    expect(readTextFile(sentinelPath) == "occupied manifest path",
           "Failed project save preserves occupied manifest path contents");

    expect(std::filesystem::remove_all(package) > 0,
           "Temporary manifest commit failure package deleted");
}

void projectManifestLoadsLegacyTrackWithoutDevices()
{
    std::string error;
    const auto legacyPackage = makeTemporaryPackagePath("projectname-legacy-no-devices-test");
    writeManifestText(legacyPackage, R"({
  "manifestVersion": 1,
  "name": "Legacy No Devices",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "positionBeats": 0
  },
  "tracks": [
    {
      "id": "track-legacy",
      "name": "Legacy Track",
      "type": "audio",
      "clips": [
        {
          "id": "clip-legacy",
          "name": "Legacy Clip",
          "type": "generated-audio",
          "relativePath": "audio/generated-tone.wav",
          "startBeats": 0,
          "lengthBeats": 4
        }
      ]
    }
  ]
})");

    auto loaded = projectname::ProjectModel::loadPackage(legacyPackage, error);
    expect(loaded.has_value(), "Legacy manifest without devices still loads");
    expect(loaded.has_value() && loaded->getTracks().size() == 1, "Legacy manifest loads one track");
    expect(loaded.has_value() && loaded->getTracks().front().devices.empty(),
           "Legacy manifest keeps missing devices as an empty chain");
    expect(loaded.has_value() && loaded->getTracks().front().clips.front().analysisPath.empty(),
           "Legacy manifest without analysis path keeps clip analysis empty");
    expect(loaded.has_value() && !loaded->getLoopRegion().enabled,
           "Legacy manifest without loop region loads with loop disabled");

    expect(std::filesystem::remove_all(legacyPackage) > 0, "Legacy manifest package deleted");
}

void projectManifestFailuresAreRecoverable()
{
    std::string error;

    const auto missingManifestPackage = makeTemporaryPackagePath("projectname-missing-manifest-test");
    std::filesystem::create_directories(missingManifestPackage);
    auto missingManifestLoad = projectname::ProjectModel::loadPackage(missingManifestPackage, error);
    expect(!missingManifestLoad.has_value(), "Missing manifest package is rejected");
    expect(error.find("manifest") != std::string::npos, "Missing manifest error is descriptive");
    expect(std::filesystem::remove_all(missingManifestPackage) > 0, "Missing manifest package deleted");

    const auto malformedPackage = makeTemporaryPackagePath("projectname-malformed-manifest-test");
    writeManifestText(malformedPackage, "{ not valid json");
    auto malformedLoad = projectname::ProjectModel::loadPackage(malformedPackage, error);
    expect(!malformedLoad.has_value(), "Malformed manifest JSON is rejected");
    expect(!error.empty(), "Malformed manifest error is set");
    expect(std::filesystem::remove_all(malformedPackage) > 0, "Malformed manifest package deleted");

    const auto unsupportedVersionPackage = makeTemporaryPackagePath("projectname-version-manifest-test");
    writeManifestText(unsupportedVersionPackage, R"({
  "manifestVersion": 999,
  "name": "Too New",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "positionBeats": 0
  },
  "tracks": []
})");
    auto unsupportedLoad = projectname::ProjectModel::loadPackage(unsupportedVersionPackage, error);
    expect(!unsupportedLoad.has_value(), "Unsupported manifest version is rejected");
    expect(error.find("version") != std::string::npos, "Unsupported version error is descriptive");
    expect(std::filesystem::remove_all(unsupportedVersionPackage) > 0, "Unsupported version package deleted");

    const auto tracksNotArrayPackage = makeTemporaryPackagePath("projectname-tracks-manifest-test");
    writeManifestText(tracksNotArrayPackage, R"({
  "manifestVersion": 1,
  "name": "Bad Tracks",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "positionBeats": 0
  },
  "tracks": {}
})");
    auto tracksNotArrayLoad = projectname::ProjectModel::loadPackage(tracksNotArrayPackage, error);
    expect(!tracksNotArrayLoad.has_value(), "Non-array tracks manifest is rejected");
    expect(error.find("tracks") != std::string::npos, "Tracks schema error is descriptive");
    expect(std::filesystem::remove_all(tracksNotArrayPackage) > 0, "Non-array tracks package deleted");

    const auto loopRegionNotObjectPackage = makeTemporaryPackagePath("projectname-loop-region-schema-test");
    writeManifestText(loopRegionNotObjectPackage, R"({
  "manifestVersion": 1,
  "name": "Bad Loop Region",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "positionBeats": 0
  },
  "loopRegion": [],
  "tracks": []
})");
    auto loopRegionNotObjectLoad = projectname::ProjectModel::loadPackage(loopRegionNotObjectPackage, error);
    expect(!loopRegionNotObjectLoad.has_value(), "Non-object loop region manifest is rejected");
    expect(error.find("loop region") != std::string::npos, "Loop region schema error is descriptive");
    expect(std::filesystem::remove_all(loopRegionNotObjectPackage) > 0,
           "Non-object loop region package deleted");

    const auto invalidLoopRegionPackage = makeTemporaryPackagePath("projectname-loop-region-invalid-test");
    writeManifestText(invalidLoopRegionPackage, R"({
  "manifestVersion": 1,
  "name": "Invalid Loop Region",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "positionBeats": 0
  },
  "loopRegion": {
    "enabled": true,
    "startBeats": 8,
    "lengthBeats": 0
  },
  "tracks": []
})");
    auto invalidLoopRegionLoad = projectname::ProjectModel::loadPackage(invalidLoopRegionPackage, error);
    expect(!invalidLoopRegionLoad.has_value(), "Invalid enabled loop region manifest is rejected");
    expect(error.find("loop region") != std::string::npos, "Invalid loop region error is descriptive");
    expect(std::filesystem::remove_all(invalidLoopRegionPackage) > 0,
           "Invalid loop region package deleted");

    const auto devicesNotArrayPackage = makeTemporaryPackagePath("projectname-devices-manifest-test");
    writeManifestText(devicesNotArrayPackage, R"({
  "manifestVersion": 1,
  "name": "Bad Devices",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "positionBeats": 0
  },
  "tracks": [
    {
      "id": "track-1",
      "name": "Track",
      "type": "audio",
      "devices": {},
      "clips": []
    }
  ]
})");
    auto devicesNotArrayLoad = projectname::ProjectModel::loadPackage(devicesNotArrayPackage, error);
    expect(!devicesNotArrayLoad.has_value(), "Non-array devices manifest is rejected");
    expect(error.find("devices") != std::string::npos, "Devices schema error is descriptive");
    expect(std::filesystem::remove_all(devicesNotArrayPackage) > 0, "Non-array devices package deleted");

    const auto deviceMissingIdPackage = makeTemporaryPackagePath("projectname-device-id-manifest-test");
    writeManifestText(deviceMissingIdPackage, R"({
  "manifestVersion": 1,
  "name": "Bad Device ID",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "positionBeats": 0
  },
  "tracks": [
    {
      "id": "track-1",
      "name": "Track",
      "type": "audio",
      "devices": [
        { "name": "Missing ID", "type": "builtin/generated-tone-source" }
      ],
      "clips": []
    }
  ]
})");
    auto deviceMissingIdLoad = projectname::ProjectModel::loadPackage(deviceMissingIdPackage, error);
    expect(!deviceMissingIdLoad.has_value(), "Device without id is rejected");
    expect(error.find("Device") != std::string::npos, "Device id error is descriptive");
    expect(std::filesystem::remove_all(deviceMissingIdPackage) > 0, "Device missing id package deleted");

    const auto invalidSignaturePackage = makeTemporaryPackagePath("projectname-signature-manifest-test");
    writeManifestText(invalidSignaturePackage, R"({
  "manifestVersion": 1,
  "name": "Bad Signature",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 0, "denominator": 3 },
    "positionBeats": 0
  },
  "tracks": []
})");
    auto invalidSignatureLoad = projectname::ProjectModel::loadPackage(invalidSignaturePackage, error);
    expect(!invalidSignatureLoad.has_value(), "Invalid time signature manifest is rejected");
    expect(error.find("signature") != std::string::npos, "Invalid signature error is descriptive");
    expect(std::filesystem::remove_all(invalidSignaturePackage) > 0, "Invalid signature package deleted");
}

void toneRendererProducesBoundedStereoSignal()
{
    projectname::ToneRenderer tone;
    tone.prepare(48000.0);
    tone.setFrequencyHz(440.0);
    tone.setGain(0.25f);

    std::vector<float> left(512);
    std::vector<float> right(512);
    tone.render(left.data(), right.data(), static_cast<int>(left.size()));

    auto peak = 0.0f;
    auto nonZeroSamples = 0;
    auto channelsMatch = true;

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        peak = std::max(peak, std::abs(left[index]));
        if (std::abs(left[index]) > 0.0001f)
            ++nonZeroSamples;

        if (left[index] != right[index])
            channelsMatch = false;
    }

    expect(nonZeroSamples > 0, "Tone renderer produces non-zero samples");
    expect(peak <= 0.2501f, "Tone renderer respects gain bound");
    expect(peak > 0.20f, "Tone renderer reaches expected amplitude");
    expect(channelsMatch, "Tone renderer writes matching stereo samples");
}

void audioEngineStubRendersOnlyWhileEnabled()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(48000.0);
    audioEngine.setGeneratedToneFrequencyHz(330.0);
    audioEngine.setGeneratedToneGain(0.2f);

    std::vector<float> left(256, 1.0f);
    std::vector<float> right(256, 1.0f);
    audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

    auto stoppedPeak = 0.0f;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        stoppedPeak = std::max(stoppedPeak, std::abs(left[index]));
        stoppedPeak = std::max(stoppedPeak, std::abs(right[index]));
    }

    expect(stoppedPeak == 0.0f, "Audio engine writes silence while stopped");

    audioEngine.startGeneratedTone();
    expect(audioEngine.isGeneratedToneEnabled(), "Audio engine generated tone starts");
    audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

    auto playingPeak = 0.0f;
    auto nonZeroSamples = 0;
    auto channelsMatch = true;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        playingPeak = std::max(playingPeak, std::abs(left[index]));
        if (std::abs(left[index]) > 0.0001f)
            ++nonZeroSamples;

        if (left[index] != right[index])
            channelsMatch = false;
    }

    expect(nonZeroSamples > 0, "Audio engine renders generated tone samples");
    expect(playingPeak <= 0.2001f, "Audio engine respects generated tone gain");
    expect(channelsMatch, "Audio engine renders matching stereo output");

    audioEngine.stop();
    expect(!audioEngine.isGeneratedToneEnabled(), "Audio engine generated tone stops");
    audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

    auto stoppedAgainPeak = 0.0f;
    for (const auto sample : left)
        stoppedAgainPeak = std::max(stoppedAgainPeak, std::abs(sample));

    expect(stoppedAgainPeak == 0.0f, "Audio engine returns to silence after stop");
}

void audioEngineStubRendersInterleavedInt16()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(44100.0);
    audioEngine.setGeneratedToneGain(0.18f);
    audioEngine.startGeneratedTone();

    std::vector<std::int16_t> samples(512 * 2);
    audioEngine.renderInterleavedInt16(samples.data(), 512, 2);

    auto nonZeroSamples = 0;
    auto channelsMatch = true;

    for (std::size_t index = 0; index < samples.size(); index += 2)
    {
        if (samples[index] != 0)
            ++nonZeroSamples;

        if (samples[index] != samples[index + 1])
            channelsMatch = false;
    }

    expect(nonZeroSamples > 0, "Audio engine renders non-zero interleaved int16 samples");
    expect(channelsMatch, "Audio engine interleaved int16 output keeps stereo channels matched");
}

void audioEngineStubPlaysGeneratedClipForPreparedDuration()
{
    constexpr auto sampleRate = 1000.0;
    constexpr auto clipFrameCount = 120;

    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(sampleRate);
    audioEngine.setGeneratedToneFrequencyHz(50.0);
    audioEngine.setGeneratedToneGain(0.3f);
    audioEngine.startGeneratedClip(static_cast<double>(clipFrameCount) / sampleRate);

    expect(!audioEngine.isGeneratedToneEnabled(), "Generated clip does not report continuous tone mode");
    expect(audioEngine.isGeneratedClipPlaying(), "Generated clip starts playing");

    std::vector<float> left(clipFrameCount);
    std::vector<float> right(clipFrameCount);
    audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

    auto nonZeroSamples = 0;
    auto channelsMatch = true;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (std::abs(left[index]) > 0.0001f)
            ++nonZeroSamples;

        if (left[index] != right[index])
            channelsMatch = false;
    }

    expect(nonZeroSamples > 0, "Generated clip renders non-zero samples");
    expect(channelsMatch, "Generated clip renders matching stereo output");
    expect(audioEngine.getGeneratedClipPositionSamples() == clipFrameCount,
           "Generated clip advances by rendered frame count");
    expect(!audioEngine.isGeneratedClipPlaying(), "Generated clip stops when duration is exhausted");

    std::vector<float> trailing(16, 1.0f);
    audioEngine.render(trailing.data(), nullptr, static_cast<int>(trailing.size()));

    auto trailingPeak = 0.0f;
    for (const auto sample : trailing)
        trailingPeak = std::max(trailingPeak, std::abs(sample));

    expect(trailingPeak == 0.0f, "Generated clip renders silence after it has ended");
}

void audioEngineStubRejectsInvalidGeneratedClipLengths()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(44100.0);

    audioEngine.startGeneratedClip(0.0);
    expect(!audioEngine.isGeneratedClipPlaying(), "Zero-length generated clip is rejected");

    audioEngine.startGeneratedClip(-1.0);
    expect(!audioEngine.isGeneratedClipPlaying(), "Negative generated clip is rejected");
}

void audioEngineStubSchedulesGeneratedClipOnTimeline()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(1000.0);
    audioEngine.setGeneratedToneFrequencyHz(50.0);
    audioEngine.setGeneratedToneGain(0.25f);
    audioEngine.setTimelinePositionSamples(0);
    audioEngine.startScheduledGeneratedClip(32, 64);

    std::vector<float> left(128);
    std::vector<float> right(128);
    audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

    auto preStartPeak = 0.0f;
    auto clipNonZeroSamples = 0;
    auto postEndPeak = 0.0f;
    auto channelsMatch = true;

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (index < 32)
            preStartPeak = std::max(preStartPeak, std::abs(left[index]));
        else if (index < 96)
        {
            if (std::abs(left[index]) > 0.0001f)
                ++clipNonZeroSamples;
        }
        else
        {
            postEndPeak = std::max(postEndPeak, std::abs(left[index]));
        }

        if (left[index] != right[index])
            channelsMatch = false;
    }

    expect(preStartPeak == 0.0f, "Scheduled generated clip renders silence before start sample");
    expect(clipNonZeroSamples > 0, "Scheduled generated clip renders during its timeline region");
    expect(postEndPeak == 0.0f, "Scheduled generated clip renders silence after end sample");
    expect(channelsMatch, "Scheduled generated clip keeps stereo channels matched");
    expect(audioEngine.getGeneratedClipPositionSamples() == 64,
           "Scheduled generated clip advances clip-local position by clip length");
    expect(audioEngine.getTimelinePositionSamples() == 96,
           "Scheduled generated clip advances timeline through clip end");
    expect(!audioEngine.isGeneratedClipPlaying(), "Scheduled generated clip stops at its timeline end");
}

void audioEngineStubCanSeekIntoScheduledGeneratedClip()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(1000.0);
    audioEngine.setGeneratedToneFrequencyHz(50.0);
    audioEngine.setGeneratedToneGain(0.25f);
    audioEngine.setTimelinePositionSamples(48);
    audioEngine.startScheduledGeneratedClip(32, 64);

    std::vector<float> left(16);
    audioEngine.render(left.data(), nullptr, static_cast<int>(left.size()));

    auto nonZeroSamples = 0;
    for (const auto sample : left)
    {
        if (std::abs(sample) > 0.0001f)
            ++nonZeroSamples;
    }

    expect(nonZeroSamples > 0, "Scheduled generated clip renders after seeking into clip");
    expect(audioEngine.getGeneratedClipPositionSamples() == 32,
           "Scheduled generated clip uses seeked timeline position for clip-local offset");
    expect(audioEngine.getTimelinePositionSamples() == 64,
           "Scheduled generated clip advances timeline from seeked position");
    expect(audioEngine.isGeneratedClipPlaying(), "Scheduled generated clip remains active before its end");
}

void audioEngineStubStopCancelsScheduledGeneratedClip()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(1000.0);
    audioEngine.setGeneratedToneGain(0.25f);
    audioEngine.startScheduledGeneratedClip(0, 64);
    audioEngine.stop();

    std::vector<float> left(32, 1.0f);
    audioEngine.render(left.data(), nullptr, static_cast<int>(left.size()));

    auto peak = 0.0f;
    for (const auto sample : left)
        peak = std::max(peak, std::abs(sample));

    expect(peak == 0.0f, "Stopping scheduled generated clip renders silence");
    expect(!audioEngine.isGeneratedClipPlaying(), "Stopped scheduled generated clip reports inactive");
}

void audioEngineStubSchedulesPreparedMonoClipBuffer()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(1000.0);
    audioEngine.setTimelinePositionSamples(0);
    audioEngine.setPreparedMonoClipSamples({ 0.10f, -0.20f, 0.30f, -0.40f });
    audioEngine.startScheduledPreparedMonoClip(4);

    expect(audioEngine.getPreparedClipLengthSamples() == 4,
           "Prepared mono clip reports loaded sample count");

    std::vector<float> left(12, 1.0f);
    std::vector<float> right(12, 1.0f);
    audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

    auto preStartPeak = 0.0f;
    auto postEndPeak = 0.0f;
    auto channelsMatch = true;

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (index < 4)
            preStartPeak = std::max(preStartPeak, std::abs(left[index]));
        else if (index >= 8)
            postEndPeak = std::max(postEndPeak, std::abs(left[index]));

        if (left[index] != right[index])
            channelsMatch = false;
    }

    expect(preStartPeak == 0.0f, "Prepared mono clip renders silence before start sample");
    expect(std::abs(left[4] - 0.10f) < 0.0001f, "Prepared mono clip renders sample 0");
    expect(std::abs(left[5] - -0.20f) < 0.0001f, "Prepared mono clip renders sample 1");
    expect(std::abs(left[6] - 0.30f) < 0.0001f, "Prepared mono clip renders sample 2");
    expect(std::abs(left[7] - -0.40f) < 0.0001f, "Prepared mono clip renders sample 3");
    expect(postEndPeak == 0.0f, "Prepared mono clip renders silence after end sample");
    expect(channelsMatch, "Prepared mono clip fans out to matching stereo channels");
    expect(audioEngine.getGeneratedClipPositionSamples() == 4,
           "Prepared mono clip advances clip-local position by buffer length");
    expect(audioEngine.getTimelinePositionSamples() == 8,
           "Prepared mono clip advances timeline through buffer end");
    expect(!audioEngine.isGeneratedClipPlaying(), "Prepared mono clip stops at buffer end");
}

void audioEngineStubCanSeekIntoPreparedMonoClipBuffer()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(1000.0);
    audioEngine.setTimelinePositionSamples(6);
    audioEngine.setPreparedMonoClipSamples({ 0.10f, 0.20f, 0.30f, 0.40f, 0.50f });
    audioEngine.startScheduledPreparedMonoClip(4);

    std::vector<float> left(2);
    audioEngine.render(left.data(), nullptr, static_cast<int>(left.size()));

    expect(std::abs(left[0] - 0.30f) < 0.0001f,
           "Prepared mono clip seek renders buffer sample matching timeline offset");
    expect(std::abs(left[1] - 0.40f) < 0.0001f,
           "Prepared mono clip continues from seeked offset");
    expect(audioEngine.getGeneratedClipPositionSamples() == 4,
           "Prepared mono clip advances local position after seek render");
    expect(audioEngine.getTimelinePositionSamples() == 8,
           "Prepared mono clip advances timeline from seeked position");
    expect(audioEngine.isGeneratedClipPlaying(), "Prepared mono clip remains active before buffer end");
}

void audioEngineStubClampsPreparedMonoClipSamples()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(1000.0);
    audioEngine.setPreparedMonoClipSamples({ 2.0f, -2.0f, std::nanf("") });
    audioEngine.startScheduledPreparedMonoClip(0);

    std::vector<float> left(3);
    audioEngine.render(left.data(), nullptr, static_cast<int>(left.size()));

    expect(left[0] == 1.0f, "Prepared mono clip clamps high samples");
    expect(left[1] == -1.0f, "Prepared mono clip clamps low samples");
    expect(left[2] == 0.0f, "Prepared mono clip replaces non-finite samples with silence");
}

void audioEngineStubRejectsEmptyPreparedMonoClip()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(1000.0);
    audioEngine.setPreparedMonoClipSamples(std::vector<float> {});
    audioEngine.startScheduledPreparedMonoClip(0);

    expect(audioEngine.getPreparedClipLengthSamples() == 0,
           "Empty prepared mono clip reports zero length");
    expect(!audioEngine.isGeneratedClipPlaying(), "Empty prepared mono clip is rejected");
}

void audioEngineStubSumsPreparedTrackVoicesToStereo()
{
    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(1000.0);
    audioEngine.setPreparedTrackVoiceBuffers({
        {
            "clip-a",
            std::make_shared<const std::vector<float>>(
                std::vector<float> { 0.25f, 0.50f, 0.75f, 1.0f, std::nanf("") }),
        },
        {
            "clip-b",
            std::make_shared<const std::vector<float>>(
                std::vector<float> { 0.10f, 0.20f, 0.30f, 0.40f }),
        },
    });

    projectname::TrackVoiceSchedule schedule;
    schedule.renderTimelineStartSample = 10;
    schedule.frameCount = 6;

    projectname::TrackVoice first;
    first.clipId = "clip-a";
    first.renderStartOffsetSamples = 1;
    first.clipLocalStartOffsetSamples = 1;
    first.frameCount = 4;
    first.gainLeft = 0.5f;
    first.gainRight = 1.0f;
    schedule.voices.push_back(first);

    projectname::TrackVoice second;
    second.clipId = "clip-b";
    second.renderStartOffsetSamples = 2;
    second.clipLocalStartOffsetSamples = 0;
    second.frameCount = 3;
    second.gainLeft = 1.0f;
    second.gainRight = 0.25f;
    schedule.voices.push_back(second);

    projectname::TrackVoice missing;
    missing.clipId = "missing-clip";
    missing.renderStartOffsetSamples = 0;
    missing.clipLocalStartOffsetSamples = 0;
    missing.frameCount = 6;
    missing.gainLeft = 1.0f;
    missing.gainRight = 1.0f;
    schedule.voices.push_back(missing);

    audioEngine.startPreparedVoiceSchedule(std::move(schedule));

    std::vector<float> left(8, 1.0f);
    std::vector<float> right(8, 1.0f);
    audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

    expect(left[0] == 0.0f && right[0] == 0.0f,
           "Prepared voice mixer renders silence before any voice starts");
    expect(std::abs(left[1] - 0.25f) < 0.0001f && std::abs(right[1] - 0.50f) < 0.0001f,
           "Prepared voice mixer applies first voice pan and gain");
    expect(std::abs(left[2] - 0.475f) < 0.0001f && std::abs(right[2] - 0.775f) < 0.0001f,
           "Prepared voice mixer sums overlapping voices");
    expect(std::abs(left[3] - 0.70f) < 0.0001f && right[3] == 1.0f,
           "Prepared voice mixer clips right channel deterministically");
    expect(std::abs(left[4] - 0.30f) < 0.0001f && std::abs(right[4] - 0.075f) < 0.0001f,
           "Prepared voice mixer treats non-finite prepared samples as silence");
    expect(left[5] == 0.0f && right[5] == 0.0f,
           "Prepared voice mixer renders silence after voices end inside schedule");
    expect(left[6] == 0.0f && right[6] == 0.0f && left[7] == 0.0f && right[7] == 0.0f,
           "Prepared voice mixer renders silence after schedule end");
    expect(audioEngine.getTimelinePositionSamples() == 18,
           "Prepared voice mixer advances timeline through rendered block");
    expect(!audioEngine.isGeneratedClipPlaying(),
           "Prepared voice mixer stops after schedule end");
}

void timelinePlaybackPlanMapsImportedClipBeatsToSamples()
{
    auto project = makeProjectWithImportedTimelineClip(2.0, 4.0);

    projectname::ProjectTrack generatedTrack;
    generatedTrack.id = "track-generated-playback";
    generatedTrack.name = "Generated Playback";
    generatedTrack.type = "audio";
    projectname::ProjectClip generatedClip;
    generatedClip.id = "clip-generated-playback";
    generatedClip.name = "Generated Clip";
    generatedClip.type = "generated-audio";
    generatedClip.relativePath = "audio/generated.wav";
    generatedClip.startBeats = 0.0;
    generatedClip.lengthBeats = 4.0;
    generatedTrack.clips.push_back(generatedClip);
    project.addTrack(std::move(generatedTrack));

    projectname::TimelinePlaybackPlanOptions options;
    options.sampleRateHz = 1000.0;
    const auto plan = projectname::buildImportedAudioTimelinePlaybackPlan(project, options);

    expect(plan.clips.size() == 1, "Timeline playback plan includes only imported audio clips");
    if (plan.clips.empty())
        return;

    const auto& clip = plan.clips.front();
    expect(clip.timelineStartSample == 1000, "Timeline playback plan maps clip start beats to samples");
    expect(clip.timelineLengthSamples == 2000, "Timeline playback plan maps clip length beats to samples");
    expect(clip.timelineEndSample == 3000, "Timeline playback plan computes exclusive end sample");
    expect(!clip.containsTimelineSample(999), "Timeline playback clip excludes pre-roll sample");
    expect(clip.containsTimelineSample(1000), "Timeline playback clip includes first clip sample");
    expect(clip.containsTimelineSample(2999), "Timeline playback clip includes final clip sample");
    expect(!clip.containsTimelineSample(3000), "Timeline playback clip excludes post-clip sample");

    const auto preRoll = projectname::makeImportedAudioClipPlaybackActivation(plan, 0, 500);
    expect(preRoll.has_value()
               && preRoll->timelinePlaybackStartSample == 1000
               && preRoll->clipLocalStartOffsetSamples == 0
               && preRoll->clipLengthSamples == 2000,
           "Timeline playback activation schedules future clip entry from pre-roll");

    const auto nextPreRoll = projectname::findNextImportedAudioClipPlaybackActivation(plan, 500);
    expect(nextPreRoll.has_value()
               && nextPreRoll->timelinePlaybackStartSample == 1000
               && nextPreRoll->clipLocalStartOffsetSamples == 0,
           "Timeline playback next activation finds future clip entry");

    const auto entry = projectname::findActiveImportedAudioClipPlaybackActivation(plan, 1000);
    expect(entry.has_value()
               && entry->timelinePlaybackStartSample == 1000
               && entry->clipLocalStartOffsetSamples == 0,
           "Timeline playback activation starts at clip entry");

    const auto seek = projectname::findActiveImportedAudioClipPlaybackActivation(plan, 1500);
    expect(seek.has_value()
               && seek->timelinePlaybackStartSample == 1500
               && seek->clipLocalStartOffsetSamples == 500,
           "Timeline playback activation reports clip-local seek offset");

    const auto postClip = projectname::findActiveImportedAudioClipPlaybackActivation(plan, 3000);
    expect(!postClip.has_value(), "Timeline playback activation ignores post-clip transport positions");

    const auto nextPostClip = projectname::findNextImportedAudioClipPlaybackActivation(plan, 3000);
    expect(!nextPostClip.has_value(), "Timeline playback next activation ignores positions after all clips");
}

void timelinePlaybackPlanSchedulesPreparedClipRendering()
{
    auto project = makeProjectWithImportedTimelineClip(2.0, 4.0);
    projectname::TimelinePlaybackPlanOptions options;
    options.sampleRateHz = 10.0;
    const auto plan = projectname::buildImportedAudioTimelinePlaybackPlan(project, options);

    expect(plan.clips.size() == 1, "Prepared clip render test builds one timeline playback clip");
    if (plan.clips.empty())
        return;

    std::vector<float> samples;
    for (int index = 0; index < 20; ++index)
        samples.push_back(static_cast<float>(index + 1) / 100.0f);

    {
        const auto activation = projectname::makeImportedAudioClipPlaybackActivation(plan, 0, 0);
        expect(activation.has_value(), "Prepared clip render test creates pre-roll activation");

        projectname::AudioEngineStub audioEngine;
        audioEngine.prepare(10.0);
        audioEngine.setTimelinePositionSamples(0);
        audioEngine.setPreparedMonoClipSamples(samples);
        audioEngine.startScheduledPreparedMonoClip(activation->timelinePlaybackStartSample,
                                                   activation->clipLocalStartOffsetSamples,
                                                   activation->clipLengthSamples);

        std::vector<float> left(35, 1.0f);
        std::vector<float> right(35, 1.0f);
        audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

        auto preRollPeak = 0.0f;
        auto postClipPeak = 0.0f;
        auto channelsMatch = true;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (index < 10)
                preRollPeak = std::max(preRollPeak, std::abs(left[index]));
            else if (index >= 30)
                postClipPeak = std::max(postClipPeak, std::abs(left[index]));

            if (left[index] != right[index])
                channelsMatch = false;
        }

        expect(preRollPeak == 0.0f, "Timeline prepared clip renders pre-roll silence");
        expect(std::abs(left[10] - samples[0]) < 0.0001f,
               "Timeline prepared clip renders first sample at clip entry");
        expect(std::abs(left[15] - samples[5]) < 0.0001f,
               "Timeline prepared clip advances through clip-local samples after entry");
        expect(postClipPeak == 0.0f, "Timeline prepared clip renders post-clip silence");
        expect(channelsMatch, "Timeline prepared clip keeps stereo channels matched");
    }

    {
        const auto activation = projectname::findActiveImportedAudioClipPlaybackActivation(plan, 15);
        expect(activation.has_value(), "Prepared clip render test creates seek activation");

        projectname::AudioEngineStub audioEngine;
        audioEngine.prepare(10.0);
        audioEngine.setTimelinePositionSamples(15);
        audioEngine.setPreparedMonoClipSamples(samples);
        audioEngine.startScheduledPreparedMonoClip(activation->timelinePlaybackStartSample,
                                                   activation->clipLocalStartOffsetSamples,
                                                   activation->clipLengthSamples);

        std::vector<float> left(2);
        audioEngine.render(left.data(), nullptr, static_cast<int>(left.size()));

        expect(std::abs(left[0] - samples[5]) < 0.0001f,
               "Timeline prepared clip seek renders correct clip-local sample");
        expect(std::abs(left[1] - samples[6]) < 0.0001f,
               "Timeline prepared clip seek continues from clip-local offset");
    }

    {
        std::string error;
        expect(project.setLoopRegion(3.0, 2.0, error), "Prepared clip render test sets loop region");
        projectname::AppSession session(project);
        session.getTransport().setPositionBeats(4.5);
        session.play();
        session.advanceSeconds(0.25);
        expect(std::abs(session.getTransport().getPositionBeats() - 3.0) < 0.0001,
               "Prepared clip render test wraps session transport to loop start");

        const auto currentTimelineSample = projectname::beatsToTimelineSamples(
            session.getTransport().getPositionBeats(),
            session.getTransport().getTempoBpm(),
            options.sampleRateHz);
        expect(currentTimelineSample.has_value(), "Prepared clip render test maps looped position to samples");
        if (!currentTimelineSample.has_value())
            return;

        const auto activation =
            projectname::findActiveImportedAudioClipPlaybackActivation(plan, *currentTimelineSample);
        expect(activation.has_value()
                   && activation->timelinePlaybackStartSample == 15
                   && activation->clipLocalStartOffsetSamples == 5,
               "Timeline playback activation starts loop wrap inside the imported clip");
        if (!activation.has_value())
            return;

        projectname::AudioEngineStub audioEngine;
        audioEngine.prepare(10.0);
        audioEngine.setTimelinePositionSamples(*currentTimelineSample);
        audioEngine.setPreparedMonoClipSamples(samples);
        audioEngine.startScheduledPreparedMonoClip(activation->timelinePlaybackStartSample,
                                                   activation->clipLocalStartOffsetSamples,
                                                   activation->clipLengthSamples);

        std::vector<float> left(1);
        audioEngine.render(left.data(), nullptr, static_cast<int>(left.size()));

        expect(std::abs(left[0] - samples[5]) < 0.0001f,
               "Timeline prepared clip loop wrap renders correct clip-local sample");
    }
}

void trackVoiceScheduleBuildsMixerReadyVoiceWindows()
{
    auto project = projectname::ProjectModel::createDefault();
    project.getTransport().setTempoBpm(120.0);

    projectname::ProjectTrack trackA;
    trackA.id = "track-voice-a";
    trackA.name = "Voice A";
    trackA.type = "audio";
    trackA.clips.push_back(makeImportedTimelineCacheClip(1, 0.0));
    trackA.clips.back().lengthBeats = 2.0;

    projectname::ProjectTrack trackB;
    trackB.id = "track-voice-b";
    trackB.name = "Voice B";
    trackB.type = "audio";
    trackB.clips.push_back(makeImportedTimelineCacheClip(2, 1.0));
    trackB.clips.back().id = "clip-voice-b";
    trackB.clips.back().relativePath = "audio/voice-b.wav";
    trackB.clips.back().lengthBeats = 2.0;

    project.addTrack(std::move(trackA));
    project.addTrack(std::move(trackB));

    projectname::TimelinePlaybackPlanOptions planOptions;
    planOptions.sampleRateHz = 10.0;
    const auto plan = projectname::buildImportedAudioTimelinePlaybackPlan(project, planOptions);

    expect(plan.clips.size() == 2, "Track voice schedule test builds two imported timeline clips");

    const std::vector<projectname::TrackMixState> mixStates {
        { "track-voice-a", 0.5f, -1.0f, false, false },
        { "track-voice-b", 0.25f, 1.0f, false, false },
    };

    auto schedule = projectname::buildTrackVoiceSchedule(plan, 4, 8, mixStates);
    expect(schedule.renderTimelineStartSample == 4 && schedule.frameCount == 8,
           "Track voice schedule records render window");
    expect(schedule.voices.size() == 2,
           "Track voice schedule returns overlapping voices for render window");

    if (schedule.voices.size() == 2)
    {
        const auto& first = schedule.voices[0];
        expect(first.trackId == "track-voice-a" && first.clipId == "clip-cache-1",
               "Track voice schedule preserves first voice identity");
        expect(first.renderStartOffsetSamples == 0,
               "Track voice schedule clips first voice to render-window start");
        expect(first.clipLocalStartOffsetSamples == 4,
               "Track voice schedule computes first voice clip-local offset");
        expect(first.frameCount == 6,
               "Track voice schedule computes first voice frame count");
        expect(std::abs(first.gainLeft - 0.5f) < 0.0001f && first.gainRight == 0.0f,
               "Track voice schedule applies left pan and gain");

        const auto& second = schedule.voices[1];
        expect(second.trackId == "track-voice-b" && second.clipId == "clip-voice-b",
               "Track voice schedule preserves second voice identity");
        expect(second.renderStartOffsetSamples == 1,
               "Track voice schedule computes second voice render offset");
        expect(second.clipLocalStartOffsetSamples == 0,
               "Track voice schedule starts second voice at clip beginning");
        expect(second.frameCount == 7,
               "Track voice schedule clips second voice to render-window end");
        expect(second.gainLeft == 0.0f && std::abs(second.gainRight - 0.25f) < 0.0001f,
               "Track voice schedule applies right pan and gain");
    }

    const std::vector<projectname::TrackMixState> soloStates {
        { "track-voice-a", 1.0f, 0.0f, false, true },
        { "track-voice-b", 1.0f, 0.0f, false, false },
    };
    auto soloSchedule = projectname::buildTrackVoiceSchedule(plan, 0, 16, soloStates);
    expect(soloSchedule.voices.size() == 1
               && soloSchedule.voices.front().trackId == "track-voice-a",
           "Track voice schedule mutes non-solo tracks when a solo exists");

    const std::vector<projectname::TrackMixState> mutedSoloStates {
        { "track-voice-a", 1.0f, 0.0f, true, true },
        { "track-voice-b", 1.0f, 0.0f, false, true },
    };
    auto mutedSoloSchedule = projectname::buildTrackVoiceSchedule(plan, 0, 16, mutedSoloStates);
    expect(mutedSoloSchedule.voices.size() == 1
               && mutedSoloSchedule.voices.front().trackId == "track-voice-b",
           "Track voice schedule keeps muted solo tracks silent");

    projectname::TrackVoiceScheduleOptions limitedOptions;
    limitedOptions.maxVoices = 1;
    auto limited = projectname::buildTrackVoiceSchedule(plan, 4, 8, mixStates, limitedOptions);
    expect(limited.voices.size() == 1 && limited.voiceLimitReached,
           "Track voice schedule reports deterministic voice limit truncation");
}

void wavAudioImporterLoadsStereoPcm16AsPreparedMono()
{
    const auto wavPath = makeTemporaryAudioPath("projectname-import-test");
    writePcm16Wav(
        wavPath,
        48000,
        2,
        {
            32767, 32767,
            0, 0,
            -32768, -32768,
            32767, 0,
        });

    auto sawInitialProgress = false;
    auto sawCompleteProgress = false;

    projectname::WavDecodeOptions options;
    options.progressCallback =
        [&sawInitialProgress, &sawCompleteProgress](const projectname::WavDecodeProgress& progress)
        {
            if (progress.framesDecoded == 0 && progress.totalFrames == 4 && progress.percent == 0)
                sawInitialProgress = true;

            if (progress.framesDecoded == 4 && progress.totalFrames == 4 && progress.percent == 100)
                sawCompleteProgress = true;
        };

    std::string error;
    auto clip = projectname::loadPcm16WavAsPreparedMonoClip(wavPath, options, error);
    expect(clip.has_value(), "PCM16 WAV imports as prepared mono clip");
    expect(error.empty(), "PCM16 WAV import leaves error empty");
    expect(sawInitialProgress, "WAV importer reports initial frame progress");
    expect(sawCompleteProgress, "WAV importer reports completed frame progress");
    expect(clip.has_value() && clip->sampleRateHz == 48000.0, "WAV importer reports sample rate");
    expect(clip.has_value() && clip->sourceChannelCount == 2, "WAV importer reports source channel count");
    expect(clip.has_value() && clip->frameCount == 4, "WAV importer reports frame count");
    expect(clip.has_value() && clip->samples.size() == 4, "WAV importer creates one mono sample per frame");
    expect(clip.has_value() && std::abs(clip->samples[0] - 1.0f) < 0.0001f,
           "WAV importer decodes full-scale positive samples");
    expect(clip.has_value() && clip->samples[1] == 0.0f,
           "WAV importer decodes zero samples");
    expect(clip.has_value() && clip->samples[2] == -1.0f,
           "WAV importer decodes full-scale negative samples");
    expect(clip.has_value() && std::abs(clip->samples[3] - 0.5f) < 0.0001f,
           "WAV importer averages stereo samples to mono");

    if (clip.has_value())
    {
        projectname::AudioEngineStub audioEngine;
        audioEngine.prepare(clip->sampleRateHz);
        audioEngine.setTimelinePositionSamples(0);
        audioEngine.setPreparedMonoClipSamples(clip->samples);
        audioEngine.startScheduledPreparedMonoClip(2);

        std::vector<float> left(8, 1.0f);
        std::vector<float> right(8, 1.0f);
        audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

        auto preStartPeak = 0.0f;
        auto postEndPeak = 0.0f;
        auto channelsMatch = true;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            if (index < 2)
                preStartPeak = std::max(preStartPeak, std::abs(left[index]));
            else if (index >= 6)
                postEndPeak = std::max(postEndPeak, std::abs(left[index]));

            if (left[index] != right[index])
                channelsMatch = false;
        }

        expect(preStartPeak == 0.0f, "Imported prepared clip renders silence before start offset");
        expect(std::abs(left[2] - 1.0f) < 0.0001f, "Imported prepared clip renders sample 0");
        expect(left[3] == 0.0f, "Imported prepared clip renders sample 1");
        expect(left[4] == -1.0f, "Imported prepared clip renders sample 2");
        expect(std::abs(left[5] - 0.5f) < 0.0001f, "Imported prepared clip renders sample 3");
        expect(postEndPeak == 0.0f, "Imported prepared clip renders silence after end");
        expect(channelsMatch, "Imported prepared clip renders matching stereo output");
        expect(audioEngine.getGeneratedClipPositionSamples() == 4,
               "Imported prepared clip advances clip-local playback by imported frame count");
        expect(!audioEngine.isGeneratedClipPlaying(), "Imported prepared clip stops at end");
    }

    expect(std::filesystem::remove(wavPath), "Temporary WAV import file deleted");
}

void wavAudioImporterCancelsDuringDecode()
{
    const auto wavPath = makeTemporaryAudioPath("projectname-import-decode-cancel-test");
    writePcm16Wav(wavPath, 44100, 1, { 1000, 2000, 3000, 4000, 5000, 6000 });

    std::atomic_bool cancelRequested { false };
    auto sawNonZeroProgress = false;

    projectname::WavDecodeOptions options;
    options.cancelRequested = &cancelRequested;
    options.progressCallback =
        [&cancelRequested, &sawNonZeroProgress](const projectname::WavDecodeProgress& progress)
        {
            if (progress.framesDecoded > 0)
            {
                sawNonZeroProgress = true;
                cancelRequested.store(true, std::memory_order_release);
            }
        };

    std::string error;
    auto clip = projectname::loadPcm16WavAsPreparedMonoClip(wavPath, options, error);
    expect(!clip.has_value(), "WAV importer reports decode cancellation");
    expect(error.find("cancelled") != std::string::npos, "WAV importer cancellation error is descriptive");
    expect(sawNonZeroProgress, "WAV importer reports progress before decode cancellation");

    expect(std::filesystem::remove(wavPath), "Temporary decode cancel WAV file deleted");
}

void wavImportedPreparedClipStopsAndRestarts()
{
    const auto wavPath = makeTemporaryAudioPath("projectname-import-restart-test");
    writePcm16Wav(wavPath, 44100, 1, { 16384, -16384, 8192 });

    std::string error;
    auto clip = projectname::loadPcm16WavAsPreparedMonoClip(wavPath, error);
    expect(clip.has_value(), "Restart test WAV imports");

    if (clip.has_value())
    {
        projectname::AudioEngineStub audioEngine;
        audioEngine.prepare(clip->sampleRateHz);
        audioEngine.setPreparedMonoClipSamples(clip->samples);
        audioEngine.startScheduledPreparedMonoClip(0);

        std::vector<float> firstRender(1);
        audioEngine.render(firstRender.data(), nullptr, static_cast<int>(firstRender.size()));
        expect(firstRender[0] > 0.49f && firstRender[0] < 0.51f,
               "Imported prepared clip starts playback from first sample");

        audioEngine.stop();
        std::vector<float> stoppedRender(2, 1.0f);
        audioEngine.render(stoppedRender.data(), nullptr, static_cast<int>(stoppedRender.size()));
        expect(stoppedRender[0] == 0.0f && stoppedRender[1] == 0.0f,
               "Imported prepared clip stop renders silence");

        audioEngine.setTimelinePositionSamples(0);
        audioEngine.startScheduledPreparedMonoClip(0);
        std::vector<float> restartRender(1);
        audioEngine.render(restartRender.data(), nullptr, static_cast<int>(restartRender.size()));
        expect(restartRender[0] > 0.49f && restartRender[0] < 0.51f,
               "Imported prepared clip restarts from first sample");
    }

    expect(std::filesystem::remove(wavPath), "Temporary restart WAV file deleted");
}

void wavAudioImporterRejectsUnsupportedFiles()
{
    const auto notWavPath = makeTemporaryAudioPath("projectname-import-invalid-test");
    {
        std::ofstream file(notWavPath, std::ios::binary | std::ios::trunc);
        file << "not a wave";
    }

    std::string error;
    auto invalidClip = projectname::loadPcm16WavAsPreparedMonoClip(notWavPath, error);
    expect(!invalidClip.has_value(), "WAV importer rejects invalid RIFF files");
    expect(!error.empty(), "WAV importer reports invalid RIFF error");
    expect(std::filesystem::remove(notWavPath), "Temporary invalid WAV file deleted");

    const auto unsupportedPath = makeTemporaryAudioPath("projectname-import-unsupported-test");
    {
        std::ofstream file(unsupportedPath, std::ios::binary | std::ios::trunc);
        writeFourCc(file, "RIFF");
        writeUInt32LittleEndian(file, 40);
        writeFourCc(file, "WAVE");
        writeFourCc(file, "fmt ");
        writeUInt32LittleEndian(file, 16);
        writeUInt16LittleEndian(file, 1);
        writeUInt16LittleEndian(file, 1);
        writeUInt32LittleEndian(file, 48000);
        writeUInt32LittleEndian(file, 48000 * 3);
        writeUInt16LittleEndian(file, 3);
        writeUInt16LittleEndian(file, 24);
        writeFourCc(file, "data");
        writeUInt32LittleEndian(file, 3);
        file.put('\0');
        file.put('\0');
        file.put('\0');
    }

    auto unsupportedClip = projectname::loadPcm16WavAsPreparedMonoClip(unsupportedPath, error);
    expect(!unsupportedClip.has_value(), "WAV importer rejects unsupported bit depth");
    expect(error.find("16-bit") != std::string::npos, "WAV importer reports unsupported bit depth");
    expect(std::filesystem::remove(unsupportedPath), "Temporary unsupported WAV file deleted");
}

void projectAudioImportCopiesWavIntoPackageAndPersistsClip()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Imported Audio Package Test");
    project.getTransport().setTempoBpm(120.0);

    const auto package = makeTemporaryPackagePath("projectname-package-import-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-source-import-test");
    writePcm16Wav(wavPath, 48000, 1, { 32767, 0, -32768, 16384 });

    std::string error;
    auto result = projectname::importPcm16WavIntoProjectPackage(project, package, wavPath, error);
    expect(result.has_value(), "Project audio import succeeds");
    expect(error.empty(), "Project audio import leaves error empty");
    expect(result.has_value() && std::filesystem::is_regular_file(result->copiedAudioPath),
           "Project audio import copies WAV into package");
    expect(result.has_value() && result->clip.type == "audio-file",
           "Project audio import creates an audio-file clip");
    expect(result.has_value() && result->clip.relativePath.rfind("audio/", 0) == 0,
           "Project audio import stores package-relative audio path");
    expect(result.has_value() && result->clip.analysisPath.rfind("analysis/", 0) == 0,
           "Project audio import stores package-relative analysis path");
    expect(result.has_value() && result->preparedClip.samples.size() == 4,
           "Project audio import returns prepared samples for playback");
    expect(result.has_value() && result->clip.lengthBeats > 0.0,
           "Project audio import computes clip length in beats");
    expect(result.has_value() && std::filesystem::is_regular_file(result->waveformSummaryPath),
           "Project audio import writes waveform summary");
    expect(!std::filesystem::exists(package / ".projectname-staging"),
           "Project audio import cleans staging folder after success");

    const auto manifestText = readTextFile(package / "manifest.json");
    expect(manifestText.find("\"type\": \"audio-file\"") != std::string::npos,
           "Project manifest persists imported audio-file clip type");
    expect(result.has_value() && manifestText.find(result->clip.relativePath) != std::string::npos,
           "Project manifest persists imported relative audio path");
    expect(result.has_value() && manifestText.find(result->clip.analysisPath) != std::string::npos,
           "Project manifest persists imported analysis path");

    auto summary = result.has_value()
        ? projectname::loadWaveformSummary(result->waveformSummaryPath, error)
        : std::optional<projectname::WaveformSummary> {};
    expect(summary.has_value(), "Project audio import waveform summary loads");
    expect(summary.has_value() && summary->sampleRateHz == 48000.0,
           "Project audio import waveform summary stores sample rate");
    expect(summary.has_value() && summary->frameCount == 4,
           "Project audio import waveform summary stores frame count");
    expect(summary.has_value() && !summary->buckets.empty(),
           "Project audio import waveform summary stores buckets");
    expect(summary.has_value() && summary->buckets.front().peak > 0.99f,
           "Project audio import waveform summary stores peak data");

    auto thumbnail = projectname::loadFirstImportedAudioWaveform(project, package);
    expect(thumbnail.state == projectname::WaveformThumbnailState::ready,
           "Waveform thumbnail loader finds imported audio summary");
    expect(thumbnail.clip.analysisPath == result->clip.analysisPath,
           "Waveform thumbnail loader preserves clip analysis path");
    auto columns = projectname::makeWaveformPeakColumns(thumbnail.summary, 16);
    expect(!columns.empty(), "Waveform thumbnail peak columns are generated");
    expect(columns.front() > 0.99f, "Waveform thumbnail peak columns preserve peak values");

    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(loaded.has_value(), "Project with imported audio loads");
    expect(loaded.has_value() && *loaded == project, "Loaded imported-audio project equals in-memory project");

    if (result.has_value())
    {
        expect(std::filesystem::remove(result->waveformSummaryPath),
               "Temporary imported waveform summary file deleted for recovery test");
        auto loadedWithoutAnalysis = projectname::ProjectModel::loadPackage(package, error);
        expect(loadedWithoutAnalysis.has_value(), "Project loads with missing waveform summary");
        expect(loadedWithoutAnalysis.has_value() && *loadedWithoutAnalysis == project,
               "Missing waveform summary does not alter manifest model");
        auto missingThumbnail = projectname::loadFirstImportedAudioWaveform(project, package);
        expect(missingThumbnail.state == projectname::WaveformThumbnailState::missingAnalysis,
               "Waveform thumbnail loader reports missing analysis");
    }

    expect(std::filesystem::remove(wavPath), "Temporary import source WAV deleted");
    expect(std::filesystem::remove_all(package) > 0, "Temporary imported project package deleted");
}

void importedMediaPackageInventoryClassifiesReferencesAndCandidates()
{
    const auto package = makeTemporaryPackagePath("projectname-media-inventory-test");

    writeManifestText(package,
                      R"({
                        "tracks": [
                          {
                            "clips": [
                              {
                                "type": "audio-file",
                                "relativePath": "audio/current.wav",
                                "analysisPath": "analysis/current.waveform.json"
                              }
                            ]
                          }
                        ]
                      })");
    writeTextFile(package / "backups" / "manifest.previous.json",
                  R"({
                    "tracks": [
                      {
                        "clips": [
                          {
                            "type": "audio-file",
                            "relativePath": "audio/backup.wav",
                            "analysisPath": "analysis/backup.waveform.json"
                          }
                        ]
                      }
                    ]
                  })");

    writeTextFile(package / "audio" / "current.wav", "current");
    writeTextFile(package / "analysis" / "current.waveform.json", "{}");
    writeTextFile(package / "audio" / "backup.wav", "backup");
    writeTextFile(package / "analysis" / "backup.waveform.json", "{}");
    writeTextFile(package / "audio" / "session.wav", "session");
    writeTextFile(package / "analysis" / "session.waveform.json", "{}");
    writeTextFile(package / "audio" / "orphan.wav", "orphan");
    writeTextFile(package / "analysis" / "orphan.waveform.json", "{}");

    projectname::ImportedMediaPackageInventoryOptions options;
    options.protectedSessionReferences.push_back(
        { projectname::ImportedMediaPackageAssetKind::audio, "audio/session.wav" });
    options.protectedSessionReferences.push_back(
        { projectname::ImportedMediaPackageAssetKind::analysis, "analysis/session.waveform.json" });

    const auto inventory = projectname::buildImportedMediaPackageInventory(package, options);

    expect(inventory.currentManifestRead, "Media inventory reads current manifest");
    expect(inventory.previousManifestBackupRead, "Media inventory reads previous manifest backup");
    expect(inventory.error.empty(), "Media inventory leaves error empty for valid package");
    expect(inventory.missingReferences.empty(), "Media inventory does not report existing references as missing");
    expect(inventory.unsafeReferences.empty(), "Media inventory does not report safe references as unsafe");

    const auto* current = findInventoryAsset(inventory,
                                             projectname::ImportedMediaPackageAssetKind::audio,
                                             "audio/current.wav");
    expect(current != nullptr
               && hasInventoryReferenceSource(current->referenceSources,
                                              projectname::ImportedMediaPackageReferenceSource::currentManifest)
               && !current->unreferencedCandidate,
           "Media inventory protects current manifest audio asset");

    const auto* backup = findInventoryAsset(inventory,
                                            projectname::ImportedMediaPackageAssetKind::audio,
                                            "audio/backup.wav");
    expect(backup != nullptr
               && hasInventoryReferenceSource(backup->referenceSources,
                                              projectname::ImportedMediaPackageReferenceSource::previousManifestBackup)
               && !backup->unreferencedCandidate,
           "Media inventory protects previous manifest backup audio asset");

    const auto* session = findInventoryAsset(inventory,
                                             projectname::ImportedMediaPackageAssetKind::audio,
                                             "audio/session.wav");
    expect(session != nullptr
               && hasInventoryReferenceSource(session->referenceSources,
                                              projectname::ImportedMediaPackageReferenceSource::sessionProtected)
               && !session->unreferencedCandidate,
           "Media inventory protects caller-provided session audio asset");

    const auto* orphanAudio = findInventoryAsset(inventory,
                                                 projectname::ImportedMediaPackageAssetKind::audio,
                                                 "audio/orphan.wav");
    expect(orphanAudio != nullptr && orphanAudio->unreferencedCandidate,
           "Media inventory reports unreferenced package audio as candidate");

    const auto* orphanAnalysis = findInventoryAsset(inventory,
                                                    projectname::ImportedMediaPackageAssetKind::analysis,
                                                    "analysis/orphan.waveform.json");
    expect(orphanAnalysis != nullptr && orphanAnalysis->unreferencedCandidate,
           "Media inventory reports unreferenced waveform summary as candidate");

    expect(std::filesystem::remove_all(package) > 0, "Temporary media inventory package deleted");
}

void importedMediaPackageInventoryReportsUnsafeAndMissingReferences()
{
    const auto package = makeTemporaryPackagePath("projectname-media-inventory-invalid-test");

    writeManifestText(package,
                      R"({
                        "tracks": [
                          {
                            "clips": [
                              {
                                "type": "audio-file",
                                "relativePath": "audio/missing.wav",
                                "analysisPath": "analysis/missing.waveform.json"
                              },
                              {
                                "type": "audio-file",
                                "relativePath": "../outside.wav",
                                "analysisPath": "analysis/../outside.waveform.json"
                              },
                              {
                                "type": "audio-file",
                                "relativePath": "samples/not-owned.wav",
                                "analysisPath": "audio/not-analysis.waveform.json"
                              },
                              {
                                "type": "audio-file",
                                "relativePath": "audio/./not-normal.wav"
                              }
                            ]
                          }
                        ]
                      })");

    const auto inventory = projectname::buildImportedMediaPackageInventory(package);

    expect(inventory.currentManifestRead, "Invalid media inventory test still reads manifest");
    expect(findInventoryMissingReference(inventory,
                                         projectname::ImportedMediaPackageAssetKind::audio,
                                         "audio/missing.wav") != nullptr,
           "Media inventory reports missing referenced audio");
    expect(findInventoryMissingReference(inventory,
                                         projectname::ImportedMediaPackageAssetKind::analysis,
                                         "analysis/missing.waveform.json") != nullptr,
           "Media inventory reports missing referenced waveform summary");
    expect(hasUnsafeInventoryReference(inventory,
                                       projectname::ImportedMediaPackageAssetKind::audio,
                                       "../outside.wav"),
           "Media inventory rejects audio paths that escape the package");
    expect(hasUnsafeInventoryReference(inventory,
                                       projectname::ImportedMediaPackageAssetKind::analysis,
                                       "analysis/../outside.waveform.json"),
           "Media inventory rejects analysis paths that escape the package");
    expect(hasUnsafeInventoryReference(inventory,
                                       projectname::ImportedMediaPackageAssetKind::audio,
                                       "samples/not-owned.wav"),
           "Media inventory rejects imported audio outside the audio folder");
    expect(hasUnsafeInventoryReference(inventory,
                                       projectname::ImportedMediaPackageAssetKind::analysis,
                                       "audio/not-analysis.waveform.json"),
           "Media inventory rejects waveform summaries outside the analysis folder");
    expect(hasUnsafeInventoryReference(inventory,
                                       projectname::ImportedMediaPackageAssetKind::audio,
                                       "audio/./not-normal.wav"),
           "Media inventory rejects non-normalized package paths");

    expect(std::filesystem::remove_all(package) > 0, "Temporary invalid media inventory package deleted");
}

void importedMediaPackageInventoryClassifiesStagingDirectories()
{
    const auto package = makeTemporaryPackagePath("projectname-media-inventory-staging-test");
    writeManifestText(package, R"({ "tracks": [] })");
    writeTextFile(package / ".projectname-staging" / "audio-import-a" / "staged.wav", "a");
    writeTextFile(package / ".projectname-staging" / "media-relink-b" / "staged.wav", "b");

    const auto idleInventory = projectname::buildImportedMediaPackageInventory(package);
    expect(idleInventory.stagingDirectories.size() == 2,
           "Media inventory reports direct staging directories");
    expect(idleInventory.stagingDirectories.size() == 2
               && idleInventory.stagingDirectories[0].staleCandidate
               && idleInventory.stagingDirectories[1].staleCandidate,
           "Media inventory marks staging stale when no package work is active");
    expect(idleInventory.stagingDirectories.size() == 2
               && idleInventory.stagingDirectories[0].relativePath.rfind(".projectname-staging/", 0) == 0,
           "Media inventory reports staging with package-relative paths");

    projectname::ImportedMediaPackageInventoryOptions activeOptions;
    activeOptions.packageWorkInProgress = true;
    const auto activeInventory = projectname::buildImportedMediaPackageInventory(package, activeOptions);
    expect(activeInventory.stagingDirectories.size() == 2,
           "Media inventory still reports staging while package work is active");
    expect(activeInventory.stagingDirectories.size() == 2
               && !activeInventory.stagingDirectories[0].staleCandidate
               && !activeInventory.stagingDirectories[1].staleCandidate,
           "Media inventory does not mark staging stale while package work is active");

    expect(std::filesystem::remove_all(package) > 0, "Temporary staging inventory package deleted");
}

void packageMediaQuarantineRestoreManifestRoundTrips()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-manifest-test");
    const auto cleanupId = std::string("2026-06-18T18-30-00Z-test");
    const auto manifestPath = package / "backups" / "media-trash" / cleanupId / "restore-manifest.json";
    std::filesystem::create_directories(manifestPath.parent_path());

    auto manifest = makeTestQuarantineRestoreManifest(cleanupId);
    std::string error;
    expect(projectname::validatePackageMediaQuarantineRestoreManifest(manifest, error),
           "Quarantine restore manifest validates");
    expect(error.empty(), "Valid quarantine restore manifest leaves error empty");
    expect(projectname::savePackageMediaQuarantineRestoreManifest(manifest, manifestPath, error),
           "Quarantine restore manifest saves");
    const auto text = readTextFile(manifestPath);
    expect(text.find("\"schemaVersion\": 1") != std::string::npos,
           "Quarantine restore manifest writes human-readable schema version");
    expect(text.find("\"movedEntries\"") != std::string::npos,
           "Quarantine restore manifest writes moved entries");

    const auto loaded = projectname::loadPackageMediaQuarantineRestoreManifest(manifestPath, error);
    expect(loaded.has_value(), "Quarantine restore manifest loads");
    expect(loaded.has_value() && loaded->cleanupId == cleanupId,
           "Loaded quarantine restore manifest preserves cleanup id");
    expect(loaded.has_value() && loaded->movedEntries.size() == 2,
           "Loaded quarantine restore manifest preserves moved entries");
    expect(loaded.has_value()
               && loaded->movedEntries.front().kind == projectname::PackageMediaQuarantineEntryKind::audio
               && loaded->movedEntries.front().byteSize.has_value()
               && *loaded->movedEntries.front().byteSize == 1234,
           "Loaded quarantine restore manifest preserves audio metadata");
    expect(loaded.has_value() && loaded->skippedEntries.size() == 1
               && loaded->skippedEntries.front().reason == "current-manifest-reference",
           "Loaded quarantine restore manifest preserves skipped entries");

    expect(std::filesystem::remove_all(package) > 0, "Temporary quarantine manifest package deleted");
}

void packageMediaQuarantineRestoreManifestRejectsUnsafePaths()
{
    auto manifest = makeTestQuarantineRestoreManifest("2026-06-18T18-31-00Z-test");
    manifest.movedEntries.front().originalRelativePath = "../outside.wav";

    std::string error;
    expect(!projectname::validatePackageMediaQuarantineRestoreManifest(manifest, error),
           "Quarantine restore manifest rejects escaping original path");
    expect(error.find("Original quarantine manifest path") != std::string::npos,
           "Quarantine restore manifest reports unsafe original path");

    manifest = makeTestQuarantineRestoreManifest("2026-06-18T18-31-00Z-test");
    manifest.movedEntries.front().quarantineRelativePath =
        "backups/media-trash/wrong-id/audio/orphan.wav";
    expect(!projectname::validatePackageMediaQuarantineRestoreManifest(manifest, error),
           "Quarantine restore manifest rejects quarantine path outside cleanup id");
    expect(error.find("backups/media-trash") != std::string::npos,
           "Quarantine restore manifest reports unsafe quarantine path");

    manifest = makeTestQuarantineRestoreManifest("2026-06-18T18-31-00Z-test");
    manifest.skippedEntries.front().originalRelativePath = "audio/./current.wav";
    expect(!projectname::validatePackageMediaQuarantineRestoreManifest(manifest, error),
           "Quarantine restore manifest rejects non-normalized skipped path");
}

void packageMediaQuarantineRestoreManifestRejectsDuplicateDestinations()
{
    auto manifest = makeTestQuarantineRestoreManifest("2026-06-18T18-32-00Z-test");
    auto duplicateAudio = manifest.movedEntries.front();
    duplicateAudio.originalRelativePath = "audio/second-orphan.wav";
    manifest.movedEntries.push_back(std::move(duplicateAudio));

    std::string error;
    expect(!projectname::validatePackageMediaQuarantineRestoreManifest(manifest, error),
           "Quarantine restore manifest rejects duplicate quarantine destinations");
    expect(error.find("duplicate quarantine paths") != std::string::npos,
           "Quarantine restore manifest reports duplicate quarantine destination");
}

void packageMediaQuarantineRestoreManifestLoadsPartialFailureState()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-partial-test");
    const auto cleanupId = std::string("2026-06-18T18-33-00Z-test");
    const auto manifestPath = package / "backups" / "media-trash" / cleanupId / "restore-manifest.json";
    writeTextFile(manifestPath,
                  R"({
                    "schemaVersion": 1,
                    "application": "Rabbington Studio",
                    "cleanupId": "2026-06-18T18-33-00Z-test",
                    "createdAtUtc": "2026-06-18T18-33-00Z",
                    "packageDisplayPath": "Display Only.project",
                    "inventorySummary": "partial transaction fixture",
                    "state": "partial-failure",
                    "error": "Rollback could not restore one file.",
                    "movedEntries": [
                      {
                        "kind": "audio",
                        "originalPath": "audio/orphan.wav",
                        "quarantinePath": "backups/media-trash/2026-06-18T18-33-00Z-test/audio/orphan.wav",
                        "error": "Original path is occupied."
                      }
                    ],
                    "skippedEntries": [
                      {
                        "kind": "analysis",
                        "originalPath": "analysis/protected.waveform.json",
                        "reason": "previous-manifest-backup"
                      }
                    ]
                  })");

    std::string error;
    const auto loaded = projectname::loadPackageMediaQuarantineRestoreManifest(manifestPath, error);
    expect(loaded.has_value(), "Partial failure quarantine restore manifest loads");
    expect(loaded.has_value()
               && loaded->state == projectname::PackageMediaQuarantineManifestState::partialFailure,
           "Partial failure quarantine restore manifest preserves state");
    expect(loaded.has_value() && loaded->error == "Rollback could not restore one file.",
           "Partial failure quarantine restore manifest preserves error");
    expect(loaded.has_value() && loaded->movedEntries.size() == 1
               && loaded->movedEntries.front().error == "Original path is occupied.",
           "Partial failure quarantine restore manifest preserves entry error");

    expect(std::filesystem::remove_all(package) > 0, "Temporary partial quarantine manifest package deleted");
}

void packageMediaRestoreEntrySelectionTracksExplicitRestorableChoices()
{
    auto manifest = makeTestQuarantineRestoreManifest("2026-06-18T18-34-00Z-test");
    const auto selection = projectname::buildPackageMediaRestoreEntrySelection(
        manifest,
        { "audio/orphan.wav", "audio/stale.wav" });

    expect(selection.cleanupId == manifest.cleanupId,
           "Restore selection preserves cleanup id");
    expect(selection.entries.size() == 2 && selection.restorableEntryCount == 2,
           "Restore selection lists restorable moved entries");
    expect(selection.hasRestorableEntries,
           "Restore selection reports available restorable entries");
    expect(selection.hasSelection && selection.selectedRestorableEntryCount == 1,
           "Restore selection keeps explicitly selected restorable entry");
    expect(selection.restoreActionEnabled,
           "Restore selection enables restore when a restorable entry is selected");
    expect(selection.selectedOriginalRelativePaths == std::vector<std::string> { "audio/orphan.wav" },
           "Restore selection exposes selected original paths for command requests");
    expect(selection.staleSelectedOriginalRelativePaths == std::vector<std::string> { "audio/stale.wav" },
           "Restore selection records stale selected original paths");

    const auto* audio = findRestoreSelectionEntry(selection, "audio/orphan.wav");
    const auto* analysis = findRestoreSelectionEntry(selection, "analysis/orphan.waveform.json");
    expect(audio != nullptr && audio->selected && audio->restorable,
           "Restore selection marks selected audio entry");
    expect(analysis != nullptr && !analysis->selected && analysis->restorable,
           "Restore selection leaves unselected analysis entry restorable");

    auto toggled = projectname::togglePackageMediaRestoreEntrySelection(
        selection,
        "analysis/orphan.waveform.json");
    expect(toggled.selectedOriginalRelativePaths
               == std::vector<std::string> { "audio/orphan.wav", "analysis/orphan.waveform.json" },
           "Restore selection toggles restorable entries on");

    toggled = projectname::togglePackageMediaRestoreEntrySelection(toggled, "audio/orphan.wav");
    expect(toggled.selectedOriginalRelativePaths
               == std::vector<std::string> { "analysis/orphan.waveform.json" },
           "Restore selection toggles restorable entries off");

    const auto unknownToggled = projectname::togglePackageMediaRestoreEntrySelection(
        toggled,
        "audio/missing.wav");
    expect(unknownToggled.selectedOriginalRelativePaths == toggled.selectedOriginalRelativePaths,
           "Restore selection ignores unknown toggle paths");

    const auto cleared = projectname::clearPackageMediaRestoreEntrySelection(toggled);
    expect(!cleared.hasSelection && !cleared.restoreActionEnabled,
           "Restore selection disables restore for empty selection");
    expect(cleared.staleSelectedOriginalRelativePaths.empty(),
           "Restore selection clear removes stale selected paths");
    expect(cleared.restoreUnavailableReason.find("Select media entries") != std::string::npos,
           "Restore selection explains empty selection");

    const auto allSelected = projectname::selectAllPackageMediaRestoreEntries(cleared);
    expect(allSelected.hasSelection && allSelected.selectedRestorableEntryCount == 2,
           "Restore selection select-all chooses every restorable entry");
    expect(allSelected.selectedOriginalRelativePaths
               == std::vector<std::string> { "audio/orphan.wav", "analysis/orphan.waveform.json" },
           "Restore selection select-all preserves manifest entry order");
    expect(allSelected.staleSelectedOriginalRelativePaths.empty(),
           "Restore selection select-all removes stale selected paths");
}

void packageMediaRestoreEntrySelectionModelsReviewAndEmptyStates()
{
    {
        auto restoredManifest = makeTestQuarantineRestoreManifest("2026-06-18T18-35-00Z-restored");
        restoredManifest.state = projectname::PackageMediaQuarantineManifestState::restored;
        for (auto& entry : restoredManifest.movedEntries)
            entry.restored = true;

        const auto selection = projectname::buildPackageMediaRestoreEntrySelection(
            restoredManifest,
            { "audio/orphan.wav" });
        expect(selection.restorableEntryCount == 0 && !selection.hasSelection,
               "Restore selection does not select already-restored entries");
        expect(!selection.restoreActionEnabled,
               "Restore selection disables restored batches");

        const auto* audio = findRestoreSelectionEntry(selection, "audio/orphan.wav");
        expect(audio != nullptr
                   && !audio->restorable
                   && audio->unavailableReason.find("already been restored") != std::string::npos,
               "Restore selection explains already-restored entries");
    }

    {
        auto conflictManifest = makeTestQuarantineRestoreManifest("2026-06-18T18-36-00Z-conflict");
        conflictManifest.state = projectname::PackageMediaQuarantineManifestState::restoreConflict;
        conflictManifest.error = "Restore conflict needs review.";
        conflictManifest.movedEntries.front().restoreConflict = true;

        auto selection = projectname::buildPackageMediaRestoreEntrySelection(
            conflictManifest,
            { "analysis/orphan.waveform.json" });
        expect(selection.restorableEntryCount == 1 && selection.hasSelection,
               "Restore selection keeps clean entries selected in conflict batches");
        expect(selection.blockedByReviewState && !selection.restoreActionEnabled,
               "Restore selection blocks conflict batches from restore execution");
        expect(selection.restoreUnavailableReason.find("Review restore") != std::string::npos,
               "Restore selection explains conflict review block");

        selection = projectname::selectAllPackageMediaRestoreEntries(selection);
        expect(selection.selectedRestorableEntryCount == 1 && !selection.restoreActionEnabled,
               "Restore selection select-all remains blocked for conflict batches");
    }

    {
        auto partialManifest = makeTestQuarantineRestoreManifest("2026-06-18T18-37-00Z-partial");
        partialManifest.state = projectname::PackageMediaQuarantineManifestState::partialFailure;
        partialManifest.error = "Restore batch needs review.";
        partialManifest.movedEntries.front().error = "Quarantine path is missing.";

        const auto selection = projectname::buildPackageMediaRestoreEntrySelection(
            partialManifest,
            { "analysis/orphan.waveform.json" });
        expect(selection.restorableEntryCount == 1 && selection.hasSelection,
               "Restore selection keeps clean entries selected in partial-failure batches");
        expect(selection.blockedByReviewState && !selection.restoreActionEnabled,
               "Restore selection blocks partial-failure batches from restore execution");

        const auto* audio = findRestoreSelectionEntry(selection, "audio/orphan.wav");
        expect(audio != nullptr
                   && !audio->restorable
                   && audio->unavailableReason.find("partial restore failure") != std::string::npos,
               "Restore selection explains partial-failure entries");
    }

    {
        projectname::PackageMediaQuarantineRestoreManifest emptyManifest;
        emptyManifest.cleanupId = "2026-06-18T18-38-00Z-empty";
        emptyManifest.createdAtUtc = "2026-06-18T18-38-00Z";
        emptyManifest.packageDisplayPath = "Display Only.project";
        emptyManifest.inventorySummary = "empty";

        const auto selection = projectname::buildPackageMediaRestoreEntrySelection(emptyManifest);
        expect(selection.entries.empty() && !selection.hasRestorableEntries,
               "Restore selection handles empty moved-entry manifests");
        expect(!selection.restoreActionEnabled,
               "Restore selection disables empty moved-entry manifests");
        expect(selection.restoreUnavailableReason.find("no media entries") != std::string::npos,
               "Restore selection explains empty moved-entry manifests");
    }
}

void packageMediaQuarantinePreflightPlansCandidates()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-preflight-test");
    const auto cleanupId = std::string("2026-06-18T19-00-00Z-test");
    auto inventory = makePreflightInventoryWithCandidates(package);
    auto request = makePreflightRequest(std::move(inventory), cleanupId);

    const auto result = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
    expect(result.status == projectname::PackageMediaQuarantinePreflightStatus::planned,
           "Quarantine preflight plans valid candidates");
    expect(result.restoreManifest.has_value(),
           "Quarantine preflight returns restore manifest draft");
    if (result.restoreManifest.has_value())
    {
        const auto& manifest = *result.restoreManifest;
        expect(manifest.cleanupId == cleanupId,
               "Quarantine preflight preserves cleanup id");
        expect(manifest.movedEntries.size() == 3,
               "Quarantine preflight plans audio, analysis, and staging moves");
        expect(findQuarantineMovedEntry(manifest,
                                        projectname::PackageMediaQuarantineEntryKind::audio,
                                        "audio/orphan.wav") != nullptr,
               "Quarantine preflight plans audio asset");
        const auto* audio = findQuarantineMovedEntry(manifest,
                                                     projectname::PackageMediaQuarantineEntryKind::audio,
                                                     "audio/orphan.wav");
        expect(audio != nullptr
                   && audio->quarantineRelativePath
                       == "backups/media-trash/" + cleanupId + "/audio/orphan.wav"
                   && audio->byteSize.has_value(),
               "Quarantine preflight creates audio quarantine path and byte size");
        expect(findQuarantineMovedEntry(manifest,
                                        projectname::PackageMediaQuarantineEntryKind::analysis,
                                        "analysis/orphan.waveform.json") != nullptr,
               "Quarantine preflight plans analysis asset");
        expect(findQuarantineMovedEntry(manifest,
                                        projectname::PackageMediaQuarantineEntryKind::stagingDirectory,
                                        ".projectname-staging/audio-import-a") != nullptr,
               "Quarantine preflight plans stale staging directory");
        const auto* skipped = findQuarantineSkippedEntry(manifest, "audio/current.wav");
        expect(skipped != nullptr && skipped->reason == "protected-reference",
               "Quarantine preflight records protected asset as skipped");

        std::string error;
        expect(projectname::validatePackageMediaQuarantineRestoreManifest(manifest, error),
               "Quarantine preflight draft validates as restore manifest");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary quarantine preflight package deleted");
}

void packageMediaQuarantinePreflightRejectsBlockedInventory()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-preflight-blocked-test");

    {
        auto request = makePreflightRequest(makePreflightInventoryWithCandidates(package), "bad/id");
        const auto result = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
        expect(result.status == projectname::PackageMediaQuarantinePreflightStatus::invalidCleanupId,
               "Quarantine preflight rejects invalid cleanup id");
    }

    {
        auto request = makePreflightRequest(makePreflightInventoryWithCandidates(package));
        request.packageWorkInProgress = true;
        const auto result = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
        expect(result.status == projectname::PackageMediaQuarantinePreflightStatus::activePackageWork,
               "Quarantine preflight rejects active package work");
    }

    {
        auto inventory = makePreflightInventoryWithCandidates(package);
        inventory.stagingDirectories.front().staleCandidate = false;
        auto request = makePreflightRequest(std::move(inventory));
        const auto result = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
        expect(result.status == projectname::PackageMediaQuarantinePreflightStatus::activePackageWork,
               "Quarantine preflight rejects active staging directory");
    }

    {
        auto inventory = makePreflightInventoryWithCandidates(package);
        projectname::ImportedMediaPackageUnsafeReference unsafe;
        unsafe.kind = projectname::ImportedMediaPackageAssetKind::audio;
        unsafe.relativePath = "../outside.wav";
        unsafe.reason = "Escapes package";
        inventory.unsafeReferences.push_back(std::move(unsafe));
        auto request = makePreflightRequest(std::move(inventory));
        const auto result = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
        expect(result.status == projectname::PackageMediaQuarantinePreflightStatus::unsafeReferences,
               "Quarantine preflight rejects unsafe inventory references");
    }

    {
        auto inventory = makePreflightInventoryWithCandidates(package);
        projectname::ImportedMediaPackageMissingReference missing;
        missing.kind = projectname::ImportedMediaPackageAssetKind::analysis;
        missing.relativePath = "analysis/missing.waveform.json";
        inventory.missingReferences.push_back(std::move(missing));
        auto request = makePreflightRequest(std::move(inventory));
        const auto result = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
        expect(result.status == projectname::PackageMediaQuarantinePreflightStatus::missingReferences,
               "Quarantine preflight rejects missing inventory references");
    }

    {
        auto request = makePreflightRequest(makePreflightInventoryWithCandidates(package));
        request.requestedOriginalRelativePaths.push_back("audio/current.wav");
        const auto result = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
        expect(result.status == projectname::PackageMediaQuarantinePreflightStatus::protectedReference,
               "Quarantine preflight rejects requested protected asset");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary blocked preflight package deleted");
}

void packageMediaQuarantinePreflightRejectsEmptyAndDuplicatePlans()
{
    {
        projectname::ImportedMediaPackageInventory emptyInventory;
        auto request = makePreflightRequest(std::move(emptyInventory));
        const auto result = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
        expect(result.status == projectname::PackageMediaQuarantinePreflightStatus::noMovableCandidates,
               "Quarantine preflight rejects empty plans");
    }

    {
        const auto package = makeTemporaryPackagePath("projectname-quarantine-preflight-duplicate-test");
        auto request = makePreflightRequest(makePreflightInventoryWithCandidates(package));
        request.requestedOriginalRelativePaths.push_back("audio/orphan.wav");
        request.requestedOriginalRelativePaths.push_back("audio/orphan.wav");

        const auto result = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
        expect(result.status
                   == projectname::PackageMediaQuarantinePreflightStatus::duplicateQuarantineDestination,
               "Quarantine preflight rejects duplicate requested paths");

        expect(std::filesystem::remove_all(package) > 0, "Temporary duplicate preflight package deleted");
    }
}

void packageMediaQuarantineCommandMovesCandidates()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-command-test");
    const auto cleanupId = std::string("2026-06-18T20-00-00Z-test");
    auto inventory = makePreflightInventoryWithCandidates(package);
    auto request = makePreflightRequest(std::move(inventory), cleanupId);
    const auto preflight = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
    expect(preflight.restoreManifest.has_value(), "Quarantine command test preflight creates manifest");

    if (preflight.restoreManifest.has_value())
    {
        projectname::PackageMediaQuarantineCommandRequest command;
        command.packageDirectory = package;
        command.restoreManifestDraft = *preflight.restoreManifest;

        const auto result = projectname::quarantinePackageMedia(std::move(command));
        expect(result.status == projectname::PackageMediaQuarantineCommandStatus::completed,
               "Quarantine command moves valid candidates");
        expect(std::filesystem::is_regular_file(result.restoreManifestPath),
               "Quarantine command commits restore manifest");
        expect(!std::filesystem::exists(result.temporaryRestoreManifestPath),
               "Quarantine command removes temporary manifest after success");
        expect(!std::filesystem::exists(package / "audio" / "orphan.wav"),
               "Quarantine command removes original audio candidate");
        expect(std::filesystem::is_regular_file(package
                                                / "backups"
                                                / "media-trash"
                                                / cleanupId
                                                / "audio"
                                                / "orphan.wav"),
               "Quarantine command moves audio candidate to quarantine");
        expect(!std::filesystem::exists(package / "analysis" / "orphan.waveform.json"),
               "Quarantine command removes original analysis candidate");
        expect(std::filesystem::is_regular_file(package
                                                / "backups"
                                                / "media-trash"
                                                / cleanupId
                                                / "analysis"
                                                / "orphan.waveform.json"),
               "Quarantine command moves analysis candidate to quarantine");
        expect(!std::filesystem::exists(package / ".projectname-staging" / "audio-import-a"),
               "Quarantine command removes original stale staging directory");
        expect(std::filesystem::is_directory(package
                                             / "backups"
                                             / "media-trash"
                                             / cleanupId
                                             / "staging"
                                             / "audio-import-a"),
               "Quarantine command moves stale staging directory to quarantine");

        std::string error;
        const auto loaded = projectname::loadPackageMediaQuarantineRestoreManifest(
            result.restoreManifestPath,
            error);
        expect(loaded.has_value(), "Committed quarantine restore manifest loads");
        expect(loaded.has_value()
                   && loaded->state == projectname::PackageMediaQuarantineManifestState::completed,
               "Committed quarantine restore manifest records completed state");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary quarantine command package deleted");
}

void packageMediaQuarantineCommandRollsBackOnDestinationConflict()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-command-conflict-test");
    const auto cleanupId = std::string("2026-06-18T20-10-00Z-test");
    auto inventory = makePreflightInventoryWithCandidates(package);
    auto request = makePreflightRequest(std::move(inventory), cleanupId);
    const auto preflight = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
    expect(preflight.restoreManifest.has_value(), "Quarantine conflict test preflight creates manifest");

    if (preflight.restoreManifest.has_value())
    {
        const auto* analysis = findQuarantineMovedEntry(*preflight.restoreManifest,
                                                        projectname::PackageMediaQuarantineEntryKind::analysis,
                                                        "analysis/orphan.waveform.json");
        expect(analysis != nullptr, "Quarantine conflict test has analysis move");
        if (analysis != nullptr)
            writeTextFile(package / analysis->quarantineRelativePath, "occupied");

        projectname::PackageMediaQuarantineCommandRequest command;
        command.packageDirectory = package;
        command.restoreManifestDraft = *preflight.restoreManifest;

        const auto result = projectname::quarantinePackageMedia(std::move(command));
        expect(result.status == projectname::PackageMediaQuarantineCommandStatus::destinationOccupied,
               "Quarantine command reports occupied destination");
        expect(std::filesystem::is_regular_file(package / "audio" / "orphan.wav"),
               "Quarantine command rolls back previously moved audio");
        expect(!std::filesystem::exists(package
                                        / "backups"
                                        / "media-trash"
                                        / cleanupId
                                        / "audio"
                                        / "orphan.wav"),
               "Quarantine command removes rolled-back audio from quarantine");
        expect(std::filesystem::is_regular_file(package / "analysis" / "orphan.waveform.json"),
               "Quarantine command leaves conflicting analysis source in place");
        expect(analysis != nullptr
                   && std::filesystem::is_regular_file(package / analysis->quarantineRelativePath),
               "Quarantine command preserves the occupied destination");
        expect(!std::filesystem::exists(result.restoreManifestPath),
               "Quarantine command does not commit manifest after successful rollback");
        expect(!std::filesystem::exists(result.temporaryRestoreManifestPath),
               "Quarantine command removes temporary manifest after rollback");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary quarantine conflict package deleted");
}

void packageMediaQuarantineCommandCleansTempManifestOnMissingSource()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-command-missing-test");
    const auto cleanupId = std::string("2026-06-18T20-20-00Z-test");
    auto inventory = makePreflightInventoryWithCandidates(package);
    auto request = makePreflightRequest(std::move(inventory), cleanupId);
    const auto preflight = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
    expect(preflight.restoreManifest.has_value(), "Quarantine missing-source test preflight creates manifest");

    if (preflight.restoreManifest.has_value())
    {
        std::filesystem::remove(package / "audio" / "orphan.wav");

        projectname::PackageMediaQuarantineCommandRequest command;
        command.packageDirectory = package;
        command.restoreManifestDraft = *preflight.restoreManifest;

        const auto result = projectname::quarantinePackageMedia(std::move(command));
        expect(result.status == projectname::PackageMediaQuarantineCommandStatus::sourceMissing,
               "Quarantine command reports missing source");
        expect(!std::filesystem::exists(result.restoreManifestPath),
               "Quarantine command does not commit manifest for missing source");
        expect(!std::filesystem::exists(result.temporaryRestoreManifestPath),
               "Quarantine command removes temporary manifest for missing source");
        expect(std::filesystem::is_regular_file(package / "analysis" / "orphan.waveform.json"),
               "Quarantine command does not move later candidates after missing source");
        expect(std::filesystem::is_directory(package / ".projectname-staging" / "audio-import-a"),
               "Quarantine command does not move staging after missing source");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary quarantine missing package deleted");
}

void packageMediaQuarantineRestoreCommandRestoresAllCandidates()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-restore-test");
    const auto cleanupId = std::string("2026-06-18T21-00-00Z-test");
    auto inventory = makePreflightInventoryWithCandidates(package);
    auto request = makePreflightRequest(std::move(inventory), cleanupId);
    const auto preflight = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
    expect(preflight.restoreManifest.has_value(), "Quarantine restore test preflight creates manifest");

    if (preflight.restoreManifest.has_value())
    {
        projectname::PackageMediaQuarantineCommandRequest quarantineRequest;
        quarantineRequest.packageDirectory = package;
        quarantineRequest.restoreManifestDraft = *preflight.restoreManifest;
        const auto quarantine = projectname::quarantinePackageMedia(std::move(quarantineRequest));
        expect(quarantine.status == projectname::PackageMediaQuarantineCommandStatus::completed,
               "Quarantine restore test moves candidates before restore");

        projectname::PackageMediaQuarantineRestoreCommandRequest restoreRequest;
        restoreRequest.packageDirectory = package;
        restoreRequest.restoreManifestPath = quarantine.restoreManifestPath;
        const auto restored = projectname::restorePackageMediaFromQuarantine(std::move(restoreRequest));
        expect(restored.status == projectname::PackageMediaQuarantineRestoreCommandStatus::restored,
               "Quarantine restore command restores all entries");
        expect(restored.restoredCount == 3,
               "Quarantine restore command reports restored entries");
        expect(restored.conflictCount == 0 && restored.missingCount == 0,
               "Quarantine restore command reports no conflicts or missing paths");
        expect(!std::filesystem::exists(restored.temporaryRestoreManifestPath),
               "Quarantine restore command removes temporary manifest after success");
        expect(std::filesystem::is_regular_file(package / "audio" / "orphan.wav"),
               "Quarantine restore command restores audio file");
        expect(std::filesystem::is_regular_file(package / "analysis" / "orphan.waveform.json"),
               "Quarantine restore command restores analysis file");
        expect(std::filesystem::is_directory(package / ".projectname-staging" / "audio-import-a"),
               "Quarantine restore command restores stale staging directory");
        expect(!std::filesystem::exists(package
                                        / "backups"
                                        / "media-trash"
                                        / cleanupId
                                        / "audio"
                                        / "orphan.wav"),
               "Quarantine restore command removes restored audio from quarantine");

        std::string error;
        const auto loaded = projectname::loadPackageMediaQuarantineRestoreManifest(
            restored.restoreManifestPath,
            error);
        expect(loaded.has_value(), "Restored quarantine manifest reloads");
        expect(loaded.has_value()
                   && loaded->state == projectname::PackageMediaQuarantineManifestState::restored,
               "Restored quarantine manifest records restored state");
        const auto* audio = loaded.has_value()
            ? findQuarantineMovedEntry(*loaded,
                                       projectname::PackageMediaQuarantineEntryKind::audio,
                                       "audio/orphan.wav")
            : nullptr;
        expect(audio != nullptr && audio->restored && !audio->restoreConflict,
               "Restored quarantine manifest marks audio restored");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary quarantine restore package deleted");
}

void packageMediaQuarantineRestoreCommandRestoresSelectedEntries()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-selected-restore-test");
    const auto cleanupId = std::string("2026-06-18T21-10-00Z-test");
    auto inventory = makePreflightInventoryWithCandidates(package);
    auto request = makePreflightRequest(std::move(inventory), cleanupId);
    const auto preflight = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
    expect(preflight.restoreManifest.has_value(), "Selected quarantine restore test preflight creates manifest");

    if (preflight.restoreManifest.has_value())
    {
        projectname::PackageMediaQuarantineCommandRequest quarantineRequest;
        quarantineRequest.packageDirectory = package;
        quarantineRequest.restoreManifestDraft = *preflight.restoreManifest;
        const auto quarantine = projectname::quarantinePackageMedia(std::move(quarantineRequest));
        expect(quarantine.status == projectname::PackageMediaQuarantineCommandStatus::completed,
               "Selected quarantine restore test moves candidates before restore");

        projectname::PackageMediaQuarantineRestoreCommandRequest restoreRequest;
        restoreRequest.packageDirectory = package;
        restoreRequest.restoreManifestPath = quarantine.restoreManifestPath;
        restoreRequest.selectedOriginalRelativePaths.push_back("audio/orphan.wav");
        const auto restored = projectname::restorePackageMediaFromQuarantine(std::move(restoreRequest));
        expect(restored.status == projectname::PackageMediaQuarantineRestoreCommandStatus::restored,
               "Quarantine restore command restores selected entry");
        expect(restored.restoredCount == 1,
               "Quarantine restore command reports selected restore count");
        expect(std::filesystem::is_regular_file(package / "audio" / "orphan.wav"),
               "Quarantine restore command restores selected audio");
        expect(!std::filesystem::exists(package / "analysis" / "orphan.waveform.json"),
               "Quarantine restore command leaves unselected analysis original absent");
        expect(std::filesystem::is_regular_file(package
                                                / "backups"
                                                / "media-trash"
                                                / cleanupId
                                                / "analysis"
                                                / "orphan.waveform.json"),
               "Quarantine restore command leaves unselected analysis quarantined");
        expect(!std::filesystem::exists(restored.temporaryRestoreManifestPath),
               "Quarantine restore command removes temporary manifest after selected restore");

        std::string error;
        const auto loaded = projectname::loadPackageMediaQuarantineRestoreManifest(
            restored.restoreManifestPath,
            error);
        expect(loaded.has_value()
                   && loaded->state == projectname::PackageMediaQuarantineManifestState::completed,
               "Selected restore keeps manifest completed until all entries restore");
        const auto* audio = loaded.has_value()
            ? findQuarantineMovedEntry(*loaded,
                                       projectname::PackageMediaQuarantineEntryKind::audio,
                                       "audio/orphan.wav")
            : nullptr;
        const auto* analysis = loaded.has_value()
            ? findQuarantineMovedEntry(*loaded,
                                       projectname::PackageMediaQuarantineEntryKind::analysis,
                                       "analysis/orphan.waveform.json")
            : nullptr;
        expect(audio != nullptr && audio->restored,
               "Selected restore marks selected audio restored");
        expect(analysis != nullptr && !analysis->restored,
               "Selected restore leaves unselected analysis unrestored");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary selected restore package deleted");
}

void packageMediaQuarantineRestoreCommandMarksOccupiedOriginalConflicts()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-restore-conflict-test");
    const auto cleanupId = std::string("2026-06-18T21-20-00Z-test");
    auto inventory = makePreflightInventoryWithCandidates(package);
    auto request = makePreflightRequest(std::move(inventory), cleanupId);
    const auto preflight = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
    expect(preflight.restoreManifest.has_value(), "Conflict quarantine restore test preflight creates manifest");

    if (preflight.restoreManifest.has_value())
    {
        projectname::PackageMediaQuarantineCommandRequest quarantineRequest;
        quarantineRequest.packageDirectory = package;
        quarantineRequest.restoreManifestDraft = *preflight.restoreManifest;
        const auto quarantine = projectname::quarantinePackageMedia(std::move(quarantineRequest));
        expect(quarantine.status == projectname::PackageMediaQuarantineCommandStatus::completed,
               "Conflict quarantine restore test moves candidates before restore");
        writeTextFile(package / "audio" / "orphan.wav", "active replacement");

        projectname::PackageMediaQuarantineRestoreCommandRequest restoreRequest;
        restoreRequest.packageDirectory = package;
        restoreRequest.restoreManifestPath = quarantine.restoreManifestPath;
        const auto restored = projectname::restorePackageMediaFromQuarantine(std::move(restoreRequest));
        expect(restored.status == projectname::PackageMediaQuarantineRestoreCommandStatus::restoreConflict,
               "Quarantine restore command reports occupied original conflict");
        expect(restored.conflictCount == 1,
               "Quarantine restore command reports one conflict");
        expect(readTextFile(package / "audio" / "orphan.wav") == "active replacement",
               "Quarantine restore command does not overwrite occupied original");
        expect(std::filesystem::is_regular_file(package
                                                / "backups"
                                                / "media-trash"
                                                / cleanupId
                                                / "audio"
                                                / "orphan.wav"),
               "Quarantine restore command leaves conflicted audio quarantined");
        expect(std::filesystem::is_regular_file(package / "analysis" / "orphan.waveform.json"),
               "Quarantine restore command restores non-conflicting analysis");
        expect(std::filesystem::is_directory(package / ".projectname-staging" / "audio-import-a"),
               "Quarantine restore command restores non-conflicting staging");
        expect(!std::filesystem::exists(restored.temporaryRestoreManifestPath),
               "Quarantine restore command removes temporary manifest after conflict");

        std::string error;
        const auto loaded = projectname::loadPackageMediaQuarantineRestoreManifest(
            restored.restoreManifestPath,
            error);
        expect(loaded.has_value()
                   && loaded->state == projectname::PackageMediaQuarantineManifestState::restoreConflict,
               "Conflict restore manifest records restore-conflict state");
        const auto* audio = loaded.has_value()
            ? findQuarantineMovedEntry(*loaded,
                                       projectname::PackageMediaQuarantineEntryKind::audio,
                                       "audio/orphan.wav")
            : nullptr;
        expect(audio != nullptr && audio->restoreConflict && !audio->restored,
               "Conflict restore manifest marks audio conflict without restored flag");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary conflict restore package deleted");
}

void packageMediaQuarantineRestoreCommandPersistsMissingQuarantinePath()
{
    const auto package = makeTemporaryPackagePath("projectname-quarantine-restore-missing-test");
    const auto cleanupId = std::string("2026-06-18T21-30-00Z-test");
    auto inventory = makePreflightInventoryWithCandidates(package);
    auto request = makePreflightRequest(std::move(inventory), cleanupId);
    const auto preflight = projectname::buildPackageMediaQuarantinePreflightPlan(std::move(request));
    expect(preflight.restoreManifest.has_value(), "Missing quarantine restore test preflight creates manifest");

    if (preflight.restoreManifest.has_value())
    {
        projectname::PackageMediaQuarantineCommandRequest quarantineRequest;
        quarantineRequest.packageDirectory = package;
        quarantineRequest.restoreManifestDraft = *preflight.restoreManifest;
        const auto quarantine = projectname::quarantinePackageMedia(std::move(quarantineRequest));
        expect(quarantine.status == projectname::PackageMediaQuarantineCommandStatus::completed,
               "Missing quarantine restore test moves candidates before restore");
        std::filesystem::remove(package
                                / "backups"
                                / "media-trash"
                                / cleanupId
                                / "analysis"
                                / "orphan.waveform.json");

        projectname::PackageMediaQuarantineRestoreCommandRequest restoreRequest;
        restoreRequest.packageDirectory = package;
        restoreRequest.restoreManifestPath = quarantine.restoreManifestPath;
        const auto restored = projectname::restorePackageMediaFromQuarantine(std::move(restoreRequest));
        expect(restored.status == projectname::PackageMediaQuarantineRestoreCommandStatus::missingQuarantinePath,
               "Quarantine restore command reports missing quarantine path");
        expect(restored.missingCount == 1,
               "Quarantine restore command reports missing path count");
        expect(std::filesystem::is_regular_file(package / "audio" / "orphan.wav"),
               "Quarantine restore command restores other entries despite missing path");
        expect(!std::filesystem::exists(package / "analysis" / "orphan.waveform.json"),
               "Quarantine restore command cannot restore missing analysis quarantine");
        expect(!std::filesystem::exists(restored.temporaryRestoreManifestPath),
               "Quarantine restore command removes temporary manifest after missing path");

        std::string error;
        const auto loaded = projectname::loadPackageMediaQuarantineRestoreManifest(
            restored.restoreManifestPath,
            error);
        expect(loaded.has_value()
                   && loaded->state == projectname::PackageMediaQuarantineManifestState::partialFailure,
               "Missing restore manifest records partial-failure state");
        const auto* analysis = loaded.has_value()
            ? findQuarantineMovedEntry(*loaded,
                                       projectname::PackageMediaQuarantineEntryKind::analysis,
                                       "analysis/orphan.waveform.json")
            : nullptr;
        expect(analysis != nullptr && !analysis->error.empty(),
               "Missing restore manifest records analysis entry error");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary missing restore package deleted");
}

void backgroundPackageMediaCleanupJobQuarantinesPackage()
{
    const auto package = makeTemporaryPackagePath("projectname-background-cleanup-quarantine-test");
    const auto cleanupId = std::string("2026-06-18T22-00-00Z-test");
    writePackageMediaCleanupJobFixture(package);

    projectname::BackgroundPackageMediaCleanupJob job(
        makeBackgroundCleanupQuarantineRequest(package, cleanupId));
    job.start();
    const auto startedProgress = job.getProgress();
    expect(startedProgress.phase != projectname::BackgroundPackageMediaCleanupPhase::pending,
           "Background cleanup job reports progress after start");

    const auto result = job.waitForResult();
    const auto completedProgress = job.getProgress();
    expect(!result.cancelled, "Background cleanup quarantine job is not cancelled");
    expect(result.preflight.status == projectname::PackageMediaQuarantinePreflightStatus::planned,
           "Background cleanup quarantine job plans candidates");
    expect(result.quarantine.status == projectname::PackageMediaQuarantineCommandStatus::completed,
           "Background cleanup quarantine job completes command");
    expect(completedProgress.phase == projectname::BackgroundPackageMediaCleanupPhase::completed,
           "Background cleanup quarantine job reports completed phase");
    expect(completedProgress.percent == 100,
           "Background cleanup quarantine job reports 100 percent");
    expect(std::filesystem::is_regular_file(result.quarantine.restoreManifestPath),
           "Background cleanup quarantine job writes restore manifest");
    expect(!std::filesystem::exists(package / "audio" / "orphan.wav"),
           "Background cleanup quarantine job moves audio original");
    expect(std::filesystem::is_regular_file(package
                                            / "backups"
                                            / "media-trash"
                                            / cleanupId
                                            / "audio"
                                            / "orphan.wav"),
           "Background cleanup quarantine job moves audio to quarantine");

    expect(std::filesystem::remove_all(package) > 0, "Temporary background cleanup package deleted");
}

void backgroundPackageMediaCleanupJobRestoresPackage()
{
    const auto package = makeTemporaryPackagePath("projectname-background-cleanup-restore-test");
    const auto cleanupId = std::string("2026-06-18T22-10-00Z-test");
    writePackageMediaCleanupJobFixture(package);

    projectname::BackgroundPackageMediaCleanupJob quarantineJob(
        makeBackgroundCleanupQuarantineRequest(package, cleanupId));
    const auto quarantine = quarantineJob.waitForResult();
    expect(quarantine.quarantine.status == projectname::PackageMediaQuarantineCommandStatus::completed,
           "Background cleanup restore test quarantines package first");

    projectname::BackgroundPackageMediaCleanupRequest restoreRequest;
    restoreRequest.operation = projectname::BackgroundPackageMediaCleanupOperation::restore;
    restoreRequest.packageDirectory = package;
    restoreRequest.restoreManifestPath = quarantine.quarantine.restoreManifestPath;
    projectname::BackgroundPackageMediaCleanupJob restoreJob(std::move(restoreRequest));
    restoreJob.start();
    const auto result = restoreJob.waitForResult();
    const auto completedProgress = restoreJob.getProgress();

    expect(result.restore.status == projectname::PackageMediaQuarantineRestoreCommandStatus::restored,
           "Background cleanup restore job completes command");
    expect(result.restore.restoredCount == 3,
           "Background cleanup restore job reports restored entries");
    expect(completedProgress.phase == projectname::BackgroundPackageMediaCleanupPhase::completed,
           "Background cleanup restore job reports completed phase");
    expect(std::filesystem::is_regular_file(package / "audio" / "orphan.wav"),
           "Background cleanup restore job restores audio");
    expect(std::filesystem::is_regular_file(package / "analysis" / "orphan.waveform.json"),
           "Background cleanup restore job restores analysis");
    expect(std::filesystem::is_directory(package / ".projectname-staging" / "audio-import-a"),
           "Background cleanup restore job restores staging");

    expect(std::filesystem::remove_all(package) > 0, "Temporary background restore package deleted");
}

void backgroundPackageMediaCleanupJobRestoresSelectedEntries()
{
    const auto package = makeTemporaryPackagePath("projectname-background-selected-restore-test");
    const auto cleanupId = std::string("2026-06-18T22-11-00Z-test");
    writePackageMediaCleanupJobFixture(package);

    projectname::BackgroundPackageMediaCleanupJob quarantineJob(
        makeBackgroundCleanupQuarantineRequest(package, cleanupId));
    const auto quarantine = quarantineJob.waitForResult();
    expect(quarantine.quarantine.status == projectname::PackageMediaQuarantineCommandStatus::completed,
           "Background selected restore test quarantines package first");

    projectname::BackgroundPackageMediaCleanupRequest restoreRequest;
    restoreRequest.operation = projectname::BackgroundPackageMediaCleanupOperation::restore;
    restoreRequest.packageDirectory = package;
    restoreRequest.restoreManifestPath = quarantine.quarantine.restoreManifestPath;
    restoreRequest.selectedRestoreOriginalRelativePaths.push_back("audio/orphan.wav");
    projectname::BackgroundPackageMediaCleanupJob restoreJob(std::move(restoreRequest));
    restoreJob.start();
    const auto result = restoreJob.waitForResult();

    expect(result.restore.status == projectname::PackageMediaQuarantineRestoreCommandStatus::restored,
           "Background selected restore job completes command");
    expect(result.restore.restoredCount == 1,
           "Background selected restore job reports selected restore count");
    expect(std::filesystem::is_regular_file(package / "audio" / "orphan.wav"),
           "Background selected restore job restores selected audio");
    expect(!std::filesystem::exists(package / "analysis" / "orphan.waveform.json"),
           "Background selected restore job leaves unselected analysis original absent");
    expect(std::filesystem::is_regular_file(package
                                            / "backups"
                                            / "media-trash"
                                            / cleanupId
                                            / "analysis"
                                            / "orphan.waveform.json"),
           "Background selected restore job leaves unselected analysis quarantined");

    expect(std::filesystem::remove_all(package) > 0, "Temporary background selected restore package deleted");
}

void backgroundPackageMediaCleanupJobCancelsBeforeStart()
{
    const auto package = makeTemporaryPackagePath("projectname-background-cleanup-cancel-test");
    writePackageMediaCleanupJobFixture(package);

    projectname::BackgroundPackageMediaCleanupJob job(
        makeBackgroundCleanupQuarantineRequest(package, "2026-06-18T22-20-00Z-test"));
    job.requestCancel();
    const auto cancelledProgress = job.getProgress();
    expect(cancelledProgress.phase == projectname::BackgroundPackageMediaCleanupPhase::cancelled,
           "Background cleanup job reports cancelled before start");
    expect(cancelledProgress.cancelRequested,
           "Background cleanup job records cancel request");

    const auto result = job.waitForResult();
    expect(result.cancelled, "Background cleanup job returns cancelled result");
    expect(!std::filesystem::exists(package
                                    / "backups"
                                    / "media-trash"
                                    / "2026-06-18T22-20-00Z-test"
                                    / "restore-manifest.json"),
           "Background cleanup cancellation avoids package mutation");
    expect(std::filesystem::is_regular_file(package / "audio" / "orphan.wav"),
           "Background cleanup cancellation leaves audio original in place");

    expect(std::filesystem::remove_all(package) > 0, "Temporary background cancel package deleted");
}

void backgroundPackageMediaCleanupJobRejectsActivePackageWork()
{
    const auto package = makeTemporaryPackagePath("projectname-background-cleanup-active-test");
    writePackageMediaCleanupJobFixture(package);
    auto request = makeBackgroundCleanupQuarantineRequest(package, "2026-06-18T22-30-00Z-test");
    request.packageWorkInProgress = true;

    projectname::BackgroundPackageMediaCleanupJob job(std::move(request));
    const auto result = job.waitForResult();
    const auto progress = job.getProgress();
    expect(result.preflight.status == projectname::PackageMediaQuarantinePreflightStatus::activePackageWork,
           "Background cleanup job rejects active package work");
    expect(progress.phase == projectname::BackgroundPackageMediaCleanupPhase::failed,
           "Background cleanup job reports failed phase for active work");
    expect(!result.error.empty(),
           "Background cleanup job reports active work error");
    expect(std::filesystem::is_regular_file(package / "audio" / "orphan.wav"),
           "Background cleanup active-work rejection leaves audio original");

    expect(std::filesystem::remove_all(package) > 0, "Temporary background active package deleted");
}

void backgroundPackageMediaCleanupJobPropagatesCommandFailure()
{
    const auto package = makeTemporaryPackagePath("projectname-background-cleanup-failure-test");
    const auto cleanupId = std::string("2026-06-18T22-40-00Z-test");
    writePackageMediaCleanupJobFixture(package);
    writeTextFile(package
                      / "backups"
                      / "media-trash"
                      / cleanupId
                      / "audio"
                      / "orphan.wav",
                  "occupied");

    projectname::BackgroundPackageMediaCleanupJob job(
        makeBackgroundCleanupQuarantineRequest(package, cleanupId));
    const auto result = job.waitForResult();
    const auto progress = job.getProgress();
    expect(result.quarantine.status == projectname::PackageMediaQuarantineCommandStatus::destinationOccupied,
           "Background cleanup job propagates quarantine command failure");
    expect(progress.phase == projectname::BackgroundPackageMediaCleanupPhase::failed,
           "Background cleanup job reports failed phase for command failure");
    expect(!result.error.empty(),
           "Background cleanup job reports command failure error");
    expect(std::filesystem::is_regular_file(package / "audio" / "orphan.wav"),
           "Background cleanup command failure leaves audio original");

    expect(std::filesystem::remove_all(package) > 0, "Temporary background failure package deleted");
}

void packageMediaCleanupStatusMapsInventoryAndPreflightStates()
{
    {
        projectname::ImportedMediaPackageInventory inventory;
        const auto empty = projectname::describePackageMediaCleanupInventory(inventory);
        expect(empty.kind == projectname::PackageMediaCleanupStatusKind::noCandidates,
               "Cleanup status reports no inventory candidates");
        expect(empty.severity == projectname::PackageMediaCleanupStatusSeverity::success,
               "Cleanup status treats no candidates as successful");
    }

    {
        projectname::ImportedMediaPackageInventory inventory;
        projectname::ImportedMediaPackageAsset asset;
        asset.relativePath = "audio/orphan.wav";
        asset.unreferencedCandidate = true;
        inventory.assets.push_back(std::move(asset));

        const auto review = projectname::describePackageMediaCleanupInventory(inventory);
        expect(review.kind == projectname::PackageMediaCleanupStatusKind::reviewAvailable,
               "Cleanup status reports inventory candidates");
        expect(review.browserAffordanceId
                   == std::string(projectname::packageMediaCleanupAffordanceIds::browserReviewAction),
               "Cleanup inventory status targets browser review action");
    }

    {
        projectname::ImportedMediaPackageInventory inventory;
        projectname::ImportedMediaPackageUnsafeReference unsafe;
        unsafe.relativePath = "../outside.wav";
        unsafe.reason = "unsafe path";
        inventory.unsafeReferences.push_back(std::move(unsafe));

        const auto unsafeStatus = projectname::describePackageMediaCleanupInventory(inventory);
        expect(unsafeStatus.kind == projectname::PackageMediaCleanupStatusKind::unsafeReferences,
               "Cleanup status reports unsafe inventory references");
        expect(unsafeStatus.severity == projectname::PackageMediaCleanupStatusSeverity::warning,
               "Cleanup unsafe reference status is warning severity");
    }

    {
        projectname::ImportedMediaPackageInventory inventory;
        projectname::ImportedMediaPackageMissingReference missing;
        missing.relativePath = "audio/missing.wav";
        inventory.missingReferences.push_back(std::move(missing));

        const auto missingStatus = projectname::describePackageMediaCleanupInventory(inventory);
        expect(missingStatus.kind == projectname::PackageMediaCleanupStatusKind::missingReferences,
               "Cleanup status reports missing inventory references");
        expect(missingStatus.inspectorAffordanceId
                   == std::string(projectname::packageMediaCleanupAffordanceIds::inspectorRestoreOptions),
               "Cleanup missing reference status targets inspector restore options");
    }

    {
        projectname::PackageMediaQuarantinePreflightResult preflight;
        preflight.status = projectname::PackageMediaQuarantinePreflightStatus::planned;
        preflight.restoreManifest =
            makeTestQuarantineRestoreManifest("2026-06-18T23-00-00Z-status");

        const auto ready = projectname::describePackageMediaCleanupPreflight(preflight);
        expect(ready.kind == projectname::PackageMediaCleanupStatusKind::preflightReady,
               "Cleanup status reports planned preflight as ready");
        expect(ready.browserAffordanceId
                   == std::string(projectname::packageMediaCleanupAffordanceIds::browserCleanupAction),
               "Cleanup preflight-ready status targets cleanup confirmation");
    }

    {
        projectname::PackageMediaQuarantinePreflightResult preflight;
        preflight.status = projectname::PackageMediaQuarantinePreflightStatus::activePackageWork;
        preflight.error = "Package work is active.";

        const auto active = projectname::describePackageMediaCleanupPreflight(preflight);
        expect(active.kind == projectname::PackageMediaCleanupStatusKind::activePackageWork,
               "Cleanup status reports active package work");
        expect(active.browserAffordanceId
                   == std::string(projectname::packageMediaCleanupAffordanceIds::browserCleanupDisabled),
               "Cleanup active-work status disables browser cleanup action");
    }

    {
        projectname::PackageMediaQuarantinePreflightResult preflight;
        preflight.status = projectname::PackageMediaQuarantinePreflightStatus::noMovableCandidates;

        const auto noCandidates = projectname::describePackageMediaCleanupPreflight(preflight);
        expect(noCandidates.kind == projectname::PackageMediaCleanupStatusKind::noCandidates,
               "Cleanup status reports no preflight candidates");
    }
}

void packageMediaCleanupStatusMapsQuarantineRestoreAndCancellation()
{
    {
        projectname::PackageMediaQuarantineCommandResult quarantine;
        quarantine.status = projectname::PackageMediaQuarantineCommandStatus::completed;
        quarantine.restoreManifest =
            makeTestQuarantineRestoreManifest("2026-06-18T23-10-00Z-status");

        const auto completed = projectname::describePackageMediaCleanupQuarantine(quarantine);
        expect(completed.kind == projectname::PackageMediaCleanupStatusKind::quarantineCompleted,
               "Cleanup status reports quarantine completion");
        expect(completed.severity == projectname::PackageMediaCleanupStatusSeverity::success,
               "Cleanup quarantine completion is success severity");
        expect(completed.browserAffordanceId
                   == std::string(projectname::packageMediaCleanupAffordanceIds::browserRestoreList),
               "Cleanup quarantine completion targets restore list");
    }

    {
        projectname::PackageMediaQuarantineCommandResult quarantine;
        quarantine.status = projectname::PackageMediaQuarantineCommandStatus::destinationOccupied;
        quarantine.error = "Quarantine destination is occupied.";

        const auto failed = projectname::describePackageMediaCleanupQuarantine(quarantine);
        expect(failed.kind == projectname::PackageMediaCleanupStatusKind::cleanupFailed,
               "Cleanup status reports quarantine failure");
        expect(failed.severity == projectname::PackageMediaCleanupStatusSeverity::error,
               "Cleanup quarantine failure is error severity");
        expect(failed.detailText == "Quarantine destination is occupied.",
               "Cleanup quarantine failure preserves command detail");
    }

    {
        projectname::PackageMediaQuarantineRestoreCommandResult restore;
        restore.status = projectname::PackageMediaQuarantineRestoreCommandStatus::restored;
        restore.restoredCount = 3;

        const auto restored = projectname::describePackageMediaCleanupRestore(restore);
        expect(restored.kind == projectname::PackageMediaCleanupStatusKind::restoreCompleted,
               "Cleanup status reports restore completion");
        expect(restored.detailText == "3 item(s) restored.",
               "Cleanup restore completion reports restored count");
    }

    {
        projectname::PackageMediaQuarantineRestoreCommandResult restore;
        restore.status = projectname::PackageMediaQuarantineRestoreCommandStatus::restoreConflict;
        restore.conflictCount = 1;

        const auto conflict = projectname::describePackageMediaCleanupRestore(restore);
        expect(conflict.kind == projectname::PackageMediaCleanupStatusKind::restoreConflict,
               "Cleanup status reports restore conflict");
        expect(conflict.inspectorAffordanceId
                   == std::string(projectname::packageMediaCleanupAffordanceIds::inspectorConflictNotice),
               "Cleanup restore conflict targets inspector conflict notice");
    }

    {
        projectname::PackageMediaQuarantineRestoreCommandResult restore;
        restore.status = projectname::PackageMediaQuarantineRestoreCommandStatus::missingQuarantinePath;
        restore.missingCount = 1;

        const auto partial = projectname::describePackageMediaCleanupRestore(restore);
        expect(partial.kind == projectname::PackageMediaCleanupStatusKind::partialFailure,
               "Cleanup status reports restore partial failure");
        expect(partial.severity == projectname::PackageMediaCleanupStatusSeverity::error,
               "Cleanup restore partial failure is error severity");
    }

    {
        auto manifest = makeTestQuarantineRestoreManifest("2026-06-18T23-20-00Z-status");
        manifest.state = projectname::PackageMediaQuarantineManifestState::restoreConflict;
        manifest.movedEntries.front().restoreConflict = true;

        const auto conflict = projectname::describePackageMediaCleanupRestoreManifest(manifest);
        expect(conflict.kind == projectname::PackageMediaCleanupStatusKind::restoreConflict,
               "Cleanup status maps restore-manifest conflict");
    }

    {
        projectname::BackgroundPackageMediaCleanupProgress progress;
        progress.phase = projectname::BackgroundPackageMediaCleanupPhase::inventory;
        progress.percent = 15;

        const auto running = projectname::describePackageMediaCleanupProgress(progress);
        expect(running.kind == projectname::PackageMediaCleanupStatusKind::inventoryRunning,
               "Cleanup status maps inventory progress");
        expect(running.statusBarAffordanceId
                   == std::string(projectname::packageMediaCleanupAffordanceIds::statusBarProgress),
               "Cleanup progress targets status bar progress affordance");
    }

    {
        projectname::BackgroundPackageMediaCleanupResult result;
        result.cancelled = true;

        const auto cancelled = projectname::describePackageMediaCleanupResult(result);
        expect(cancelled.kind == projectname::PackageMediaCleanupStatusKind::cancelled,
               "Cleanup status maps cancellation");
        expect(cancelled.statusText == "Cleanup was cancelled before moving media.",
               "Cleanup cancellation text is stable");
    }
}

void packageMediaCleanupBatchDiscoveryListsValidBatchesNewestFirst()
{
    const auto package = makeTemporaryPackagePath("projectname-cleanup-batch-discovery-test");
    const auto completedId = std::string("2026-06-18T23-00-00Z-completed");
    const auto restoredId = std::string("2026-06-18T23-10-00Z-restored");
    const auto conflictId = std::string("2026-06-18T23-20-00Z-conflict");
    const auto partialId = std::string("2026-06-18T23-30-00Z-partial");

    saveTestCleanupBatchManifest(package,
                                 completedId,
                                 "2026-06-18T23-00-00Z",
                                 projectname::PackageMediaQuarantineManifestState::completed);
    saveTestCleanupBatchManifest(package,
                                 restoredId,
                                 "2026-06-18T23-10-00Z",
                                 projectname::PackageMediaQuarantineManifestState::restored);
    saveTestCleanupBatchManifest(package,
                                 conflictId,
                                 "2026-06-18T23-20-00Z",
                                 projectname::PackageMediaQuarantineManifestState::restoreConflict);
    saveTestCleanupBatchManifest(package,
                                 partialId,
                                 "2026-06-18T23-30-00Z",
                                 projectname::PackageMediaQuarantineManifestState::partialFailure);

    const auto result = projectname::discoverPackageMediaCleanupBatches(package);
    expect(result.error.empty(), "Cleanup batch discovery leaves result error empty for valid batches");
    expect(result.issues.empty(), "Cleanup batch discovery reports no issues for valid batches");
    expect(result.batches.size() == 4, "Cleanup batch discovery lists all valid batches");

    if (result.batches.size() == 4)
    {
        expect(result.batches[0].cleanupId == partialId
                   && result.batches[1].cleanupId == conflictId
                   && result.batches[2].cleanupId == restoredId
                   && result.batches[3].cleanupId == completedId,
               "Cleanup batch discovery sorts newest first");
        expect(result.batches[0].status.kind == projectname::PackageMediaCleanupStatusKind::partialFailure,
               "Cleanup batch discovery attaches partial-failure status");
        expect(result.batches[1].status.kind == projectname::PackageMediaCleanupStatusKind::restoreConflict,
               "Cleanup batch discovery attaches restore-conflict status");
        expect(result.batches[2].status.kind == projectname::PackageMediaCleanupStatusKind::restoreCompleted,
               "Cleanup batch discovery attaches restored status");
        expect(result.batches[3].status.kind == projectname::PackageMediaCleanupStatusKind::quarantineCompleted,
               "Cleanup batch discovery attaches completed quarantine status");
        expect(result.batches.front().manifestRelativePath.generic_string()
                   == "backups/media-trash/" + partialId + "/restore-manifest.json",
               "Cleanup batch discovery stores package-relative manifest path");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary cleanup batch discovery package deleted");
}

void packageMediaCleanupBatchDiscoveryReportsInvalidAndUnreadableBatches()
{
    const auto package = makeTemporaryPackagePath("projectname-cleanup-batch-issue-test");
    const auto validId = std::string("2026-06-18T23-40-00Z-valid");
    const auto unreadableId = std::string("2026-06-18T23-50-00Z-unreadable");
    const auto missingId = std::string("2026-06-19T00-00-00Z-missing");
    const auto invalidId = std::string("bad id");

    saveTestCleanupBatchManifest(package,
                                 validId,
                                 "2026-06-18T23-40-00Z",
                                 projectname::PackageMediaQuarantineManifestState::completed);
    writeTextFile(package
                      / "backups"
                      / "media-trash"
                      / unreadableId
                      / "restore-manifest.json",
                  "{ invalid json");
    std::filesystem::create_directories(package / "backups" / "media-trash" / missingId);
    writeTextFile(package
                      / "backups"
                      / "media-trash"
                      / invalidId
                      / "restore-manifest.json",
                  "{ }");

    const auto result = projectname::discoverPackageMediaCleanupBatches(package);
    expect(result.batches.size() == 1, "Cleanup batch discovery keeps valid batches with issues present");
    expect(result.batches.size() == 1 && result.batches.front().cleanupId == validId,
           "Cleanup batch discovery loads the valid batch");
    expect(hasCleanupBatchDiscoveryIssue(result,
                                         projectname::PackageMediaCleanupBatchDiscoveryIssueKind::invalidCleanupId,
                                         invalidId),
           "Cleanup batch discovery reports invalid cleanup ids");
    expect(hasCleanupBatchDiscoveryIssue(result,
                                         projectname::PackageMediaCleanupBatchDiscoveryIssueKind::manifestLoadFailed,
                                         unreadableId),
           "Cleanup batch discovery reports unreadable manifests");
    expect(hasCleanupBatchDiscoveryIssue(result,
                                         projectname::PackageMediaCleanupBatchDiscoveryIssueKind::manifestMissing,
                                         missingId),
           "Cleanup batch discovery reports missing manifests");

    expect(std::filesystem::remove_all(package) > 0, "Temporary cleanup batch issue package deleted");
}

void packageMediaMaintenanceViewModelSummarizesEmptyAndCleanupCandidateStates()
{
    {
        projectname::PackageMediaMaintenanceViewModelRequest request;
        request.inventory = {};
        request.discovery = {};

        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        expect(model.inventoryStatus.kind == projectname::PackageMediaCleanupStatusKind::noCandidates,
               "Maintenance view model reports empty package inventory");
        expect(!model.cleanupReviewAvailable,
               "Maintenance view model does not offer cleanup review for empty inventory");
        expect(model.batches.empty(), "Maintenance view model has no batches for empty discovery");
        expect(!model.restoreActionEnabled,
               "Maintenance view model disables restore without a selected batch");
        expect(!model.restoreUnavailableReason.empty(),
               "Maintenance view model explains missing restore selection");
    }

    {
        const auto package = makeTemporaryPackagePath("projectname-maintenance-candidates-test");
        projectname::PackageMediaMaintenanceViewModelRequest request;
        request.inventory = makePreflightInventoryWithCandidates(package);
        request.discovery = {};

        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        expect(model.inventoryStatus.kind == projectname::PackageMediaCleanupStatusKind::reviewAvailable,
               "Maintenance view model reports cleanup candidates");
        expect(model.cleanupReviewAvailable,
               "Maintenance view model offers cleanup review for candidates");
        expect(model.cleanupCandidateCount == 2,
               "Maintenance view model counts unreferenced media candidates");
        expect(model.staleStagingCandidateCount == 1,
               "Maintenance view model counts stale staging candidates");
        expect(!model.hasDiscoveryIssues,
               "Maintenance view model has no discovery issues when discovery is empty");

        expect(std::filesystem::remove_all(package) > 0,
               "Temporary maintenance candidate package deleted");
    }
}

void packageMediaMaintenanceViewModelCombinesBatchRowsAndRestoreEnablement()
{
    const auto package = makeTemporaryPackagePath("projectname-maintenance-batch-test");
    const auto completedId = std::string("2026-06-19T00-10-00Z-completed");
    const auto restoredId = std::string("2026-06-19T00-20-00Z-restored");
    const auto conflictId = std::string("2026-06-19T00-30-00Z-conflict");
    const auto partialId = std::string("2026-06-19T00-40-00Z-partial");

    saveTestCleanupBatchManifest(package,
                                 completedId,
                                 "2026-06-19T00-10-00Z",
                                 projectname::PackageMediaQuarantineManifestState::completed);
    saveTestCleanupBatchManifest(package,
                                 restoredId,
                                 "2026-06-19T00-20-00Z",
                                 projectname::PackageMediaQuarantineManifestState::restored);
    saveTestCleanupBatchManifest(package,
                                 conflictId,
                                 "2026-06-19T00-30-00Z",
                                 projectname::PackageMediaQuarantineManifestState::restoreConflict);
    saveTestCleanupBatchManifest(package,
                                 partialId,
                                 "2026-06-19T00-40-00Z",
                                 projectname::PackageMediaQuarantineManifestState::partialFailure);

    const auto discovery = projectname::discoverPackageMediaCleanupBatches(package);
    expect(discovery.batches.size() == 4,
           "Maintenance view model batch test discovers fixture batches");

    {
        projectname::PackageMediaMaintenanceViewModelRequest request;
        request.discovery = discovery;
        request.selectedCleanupId = completedId;
        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        expect(model.hasSelectedBatch && model.selectedCleanupId == completedId,
               "Maintenance view model preserves selected completed batch");
        expect(!model.restoreActionEnabled,
               "Maintenance view model requires selected entries before enabling restore");
        expect(model.restoreUnavailableReason.find("Select media entries") != std::string::npos,
               "Maintenance view model explains missing restore-entry selection");
        expect(model.batches[static_cast<std::size_t>(model.selectedBatchIndex)].restoreActionEnabled,
               "Maintenance view model marks completed row restorable");
        const auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
        expect(selected.movedEntryCount == 2 && selected.restorableEntryCount == 2,
               "Maintenance view model counts completed batch restorable entries");
        expect(selected.entryPreviews.size() == 2
                   && selected.entryPreviews.front().originalRelativePath == "audio/orphan.wav",
               "Maintenance view model carries completed batch entry preview paths");
        expect(model.restoreEntrySelection.entries.size() == 2
                   && model.restoreEntrySelection.selectedOriginalRelativePaths.empty(),
               "Maintenance view model exposes unselected restore-entry candidates");

        auto allSelected =
            projectname::selectAllPackageMediaRestoreEntriesInSelectedBatch(model);
        expect(allSelected.restoreActionEnabled
                   && allSelected.restoreEntrySelection.selectedRestorableEntryCount == 2,
               "Maintenance view model select-all enables restore for selected entries");

        auto toggled =
            projectname::togglePackageMediaRestoreEntryInSelectedBatch(allSelected, "audio/orphan.wav");
        expect(toggled.restoreActionEnabled
                   && toggled.restoreEntrySelection.selectedOriginalRelativePaths
                       == std::vector<std::string> { "analysis/orphan.waveform.json" },
               "Maintenance view model toggles selected restore entries");

        const auto cleared =
            projectname::clearPackageMediaRestoreEntriesInSelectedBatch(std::move(toggled));
        expect(!cleared.restoreActionEnabled
                   && cleared.restoreEntrySelection.selectedOriginalRelativePaths.empty(),
               "Maintenance view model clear disables restore for empty entry selection");
    }

    {
        projectname::PackageMediaMaintenanceViewModelRequest request;
        request.discovery = discovery;
        request.selectedCleanupId = completedId;
        request.selectedRestoreOriginalRelativePaths.push_back("audio/orphan.wav");
        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        expect(model.restoreActionEnabled,
               "Maintenance view model enables restore after entry selection");
        expect(model.restoreEntrySelection.selectedOriginalRelativePaths
                   == std::vector<std::string> { "audio/orphan.wav" },
               "Maintenance view model carries selected restore-entry paths");
    }

    {
        projectname::PackageMediaMaintenanceViewModelRequest request;
        request.discovery = discovery;
        request.selectedCleanupId = restoredId;
        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        expect(model.hasSelectedBatch && model.selectedCleanupId == restoredId,
               "Maintenance view model preserves selected restored batch");
        expect(!model.restoreActionEnabled,
               "Maintenance view model disables restore for restored batch");
        expect(model.restoreUnavailableReason.find("no unrestored media") != std::string::npos,
               "Maintenance view model explains restored batch disablement");
        const auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
        expect(selected.restoredEntryCount == 2 && selected.restorableEntryCount == 0,
               "Maintenance view model counts restored batch entries");
        expect(selected.entryPreviews.size() == 2 && selected.entryPreviews.front().restored,
               "Maintenance view model marks restored entry previews");
    }

    {
        projectname::PackageMediaMaintenanceViewModelRequest request;
        request.discovery = discovery;
        request.selectedCleanupId = conflictId;
        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        expect(model.hasSelectedBatch && model.selectedCleanupId == conflictId,
               "Maintenance view model preserves selected conflicted batch");
        expect(!model.restoreActionEnabled,
               "Maintenance view model disables restore for conflicted batch");
        expect(model.restoreUnavailableReason.find("conflicts") != std::string::npos,
               "Maintenance view model explains conflicted batch disablement");
        const auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
        expect(selected.conflictCount == 1 && selected.restorableEntryCount == 1,
               "Maintenance view model counts conflict batch entries");
        expect(selected.entryPreviews.size() == 2 && selected.entryPreviews.front().restoreConflict,
               "Maintenance view model marks conflict entry previews");
    }

    {
        projectname::PackageMediaMaintenanceViewModelRequest request;
        request.discovery = discovery;
        request.selectedCleanupId = partialId;
        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        expect(model.hasSelectedBatch && model.selectedCleanupId == partialId,
               "Maintenance view model preserves selected partial-failure batch");
        expect(!model.restoreActionEnabled,
               "Maintenance view model disables restore for partial-failure batch");
        expect(model.restoreUnavailableReason.find("Review restore") != std::string::npos,
               "Maintenance view model explains partial-failure disablement");
        const auto& selected = model.batches[static_cast<std::size_t>(model.selectedBatchIndex)];
        expect(selected.errorCount == 1 && selected.restorableEntryCount == 1,
               "Maintenance view model counts partial-failure entries");
        expect(selected.entryPreviews.size() == 2 && selected.entryPreviews.front().hasError,
               "Maintenance view model marks partial-failure entry previews");
    }

    {
        projectname::PackageMediaMaintenanceViewModelRequest request;
        request.discovery = discovery;
        request.selectedCleanupId = "missing-selection";
        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        expect(model.hasSelectedBatch && model.selectedCleanupId == partialId,
               "Maintenance view model falls back to newest batch when selection is stale");
        expect(model.selectedBatchIndex == 0,
               "Maintenance view model marks fallback batch index");
        expect(!model.restoreActionEnabled,
               "Maintenance view model applies fallback batch restore state");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary maintenance batch package deleted");
}

void packageMediaMaintenanceViewModelCarriesDiscoveryIssues()
{
    const auto package = makeTemporaryPackagePath("projectname-maintenance-issues-test");
    const auto validId = std::string("2026-06-19T00-50-00Z-valid");
    const auto invalidId = std::string("bad id");

    saveTestCleanupBatchManifest(package,
                                 validId,
                                 "2026-06-19T00-50-00Z",
                                 projectname::PackageMediaQuarantineManifestState::completed);
    writeTextFile(package
                      / "backups"
                      / "media-trash"
                      / invalidId
                      / "restore-manifest.json",
                  "{ }");

    projectname::PackageMediaMaintenanceViewModelRequest request;
    request.discovery = projectname::discoverPackageMediaCleanupBatches(package);
    request.selectedCleanupId = validId;
    request.selectedRestoreOriginalRelativePaths.push_back("audio/orphan.wav");

    const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
    expect(model.hasDiscoveryIssues,
           "Maintenance view model reports discovery issues");
    expect(model.discoveryIssues.size() == 1,
           "Maintenance view model carries discovery issue count");
    expect(model.batches.size() == 1 && model.batches.front().cleanupId == validId,
           "Maintenance view model keeps valid batches beside discovery issues");
    expect(model.restoreActionEnabled,
           "Maintenance view model still enables valid selected batch restore");

    expect(std::filesystem::remove_all(package) > 0, "Temporary maintenance issue package deleted");
}

void packageMediaMaintenanceBrowserRowsRenderSelectableBatchState()
{
    {
        const projectname::PackageMediaMaintenanceViewModel model;
        const auto rows = projectname::buildPackageMediaMaintenanceBrowserRows(
            model,
            { false, false, 2 });

        const auto* batchCount = findMaintenanceBrowserRow(
            rows,
            projectname::PackageMediaMaintenanceBrowserRowKind::batchCount);
        expect(batchCount != nullptr && batchCount->text == "Batches: waiting for scan",
               "Maintenance browser rows render waiting state before scan snapshot");
        expect(countSelectableMaintenanceBrowserRows(rows) == 0,
               "Maintenance browser rows have no selectable rows before scan snapshot");
        expect(rows.selectedRowIndex < 0,
               "Maintenance browser rows have no selected row before scan snapshot");
        expect(!rows.restoreAction.visible && !rows.restoreAction.enabled,
               "Maintenance browser restore action stays hidden before scan snapshot");
        expect(!rows.cleanupAction.visible && !rows.cleanupAction.enabled,
               "Maintenance browser cleanup action stays hidden before scan snapshot");
    }

    {
        projectname::PackageMediaMaintenanceViewModelRequest request;
        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        const auto rows = projectname::buildPackageMediaMaintenanceBrowserRows(
            model,
            { true, false, 2 });

        const auto* selected = findMaintenanceBrowserRow(
            rows,
            projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatch);
        expect(selected != nullptr && selected->text == "Selected: none",
               "Maintenance browser rows render empty batch selection state");
        const auto* entrySummary = findMaintenanceBrowserRow(
            rows,
            projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntrySummary);
        expect(entrySummary != nullptr
                   && entrySummary->text == "Entries: no cleanup batch selected",
               "Maintenance browser rows render empty detail state");
        expect(hasMaintenanceBrowserRowText(
                   rows,
                   projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
                   "no cleanup batch selected"),
               "Maintenance browser rows render empty path detail state");
        expect(countSelectableMaintenanceBrowserRows(rows) == 0,
               "Maintenance browser rows expose no selectable rows for empty batches");
        expect(rows.restoreAction.visible && !rows.restoreAction.enabled,
               "Maintenance browser restore action is visible but disabled without selection");
        expect(rows.restoreAction.disabledReason.find("Select") != std::string::npos,
               "Maintenance browser restore action exposes no-selection disabled reason");
        expect(rows.cleanupAction.visible && !rows.cleanupAction.enabled,
               "Maintenance browser cleanup action is visible but disabled without candidates");
        expect(rows.cleanupAction.disabledReason.find("No cleanup candidates") != std::string::npos,
               "Maintenance browser cleanup action exposes empty-candidate disabled reason");
    }

    {
        const auto package = makeTemporaryPackagePath("projectname-maintenance-cleanup-action-test");
        projectname::PackageMediaMaintenanceViewModelRequest request;
        request.inventory = makePreflightInventoryWithCandidates(package);
        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        const auto rows = projectname::buildPackageMediaMaintenanceBrowserRows(
            model,
            { true, false, 2 });

        const auto* cleanupRow = findMaintenanceBrowserRow(
            rows,
            projectname::PackageMediaMaintenanceBrowserRowKind::cleanupSummary);
        expect(cleanupRow != nullptr && cleanupRow->text == "Cleanup: available",
               "Maintenance browser rows render cleanup availability");
        expect(rows.cleanupAction.visible && rows.cleanupAction.enabled,
               "Maintenance browser cleanup action enables for cleanup candidates");
        expect(rows.cleanupAction.disabledReason.empty(),
               "Maintenance browser cleanup action has no disabled reason when enabled");

        const auto activeRows = projectname::buildPackageMediaMaintenanceBrowserRows(
            model,
            { true, false, 2, true });
        expect(activeRows.cleanupAction.visible && !activeRows.cleanupAction.enabled,
               "Maintenance browser cleanup action disables during active package work");
        expect(activeRows.cleanupAction.disabledReason.find("busy") != std::string::npos,
               "Maintenance browser cleanup action exposes active-work disabled reason");

        expect(std::filesystem::remove_all(package) > 0,
               "Temporary maintenance cleanup action package deleted");
    }

    {
        projectname::PackageMediaMaintenanceViewModelRequest request;
        projectname::ImportedMediaPackageMissingReference missing;
        missing.relativePath = "audio/missing.wav";
        request.inventory.missingReferences.push_back(std::move(missing));
        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        const auto rows = projectname::buildPackageMediaMaintenanceBrowserRows(
            model,
            { true, false, 2 });
        expect(rows.cleanupAction.visible && !rows.cleanupAction.enabled,
               "Maintenance browser cleanup action disables for missing references");
        expect(rows.cleanupAction.disabledReason.find("missing") != std::string::npos,
               "Maintenance browser cleanup action exposes missing-reference disabled reason");
    }

    {
        projectname::PackageMediaMaintenanceViewModelRequest request;
        projectname::ImportedMediaPackageUnsafeReference unsafe;
        unsafe.relativePath = "../outside.wav";
        unsafe.reason = "Path escapes package.";
        request.inventory.unsafeReferences.push_back(std::move(unsafe));
        const auto model = projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
        const auto rows = projectname::buildPackageMediaMaintenanceBrowserRows(
            model,
            { true, false, 2 });
        expect(rows.cleanupAction.visible && !rows.cleanupAction.enabled,
               "Maintenance browser cleanup action disables for unsafe references");
        expect(rows.cleanupAction.disabledReason.find("unsafe") != std::string::npos,
               "Maintenance browser cleanup action exposes unsafe-reference disabled reason");
    }

    const auto package = makeTemporaryPackagePath("projectname-maintenance-browser-rows-test");
    const auto completedId = std::string("2026-06-19T00-10-00Z-completed");
    const auto restoredId = std::string("2026-06-19T00-20-00Z-restored");
    const auto conflictId = std::string("2026-06-19T00-30-00Z-conflict");
    const auto partialId = std::string("2026-06-19T00-40-00Z-partial");

    saveTestCleanupBatchManifest(package,
                                 completedId,
                                 "2026-06-19T00-10-00Z",
                                 projectname::PackageMediaQuarantineManifestState::completed);
    saveTestCleanupBatchManifest(package,
                                 restoredId,
                                 "2026-06-19T00-20-00Z",
                                 projectname::PackageMediaQuarantineManifestState::restored);
    saveTestCleanupBatchManifest(package,
                                 conflictId,
                                 "2026-06-19T00-30-00Z",
                                 projectname::PackageMediaQuarantineManifestState::restoreConflict);
    saveTestCleanupBatchManifest(package,
                                 partialId,
                                 "2026-06-19T00-40-00Z",
                                 projectname::PackageMediaQuarantineManifestState::partialFailure);

    projectname::PackageMediaMaintenanceViewModelRequest request;
    request.discovery = projectname::discoverPackageMediaCleanupBatches(package);
    request.selectedCleanupId = completedId;
    request.selectedRestoreOriginalRelativePaths.push_back("audio/orphan.wav");
    const auto oldestSelectedModel =
        projectname::buildPackageMediaMaintenanceViewModel(std::move(request));
    const auto oldestSelectedRows = projectname::buildPackageMediaMaintenanceBrowserRows(
        oldestSelectedModel,
        { true, false, 2 });

    const auto* completedRow = findMaintenanceBrowserBatchRow(oldestSelectedRows, completedId);
    expect(completedRow != nullptr && completedRow->selectable && completedRow->selected,
           "Maintenance browser rows keep an older selected batch visible and selectable");
    expect(completedRow != nullptr && completedRow->text.find("selected") != std::string::npos,
           "Maintenance browser rows render selected batch text");
    expect(completedRow != nullptr && completedRow->text.find("Batch 4") != std::string::npos,
           "Maintenance browser rows label visible selected batch with its full batch order");
    expect(countMaintenanceBrowserRows(
               oldestSelectedRows,
               projectname::PackageMediaMaintenanceBrowserRowKind::batch) == 2,
           "Maintenance browser rows cap visible selectable batch rows");
    expect(countSelectableMaintenanceBrowserRows(oldestSelectedRows) == 6,
           "Maintenance browser rows expose batch, selection command, and entry selectors");
    expect(oldestSelectedRows.selectedRowIndex >= 0,
           "Maintenance browser rows expose selected row index for UI focus painting");
    expect(oldestSelectedRows.focusedRowIndex >= 0
               && oldestSelectedRows.focusedSelectionId
                   == std::string(projectname::packageMediaMaintenanceBrowserSelectionIds::batchPrefix)
                       + completedId,
           "Maintenance browser rows default keyboard focus to the selected batch row");
    expect(oldestSelectedRows.restoreSelectAllKeyboardEnabled,
           "Maintenance browser rows enable keyboard select-all for restorable selected batch");
    expect(oldestSelectedRows.restoreClearSelectionKeyboardEnabled,
           "Maintenance browser rows enable keyboard clear when restore entries are selected");
    expect(!oldestSelectedRows.restoreToggleFocusedEntryKeyboardEnabled,
           "Maintenance browser rows do not toggle a focused batch as a restore entry");
    expect(oldestSelectedModel.batches[static_cast<std::size_t>(oldestSelectedModel.selectedBatchIndex)]
                   .manifestPath.filename() == "restore-manifest.json",
           "Maintenance view model carries restore manifest path for selected batch");
    expect(oldestSelectedRows.restoreAction.visible && oldestSelectedRows.restoreAction.enabled,
           "Maintenance browser restore action enables for selected restore entries");
    expect(oldestSelectedRows.restoreAction.disabledReason.empty(),
           "Maintenance browser restore action has no disabled reason when enabled");
    expect(findMaintenanceBrowserRow(
               oldestSelectedRows,
               projectname::PackageMediaMaintenanceBrowserRowKind::restoreSelectAll) != nullptr,
           "Maintenance browser rows expose restore select-all command");
    expect(findMaintenanceBrowserRow(
               oldestSelectedRows,
               projectname::PackageMediaMaintenanceBrowserRowKind::restoreClearSelection) != nullptr,
           "Maintenance browser rows expose restore clear-selection command");
    const auto* completedEntrySummary = findMaintenanceBrowserRow(
        oldestSelectedRows,
        projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntrySummary);
    expect(completedEntrySummary != nullptr
               && completedEntrySummary->text == "Entries: 2 moved / 0 restored / 2 restorable / 1 selected",
           "Maintenance browser rows show completed batch entry counts");
    const auto* completedReviewSummary = findMaintenanceBrowserRow(
        oldestSelectedRows,
        projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchReviewSummary);
    expect(completedReviewSummary != nullptr
               && completedReviewSummary->text == "Review: 0 conflicts / 0 errors",
           "Maintenance browser rows show completed batch review counts");
    expect(countMaintenanceBrowserRows(
               oldestSelectedRows,
               projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath) == 2,
           "Maintenance browser rows show package-relative paths for selected batch entries");
    expect(hasMaintenanceBrowserRowText(
               oldestSelectedRows,
               projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
               "audio/orphan.wav")
               && hasMaintenanceBrowserRowText(
                   oldestSelectedRows,
                   projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
                   "analysis/orphan.waveform.json"),
           "Maintenance browser rows include selected batch original paths");
    const auto* selectedAudioEntry = findMaintenanceBrowserRestoreEntryRow(oldestSelectedRows,
                                                                           "audio/orphan.wav");
    expect(selectedAudioEntry != nullptr
               && selectedAudioEntry->selectable
               && selectedAudioEntry->selected
               && selectedAudioEntry->selectionId
                   == std::string(projectname::packageMediaMaintenanceBrowserSelectionIds::restoreEntryPrefix)
                       + "audio/orphan.wav",
           "Maintenance browser rows expose selected audio restore-entry toggle id");
    expect(selectedAudioEntry != nullptr && selectedAudioEntry->detailActions.empty(),
           "Maintenance browser rows do not expose review detail actions for restorable entries");

    projectname::PackageMediaMaintenanceBrowserRowsOptions entryFocusOptions;
    entryFocusOptions.hasSnapshot = true;
    entryFocusOptions.focusedSelectionId =
        std::string(projectname::packageMediaMaintenanceBrowserSelectionIds::restoreEntryPrefix)
        + "analysis/orphan.waveform.json";
    const auto entryFocusedRows = projectname::buildPackageMediaMaintenanceBrowserRows(
        oldestSelectedModel,
        std::move(entryFocusOptions));
    const auto* focusedAnalysisEntry = findMaintenanceBrowserRestoreEntryRow(
        entryFocusedRows,
        "analysis/orphan.waveform.json");
    expect(focusedAnalysisEntry != nullptr
               && focusedAnalysisEntry->keyboardFocused
               && focusedAnalysisEntry->selectable
               && !focusedAnalysisEntry->selected,
           "Maintenance browser rows can keyboard-focus an unselected restore entry");
    expect(entryFocusedRows.restoreToggleFocusedEntryKeyboardEnabled
               && entryFocusedRows.focusedRestoreEntryOriginalRelativePath
                   == "analysis/orphan.waveform.json",
           "Maintenance browser rows enable keyboard toggle for focused restorable entry");
    expect(projectname::focusAdjacentPackageMediaMaintenanceBrowserSelectionId(
               entryFocusedRows,
               projectname::PackageMediaMaintenanceBrowserFocusDirection::previous)
               == std::string(projectname::packageMediaMaintenanceBrowserSelectionIds::restoreEntryPrefix)
                   + "audio/orphan.wav",
           "Maintenance browser row focus moves to previous selectable restore entry");
    expect(projectname::focusAdjacentPackageMediaMaintenanceBrowserSelectionId(
               entryFocusedRows,
               projectname::PackageMediaMaintenanceBrowserFocusDirection::next)
               == std::string(projectname::packageMediaMaintenanceBrowserSelectionIds::batchPrefix)
                   + partialId,
           "Maintenance browser row focus moves from restore entries to the next selectable row");

    const auto clearedSelectionModel =
        projectname::clearPackageMediaRestoreEntriesInSelectedBatch(oldestSelectedModel);
    const auto clearedSelectionRows = projectname::buildPackageMediaMaintenanceBrowserRows(
        clearedSelectionModel,
        { true, false, 2 });
    expect(clearedSelectionRows.restoreSelectAllKeyboardEnabled
               && !clearedSelectionRows.restoreClearSelectionKeyboardEnabled,
           "Maintenance browser rows keep select-all but disable clear for empty restore selection");

    const auto busyRestoreRows = projectname::buildPackageMediaMaintenanceBrowserRows(
        oldestSelectedModel,
        { true, false, 2, true });
    expect(busyRestoreRows.restoreAction.visible && !busyRestoreRows.restoreAction.enabled,
           "Maintenance browser restore action disables while package files are busy");
    expect(busyRestoreRows.restoreAction.disabledReason.find("busy") != std::string::npos,
           "Maintenance browser restore action exposes package-busy disabled reason");
    expect(!busyRestoreRows.restoreSelectAllKeyboardEnabled
               && !busyRestoreRows.restoreClearSelectionKeyboardEnabled
               && !busyRestoreRows.restoreToggleFocusedEntryKeyboardEnabled,
           "Maintenance browser rows disable restore-selection keyboard commands while package files are busy");

    auto staleSelectionModel =
        projectname::selectPackageMediaMaintenanceBatch(oldestSelectedModel, "missing-selection");
    expect(staleSelectionModel.hasSelectedBatch && staleSelectionModel.selectedCleanupId == partialId,
           "Maintenance view model selection helper falls back to newest valid batch");
    const auto staleSelectionRows = projectname::buildPackageMediaMaintenanceBrowserRows(
        staleSelectionModel,
        { true, false, 2 });
    const auto* partialRow = findMaintenanceBrowserBatchRow(staleSelectionRows, partialId);
    expect(partialRow != nullptr && partialRow->selected,
           "Maintenance browser rows render stale-selection fallback as selected");
    expect(staleSelectionRows.restoreAction.visible && !staleSelectionRows.restoreAction.enabled,
           "Maintenance browser restore action disables for partial-failure fallback");
    expect(staleSelectionRows.restoreAction.disabledReason.find("Review restore")
               != std::string::npos,
           "Maintenance browser restore action carries partial-failure disabled reason");
    const auto* partialEntrySummary = findMaintenanceBrowserRow(
        staleSelectionRows,
        projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntrySummary);
    expect(partialEntrySummary != nullptr
               && partialEntrySummary->text == "Entries: 2 moved / 0 restored / 1 restorable / 0 selected",
           "Maintenance browser rows show partial-failure entry counts");
    const auto* partialReviewSummary = findMaintenanceBrowserRow(
        staleSelectionRows,
        projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchReviewSummary);
    expect(partialReviewSummary != nullptr
               && partialReviewSummary->text == "Review: 0 conflicts / 1 errors",
           "Maintenance browser rows show partial-failure review counts");
    expect(hasMaintenanceBrowserRowText(
               staleSelectionRows,
               projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
               "partial restore failure"),
           "Maintenance browser rows mark partial-failure entry path state");
    const auto partialDetailSelectionId =
        std::string(projectname::packageMediaMaintenanceBrowserSelectionIds::restoreDetailPrefix)
        + "audio/orphan.wav";
    const auto partialManifestPath =
        (package / "backups" / "media-trash" / partialId / "restore-manifest.json").string();
    const auto* partialFailureEntry =
        findMaintenanceBrowserRestoreEntryRow(staleSelectionRows, "audio/orphan.wav");
    expect(partialFailureEntry != nullptr
               && partialFailureEntry->selectable
               && partialFailureEntry->selectionId == partialDetailSelectionId,
           "Maintenance browser rows make partial-failure entries focusable for detail actions");
    expect(partialFailureEntry != nullptr
               && hasMaintenanceBrowserDetailAction(
                   *partialFailureEntry,
                   projectname::PackageMediaMaintenanceDetailActionKind::copyPackageRelativePath,
                   "audio/orphan.wav")
               && hasMaintenanceBrowserDetailAction(
                   *partialFailureEntry,
                   projectname::PackageMediaMaintenanceDetailActionKind::revealRestoreManifest,
                   partialManifestPath),
           "Maintenance browser rows expose non-mutating partial-failure detail actions");
    projectname::PackageMediaMaintenanceBrowserRowsOptions partialDetailFocusOptions;
    partialDetailFocusOptions.hasSnapshot = true;
    partialDetailFocusOptions.focusedSelectionId = partialDetailSelectionId;
    const auto partialDetailFocusedRows = projectname::buildPackageMediaMaintenanceBrowserRows(
        staleSelectionModel,
        std::move(partialDetailFocusOptions));
    const auto* focusedPartialFailureEntry =
        findMaintenanceBrowserRestoreEntryRow(partialDetailFocusedRows, "audio/orphan.wav");
    expect(focusedPartialFailureEntry != nullptr
               && focusedPartialFailureEntry->keyboardFocused
               && !partialDetailFocusedRows.restoreToggleFocusedEntryKeyboardEnabled
               && hasMaintenanceBrowserFocusedDetailAction(
                   partialDetailFocusedRows,
                   projectname::PackageMediaMaintenanceDetailActionKind::copyPackageRelativePath,
                   "audio/orphan.wav"),
           "Maintenance browser rows keep partial-failure detail actions available from keyboard focus");
    const auto* partialCopyShortcutAction =
        projectname::selectPackageMediaMaintenanceFocusedDetailAction(
            partialDetailFocusedRows.focusedDetailActions,
            projectname::PackageMediaMaintenanceDetailActionIntent::copyShortcut);
    const auto* partialActivationAction =
        projectname::selectPackageMediaMaintenanceFocusedDetailAction(
            partialDetailFocusedRows.focusedDetailActions,
            projectname::PackageMediaMaintenanceDetailActionIntent::activation);
    expect(partialCopyShortcutAction != nullptr
               && partialCopyShortcutAction->kind
                   == projectname::PackageMediaMaintenanceDetailActionKind::copyPackageRelativePath
               && partialCopyShortcutAction->value == "audio/orphan.wav",
           "Maintenance browser maps focused partial-failure copy shortcut to package-relative path");
    expect(partialActivationAction != nullptr
               && partialActivationAction->kind
                   == projectname::PackageMediaMaintenanceDetailActionKind::revealRestoreManifest
               && partialActivationAction->value == partialManifestPath,
           "Maintenance browser maps focused partial-failure activation to restore manifest reveal");

    auto conflictSelectionModel =
        projectname::selectPackageMediaMaintenanceBatch(staleSelectionModel, conflictId);
    const auto conflictRows = projectname::buildPackageMediaMaintenanceBrowserRows(
        conflictSelectionModel,
        { true, false, 2 });
    expect(conflictRows.restoreAction.visible && !conflictRows.restoreAction.enabled,
           "Maintenance browser restore action disables for conflict batch");
    expect(conflictRows.restoreAction.disabledReason.find("conflicts") != std::string::npos,
           "Maintenance browser restore action carries conflict disabled reason");
    expect(!conflictRows.restoreSelectAllKeyboardEnabled
               && !conflictRows.restoreClearSelectionKeyboardEnabled
               && !conflictRows.restoreToggleFocusedEntryKeyboardEnabled,
           "Maintenance browser rows disable restore-selection keyboard commands for conflict batches");
    const auto* conflictReviewSummary = findMaintenanceBrowserRow(
        conflictRows,
        projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchReviewSummary);
    expect(conflictReviewSummary != nullptr
               && conflictReviewSummary->text == "Review: 1 conflicts / 0 errors",
           "Maintenance browser rows show conflict review counts");
    expect(hasMaintenanceBrowserRowText(
               conflictRows,
               projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
               "restore conflict"),
           "Maintenance browser rows mark conflict entry path state");
    const auto conflictDetailSelectionId =
        std::string(projectname::packageMediaMaintenanceBrowserSelectionIds::restoreDetailPrefix)
        + "audio/orphan.wav";
    const auto conflictManifestPath =
        (package / "backups" / "media-trash" / conflictId / "restore-manifest.json").string();
    const auto* conflictFailureEntry =
        findMaintenanceBrowserRestoreEntryRow(conflictRows, "audio/orphan.wav");
    expect(conflictFailureEntry != nullptr
               && conflictFailureEntry->selectable
               && conflictFailureEntry->selectionId == conflictDetailSelectionId,
           "Maintenance browser rows make conflict entries focusable for detail actions");
    expect(conflictFailureEntry != nullptr
               && hasMaintenanceBrowserDetailAction(
                   *conflictFailureEntry,
                   projectname::PackageMediaMaintenanceDetailActionKind::copyPackageRelativePath,
                   "audio/orphan.wav")
               && hasMaintenanceBrowserDetailAction(
                   *conflictFailureEntry,
                   projectname::PackageMediaMaintenanceDetailActionKind::revealRestoreManifest,
                   conflictManifestPath),
           "Maintenance browser rows expose non-mutating conflict detail actions");
    projectname::PackageMediaMaintenanceBrowserRowsOptions conflictDetailFocusOptions;
    conflictDetailFocusOptions.hasSnapshot = true;
    conflictDetailFocusOptions.focusedSelectionId = conflictDetailSelectionId;
    const auto conflictDetailFocusedRows = projectname::buildPackageMediaMaintenanceBrowserRows(
        conflictSelectionModel,
        std::move(conflictDetailFocusOptions));
    const auto* focusedConflictEntry =
        findMaintenanceBrowserRestoreEntryRow(conflictDetailFocusedRows, "audio/orphan.wav");
    expect(focusedConflictEntry != nullptr
               && focusedConflictEntry->keyboardFocused
               && !conflictDetailFocusedRows.restoreToggleFocusedEntryKeyboardEnabled
               && hasMaintenanceBrowserFocusedDetailAction(
                   conflictDetailFocusedRows,
                   projectname::PackageMediaMaintenanceDetailActionKind::copyPackageRelativePath,
                   "audio/orphan.wav"),
           "Maintenance browser rows keep conflict detail actions available from keyboard focus");
    const auto* conflictCopyShortcutAction =
        projectname::selectPackageMediaMaintenanceFocusedDetailAction(
            conflictDetailFocusedRows.focusedDetailActions,
            projectname::PackageMediaMaintenanceDetailActionIntent::copyShortcut);
    const auto* conflictActivationAction =
        projectname::selectPackageMediaMaintenanceFocusedDetailAction(
            conflictDetailFocusedRows.focusedDetailActions,
            projectname::PackageMediaMaintenanceDetailActionIntent::activation);
    expect(conflictCopyShortcutAction != nullptr
               && conflictCopyShortcutAction->kind
                   == projectname::PackageMediaMaintenanceDetailActionKind::copyPackageRelativePath
               && conflictCopyShortcutAction->value == "audio/orphan.wav",
           "Maintenance browser maps focused conflict copy shortcut to package-relative path");
    expect(conflictActivationAction != nullptr
               && conflictActivationAction->kind
                   == projectname::PackageMediaMaintenanceDetailActionKind::revealRestoreManifest
               && conflictActivationAction->value == conflictManifestPath,
           "Maintenance browser maps focused conflict activation to restore manifest reveal");

    const auto restoredSelectionModel =
        projectname::selectPackageMediaMaintenanceBatch(conflictSelectionModel, restoredId);
    const auto restoredRows = projectname::buildPackageMediaMaintenanceBrowserRows(
        restoredSelectionModel,
        { true, false, 2 });
    expect(restoredRows.restoreAction.visible && !restoredRows.restoreAction.enabled,
           "Maintenance browser restore action disables for already-restored batch");
    expect(restoredRows.restoreAction.disabledReason.find("no unrestored media")
               != std::string::npos,
           "Maintenance browser restore action carries restored-batch disabled reason");
    expect(!restoredRows.restoreSelectAllKeyboardEnabled
               && !restoredRows.restoreClearSelectionKeyboardEnabled
               && !restoredRows.restoreToggleFocusedEntryKeyboardEnabled,
           "Maintenance browser rows disable restore-selection keyboard commands for restored batches");
    const auto* restoredEntrySummary = findMaintenanceBrowserRow(
        restoredRows,
        projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntrySummary);
    expect(restoredEntrySummary != nullptr
               && restoredEntrySummary->text == "Entries: 2 moved / 2 restored / 0 restorable / 0 selected",
           "Maintenance browser rows show restored entry counts");
    expect(hasMaintenanceBrowserRowText(
               restoredRows,
               projectname::PackageMediaMaintenanceBrowserRowKind::selectedBatchEntryPath,
               "already been restored"),
           "Maintenance browser rows mark restored entry path state");

    expect(projectname::selectAdjacentPackageMediaCleanupId(
               conflictSelectionModel,
               projectname::PackageMediaMaintenanceBrowserSelectionDirection::previous) == partialId,
           "Maintenance browser keyboard selection moves to previous batch");
    expect(projectname::selectAdjacentPackageMediaCleanupId(
               conflictSelectionModel,
               projectname::PackageMediaMaintenanceBrowserSelectionDirection::next) == restoredId,
           "Maintenance browser keyboard selection moves to next batch");
    expect(projectname::selectAdjacentPackageMediaCleanupId(
               staleSelectionModel,
               projectname::PackageMediaMaintenanceBrowserSelectionDirection::previous) == partialId,
           "Maintenance browser keyboard selection clamps at newest batch");
    expect(projectname::selectAdjacentPackageMediaCleanupId(
               oldestSelectedModel,
               projectname::PackageMediaMaintenanceBrowserSelectionDirection::next) == completedId,
           "Maintenance browser keyboard selection clamps at oldest batch");
    expect(projectname::focusAdjacentPackageMediaMaintenanceBrowserSelectionId(
               oldestSelectedRows,
               projectname::PackageMediaMaintenanceBrowserFocusDirection::previous)
               == std::string(projectname::packageMediaMaintenanceBrowserSelectionIds::batchPrefix)
                   + partialId,
           "Maintenance browser row focus can move independently from batch keyboard selection");
    expect(projectname::focusAdjacentPackageMediaMaintenanceBrowserSelectionId(
               oldestSelectedRows,
               projectname::PackageMediaMaintenanceBrowserFocusDirection::next)
               == std::string(projectname::packageMediaMaintenanceBrowserSelectionIds::batchPrefix)
                   + completedId,
           "Maintenance browser row focus clamps at the last selectable row");

    expect(std::filesystem::remove_all(package) > 0,
           "Temporary maintenance browser rows package deleted");
}

void importedClipInspectorReportsSelectedOrFirstImportedClipMetadata()
{
    {
        const auto emptyPackage = makeTemporaryPackagePath("projectname-empty-inspector-test");
        auto empty = projectname::buildFirstImportedAudioClipInspector(projectname::ProjectModel::createDefault(),
                                                                       emptyPackage,
                                                                       44100.0);
        expect(empty.status == projectname::ImportedClipInspectorStatus::noImportedAudio,
               "Imported clip inspector reports no imported audio");
        expect(!empty.message.empty(), "Imported clip inspector explains empty state");
    }

    auto project = projectname::ProjectModel::createDefault();
    project.setName("Imported Clip Inspector Test");
    project.getTransport().setTempoBpm(120.0);

    const auto package = makeTemporaryPackagePath("projectname-imported-inspector-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-imported-inspector-source-test");
    const auto secondWavPath = makeTemporaryAudioPath("projectname-imported-inspector-second-source-test");
    writePcm16Wav(wavPath, 48000, 1, { 32767, 0, -32768, 16384 });
    writePcm16Wav(secondWavPath, 44100, 1, { 1000, -1000 });

    std::string error;
    auto result = projectname::importPcm16WavIntoProjectPackage(project, package, wavPath, error);
    expect(result.has_value(), "Imported clip inspector test imports PCM16 WAV");

    projectname::ProjectAudioImportOptions secondImportOptions;
    secondImportOptions.requestedStartBeats = 8.0;
    auto secondResult =
        projectname::importPcm16WavIntoProjectPackage(project, package, secondWavPath, secondImportOptions, error);
    expect(secondResult.has_value(),
           "Imported clip inspector test imports second PCM16 WAV: " + error);

    auto inspector = projectname::buildFirstImportedAudioClipInspector(project, package, 44100.0);
    expect(inspector.status == projectname::ImportedClipInspectorStatus::ready,
           "Imported clip inspector loads waveform metadata");
    expect(result.has_value() && inspector.clipId == result->clip.id,
           "Imported clip inspector reports first clip identity without selection");
    expect(!inspector.usingSelectedClip,
           "Imported clip inspector marks unselected first-clip fallback");
    expect(result.has_value() && inspector.relativePath == result->clip.relativePath,
           "Imported clip inspector reports package-relative audio path");
    expect(std::abs(inspector.durationSeconds - (4.0 / 48000.0)) < 0.000001,
           "Imported clip inspector computes source duration in seconds");
    expect(inspector.sourceSampleRateHz == 48000.0 && inspector.sourceFrameCount == 4,
           "Imported clip inspector reports source rate and frames");
    expect(inspector.sampleRateMismatch && !inspector.warning.empty(),
           "Imported clip inspector warns about output sample-rate mismatch");

    auto matchingRateInspector = projectname::buildFirstImportedAudioClipInspector(project, package, 48000.0);
    expect(matchingRateInspector.status == projectname::ImportedClipInspectorStatus::ready,
           "Imported clip inspector remains ready for matching output rate");
    expect(!matchingRateInspector.sampleRateMismatch && matchingRateInspector.warning.empty(),
           "Imported clip inspector clears warning when output rate matches first clip");

    if (secondResult.has_value())
    {
        expect(project.selectImportedAudioClip(secondResult->clip.id, error),
               "Imported clip inspector test selects second imported clip");
        auto selectedInspector = projectname::buildFirstImportedAudioClipInspector(project, package, 44100.0);
        expect(selectedInspector.status == projectname::ImportedClipInspectorStatus::ready,
               "Imported clip inspector loads selected clip metadata");
        expect(selectedInspector.usingSelectedClip,
               "Imported clip inspector marks selected clip usage");
        expect(selectedInspector.clipId == secondResult->clip.id,
               "Imported clip inspector prefers selected imported clip");
        expect(selectedInspector.sourceSampleRateHz == 44100.0 && selectedInspector.sourceFrameCount == 2,
               "Imported clip inspector reports selected clip source metadata");
        expect(!selectedInspector.sampleRateMismatch,
               "Imported clip inspector avoids warning for selected matching-rate clip");
    }

    if (result.has_value())
    {
        project.clearSelectedClip();
        expect(std::filesystem::remove(result->waveformSummaryPath),
               "Temporary inspector waveform summary deleted");
        auto missingAnalysisInspector =
            projectname::buildFirstImportedAudioClipInspector(project, package, 44100.0);
        expect(missingAnalysisInspector.status == projectname::ImportedClipInspectorStatus::missingAnalysis,
               "Imported clip inspector reports missing waveform analysis");
        expect(missingAnalysisInspector.sourceSampleRateHz == 0.0,
               "Imported clip inspector leaves source rate empty when analysis is missing");
    }

    {
        const auto stalePackage = makeTemporaryPackagePath("projectname-imported-inspector-stale-selection-test");
        projectname::WaveformSummary summary;
        summary.sampleRateHz = 32000.0;
        summary.frameCount = 8;
        summary.sourceFramesPerBucket = 8;
        summary.buckets.push_back({ 0.5f, 0.25f });
        expect(projectname::saveWaveformSummary(summary,
                                                stalePackage / "analysis" / "fallback.waveform.json",
                                                error),
               "Imported clip inspector stale test writes waveform summary");

        writeManifestText(stalePackage, R"({
  "manifestVersion": 1,
  "name": "Stale Selection Inspector",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "positionBeats": 0
  },
  "selection": { "clipId": "missing-selected-clip" },
  "tracks": [
    {
      "id": "track-stale-selection",
      "name": "Stale Selection",
      "type": "audio",
      "clips": [
        {
          "id": "clip-fallback-selection",
          "name": "Fallback Selection",
          "type": "audio-file",
          "relativePath": "audio/fallback.wav",
          "analysisPath": "analysis/fallback.waveform.json",
          "startBeats": 0,
          "lengthBeats": 1
        }
      ]
    }
  ]
})");

        auto staleProject = projectname::ProjectModel::loadPackage(stalePackage, error);
        expect(staleProject.has_value()
                   && staleProject->getSelectedClipId() == "missing-selected-clip",
               "Imported clip inspector stale test loads stale selected id");
        auto staleInspector = staleProject.has_value()
            ? projectname::buildFirstImportedAudioClipInspector(*staleProject, stalePackage, 32000.0)
            : projectname::ImportedClipInspectorState {};
        expect(staleInspector.status == projectname::ImportedClipInspectorStatus::ready,
               "Imported clip inspector falls back when selected clip is stale");
        expect(!staleInspector.usingSelectedClip
                   && staleInspector.selectedClipId == "missing-selected-clip"
                   && staleInspector.clipId == "clip-fallback-selection",
               "Imported clip inspector preserves stale selected id while showing fallback clip");

        expect(std::filesystem::remove_all(stalePackage) > 0,
               "Temporary stale selection inspector package deleted");
    }

    expect(std::filesystem::remove(wavPath), "Temporary inspector WAV deleted");
    expect(std::filesystem::remove(secondWavPath), "Temporary second inspector WAV deleted");
    expect(std::filesystem::remove_all(package) > 0, "Temporary inspector package deleted");
}

void importedClipInspectorEditDraftValidatesStartBeatCommitAndCancel()
{
    constexpr auto clipId = "clip-imported-playback";

    auto fallbackDraft =
        projectname::ImportedClipInspectorEditDraft::fromInspectorState(
            makeImportedClipInspectorEditState(false));
    expect(fallbackDraft.getAvailability()
               == projectname::ImportedClipInspectorEditAvailability::readOnlyFallback,
           "Imported clip edit draft marks fallback inspector as read-only");
    expect(!fallbackDraft.canEdit(),
           "Imported clip edit draft does not edit unselected fallback metadata");

    fallbackDraft.setStartBeatText("8.0");
    std::string error;
    expect(!fallbackDraft.makeStartBeatCommit(error).has_value(),
           "Imported clip edit draft rejects fallback start-beat commits");
    expect(!error.empty(), "Fallback start-beat rejection reports a recoverable error");

    auto draft =
        projectname::ImportedClipInspectorEditDraft::fromInspectorState(
            makeImportedClipInspectorEditState(true));
    expect(draft.canEdit(), "Imported clip edit draft allows selected clip editing");
    expect(draft.getStartBeatText() == "4",
           "Imported clip edit draft formats committed start beat for text editing");

    draft.setStartBeatText("not-a-beat");
    auto validation = draft.validateStartBeat();
    expect(validation.code == projectname::ImportedClipInspectorEditValidationCode::invalidStartBeat,
           "Imported clip edit draft rejects non-numeric start beat text");
    expect(!draft.makeStartBeatCommit(error).has_value(),
           "Imported clip edit draft does not emit commit for non-numeric start beat");

    draft.setStartBeatText("-0.25");
    validation = draft.validateStartBeat();
    expect(validation.code == projectname::ImportedClipInspectorEditValidationCode::invalidStartBeat,
           "Imported clip edit draft rejects negative start beat text");

    draft.setStartBeatText(" 8.25 ");
    auto commit = draft.makeStartBeatCommit(error);
    expect(commit.has_value() && commit->clipId == clipId,
           "Imported clip edit draft emits selected clip id for valid start commit");
    expect(commit.has_value() && std::abs(commit->startBeats - 8.25) < 0.0001,
           "Imported clip edit draft parses valid start beat commit");
    expect(error.empty(), "Valid start-beat commit leaves error empty");

    projectname::AppSession session(makeProjectWithImportedTimelineClip(4.0, 2.0));
    expect(commit.has_value()
               && session.setImportedAudioClipStartBeats(commit->clipId, commit->startBeats, error),
           "Imported clip edit draft start commit applies through app session");
    const auto* movedClip = findClipById(session.getProject(), clipId);
    expect(movedClip != nullptr && std::abs(movedClip->startBeats - 8.25) < 0.0001,
           "Applied start-beat draft commit updates selected imported clip");
    expect(session.canUndoImportedClipPlacementEdit(),
           "Applied start-beat draft commit records one placement undo entry");

    projectname::AppSession cancelSession(makeProjectWithImportedTimelineClip(4.0, 2.0));
    auto cancelDraft =
        projectname::ImportedClipInspectorEditDraft::fromInspectorState(
            makeImportedClipInspectorEditState(true));
    cancelDraft.setStartBeatText("12.0");
    cancelDraft.cancelStartBeatEdit();
    expect(cancelDraft.getStartBeatText() == "4",
           "Imported clip edit draft cancel restores committed start-beat text");
    auto cancelledCommit = cancelDraft.makeStartBeatCommit(error);
    expect(cancelledCommit.has_value()
               && std::abs(cancelledCommit->startBeats - 4.0) < 0.0001,
           "Cancelled start-beat draft resolves to original committed value");
    expect(cancelledCommit.has_value()
               && cancelSession.setImportedAudioClipStartBeats(cancelledCommit->clipId,
                                                               cancelledCommit->startBeats,
                                                               error),
           "Cancelled start-beat no-op remains acceptable to the session");
    expect(!cancelSession.canUndoImportedClipPlacementEdit(),
           "Cancelled start-beat edit does not record undo history");
}

void importedClipInspectorEditDraftValidatesMediaRelinkMetadata()
{
    constexpr auto clipId = "clip-imported-playback";

    auto fallbackDraft =
        projectname::ImportedClipInspectorEditDraft::fromInspectorState(
            makeImportedClipInspectorEditState(false));
    fallbackDraft.setMediaRelinkDraft("audio/rejected.wav",
                                      "analysis/rejected.waveform.json",
                                      1.0);
    std::string error;
    expect(!fallbackDraft.makeMediaRelinkCommit(error).has_value(),
           "Imported clip edit draft rejects fallback media relink commits");

    auto draft =
        projectname::ImportedClipInspectorEditDraft::fromInspectorState(
            makeImportedClipInspectorEditState(true));

    draft.setMediaRelinkDraft("", "analysis/relinked.waveform.json", 3.0);
    auto validation = draft.validateMediaRelink();
    expect(validation.code == projectname::ImportedClipInspectorEditValidationCode::missingMediaPath,
           "Imported clip edit draft rejects empty media path");
    expect(!draft.makeMediaRelinkCommit(error).has_value(),
           "Imported clip edit draft does not emit relink commit for missing media path");

    draft.setMediaRelinkDraft("../outside.wav", "analysis/relinked.waveform.json", 3.0);
    validation = draft.validateMediaRelink();
    expect(validation.code == projectname::ImportedClipInspectorEditValidationCode::unsafeMediaPath,
           "Imported clip edit draft rejects media paths outside the project package");

    draft.setMediaRelinkDraft("audio/relinked.wav", "../analysis/relinked.waveform.json", 3.0);
    validation = draft.validateMediaRelink();
    expect(validation.code == projectname::ImportedClipInspectorEditValidationCode::unsafeAnalysisPath,
           "Imported clip edit draft rejects unsafe analysis paths");

    draft.setMediaRelinkDraft("audio/relinked.wav",
                              "analysis/relinked.waveform.json",
                              std::numeric_limits<double>::quiet_NaN());
    validation = draft.validateMediaRelink();
    expect(validation.code == projectname::ImportedClipInspectorEditValidationCode::invalidLengthBeats,
           "Imported clip edit draft rejects non-finite relink length");

    draft.setMediaRelinkDraft("audio/relinked.wav", "", 3.25);
    auto commit = draft.makeMediaRelinkCommit(error);
    expect(commit.has_value() && commit->clipId == clipId,
           "Imported clip edit draft emits selected clip id for valid relink commit");
    expect(commit.has_value() && commit->relativePath == "audio/relinked.wav",
           "Imported clip edit draft preserves validated relink media path");
    expect(commit.has_value() && commit->analysisPath.empty(),
           "Imported clip edit draft permits empty analysis path for not-yet-generated analysis");
    expect(commit.has_value() && std::abs(commit->lengthBeats - 3.25) < 0.0001,
           "Imported clip edit draft preserves validated relink length");

    projectname::AppSession session(makeProjectWithImportedTimelineClip(4.0, 2.0));
    expect(commit.has_value()
               && session.replaceImportedAudioClipMedia(commit->clipId,
                                                        commit->relativePath,
                                                        commit->analysisPath,
                                                        commit->lengthBeats,
                                                        error),
           "Imported clip edit draft media relink commit applies through app session");
    const auto* relinkedClip = findClipById(session.getProject(), clipId);
    expect(relinkedClip != nullptr && relinkedClip->relativePath == "audio/relinked.wav",
           "Applied media relink draft commit updates imported clip media path");
    expect(relinkedClip != nullptr && relinkedClip->analysisPath.empty(),
           "Applied media relink draft commit stores empty pending analysis path");
    expect(relinkedClip != nullptr && std::abs(relinkedClip->lengthBeats - 3.25) < 0.0001,
           "Applied media relink draft commit updates imported clip length");
    expect(session.canUndoImportedClipMediaReplacementEdit(),
           "Applied media relink draft commit records one media replacement undo entry");

    projectname::AppSession cancelSession(makeProjectWithImportedTimelineClip(4.0, 2.0));
    auto cancelDraft =
        projectname::ImportedClipInspectorEditDraft::fromInspectorState(
            makeImportedClipInspectorEditState(true));
    cancelDraft.setMediaRelinkDraft("audio/cancelled.wav",
                                    "analysis/cancelled.waveform.json",
                                    6.0);
    cancelDraft.cancelMediaRelinkEdit();
    expect(cancelDraft.getMediaRelativePath() == "audio/timeline-clip.wav"
               && cancelDraft.getAnalysisPath() == "analysis/timeline-clip.waveform.json"
               && std::abs(cancelDraft.getLengthBeats() - 2.0) < 0.0001,
           "Imported clip edit draft cancel restores committed media relink metadata");
    auto cancelledCommit = cancelDraft.makeMediaRelinkCommit(error);
    expect(cancelledCommit.has_value()
               && cancelSession.replaceImportedAudioClipMedia(cancelledCommit->clipId,
                                                              cancelledCommit->relativePath,
                                                              cancelledCommit->analysisPath,
                                                              cancelledCommit->lengthBeats,
                                                              error),
           "Cancelled media relink no-op remains acceptable to the session");
    expect(!cancelSession.canUndoImportedClipMediaReplacementEdit(),
           "Cancelled media relink edit does not record undo history");
}

void importedClipMediaRelinkPreparationStagesValidatedMetadata()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    std::string error;
    expect(project.selectImportedAudioClip(clipId, error),
           "Media relink preparation test selects imported clip");

    const auto package = makeTemporaryPackagePath("projectname-media-relink-prep-test");
    const auto sourceWav = makeTemporaryAudioPath("projectname-media-relink-source-test");
    writePcm16Wav(sourceWav, 10, 1, { 1000, 2000, 3000, 4000, 5000 });

    projectname::ImportedClipMediaRelinkPreparationRequest request;
    request.project = project;
    request.packageDirectory = package;
    request.sourceWavPath = sourceWav;
    request.selectedClipId = clipId;

    auto result = projectname::prepareImportedClipMediaRelink(std::move(request));
    expect(result.status == projectname::ImportedClipMediaRelinkPreparationStatus::prepared,
           "Media relink preparation stages valid PCM16 WAV source");
    expect(result.preparation.has_value(),
           "Media relink preparation returns prepared metadata");

    if (result.preparation.has_value())
    {
        const auto& preparation = *result.preparation;
        expect(preparation.clipId == clipId,
               "Media relink preparation preserves selected clip id");
        expect(preparation.relativePath.rfind("audio/", 0) == 0,
               "Media relink preparation returns package-relative audio path");
        expect(preparation.analysisPath.rfind("analysis/", 0) == 0,
               "Media relink preparation returns package-relative analysis path");
        expect(std::abs(preparation.lengthBeats - 1.0) < 0.0001,
               "Media relink preparation calculates length from source frames and tempo");
        expect(std::filesystem::is_regular_file(preparation.stagedAudioPath),
               "Media relink preparation stages audio copy");
        expect(std::filesystem::is_regular_file(preparation.stagedAnalysisPath),
               "Media relink preparation stages waveform summary");
        expect(!std::filesystem::exists(preparation.finalAudioPath),
               "Media relink preparation does not commit final audio before session commit");
        expect(!std::filesystem::exists(preparation.finalAnalysisPath),
               "Media relink preparation does not commit final analysis before session commit");

        auto draft =
            projectname::ImportedClipInspectorEditDraft::fromInspectorState(
                makeImportedClipInspectorEditState(true));
        draft.setMediaRelinkDraft(preparation.relativePath,
                                  preparation.analysisPath,
                                  preparation.lengthBeats);
        expect(draft.makeMediaRelinkCommit(error).has_value(),
               "Media relink preparation metadata validates through inspector edit draft");

        projectname::AppSession session(project);
        auto commit = projectname::commitPreparedImportedClipMediaRelink(session, preparation);
        expect(commit.status == projectname::ImportedClipMediaRelinkCommitStatus::committed,
               "Media relink preparation commits staged files through app session");
        expect(commit.commit.has_value() && commit.commit->relativePath == preparation.relativePath,
               "Media relink preparation commit returns draft-compatible relative path");
        expect(std::filesystem::is_regular_file(preparation.finalAudioPath),
               "Media relink preparation commit moves audio into final package path");
        expect(std::filesystem::is_regular_file(preparation.finalAnalysisPath),
               "Media relink preparation commit moves waveform summary into final package path");
        expect(!std::filesystem::exists(preparation.stagingDirectory),
               "Media relink preparation commit removes staging directory");

        const auto* relinkedClip = findClipById(session.getProject(), clipId);
        expect(relinkedClip != nullptr && relinkedClip->relativePath == preparation.relativePath,
               "Media relink preparation commit updates imported clip media path");
        expect(relinkedClip != nullptr && relinkedClip->analysisPath == preparation.analysisPath,
               "Media relink preparation commit updates imported clip analysis path");
        expect(relinkedClip != nullptr && std::abs(relinkedClip->lengthBeats - 1.0) < 0.0001,
               "Media relink preparation commit updates imported clip length");
        expect(relinkedClip != nullptr && std::abs(relinkedClip->startBeats - 4.0) < 0.0001,
               "Media relink preparation commit preserves imported clip start beat");
        expect(session.canUndoImportedClipMediaReplacementEdit(),
               "Media relink preparation commit records media replacement undo history");
    }

    expect(std::filesystem::remove(sourceWav), "Temporary media relink source WAV deleted");
    expect(std::filesystem::remove_all(package) > 0, "Temporary media relink package deleted");
}

void importedClipMediaRelinkPreparationRejectsInvalidSourceWithoutStaging()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    std::string error;
    expect(project.selectImportedAudioClip(clipId, error),
           "Invalid media relink preparation test selects imported clip");

    const auto package = makeTemporaryPackagePath("projectname-media-relink-invalid-test");
    const auto invalidWav = makeTemporaryAudioPath("projectname-media-relink-invalid-source-test");
    {
        std::ofstream file(invalidWav, std::ios::binary | std::ios::trunc);
        file << "not a wav";
    }

    projectname::ImportedClipMediaRelinkPreparationRequest request;
    request.project = project;
    request.packageDirectory = package;
    request.sourceWavPath = invalidWav;
    request.selectedClipId = clipId;

    auto result = projectname::prepareImportedClipMediaRelink(std::move(request));
    expect(result.status == projectname::ImportedClipMediaRelinkPreparationStatus::decodeFailed,
           "Media relink preparation rejects invalid WAV source");
    expect(!result.preparation.has_value(),
           "Media relink invalid source does not return preparation metadata");
    expect(!std::filesystem::exists(package / ".projectname-staging"),
           "Media relink invalid source does not create staging directory");
    expect(!result.error.empty(), "Media relink invalid source reports recoverable error");

    expect(std::filesystem::remove(invalidWav), "Temporary invalid media relink WAV deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary invalid media relink package deleted");
}

void importedClipMediaRelinkPreparationCancelsAndCleansStaging()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    std::string error;
    expect(project.selectImportedAudioClip(clipId, error),
           "Cancelled media relink preparation test selects imported clip");

    const auto package = makeTemporaryPackagePath("projectname-media-relink-cancel-test");
    const auto sourceWav = makeTemporaryAudioPath("projectname-media-relink-cancel-source-test");
    writePcm16Wav(sourceWav, 44100, 1, { 1000, 2000, 3000, 4000, 5000, 6000 });

    std::atomic_bool cancelRequested { false };
    auto sawCopyProgress = false;

    projectname::ImportedClipMediaRelinkPreparationRequest request;
    request.project = project;
    request.packageDirectory = package;
    request.sourceWavPath = sourceWav;
    request.selectedClipId = clipId;
    request.cancelRequested = &cancelRequested;
    request.copyChunkBytes = 1;
    request.progressCallback =
        [&cancelRequested, &sawCopyProgress](const projectname::ImportedClipMediaRelinkProgress& progress)
        {
            if (progress.stage == projectname::ImportedClipMediaRelinkPreparationStage::copying
                && progress.bytesCopied > 0)
            {
                sawCopyProgress = true;
                cancelRequested.store(true, std::memory_order_release);
            }
        };

    auto result = projectname::prepareImportedClipMediaRelink(std::move(request));
    expect(result.status == projectname::ImportedClipMediaRelinkPreparationStatus::cancelled,
           "Media relink preparation reports cancellation");
    expect(sawCopyProgress, "Media relink preparation cancellation test reaches staged copy progress");
    expect(!result.preparation.has_value(),
           "Cancelled media relink preparation does not return prepared metadata");
    expect(!std::filesystem::exists(package / ".projectname-staging"),
           "Cancelled media relink preparation cleans staging directory");

    expect(std::filesystem::remove(sourceWav), "Temporary cancelled media relink WAV deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary cancelled media relink package deleted");
}

void importedClipMediaRelinkCommitCleansStaleSelection()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    std::string error;
    expect(project.selectImportedAudioClip(clipId, error),
           "Stale media relink preparation test selects imported clip");

    const auto package = makeTemporaryPackagePath("projectname-media-relink-stale-test");
    const auto sourceWav = makeTemporaryAudioPath("projectname-media-relink-stale-source-test");
    writePcm16Wav(sourceWav, 10, 1, { 1000, 2000, 3000, 4000 });

    projectname::ImportedClipMediaRelinkPreparationRequest request;
    request.project = project;
    request.packageDirectory = package;
    request.sourceWavPath = sourceWav;
    request.selectedClipId = clipId;

    auto result = projectname::prepareImportedClipMediaRelink(std::move(request));
    expect(result.status == projectname::ImportedClipMediaRelinkPreparationStatus::prepared
               && result.preparation.has_value(),
           "Stale media relink test prepares staged relink");

    if (result.preparation.has_value())
    {
        const auto& preparation = *result.preparation;
        expect(std::filesystem::is_regular_file(preparation.stagedAudioPath),
               "Stale media relink test starts with staged audio");

        projectname::AppSession session(project);
        session.clearSelectedClip();
        auto commit = projectname::commitPreparedImportedClipMediaRelink(session, preparation);
        expect(commit.status == projectname::ImportedClipMediaRelinkCommitStatus::staleSelection,
               "Media relink commit rejects stale selected clip");
        expect(!std::filesystem::exists(preparation.stagingDirectory),
               "Media relink stale selection cleanup removes staging directory");
        expect(!std::filesystem::exists(preparation.finalAudioPath),
               "Media relink stale selection cleanup does not create final audio");
        expect(!std::filesystem::exists(preparation.finalAnalysisPath),
               "Media relink stale selection cleanup does not create final analysis");

        const auto* clip = findClipById(session.getProject(), clipId);
        expect(clip != nullptr && clip->relativePath == "audio/timeline-clip.wav",
               "Media relink stale selection leaves clip media path unchanged");
        expect(!session.canUndoImportedClipMediaReplacementEdit(),
               "Media relink stale selection does not record undo history");
    }

    expect(std::filesystem::remove(sourceWav), "Temporary stale media relink WAV deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary stale media relink package deleted");
}

void backgroundMediaRelinkPreparationJobPreparesSelectedClip()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    std::string error;
    expect(project.selectImportedAudioClip(clipId, error),
           "Background media relink test selects imported clip");

    const auto package = makeTemporaryPackagePath("projectname-bg-media-relink-test");
    const auto sourceWav = makeTemporaryAudioPath("projectname-bg-media-relink-source-test");
    writePcm16Wav(sourceWav, 10, 1, { 1000, 2000, 3000, 4000, 5000 });

    projectname::BackgroundMediaRelinkPreparationRequest request;
    request.project = project;
    request.packageDirectory = package;
    request.sourceWavPath = sourceWav;
    request.selectedClipId = clipId;

    projectname::BackgroundMediaRelinkPreparationJob job(std::move(request));
    expect(!job.hasStarted(), "Background media relink job starts idle");
    job.start();
    expect(job.hasStarted(), "Background media relink job reports started");

    auto result = job.waitForResult();
    auto progress = job.getProgress();
    expect(result.status == projectname::ImportedClipMediaRelinkPreparationStatus::prepared,
           "Background media relink job prepares selected clip");
    expect(result.preparation.has_value(),
           "Background media relink job returns preparation metadata");
    expect(progress.phase == projectname::BackgroundMediaRelinkPreparationPhase::completed,
           "Background media relink job reports completed phase");
    expect(progress.percent == 100,
           "Background media relink job reports complete percent");
    expect(progress.framesTotal == 5 && progress.framesProcessed == 5,
           "Background media relink job reports decode frame progress");
    expect(progress.bytesTotal > 0 && progress.bytesProcessed == progress.bytesTotal,
           "Background media relink job reports staged copy byte progress");

    if (result.preparation.has_value())
    {
        expect(std::filesystem::is_regular_file(result.preparation->stagedAudioPath),
               "Background media relink job leaves prepared staged audio for commit");
        projectname::discardPreparedImportedClipMediaRelink(*result.preparation);
    }

    expect(std::filesystem::remove(sourceWav), "Temporary background media relink source WAV deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary background media relink package deleted");
}

void backgroundMediaRelinkPreparationJobReportsInvalidSource()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    std::string error;
    expect(project.selectImportedAudioClip(clipId, error),
           "Invalid background media relink test selects imported clip");

    const auto package = makeTemporaryPackagePath("projectname-bg-media-relink-invalid-test");
    const auto invalidWav = makeTemporaryAudioPath("projectname-bg-media-relink-invalid-source-test");
    {
        std::ofstream file(invalidWav, std::ios::binary | std::ios::trunc);
        file << "not a wav";
    }

    projectname::BackgroundMediaRelinkPreparationRequest request;
    request.project = project;
    request.packageDirectory = package;
    request.sourceWavPath = invalidWav;
    request.selectedClipId = clipId;

    projectname::BackgroundMediaRelinkPreparationJob job(std::move(request));
    job.start();

    auto result = job.waitForResult();
    const auto progress = job.getProgress();
    expect(result.status == projectname::ImportedClipMediaRelinkPreparationStatus::decodeFailed,
           "Background media relink job reports invalid WAV decode failure");
    expect(!result.cancelled, "Invalid background media relink job is not cancelled");
    expect(!result.preparation.has_value(),
           "Invalid background media relink job does not return preparation");
    expect(!result.error.empty(),
           "Invalid background media relink job returns recoverable error text");
    expect(progress.phase == projectname::BackgroundMediaRelinkPreparationPhase::failed,
           "Invalid background media relink job reports failed phase");
    expect(!std::filesystem::exists(package / ".projectname-staging"),
           "Invalid background media relink job does not leave staging directory");

    expect(std::filesystem::remove(invalidWav), "Temporary invalid background media relink WAV deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0,
               "Temporary invalid background media relink package deleted");
}

void backgroundMediaRelinkPreparationJobCancelsBeforeStart()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    std::string error;
    expect(project.selectImportedAudioClip(clipId, error),
           "Cancelled background media relink test selects imported clip");

    const auto package = makeTemporaryPackagePath("projectname-bg-media-relink-cancel-test");
    const auto sourceWav = makeTemporaryAudioPath("projectname-bg-media-relink-cancel-source-test");
    writePcm16Wav(sourceWav, 10, 1, { 1000, 2000, 3000, 4000, 5000 });

    projectname::BackgroundMediaRelinkPreparationRequest request;
    request.project = project;
    request.packageDirectory = package;
    request.sourceWavPath = sourceWav;
    request.selectedClipId = clipId;

    projectname::BackgroundMediaRelinkPreparationJob job(std::move(request));
    job.requestCancel();

    auto result = job.waitForResult();
    const auto progress = job.getProgress();
    expect(result.cancelled, "Background media relink job reports cancellation");
    expect(result.status == projectname::ImportedClipMediaRelinkPreparationStatus::cancelled,
           "Background media relink cancellation keeps cancelled preparation status");
    expect(!result.preparation.has_value(),
           "Cancelled background media relink job returns no preparation");
    expect(progress.phase == projectname::BackgroundMediaRelinkPreparationPhase::cancelled,
           "Cancelled background media relink job reports cancelled phase");
    expect(!std::filesystem::exists(package / ".projectname-staging"),
           "Cancelled background media relink job leaves no staging directory");

    expect(std::filesystem::remove(sourceWav), "Temporary cancelled background media relink WAV deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0,
               "Temporary cancelled background media relink package deleted");
}

void waveformThumbnailLoaderReportsInvalidAnalysis()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto package = makeTemporaryPackagePath("projectname-waveform-invalid-analysis-test");
    std::filesystem::create_directories(package / "analysis");

    projectname::ProjectClip clip;
    clip.id = "clip-invalid-analysis";
    clip.name = "Invalid Analysis Clip";
    clip.type = "audio-file";
    clip.relativePath = "audio/source.wav";
    clip.analysisPath = "analysis/bad.waveform.json";
    clip.startBeats = 0.0;
    clip.lengthBeats = 1.0;
    expect(project.addClipToTrack("track-1", clip), "Invalid analysis test clip attaches");

    {
        std::ofstream file(package / clip.analysisPath, std::ios::trunc);
        file << "{ invalid";
    }

    auto thumbnail = projectname::loadFirstImportedAudioWaveform(project, package);
    expect(thumbnail.state == projectname::WaveformThumbnailState::invalidAnalysis,
           "Waveform thumbnail loader reports invalid analysis");
    expect(!thumbnail.error.empty(), "Waveform thumbnail loader reports invalid analysis error");

    clip.analysisPath = "../outside.waveform.json";
    auto unsafeProject = projectname::ProjectModel::createDefault();
    expect(unsafeProject.addClipToTrack("track-1", clip), "Unsafe analysis test clip attaches");
    auto unsafeThumbnail = projectname::loadFirstImportedAudioWaveform(unsafeProject, package);
    expect(unsafeThumbnail.state == projectname::WaveformThumbnailState::invalidAnalysis,
           "Waveform thumbnail loader rejects unsafe package-relative path");

    expect(std::filesystem::remove_all(package) > 0, "Temporary invalid analysis package deleted");
}

void projectModelPlacesImportedAudioClips()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip imported;
    imported.id = "clip-imported-placement";
    imported.name = "Placement Clip";
    imported.type = "audio-file";
    imported.relativePath = "audio/placement.wav";
    imported.analysisPath = "analysis/placement.waveform.json";
    imported.startBeats = 1.0;
    imported.lengthBeats = 2.0;
    expect(project.addClipToTrack("track-1", imported), "Placement test imported clip attaches");

    std::string error;
    expect(project.setImportedAudioClipStartBeats("clip-imported-placement", 12.5, error),
           "Project places imported audio clip");
    auto* placed = findClipById(project, "clip-imported-placement");
    expect(placed != nullptr && std::abs(placed->startBeats - 12.5) < 0.0001,
           "Project placement command updates imported clip start beat");
    expect(error.empty(), "Project placement success leaves error empty");

    expect(!project.setImportedAudioClipStartBeats("clip-1", 3.0, error),
           "Project placement rejects generated clips");
    const auto* generated = findClipById(project, "clip-1");
    expect(generated != nullptr && std::abs(generated->startBeats) < 0.0001,
           "Rejected generated-clip placement leaves generated clip unchanged");

    expect(!project.setImportedAudioClipStartBeats("clip-imported-placement", -1.0, error),
           "Project placement rejects negative start beats");
    expect(!project.setImportedAudioClipStartBeats("clip-imported-placement",
                                                   std::numeric_limits<double>::quiet_NaN(),
                                                   error),
           "Project placement rejects non-finite start beats");
    placed = findClipById(project, "clip-imported-placement");
    expect(placed != nullptr && std::abs(placed->startBeats - 12.5) < 0.0001,
           "Rejected placement keeps imported clip start beat unchanged");

    expect(!project.setImportedAudioClipStartBeats("missing-clip", 1.0, error),
           "Project placement reports missing imported clip");

    expect(project.replaceImportedAudioClipMedia("clip-imported-placement",
                                                 "audio/replaced-placement.wav",
                                                 "analysis/replaced-placement.waveform.json",
                                                 3.5,
                                                 error),
           "Project replaces imported audio clip media");
    placed = findClipById(project, "clip-imported-placement");
    expect(placed != nullptr && placed->relativePath == "audio/replaced-placement.wav",
           "Project media replacement updates imported clip media path");
    expect(placed != nullptr && placed->analysisPath == "analysis/replaced-placement.waveform.json",
           "Project media replacement updates imported clip analysis path");
    expect(placed != nullptr && std::abs(placed->lengthBeats - 3.5) < 0.0001,
           "Project media replacement updates imported clip length");
    expect(placed != nullptr && std::abs(placed->startBeats - 12.5) < 0.0001,
           "Project media replacement preserves imported clip start beat");

    expect(!project.replaceImportedAudioClipMedia("clip-1",
                                                  "audio/rejected.wav",
                                                  "analysis/rejected.waveform.json",
                                                  1.0,
                                                  error),
           "Project media replacement rejects generated clips");
    expect(!project.replaceImportedAudioClipMedia("clip-imported-placement",
                                                  "",
                                                  "analysis/rejected.waveform.json",
                                                  1.0,
                                                  error),
           "Project media replacement rejects empty media path");
    expect(!project.replaceImportedAudioClipMedia("clip-imported-placement",
                                                  "audio/rejected.wav",
                                                  "analysis/rejected.waveform.json",
                                                  -1.0,
                                                  error),
           "Project media replacement rejects invalid length");
}

void timelineClipLaneScalesOrdersAndPreservesWaveformStates()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto package = makeTemporaryPackagePath("projectname-timeline-lane-test");
    std::filesystem::create_directories(package / "analysis");

    projectname::WaveformSummary summary;
    summary.sampleRateHz = 48000.0;
    summary.frameCount = 4;
    summary.sourceFramesPerBucket = 1;
    summary.buckets = { { 1.0f, 0.7f }, { 0.25f, 0.2f }, { 0.5f, 0.35f }, { 0.75f, 0.5f } };

    std::string error;
    expect(projectname::saveWaveformSummary(summary, package / "analysis/ready.waveform.json", error),
           "Timeline lane test writes ready waveform summary");

    {
        std::ofstream invalid(package / "analysis/invalid.waveform.json", std::ios::trunc);
        invalid << "{ invalid";
    }

    projectname::ProjectClip ready;
    ready.id = "clip-ready-late";
    ready.name = "Ready Late";
    ready.type = "audio-file";
    ready.relativePath = "audio/ready.wav";
    ready.analysisPath = "analysis/ready.waveform.json";
    ready.startBeats = 8.0;
    ready.lengthBeats = 2.0;

    projectname::ProjectClip invalid;
    invalid.id = "clip-invalid-middle";
    invalid.name = "Invalid Middle";
    invalid.type = "audio-file";
    invalid.relativePath = "audio/invalid.wav";
    invalid.analysisPath = "analysis/invalid.waveform.json";
    invalid.startBeats = 2.0;
    invalid.lengthBeats = 4.0;

    projectname::ProjectClip missing;
    missing.id = "clip-missing-first";
    missing.name = "Missing First";
    missing.type = "audio-file";
    missing.relativePath = "audio/missing.wav";
    missing.analysisPath = "analysis/missing.waveform.json";
    missing.startBeats = 0.0;
    missing.lengthBeats = 4.0;

    expect(project.addClipToTrack("track-1", ready), "Timeline lane test attaches late ready clip");
    expect(project.addClipToTrack("track-1", invalid), "Timeline lane test attaches overlapping invalid clip");
    expect(project.addClipToTrack("track-1", missing), "Timeline lane test attaches first missing clip");

    projectname::TimelineClipLaneOptions options;
    options.viewStartBeats = 0.0;
    options.beatsPerPixel = 0.5;
    options.viewportWidthPixels = 24;
    options.clipHeightPixels = 20;
    options.rowGapPixels = 4;

    const auto layout = projectname::buildImportedAudioTimelineClipLane(project, package, options);
    expect(layout.clips.size() == 3, "Timeline lane lays out all imported audio clips");
    expect(layout.contentHeightPixels == 44, "Timeline lane stacks overlapping clips into rows");
    expect(std::abs(layout.viewEndBeats - 12.0) < 0.0001, "Timeline lane computes viewport end in beats");

    if (layout.clips.size() == 3)
    {
        expect(layout.clips[0].waveform.clip.id == "clip-missing-first",
               "Timeline lane orders clips by start beat");
        expect(layout.clips[1].waveform.clip.id == "clip-invalid-middle",
               "Timeline lane keeps second ordered clip");
        expect(layout.clips[2].waveform.clip.id == "clip-ready-late",
               "Timeline lane keeps late clip after overlapping clips");

        expect(layout.clips[0].x == 0 && layout.clips[0].width == 8,
               "Timeline lane scales first clip rectangle from beats");
        expect(layout.clips[1].x == 4 && layout.clips[1].width == 8,
               "Timeline lane scales overlapping clip rectangle from beats");
        expect(layout.clips[2].x == 16 && layout.clips[2].width == 4,
               "Timeline lane scales late clip rectangle from beats");

        expect(layout.clips[0].rowIndex == 0 && layout.clips[0].y == 0,
               "Timeline lane places first clip on the first row");
        expect(layout.clips[1].rowIndex == 1 && layout.clips[1].y == 24,
               "Timeline lane moves overlapping clip to the next row");
        expect(layout.clips[2].rowIndex == 0 && layout.clips[2].y == 0,
               "Timeline lane reuses the first row after overlap ends");

        expect(layout.clips[0].waveform.state == projectname::WaveformThumbnailState::missingAnalysis,
               "Timeline lane preserves missing-analysis state per clip");
        expect(layout.clips[1].waveform.state == projectname::WaveformThumbnailState::invalidAnalysis,
               "Timeline lane preserves invalid-analysis state per clip");
        expect(layout.clips[2].waveform.state == projectname::WaveformThumbnailState::ready,
               "Timeline lane preserves ready waveform state per clip");
        expect(!layout.clips[2].waveform.summary.buckets.empty(),
               "Timeline lane keeps ready waveform summary data");
    }

    expect(std::filesystem::remove_all(package) > 0, "Temporary timeline lane package deleted");
}

void timelineClipLaneHitTestsVisibleImportedClipsAndSelection()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto package = makeTemporaryPackagePath("projectname-timeline-hit-test");

    projectname::ProjectClip first;
    first.id = "clip-hit-first";
    first.name = "Hit First";
    first.type = "audio-file";
    first.relativePath = "audio/hit-first.wav";
    first.analysisPath = "analysis/hit-first.waveform.json";
    first.startBeats = 0.0;
    first.lengthBeats = 8.0;

    projectname::ProjectClip second;
    second.id = "clip-hit-second";
    second.name = "Hit Second";
    second.type = "audio-file";
    second.relativePath = "audio/hit-second.wav";
    second.analysisPath = "analysis/hit-second.waveform.json";
    second.startBeats = 4.0;
    second.lengthBeats = 4.0;

    expect(project.addClipToTrack("track-1", first), "Timeline hit test attaches first imported clip");
    expect(project.addClipToTrack("track-1", second), "Timeline hit test attaches overlapping imported clip");

    std::string error;
    expect(project.selectImportedAudioClip("clip-hit-second", error), "Timeline hit test selects second imported clip");

    projectname::TimelineClipLaneOptions options;
    options.beatsPerPixel = 1.0;
    options.viewportWidthPixels = 16;
    options.clipHeightPixels = 10;
    options.rowGapPixels = 2;

    const auto layout = projectname::buildImportedAudioTimelineClipLane(project, package, options);
    expect(layout.clips.size() == 2, "Timeline hit test lays out imported clips");

    if (layout.clips.size() == 2)
    {
        expect(layout.clips[0].waveform.clip.id == "clip-hit-first"
                   && layout.clips[0].x == 0
                   && layout.clips[0].y == 0
                   && layout.clips[0].width == 8
                   && !layout.clips[0].selected,
               "Timeline hit test keeps first clip unselected on row zero");
        expect(layout.clips[1].waveform.clip.id == "clip-hit-second"
                   && layout.clips[1].x == 4
                   && layout.clips[1].y == 12
                   && layout.clips[1].width == 4
                   && layout.clips[1].selected,
               "Timeline hit test marks selected overlapping clip on next row");

        const auto firstHit = projectname::hitTestTimelineClipLane(layout, 1, 1);
        expect(firstHit.has_value() && firstHit->clipId == "clip-hit-first",
               "Timeline hit test resolves the first visible clip");

        const auto secondHit = projectname::hitTestTimelineClipLane(layout, 5, 13);
        expect(secondHit.has_value() && secondHit->clipId == "clip-hit-second",
               "Timeline hit test resolves the selected overlapping clip row");

        expect(!projectname::hitTestTimelineClipLane(layout, 5, 11).has_value(),
               "Timeline hit test ignores row gaps");
        expect(!projectname::hitTestTimelineClipLane(layout, 8, 1).has_value(),
               "Timeline hit test treats clip right edges as outside");
        expect(!projectname::hitTestTimelineClipLane(layout, 20, 1).has_value(),
               "Timeline hit test ignores points outside visible clips");
    }
}

void timelineClipLaneScalesAndClipsLoopRange()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto package = makeTemporaryPackagePath("projectname-timeline-loop-range-test");

    projectname::TimelineClipLaneOptions options;
    options.viewStartBeats = 4.0;
    options.beatsPerPixel = 0.5;
    options.viewportWidthPixels = 16;
    options.clipHeightPixels = 20;

    auto layout = projectname::buildImportedAudioTimelineClipLane(project, package, options);
    expect(!layout.loopRange.has_value(), "Disabled loop region has no timeline loop range");

    std::string error;
    expect(project.setLoopRegion(6.0, 4.0, error), "Timeline loop range test sets visible loop");
    layout = projectname::buildImportedAudioTimelineClipLane(project, package, options);
    expect(layout.loopRange.has_value(), "Enabled loop region creates timeline loop range");
    expect(layout.loopRange.has_value() && layout.loopRange->visible,
           "Visible loop range reports visible");
    expect(layout.loopRange.has_value() && layout.loopRange->x == 4,
           "Loop range x scales from view start and beats-per-pixel");
    expect(layout.loopRange.has_value() && layout.loopRange->width == 8,
           "Loop range width scales from beat length");
    expect(layout.loopRange.has_value() && std::abs(layout.loopRange->endBeats - 10.0) < 0.0001,
           "Loop range records end beats");

    expect(project.setLoopRegion(2.0, 4.0, error), "Timeline loop range test sets left-clipped loop");
    layout = projectname::buildImportedAudioTimelineClipLane(project, package, options);
    expect(layout.loopRange.has_value() && layout.loopRange->visible,
           "Left-clipped loop range remains visible");
    expect(layout.loopRange.has_value() && layout.loopRange->x == -4,
           "Left-clipped loop range keeps raw negative x for renderer clipping");
    expect(layout.loopRange.has_value() && layout.loopRange->width == 8,
           "Left-clipped loop range keeps full raw width");

    expect(project.setLoopRegion(10.0, 6.0, error), "Timeline loop range test sets right-clipped loop");
    layout = projectname::buildImportedAudioTimelineClipLane(project, package, options);
    expect(layout.loopRange.has_value() && layout.loopRange->visible,
           "Right-clipped loop range remains visible");
    expect(layout.loopRange.has_value() && layout.loopRange->x == 12,
           "Right-clipped loop range keeps raw x");
    expect(layout.loopRange.has_value() && layout.loopRange->width == 12,
           "Right-clipped loop range keeps full raw width");

    expect(project.setLoopRegion(20.0, 2.0, error), "Timeline loop range test sets offscreen loop");
    layout = projectname::buildImportedAudioTimelineClipLane(project, package, options);
    expect(layout.loopRange.has_value() && !layout.loopRange->visible,
           "Offscreen loop range is retained but marked not visible");
}

void projectAudioImportPlacesClipsDeterministicallyAndAllowsExplicitStart()
{
    auto project = projectname::ProjectModel::createDefault();
    project.getTransport().setTempoBpm(120.0);

    const auto package = makeTemporaryPackagePath("projectname-import-placement-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-import-placement-source-test");
    writePcm16Wav(wavPath, 48000, 1, { 1000, -1000, 2000, -2000 });

    std::string error;
    auto first = projectname::importPcm16WavIntoProjectPackage(project, package, wavPath, error);
    expect(first.has_value(), "First placed import succeeds");
    expect(first.has_value() && std::abs(first->clip.startBeats - 4.0) < 0.0001,
           "First import starts after the existing starter clip");

    auto second = projectname::importPcm16WavIntoProjectPackage(project, package, wavPath, error);
    expect(second.has_value(), "Second placed import succeeds");
    expect(first.has_value()
               && second.has_value()
               && std::abs(second->clip.startBeats - (first->clip.startBeats + first->clip.lengthBeats)) < 0.0001,
           "Second import starts after the previous imported clip");
    expect(first.has_value()
               && second.has_value()
               && first->preparedClip.samples == second->preparedClip.samples,
           "Timeline placement leaves prepared audio handoff unchanged");

    projectname::ProjectAudioImportOptions options;
    options.requestedStartBeats = 12.5;
    auto explicitStart = projectname::importPcm16WavIntoProjectPackage(project,
                                                                       package,
                                                                       wavPath,
                                                                       options,
                                                                       error);
    expect(explicitStart.has_value(), "Explicit-start import succeeds");
    expect(explicitStart.has_value() && std::abs(explicitStart->clip.startBeats - 12.5) < 0.0001,
           "Explicit-start import uses requested start beat");
    expect(first.has_value()
               && explicitStart.has_value()
               && first->preparedClip.samples == explicitStart->preparedClip.samples,
           "Explicit timeline placement leaves prepared audio handoff unchanged");

    options.requestedStartBeats = -1.0;
    const auto beforeRejectedImport = project;
    auto rejected = projectname::importPcm16WavIntoProjectPackage(project,
                                                                 package,
                                                                 wavPath,
                                                                 options,
                                                                 error);
    expect(!rejected.has_value(), "Import rejects invalid requested start beat");
    expect(project == beforeRejectedImport, "Rejected requested start beat does not mutate project");

    if (second.has_value())
    {
        expect(project.setImportedAudioClipStartBeats(second->clip.id, 9.25, error),
               "Imported clip can be moved after import");
        expect(project.savePackage(package, error), "Moved imported clip saves");
        auto loaded = projectname::ProjectModel::loadPackage(package, error);
        const auto* loadedClip = loaded.has_value() ? findClipById(*loaded, second->clip.id) : nullptr;
        expect(loadedClip != nullptr && std::abs(loadedClip->startBeats - 9.25) < 0.0001,
               "Moved imported clip start beat persists through save/load");

        projectname::TimelineClipLaneOptions laneOptions;
        laneOptions.beatsPerPixel = 0.25;
        laneOptions.viewportWidthPixels = 80;
        const auto layout = projectname::buildImportedAudioTimelineClipLane(project, package, laneOptions);
        const auto placedInLane = std::any_of(layout.clips.begin(),
                                              layout.clips.end(),
                                              [&second](const projectname::TimelineClipLaneItem& item)
                                              {
                                                  return item.waveform.clip.id == second->clip.id
                                                      && std::abs(item.startBeats - 9.25) < 0.0001;
                                              });
        expect(placedInLane, "Timeline lane refresh model reflects moved imported clip");
    }

    expect(std::filesystem::remove(wavPath), "Temporary placement import WAV deleted");
    expect(std::filesystem::remove_all(package) > 0, "Temporary placement import package deleted");
}

void waveformAnalysisRegeneratorRestoresMissingAndInvalidSummaries()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto package = makeTemporaryPackagePath("projectname-waveform-regeneration-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-waveform-regeneration-source-test");
    writePcm16Wav(wavPath, 44100, 1, { 12000, 0, -12000, 6000, -6000, 3000, -3000, 0 });

    std::string error;
    auto import = projectname::importPcm16WavIntoProjectPackage(project, package, wavPath, error);
    expect(import.has_value(), "Waveform regeneration test import succeeds");

    if (import.has_value())
    {
        auto noOp = projectname::regenerateFirstImportedAudioWaveformAnalysis(project, package);
        expect(noOp.status == projectname::WaveformRegenerationStatus::notNeeded,
               "Waveform regeneration skips valid existing analysis");

        expect(std::filesystem::remove(import->waveformSummaryPath),
               "Waveform regeneration test removes existing summary");

        std::atomic_bool cancelRequested { false };
        projectname::WaveformRegenerationOptions cancelOptions;
        cancelOptions.cancelRequested = &cancelRequested;
        cancelOptions.decodeOptions.progressCallback =
            [&cancelRequested](const projectname::WavDecodeProgress& progress)
            {
                if (progress.framesDecoded > 0)
                    cancelRequested.store(true, std::memory_order_release);
            };

        auto cancelled = projectname::regenerateFirstImportedAudioWaveformAnalysis(project,
                                                                                  package,
                                                                                  cancelOptions);
        expect(cancelled.status == projectname::WaveformRegenerationStatus::cancelled,
               "Waveform regeneration reports cancellation");
        expect(!std::filesystem::exists(import->waveformSummaryPath),
               "Cancelled waveform regeneration does not write summary");

        projectname::BackgroundWaveformAnalysisRequest request;
        request.project = project;
        request.packageDirectory = package;
        projectname::BackgroundWaveformAnalysisJob job(std::move(request));
        expect(!job.hasStarted(), "Background waveform regeneration starts pending");
        job.start();
        auto backgroundResult = job.waitForResult();
        expect(!backgroundResult.cancelled, "Background waveform regeneration success is not cancelled");
        expect(backgroundResult.regeneration.status == projectname::WaveformRegenerationStatus::regenerated,
               "Background waveform regeneration restores missing analysis");
        expect(std::filesystem::is_regular_file(import->waveformSummaryPath),
               "Background waveform regeneration writes missing summary");

        {
            std::ofstream file(import->waveformSummaryPath, std::ios::trunc);
            file << "{ invalid";
        }

        auto repaired = projectname::regenerateFirstImportedAudioWaveformAnalysis(project, package);
        expect(repaired.status == projectname::WaveformRegenerationStatus::regenerated,
               "Waveform regeneration repairs invalid analysis");

        auto thumbnail = projectname::loadFirstImportedAudioWaveform(project, package);
        expect(thumbnail.state == projectname::WaveformThumbnailState::ready,
               "Waveform regeneration makes thumbnail ready");
        expect(repaired.summary.has_value(), "Waveform regeneration returns summary data");
        expect(repaired.summary.has_value() && thumbnail.summary.buckets.size() == repaired.summary->buckets.size(),
               "Waveform regeneration returned summary matches loaded thumbnail data");

        expect(std::filesystem::remove(import->waveformSummaryPath),
               "Waveform regeneration test removes summary before background cancel");
        projectname::BackgroundWaveformAnalysisRequest cancelRequest;
        cancelRequest.project = project;
        cancelRequest.packageDirectory = package;
        projectname::BackgroundWaveformAnalysisJob cancelJob(std::move(cancelRequest));
        cancelJob.requestCancel();
        cancelJob.start();
        auto cancelResult = cancelJob.waitForResult();
        expect(cancelResult.cancelled, "Background waveform regeneration reports pre-start cancellation");
        expect(cancelResult.regeneration.status == projectname::WaveformRegenerationStatus::cancelled,
               "Background waveform regeneration returns cancelled status");
        expect(!std::filesystem::exists(import->waveformSummaryPath),
               "Cancelled background waveform regeneration does not write summary");
    }

    expect(std::filesystem::remove(wavPath), "Temporary waveform regeneration source WAV deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary waveform regeneration package deleted");
}

void projectAudioImportUsesUniquePackageFileNames()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto package = makeTemporaryPackagePath("projectname-package-import-unique-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-duplicate-import-test");
    writePcm16Wav(wavPath, 44100, 1, { 1024, 2048, 4096 });

    std::string error;
    auto first = projectname::importPcm16WavIntoProjectPackage(project, package, wavPath, error);
    auto second = projectname::importPcm16WavIntoProjectPackage(project, package, wavPath, error);

    expect(first.has_value(), "First duplicate-source import succeeds");
    expect(second.has_value(), "Second duplicate-source import succeeds");
    expect(first.has_value() && second.has_value() && first->clip.relativePath != second->clip.relativePath,
           "Duplicate-source imports use unique relative paths");
    expect(first.has_value() && std::filesystem::is_regular_file(first->copiedAudioPath),
           "First duplicate-source import copy exists");
    expect(second.has_value() && std::filesystem::is_regular_file(second->copiedAudioPath),
           "Second duplicate-source import copy exists");
    expect(first.has_value() && second.has_value()
               && first->clip.analysisPath != second->clip.analysisPath,
           "Duplicate-source imports use unique analysis paths");
    expect(first.has_value() && std::filesystem::is_regular_file(first->waveformSummaryPath),
           "First duplicate-source import waveform summary exists");
    expect(second.has_value() && std::filesystem::is_regular_file(second->waveformSummaryPath),
           "Second duplicate-source import waveform summary exists");

    auto importedClipCount = 0;
    for (const auto& track : project.getTracks())
    {
        for (const auto& clip : track.clips)
        {
            if (clip.type == "audio-file")
                ++importedClipCount;
        }
    }
    expect(importedClipCount == 2, "Project keeps both imported audio clips");

    expect(std::filesystem::remove(wavPath), "Temporary duplicate source WAV deleted");
    expect(std::filesystem::remove_all(package) > 0, "Temporary duplicate import package deleted");
}

void projectAudioImportRejectsInvalidWavWithoutMutatingProject()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto originalProject = project;
    const auto package = makeTemporaryPackagePath("projectname-package-import-invalid-test");
    const auto invalidWavPath = makeTemporaryAudioPath("projectname-invalid-import-source-test");
    {
        std::ofstream file(invalidWavPath, std::ios::binary | std::ios::trunc);
        file << "invalid audio";
    }

    std::string error;
    auto result = projectname::importPcm16WavIntoProjectPackage(project, package, invalidWavPath, error);
    expect(!result.has_value(), "Project audio import rejects invalid WAV");
    expect(!error.empty(), "Project audio import reports invalid WAV error");
    expect(project == originalProject, "Rejected project audio import does not mutate project");
    expect(!std::filesystem::exists(package / "manifest.json"),
           "Rejected project audio import does not write project manifest");

    expect(std::filesystem::remove(invalidWavPath), "Temporary invalid import source deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary invalid import package deleted");
}

void projectAudioImportCancelsDuringDecodeWithoutMutatingProject()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto originalProject = project;
    const auto package = makeTemporaryPackagePath("projectname-package-import-decode-cancel-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-decode-cancel-import-source-test");
    writePcm16Wav(wavPath, 44100, 1, { 1000, 2000, 3000, 4000, 5000, 6000 });

    std::atomic_bool cancelRequested { false };
    auto sawDecodeProgress = false;

    projectname::ProjectAudioImportOptions options;
    options.cancelRequested = &cancelRequested;
    options.decodeProgressCallback =
        [&cancelRequested, &sawDecodeProgress](const projectname::WavDecodeProgress& progress)
        {
            if (progress.framesDecoded > 0)
            {
                sawDecodeProgress = true;
                cancelRequested.store(true, std::memory_order_release);
            }
        };

    std::string error;
    auto result = projectname::importPcm16WavIntoProjectPackage(project,
                                                                package,
                                                                wavPath,
                                                                options,
                                                                error);

    expect(!result.has_value(), "Project audio import reports decode cancellation");
    expect(error.find("cancelled") != std::string::npos,
           "Project audio import decode cancellation error is descriptive");
    expect(sawDecodeProgress, "Project audio import reports decode progress before cancellation");
    expect(project == originalProject, "Cancelled decode project audio import does not mutate project");
    expect(!std::filesystem::exists(package / "manifest.json"),
           "Cancelled decode project audio import does not write manifest");
    expect(!std::filesystem::exists(package / "audio"),
           "Cancelled decode project audio import does not create audio folder");
    expect(!std::filesystem::exists(package / ".projectname-staging"),
           "Cancelled decode project audio import does not create staging folder");

    expect(std::filesystem::remove(wavPath), "Temporary decode cancel import source deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary decode cancel package deleted");
}

void projectAudioImportCancelsDuringStagedCopyAndCleansUp()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto originalProject = project;
    const auto package = makeTemporaryPackagePath("projectname-package-import-copy-cancel-test");
    const auto sourcePath = std::filesystem::temp_directory_path()
        / ("projectname-large-import-source-" + std::to_string(std::random_device {}()) + ".bin");

    {
        std::ofstream file(sourcePath, std::ios::binary | std::ios::trunc);
        std::vector<char> bytes(128U * 1024U, '\x42');
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    projectname::PreparedMonoAudioClip preparedClip;
    preparedClip.sampleRateHz = 44100.0;
    preparedClip.sourceChannelCount = 1;
    preparedClip.frameCount = 2;
    preparedClip.samples = { 0.0f, 0.5f };

    std::atomic_bool cancelRequested { false };
    auto sawCopyProgress = false;
    auto sawNonZeroCopyProgress = false;

    projectname::ProjectAudioImportOptions options;
    options.cancelRequested = &cancelRequested;
    options.copyChunkBytes = 4096;
    options.progressCallback =
        [&cancelRequested, &sawCopyProgress, &sawNonZeroCopyProgress](const projectname::ProjectAudioImportProgress& progress)
        {
            if (progress.stage != projectname::ProjectAudioImportStage::copying)
                return;

            sawCopyProgress = true;
            if (progress.bytesCopied > 0)
            {
                sawNonZeroCopyProgress = true;
                cancelRequested.store(true, std::memory_order_release);
            }
        };

    std::string error;
    auto result = projectname::commitPreparedAudioImportToProjectPackage(project,
                                                                         package,
                                                                         sourcePath,
                                                                         std::move(preparedClip),
                                                                         options,
                                                                         error);

    expect(!result.has_value(), "Staged project audio import reports cancellation");
    expect(error.find("cancelled") != std::string::npos,
           "Staged project audio import cancellation error is descriptive");
    expect(sawCopyProgress, "Staged project audio import reports copy progress");
    expect(sawNonZeroCopyProgress, "Staged project audio import reports non-zero copy progress");
    expect(project == originalProject, "Cancelled staged project audio import does not mutate project");
    expect(!std::filesystem::exists(package / "manifest.json"),
           "Cancelled staged project audio import does not write manifest");
    expect(!std::filesystem::exists(package / ".projectname-staging"),
           "Cancelled staged project audio import cleans staging folder");

    auto committedAudioFileCount = 0;
    const auto audioDirectory = package / "audio";
    if (std::filesystem::exists(audioDirectory))
    {
        for (const auto& entry : std::filesystem::directory_iterator(audioDirectory))
        {
            if (entry.is_regular_file())
                ++committedAudioFileCount;
        }
    }
    expect(committedAudioFileCount == 0,
           "Cancelled staged project audio import does not commit an audio asset");

    expect(std::filesystem::remove(sourcePath), "Temporary staged cancel source deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary staged cancel package deleted");
}

void backgroundAudioImportJobImportsProjectPackage()
{
    auto project = projectname::ProjectModel::createDefault();
    project.setName("Background Import Test");
    const auto package = makeTemporaryPackagePath("projectname-background-import-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-background-import-source-test");
    writePcm16Wav(wavPath, 44100, 1, { 3000, -3000, 1500 });

    projectname::BackgroundAudioImportRequest request;
    request.project = project;
    request.packageDirectory = package;
    request.sourceWavPath = wavPath;
    request.requestedStartBeats = 7.5;

    projectname::BackgroundAudioImportJob job(std::move(request));
    expect(!job.hasStarted(), "Background import job starts pending");
    const auto pendingProgress = job.getProgress();
    expect(pendingProgress.phase == projectname::BackgroundAudioImportPhase::pending,
           "Background import progress starts pending");
    expect(pendingProgress.percent == 0, "Background import progress starts at 0 percent");
    expect(!pendingProgress.cancelRequested, "Background import progress starts without cancellation");
    job.start();
    expect(job.hasStarted(), "Background import job reports started");
    const auto startedProgress = job.getProgress();
    expect(startedProgress.phase != projectname::BackgroundAudioImportPhase::pending,
           "Background import progress leaves pending after start");
    expect(startedProgress.percent >= 0 && startedProgress.percent <= 100,
           "Background import progress remains bounded after start");

    auto result = job.waitForResult();
    const auto completedProgress = job.getProgress();
    expect(!result.cancelled, "Background import success is not cancelled");
    expect(result.import.has_value(), "Background import returns imported clip result");
    expect(result.error.empty(), "Background import success leaves error empty");
    expect(result.import.has_value() && !result.import->preparedClip.samples.empty(),
           "Background import returns prepared samples");
    expect(result.import.has_value() && std::abs(result.import->clip.startBeats - 7.5) < 0.0001,
           "Background import applies requested timeline start beat");
    expect(completedProgress.phase == projectname::BackgroundAudioImportPhase::completed,
           "Background import success reports completed progress");
    expect(completedProgress.percent == 100, "Background import success reaches 100 percent");
    expect(completedProgress.framesTotal > 0, "Background import success reports decoded frame total");
    expect(completedProgress.framesProcessed == completedProgress.framesTotal,
           "Background import success reports all frames decoded");
    expect(completedProgress.bytesTotal > 0, "Background import success reports copied byte total");
    expect(completedProgress.bytesProcessed == completedProgress.bytesTotal,
           "Background import success reports all bytes copied");

    std::string error;
    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(loaded.has_value(), "Background-imported package loads");
    expect(loaded.has_value() && *loaded == result.project,
           "Background import result project matches saved package");

    expect(std::filesystem::remove(wavPath), "Temporary background import source deleted");
    expect(std::filesystem::remove_all(package) > 0, "Temporary background import package deleted");
}

void backgroundAudioImportJobReportsFailureWithoutMutatingProject()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto originalProject = project;
    const auto package = makeTemporaryPackagePath("projectname-background-import-failure-test");
    const auto invalidWavPath = makeTemporaryAudioPath("projectname-background-invalid-source-test");
    {
        std::ofstream file(invalidWavPath, std::ios::binary | std::ios::trunc);
        file << "invalid audio";
    }

    projectname::BackgroundAudioImportRequest request;
    request.project = project;
    request.packageDirectory = package;
    request.sourceWavPath = invalidWavPath;

    projectname::BackgroundAudioImportJob job(std::move(request));
    job.start();
    auto result = job.waitForResult();
    const auto failedProgress = job.getProgress();

    expect(!result.cancelled, "Background import failure is not cancellation");
    expect(!result.import.has_value(), "Background import failure does not return clip result");
    expect(!result.error.empty(), "Background import failure reports error");
    expect(result.project == originalProject, "Background import failure keeps original project copy");
    expect(failedProgress.phase == projectname::BackgroundAudioImportPhase::failed,
           "Background import failure reports failed progress");
    expect(failedProgress.percent == 100, "Background import failure reaches terminal progress");
    expect(!std::filesystem::exists(package / "manifest.json"),
           "Background import failure does not write manifest");

    expect(std::filesystem::remove(invalidWavPath), "Temporary background invalid source deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary background failure package deleted");
}

void backgroundAudioImportJobCancelsBeforeStart()
{
    auto project = projectname::ProjectModel::createDefault();
    const auto originalProject = project;
    const auto package = makeTemporaryPackagePath("projectname-background-import-cancel-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-background-cancel-source-test");
    writePcm16Wav(wavPath, 44100, 1, { 1000, -1000 });

    projectname::BackgroundAudioImportRequest request;
    request.project = project;
    request.packageDirectory = package;
    request.sourceWavPath = wavPath;

    projectname::BackgroundAudioImportJob job(std::move(request));
    job.requestCancel();
    const auto cancelRequestedProgress = job.getProgress();
    expect(cancelRequestedProgress.cancelRequested, "Background import progress records cancellation request");
    expect(cancelRequestedProgress.phase == projectname::BackgroundAudioImportPhase::cancelled,
           "Background import pre-start cancellation reports cancelled progress");
    job.start();
    auto result = job.waitForResult();
    const auto cancelledProgress = job.getProgress();

    expect(result.cancelled, "Background import cancellation is reported");
    expect(!result.import.has_value(), "Cancelled background import does not return clip result");
    expect(result.project == originalProject, "Cancelled background import keeps original project copy");
    expect(cancelledProgress.phase == projectname::BackgroundAudioImportPhase::cancelled,
           "Background import cancellation stays cancelled");
    expect(cancelledProgress.percent == 0, "Background import pre-start cancellation stays at 0 percent");
    expect(!std::filesystem::exists(package / "manifest.json"),
           "Cancelled background import does not write manifest");

    expect(std::filesystem::remove(wavPath), "Temporary background cancel source deleted");
    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary background cancel package deleted");
}

void appSessionKeepsTransportInsideProjectModel()
{
    projectname::AppSession session;

    expect(!session.getTransport().isPlaying(), "Session transport starts stopped");
    expect(!session.shouldPlayGeneratedTone(), "Session generated tone starts disabled");

    session.setTempoBpm(96.0);
    expect(session.getProject().getTransport().getTempoBpm() == 96.0,
           "Session tempo writes to project transport");

    session.play();
    expect(session.getProject().getTransport().isPlaying(), "Session play updates project transport");
    expect(session.shouldPlayGeneratedTone(), "Session play enables generated tone");

    session.advanceSeconds(1.25);
    expect(std::abs(session.getProject().getTransport().getPositionBeats() - 2.0) < 0.0001,
           "Session advances project transport position");

    session.stop();
    expect(!session.getProject().getTransport().isPlaying(), "Session stop updates project transport");
    expect(!session.shouldPlayGeneratedTone(), "Session stop disables generated tone");
}

void appSessionTimelineViewportStateClampsValues()
{
    projectname::AppSession session;

    expect(std::abs(session.getTimelineViewport().viewStartBeats) < 0.0001,
           "Session timeline viewport starts at beat zero");
    expect(std::abs(session.getTimelineViewport().beatsPerPixel - 0.125) < 0.0001,
           "Session timeline viewport starts at default zoom scale");

    session.setTimelineViewport(12.5, 0.25);
    expect(std::abs(session.getTimelineViewport().viewStartBeats - 12.5) < 0.0001,
           "Session timeline viewport stores valid view start");
    expect(std::abs(session.getTimelineViewport().beatsPerPixel - 0.25) < 0.0001,
           "Session timeline viewport stores valid zoom scale");

    session.setTimelineViewport(-4.0, 0.0);
    expect(std::abs(session.getTimelineViewport().viewStartBeats) < 0.0001,
           "Session timeline viewport clamps negative view start to zero");
    expect(std::abs(session.getTimelineViewport().beatsPerPixel - (1.0 / 64.0)) < 0.0001,
           "Session timeline viewport clamps too-close zoom scale");

    session.setTimelineViewport(std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::infinity());
    expect(std::abs(session.getTimelineViewport().viewStartBeats) < 0.0001,
           "Session timeline viewport rejects infinite view start");
    expect(std::abs(session.getTimelineViewport().beatsPerPixel - 0.125) < 0.0001,
           "Session timeline viewport rejects infinite zoom scale");

    session.setTimelineViewStartBeats(3.75);
    expect(std::abs(session.getTimelineViewport().viewStartBeats - 3.75) < 0.0001,
           "Session timeline viewport updates view start independently");
    expect(std::abs(session.getTimelineViewport().beatsPerPixel - 0.125) < 0.0001,
           "Session timeline viewport independent start update preserves zoom");

    session.setTimelineBeatsPerPixel(99.0);
    expect(std::abs(session.getTimelineViewport().beatsPerPixel - 4.0) < 0.0001,
           "Session timeline viewport clamps too-far zoom scale");
    expect(std::abs(session.getTimelineViewport().viewStartBeats - 3.75) < 0.0001,
           "Session timeline viewport independent zoom update preserves start");

    session.setTimelineViewport(8.0, 0.5);
    session.nudgeTimelineViewStartBeats(-2.0);
    expect(std::abs(session.getTimelineViewport().viewStartBeats - 6.0) < 0.0001,
           "Session timeline viewport nudge pans left by beat delta");
    session.nudgeTimelineViewStartBeats(-99.0);
    expect(std::abs(session.getTimelineViewport().viewStartBeats) < 0.0001,
           "Session timeline viewport nudge clamps panning before zero");
    session.nudgeTimelineViewStartBeats(std::numeric_limits<double>::infinity());
    expect(std::abs(session.getTimelineViewport().viewStartBeats) < 0.0001,
           "Session timeline viewport nudge ignores infinite deltas");

    session.setTimelineBeatsPerPixel(0.5);
    session.scaleTimelineBeatsPerPixel(0.5);
    expect(std::abs(session.getTimelineViewport().beatsPerPixel - 0.25) < 0.0001,
           "Session timeline viewport zoom command scales beats-per-pixel");
    session.scaleTimelineBeatsPerPixel(100.0);
    expect(std::abs(session.getTimelineViewport().beatsPerPixel - 4.0) < 0.0001,
           "Session timeline viewport zoom command clamps scaled zoom");
    session.setTimelineBeatsPerPixel(0.5);
    session.scaleTimelineBeatsPerPixel(-1.0);
    expect(std::abs(session.getTimelineViewport().beatsPerPixel - 0.5) < 0.0001,
           "Session timeline viewport zoom command ignores invalid multipliers");

    expect(projectname::formatTimelineViewportIndicator(session.getTimelineViewport())
               == "Timeline view: start 0.00 beats | 0.5000 beats/px",
           "Session timeline viewport indicator formats visible view state");

    expect(projectname::formatTimelineViewportIndicator({ -5.0, std::numeric_limits<double>::infinity() })
               == "Timeline view: start 0.00 beats | 0.1250 beats/px",
           "Session timeline viewport indicator normalizes invalid view state");
}

void timelineViewportFitHelperFramesImportedAudioClips()
{
    auto emptyProject = projectname::ProjectModel::createDefault();
    expect(!projectname::fitTimelineViewportToImportedAudioClips(emptyProject, 120).has_value(),
           "Timeline viewport fit helper ignores projects without imported audio");
    expect(!projectname::fitTimelineViewportToImportedAudioClips(emptyProject, 0).has_value(),
           "Timeline viewport fit helper rejects invalid viewport width");

    projectname::ProjectClip invalidImportedClip;
    invalidImportedClip.id = "clip-fit-invalid";
    invalidImportedClip.name = "Invalid Fit Clip";
    invalidImportedClip.type = "audio-file";
    invalidImportedClip.relativePath = "audio/invalid-fit.wav";
    invalidImportedClip.analysisPath = "analysis/invalid-fit.waveform.json";
    invalidImportedClip.startBeats = 4.0;
    invalidImportedClip.lengthBeats = -1.0;
    expect(emptyProject.addClipToTrack("track-1", invalidImportedClip),
           "Timeline viewport fit test attaches invalid imported clip");
    expect(!projectname::fitTimelineViewportToImportedAudioClips(emptyProject, 120).has_value(),
           "Timeline viewport fit helper ignores invalid imported audio clips");

    auto oneClipProject = projectname::ProjectModel::createDefault();
    projectname::ProjectClip single;
    single.id = "clip-fit-single";
    single.name = "Single Fit Clip";
    single.type = "audio-file";
    single.relativePath = "audio/single-fit.wav";
    single.analysisPath = "analysis/single-fit.waveform.json";
    single.startBeats = 8.0;
    single.lengthBeats = 2.0;
    expect(oneClipProject.addClipToTrack("track-1", single),
           "Timeline viewport fit test attaches single imported clip");

    auto singleFit = projectname::fitTimelineViewportToImportedAudioClips(oneClipProject, 100);
    expect(singleFit.has_value(), "Timeline viewport fit helper frames one imported clip");
    expect(singleFit.has_value() && std::abs(singleFit->viewStartBeats - 7.0) < 0.0001,
           "Timeline viewport fit helper applies left padding for one clip");
    expect(singleFit.has_value() && std::abs(singleFit->beatsPerPixel - 0.04) < 0.0001,
           "Timeline viewport fit helper scales one clip and padding to lane width");

    auto spacedProject = projectname::ProjectModel::createDefault();
    projectname::ProjectClip early;
    early.id = "clip-fit-early";
    early.name = "Early Fit Clip";
    early.type = "audio-file";
    early.relativePath = "audio/early-fit.wav";
    early.analysisPath = "analysis/early-fit.waveform.json";
    early.startBeats = 4.0;
    early.lengthBeats = 2.0;

    projectname::ProjectClip late;
    late.id = "clip-fit-late";
    late.name = "Late Fit Clip";
    late.type = "audio-file";
    late.relativePath = "audio/late-fit.wav";
    late.analysisPath = "analysis/late-fit.waveform.json";
    late.startBeats = 18.0;
    late.lengthBeats = 6.0;

    expect(spacedProject.addClipToTrack("track-1", late),
           "Timeline viewport fit test attaches late imported clip");
    expect(spacedProject.addClipToTrack("track-1", early),
           "Timeline viewport fit test attaches early imported clip");

    auto spacedFit = projectname::fitTimelineViewportToImportedAudioClips(spacedProject, 200);
    expect(spacedFit.has_value(), "Timeline viewport fit helper frames multiple imported clips");
    expect(spacedFit.has_value() && std::abs(spacedFit->viewStartBeats - 3.0) < 0.0001,
           "Timeline viewport fit helper uses earliest imported clip start");
    expect(spacedFit.has_value() && std::abs(spacedFit->beatsPerPixel - 0.11) < 0.0001,
           "Timeline viewport fit helper scales multiple spaced clips to lane width");

    if (spacedFit.has_value())
    {
        projectname::TimelineClipLaneOptions options;
        options.viewStartBeats = spacedFit->viewStartBeats;
        options.beatsPerPixel = spacedFit->beatsPerPixel;
        options.viewportWidthPixels = 200;

        const auto layout = projectname::buildImportedAudioTimelineClipLane(spacedProject,
                                                                            makeTemporaryPackagePath("fit-unused"),
                                                                            options);
        expect(layout.clips.size() == 2,
               "Timeline viewport fit helper lane proof includes both imported clips");

        const auto allVisible = std::all_of(layout.clips.begin(),
                                            layout.clips.end(),
                                            [&options](const projectname::TimelineClipLaneItem& item)
                                            {
                                                return item.visible
                                                    && item.x >= 0
                                                    && item.x + item.width <= options.viewportWidthPixels;
                                            });
        expect(allVisible,
               "Timeline viewport fit helper makes imported clip rectangles visible in lane bounds");
    }
}

void timelineViewportCenterHelperFramesSelectedImportedAudioClip()
{
    auto unselectedProject = projectname::ProjectModel::createDefault();
    projectname::ProjectClip unselected;
    unselected.id = "clip-center-unselected";
    unselected.name = "Center Unselected";
    unselected.type = "audio-file";
    unselected.relativePath = "audio/center-unselected.wav";
    unselected.analysisPath = "analysis/center-unselected.waveform.json";
    unselected.startBeats = 8.0;
    unselected.lengthBeats = 2.0;
    expect(unselectedProject.addClipToTrack("track-1", unselected),
           "Timeline center helper test attaches unselected imported clip");
    const auto unselectedCenter = projectname::centerTimelineViewportOnSelectedImportedAudioClip(
        unselectedProject,
        { 0.0, 0.25 },
        80);
    expect(!unselectedCenter.has_value(),
           "Timeline center helper ignores projects without a selected imported clip");

    const auto stalePackage = makeTemporaryPackagePath("projectname-center-helper-stale-selection-test");
    writeManifestText(stalePackage, R"({
  "manifestVersion": 1,
  "name": "Stale Center Selection",
  "transport": {
    "tempoBpm": 120,
    "timeSignature": { "numerator": 4, "denominator": 4 },
    "positionBeats": 0
  },
  "selection": { "clipId": "missing-selected-center-clip" },
  "tracks": [
    {
      "id": "track-center-stale",
      "name": "Center Stale",
      "type": "audio",
      "clips": [
        {
          "id": "clip-center-fallback",
          "name": "Center Fallback",
          "type": "audio-file",
          "relativePath": "audio/center-fallback.wav",
          "analysisPath": "analysis/center-fallback.waveform.json",
          "startBeats": 2,
          "lengthBeats": 2
        }
      ]
    }
  ]
})");

    std::string error;
    auto staleProject = projectname::ProjectModel::loadPackage(stalePackage, error);
    expect(staleProject.has_value()
               && staleProject->getSelectedClipId() == "missing-selected-center-clip",
           "Timeline center helper stale test loads stale selected id");
    if (staleProject.has_value())
    {
        const auto staleCenter = projectname::centerTimelineViewportOnSelectedImportedAudioClip(
            *staleProject,
            { 0.0, 0.25 },
            80);
        expect(!staleCenter.has_value(),
               "Timeline center helper ignores stale selected imported clip ids");
    }
    expect(std::filesystem::remove_all(stalePackage) > 0,
           "Temporary stale center helper package deleted");

    auto selectedProject = projectname::ProjectModel::createDefault();
    projectname::ProjectClip selected;
    selected.id = "clip-center-selected";
    selected.name = "Center Selected";
    selected.type = "audio-file";
    selected.relativePath = "audio/center-selected.wav";
    selected.analysisPath = "analysis/center-selected.waveform.json";
    selected.startBeats = 40.0;
    selected.lengthBeats = 4.0;
    expect(selectedProject.addClipToTrack("track-1", selected),
           "Timeline center helper test attaches selected imported clip");
    expect(selectedProject.selectImportedAudioClip(selected.id, error),
           "Timeline center helper test selects imported clip");
    const auto invalidWidthCenter = projectname::centerTimelineViewportOnSelectedImportedAudioClip(
        selectedProject,
        { 0.0, 0.25 },
        0);
    expect(!invalidWidthCenter.has_value(),
           "Timeline center helper rejects invalid viewport width");

    auto centered = projectname::centerTimelineViewportOnSelectedImportedAudioClip(
        selectedProject,
        { 0.0, 0.25 },
        80);
    expect(centered.has_value(), "Timeline center helper frames selected imported clip");
    expect(centered.has_value() && std::abs(centered->viewStartBeats - 32.0) < 0.0001,
           "Timeline center helper centers offscreen selected imported clip");
    expect(centered.has_value() && std::abs(centered->beatsPerPixel - 0.25) < 0.0001,
           "Timeline center helper preserves current zoom scale");

    if (centered.has_value())
    {
        projectname::TimelineClipLaneOptions options;
        options.viewStartBeats = centered->viewStartBeats;
        options.beatsPerPixel = centered->beatsPerPixel;
        options.viewportWidthPixels = 80;

        const auto layout = projectname::buildImportedAudioTimelineClipLane(
            selectedProject,
            makeTemporaryPackagePath("center-unused"),
            options);
        expect(layout.clips.size() == 1,
               "Timeline center helper lane proof includes selected imported clip");

        if (!layout.clips.empty())
        {
            const auto& item = layout.clips.front();
            expect(item.selected && item.visible,
                   "Timeline center helper lane proof keeps selected clip visible");
            expect(std::abs((static_cast<double>(item.x) + static_cast<double>(item.width) * 0.5) - 40.0)
                       < 0.0001,
                   "Timeline center helper lane proof centers selected clip rectangle");
        }
    }
}

void workspaceCommandRouterPreservesFocusedWorkspaceShortcuts()
{
    const projectname::WorkspaceCommandAvailability allAvailable {
        true,
        true,
        true,
        true,
        true,
        true,
    };

    const auto previous = projectname::routeWorkspaceCommand(
        { projectname::WorkspaceCommandKey::left, false },
        allAvailable);
    expect(previous == projectname::WorkspaceCommand::selectPreviousClip,
           "Workspace command router maps left to previous clip selection");

    const auto next = projectname::routeWorkspaceCommand(
        { projectname::WorkspaceCommandKey::right, false },
        allAvailable);
    expect(next == projectname::WorkspaceCommand::selectNextClip,
           "Workspace command router maps right to next clip selection");

    const auto panLeft = projectname::routeWorkspaceCommand(
        { projectname::WorkspaceCommandKey::left, true },
        allAvailable);
    expect(panLeft == projectname::WorkspaceCommand::panViewportLeft,
           "Workspace command router maps command-left to viewport pan left");

    const auto panRight = projectname::routeWorkspaceCommand(
        { projectname::WorkspaceCommandKey::right, true },
        allAvailable);
    expect(panRight == projectname::WorkspaceCommand::panViewportRight,
           "Workspace command router maps command-right to viewport pan right");

    const auto zoomIn = projectname::routeWorkspaceCommand(
        { projectname::WorkspaceCommandKey::up, true },
        allAvailable);
    expect(zoomIn == projectname::WorkspaceCommand::zoomViewportIn,
           "Workspace command router maps command-up to viewport zoom in");

    const auto zoomOut = projectname::routeWorkspaceCommand(
        { projectname::WorkspaceCommandKey::down, true },
        allAvailable);
    expect(zoomOut == projectname::WorkspaceCommand::zoomViewportOut,
           "Workspace command router maps command-down to viewport zoom out");

    projectname::WorkspaceCommandAvailability selectionOnly;
    selectionOnly.canSelectPreviousClip = true;
    selectionOnly.canSelectNextClip = true;
    const auto commandLeftWithoutPan = projectname::routeWorkspaceCommand(
        { projectname::WorkspaceCommandKey::left, true },
        selectionOnly);
    expect(!commandLeftWithoutPan.has_value(),
           "Workspace command router keeps command-left from falling back to plain selection");

    const auto unavailablePlainLeft = projectname::routeWorkspaceCommand(
        { projectname::WorkspaceCommandKey::left, false },
        {});
    expect(!unavailablePlainLeft.has_value(),
           "Workspace command router ignores unavailable plain shortcuts");

    const auto unrelatedKey = projectname::routeWorkspaceCommand(
        { projectname::WorkspaceCommandKey::other, false },
        allAvailable);
    expect(!unrelatedKey.has_value(),
           "Workspace command router ignores unrelated keys");
}

void appCommandRegistryDescribesPrototypeTopBarCommands()
{
    const auto registry = projectname::makePrototypeAppCommandRegistry();
    expect(registry.size() == 16, "App command registry exposes the prototype app actions");

    auto expectCommand = [&registry](std::string_view id,
                                     std::string_view label,
                                     projectname::AppCommandScope scope,
                                     bool enabled,
                                     const char* message)
    {
        const auto* command = registry.findCommand(id);
        expect(command != nullptr, message);
        if (command == nullptr)
            return;

        expect(command->metadata.id == id, "App command registry preserves stable command id");
        expect(command->metadata.label == label, "App command registry preserves command label");
        expect(!command->metadata.description.empty(), "App command registry stores a command description");
        expect(command->metadata.scope == scope, "App command registry stores command scope");
        expect(command->enabled == enabled, "App command registry stores command enablement");
        if (!enabled)
            expect(!command->disabledReason.empty(), "Disabled app command carries status text");
    };

    expectCommand(projectname::AppCommandIds::transportPlay,
                  "Play",
                  projectname::AppCommandScope::transport,
                  true,
                  "App command registry contains Play");
    expectCommand(projectname::AppCommandIds::transportStop,
                  "Stop",
                  projectname::AppCommandScope::transport,
                  true,
                  "App command registry contains Stop");
    expectCommand(projectname::AppCommandIds::projectNew,
                  "New Project",
                  projectname::AppCommandScope::project,
                  true,
                  "App command registry contains New Project");
    expectCommand(projectname::AppCommandIds::projectSave,
                  "Save",
                  projectname::AppCommandScope::project,
                  true,
                  "App command registry contains Save");
    expectCommand(projectname::AppCommandIds::projectSaveAs,
                  "Save As",
                  projectname::AppCommandScope::project,
                  true,
                  "App command registry contains Save As");
    expectCommand(projectname::AppCommandIds::projectSaveAsCancel,
                  "Cancel Save As",
                  projectname::AppCommandScope::project,
                  false,
                  "App command registry contains Cancel Save As");
    expectCommand(projectname::AppCommandIds::projectCopyFailedSaveAsTarget,
                  "Copy Failed Save As Target",
                  projectname::AppCommandScope::project,
                  false,
                  "App command registry contains failed Save As target copy");
    expectCommand(projectname::AppCommandIds::projectRetryFailedSaveAsTargetManifest,
                  "Retry Failed Save As",
                  projectname::AppCommandScope::project,
                  false,
                  "App command registry contains failed Save As manifest retry");
    expectCommand(projectname::AppCommandIds::projectOpen,
                  "Open",
                  projectname::AppCommandScope::project,
                  true,
                  "App command registry contains Open");
    expectCommand(projectname::AppCommandIds::editUndo,
                  "Undo",
                  projectname::AppCommandScope::project,
                  false,
                  "App command registry contains Undo");
    expectCommand(projectname::AppCommandIds::editRedo,
                  "Redo",
                  projectname::AppCommandScope::project,
                  false,
                  "App command registry contains Redo");
    expectCommand(projectname::AppCommandIds::audioImport,
                  "Import Audio",
                  projectname::AppCommandScope::project,
                  true,
                  "App command registry contains Import Audio");
    expectCommand(projectname::AppCommandIds::audioImportCancel,
                  "Cancel Import",
                  projectname::AppCommandScope::project,
                  false,
                  "App command registry contains Cancel Import");
    expectCommand(projectname::AppCommandIds::timelinePreparationCancel,
                  "Cancel Timeline Preparation",
                  projectname::AppCommandScope::transport,
                  false,
                  "App command registry contains Cancel Timeline Preparation");
    expectCommand(projectname::AppCommandIds::audioSettingsShow,
                  "Audio/MIDI Settings",
                  projectname::AppCommandScope::audioDevice,
                  true,
                  "App command registry contains Audio/MIDI Settings");
    expectCommand(projectname::AppCommandIds::audioSettingsReset,
                  "Reset Audio/MIDI Preferences",
                  projectname::AppCommandScope::audioDevice,
                  true,
                  "App command registry contains Reset Audio/MIDI Preferences");

    expect(registry.findCommand("missing.command") == nullptr,
           "App command registry returns null for unknown commands");
    expect(!registry.isEnabled("missing.command"),
           "App command registry reports unknown commands unavailable");

    projectname::AppCommandAvailability availability;
    availability.canNewProject = false;
    availability.canSaveAs = false;
    availability.canOpen = false;
    availability.canCancelSaveAs = true;
    availability.canCopyFailedSaveAsTarget = true;
    availability.canRetryFailedSaveAsTargetManifest = true;
    availability.canUndoImportedClipEdit = true;
    availability.canRedoImportedClipEdit = true;
    availability.canImportAudio = false;
    availability.canCancelImport = true;
    availability.canCancelTimelinePreparation = true;
    availability.canResetAudioSettings = false;

    const auto busyRegistry = projectname::makePrototypeAppCommandRegistry(availability);
    expect(busyRegistry.isEnabled(projectname::AppCommandIds::editUndo),
           "App command registry enables undo from imported clip edit availability");
    expect(busyRegistry.isEnabled(projectname::AppCommandIds::editRedo),
           "App command registry enables redo from imported clip edit availability");
    expect(!busyRegistry.isEnabled(projectname::AppCommandIds::projectNew),
           "App command registry disables New Project from project availability");
    expect(!busyRegistry.isEnabled(projectname::AppCommandIds::projectSaveAs),
           "App command registry disables Save As from project availability");
    expect(busyRegistry.isEnabled(projectname::AppCommandIds::projectSaveAsCancel),
           "App command registry enables cancel Save As from availability");
    expect(busyRegistry.isEnabled(projectname::AppCommandIds::projectCopyFailedSaveAsTarget),
           "App command registry enables failed Save As target copy from availability");
    expect(busyRegistry.isEnabled(projectname::AppCommandIds::projectRetryFailedSaveAsTargetManifest),
           "App command registry enables failed Save As manifest retry from availability");
    expect(!busyRegistry.isEnabled(projectname::AppCommandIds::projectOpen),
           "App command registry disables Open from project availability");
    expect(!busyRegistry.isEnabled(projectname::AppCommandIds::audioImport),
           "App command registry disables import while import UI or job is active");
    expect(busyRegistry.isEnabled(projectname::AppCommandIds::audioImportCancel),
           "App command registry enables cancel import from availability");
    expect(busyRegistry.isEnabled(projectname::AppCommandIds::timelinePreparationCancel),
           "App command registry enables cancel timeline preparation from availability");
    expect(!busyRegistry.isEnabled(projectname::AppCommandIds::audioSettingsReset),
           "App command registry disables Audio/MIDI preference reset from availability");

    const auto* disabledImport = busyRegistry.findCommand(projectname::AppCommandIds::audioImport);
    expect(disabledImport != nullptr && !disabledImport->disabledReason.empty(),
           "Disabled import command keeps a status reason");

    const auto* enabledUndo = busyRegistry.findCommand(projectname::AppCommandIds::editUndo);
    expect(enabledUndo != nullptr && enabledUndo->disabledReason.empty(),
           "Enabled undo command clears stale disabled status text");

    const auto* enabledCancel = busyRegistry.findCommand(projectname::AppCommandIds::audioImportCancel);
    expect(enabledCancel != nullptr && enabledCancel->disabledReason.empty(),
           "Enabled cancel command clears stale disabled status text");

    const auto* disabledAudioReset = busyRegistry.findCommand(projectname::AppCommandIds::audioSettingsReset);
    expect(disabledAudioReset != nullptr && !disabledAudioReset->disabledReason.empty(),
           "Disabled Audio/MIDI reset command keeps a status reason");

    const auto* enabledSaveAsCancel = busyRegistry.findCommand(projectname::AppCommandIds::projectSaveAsCancel);
    expect(enabledSaveAsCancel != nullptr && enabledSaveAsCancel->disabledReason.empty(),
           "Enabled Save As cancel command clears stale disabled status text");

    const auto* enabledFailedSaveAsTarget =
        busyRegistry.findCommand(projectname::AppCommandIds::projectCopyFailedSaveAsTarget);
    expect(enabledFailedSaveAsTarget != nullptr && enabledFailedSaveAsTarget->disabledReason.empty(),
           "Enabled failed Save As target copy command clears stale disabled status text");

    const auto* enabledFailedSaveAsRetry =
        busyRegistry.findCommand(projectname::AppCommandIds::projectRetryFailedSaveAsTargetManifest);
    expect(enabledFailedSaveAsRetry != nullptr && enabledFailedSaveAsRetry->disabledReason.empty(),
           "Enabled failed Save As manifest retry command clears stale disabled status text");

    projectname::AppCommandRegistry manualRegistry;
    expect(manualRegistry.registerCommand({ "app.test",
                                            "Test",
                                            "Test command.",
                                            projectname::AppCommandScope::app },
                                          true),
           "App command registry accepts a new command id");
    expect(!manualRegistry.registerCommand({ "app.test",
                                             "Duplicate Test",
                                             "Duplicate command.",
                                             projectname::AppCommandScope::app },
                                           true),
           "App command registry rejects duplicate command ids");
    expect(manualRegistry.size() == 1,
           "App command registry keeps duplicate rejection from growing the registry");

    projectname::AppCommandDispatcher dispatcher;
    int saveHandlerCallCount = 0;
    expect(dispatcher.registerHandler("project.save",
                                      [&saveHandlerCallCount]()
                                      {
                                          ++saveHandlerCallCount;
                                          return projectname::AppCommandResult::handledWithStatus("Saved");
                                      }),
           "App command dispatcher accepts a handler");
    expect(!dispatcher.registerHandler("project.save",
                                       []()
                                       {
                                           return projectname::AppCommandResult::handled();
                                       }),
           "App command dispatcher rejects duplicate handlers");

    const auto handled = dispatcher.dispatch(registry, projectname::AppCommandIds::projectSave);
    expect(handled.status == projectname::AppCommandResultStatus::handledWithStatus,
           "App command dispatcher returns handler result status");
    expect(handled.message == "Saved",
           "App command dispatcher returns handler result message");
    expect(saveHandlerCallCount == 1,
           "App command dispatcher invokes enabled command handlers");

    const auto disabledCancel = dispatcher.dispatch(registry, projectname::AppCommandIds::audioImportCancel);
    expect(disabledCancel.status == projectname::AppCommandResultStatus::disabled,
           "App command dispatcher reports disabled registry commands");
    expect(!disabledCancel.message.empty(),
           "App command dispatcher returns disabled command reason");
    expect(saveHandlerCallCount == 1,
           "App command dispatcher does not invoke other handlers for disabled commands");

    const auto missingHandler = dispatcher.dispatch(registry, projectname::AppCommandIds::transportPlay);
    expect(missingHandler.status == projectname::AppCommandResultStatus::failed,
           "App command dispatcher reports enabled commands without registered handlers");

    const auto unknownCommand = dispatcher.dispatch(registry, "app.unknown");
    expect(unknownCommand.status == projectname::AppCommandResultStatus::failed,
           "App command dispatcher reports unknown command ids");
}

void audioSetupStatusModelsFirstRunReadyAndErrorStates()
{
    projectname::AudioSetupStatusRequest request;
    request.outputDeviceOpen = true;
    request.outputChannelCount = 2;
    request.sampleRateHz = 48000.0;
    request.bufferSizeSamples = 256;
    request.outputDeviceName = "Studio Output";

    const auto firstRun = projectname::buildAudioSetupStatusViewModel(request);
    expect(firstRun.kind == projectname::AudioSetupStatusKind::firstRun,
           "Audio setup model shows a first-run state for an open output");
    expect(firstRun.setupActionVisible, "Audio setup model keeps setup action visible on first run");
    expect(firstRun.dismissActionVisible, "Audio setup model exposes a first-run dismiss action");
    expect(!firstRun.needsAttention, "Audio setup first-run model does not report an error");
    expect(firstRun.lines.size() == 4, "Audio setup first-run model includes output details and a test cue");

    request.firstRunPromptDismissed = true;
    const auto ready = projectname::buildAudioSetupStatusViewModel(request);
    expect(ready.kind == projectname::AudioSetupStatusKind::ready,
           "Audio setup model shows ready after first-run dismissal");
    expect(ready.setupActionVisible, "Audio setup model keeps setup action visible after dismissal");
    expect(!ready.dismissActionVisible, "Audio setup model hides dismiss action after dismissal");
    expect(!ready.needsAttention, "Audio setup ready model does not need attention");

    request.outputDeviceOpen = false;
    request.outputChannelCount = 0;
    request.initializationError.clear();
    const auto unavailable = projectname::buildAudioSetupStatusViewModel(request);
    expect(unavailable.kind == projectname::AudioSetupStatusKind::unavailable,
           "Audio setup model reports unavailable output without initialization error");
    expect(unavailable.setupActionVisible, "Audio setup unavailable model keeps setup action visible");
    expect(unavailable.needsAttention, "Audio setup unavailable model needs attention");

    request.initializationError = "No audio device";
    const auto failed = projectname::buildAudioSetupStatusViewModel(request);
    expect(failed.kind == projectname::AudioSetupStatusKind::initializationFailed,
           "Audio setup model reports initialization failure");
    expect(failed.setupActionVisible, "Audio setup failure model keeps setup action visible");
    expect(failed.needsAttention, "Audio setup failure model needs attention");
    expect(!failed.lines.empty() && failed.lines[1].find("No audio device") != std::string::npos,
           "Audio setup failure model keeps the initialization error visible");

    request.outputDeviceOpen = true;
    request.outputChannelCount = 2;
    const auto recovered = projectname::buildAudioSetupStatusViewModel(request);
    expect(recovered.kind == projectname::AudioSetupStatusKind::ready,
           "Audio setup model treats a recovered open output as ready despite a stale init error");

    request.initializationError.clear();
    request.settingsLoadError = "Could not parse app settings file.";
    const auto settingsWarning = projectname::buildAudioSetupStatusViewModel(request);
    const auto warningLine = std::find_if(settingsWarning.lines.begin(),
                                          settingsWarning.lines.end(),
                                          [](const std::string& line)
                                          {
                                              return line.find("Settings ignored: Could not parse app settings file.")
                                                  != std::string::npos;
                                          });
    expect(warningLine != settingsWarning.lines.end(),
           "Audio setup model surfaces ignored app settings as visible Device Panel copy");
    expect(settingsWarning.needsAttention,
           "Audio setup model marks ignored app settings as needing attention");
}

void appCommandRoutesImportedClipEditUndoRedo()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    projectname::AppSession session(std::move(project));

    auto makeRegistry = [&session]()
    {
        projectname::AppCommandAvailability availability;
        availability.canUndoImportedClipEdit = session.canUndoImportedClipEdit();
        availability.canRedoImportedClipEdit = session.canRedoImportedClipEdit();
        return projectname::makePrototypeAppCommandRegistry(availability);
    };

    projectname::AppCommandDispatcher dispatcher;
    expect(dispatcher.registerHandler(std::string(projectname::AppCommandIds::editUndo),
                                      [&session]()
                                      {
                                          std::string error;
                                          if (!session.undoImportedClipEdit(error))
                                              return projectname::AppCommandResult::failed(error);

                                          return projectname::AppCommandResult::handledWithStatus("Undo");
                                      }),
           "App command dispatcher accepts imported clip edit undo handler");
    expect(dispatcher.registerHandler(std::string(projectname::AppCommandIds::editRedo),
                                      [&session]()
                                      {
                                          std::string error;
                                          if (!session.redoImportedClipEdit(error))
                                              return projectname::AppCommandResult::failed(error);

                                          return projectname::AppCommandResult::handledWithStatus("Redo");
                                      }),
           "App command dispatcher accepts imported clip edit redo handler");

    auto registry = makeRegistry();
    expect(!registry.isEnabled(projectname::AppCommandIds::editUndo),
           "App command registry disables imported clip undo with empty history");
    expect(!registry.isEnabled(projectname::AppCommandIds::editRedo),
           "App command registry disables imported clip redo with empty history");
    auto disabledUndo = dispatcher.dispatch(registry, projectname::AppCommandIds::editUndo);
    expect(disabledUndo.status == projectname::AppCommandResultStatus::disabled,
           "App command dispatcher blocks disabled imported clip undo command");

    std::string error;
    expect(session.setImportedAudioClipStartBeats(clipId, 8.0, error),
           "App command undo test records imported clip placement edit");
    expect(session.getNextImportedClipUndoEditKind() == projectname::ImportedClipEditKind::placement,
           "App command undo test sees placement edit on undo stack");

    registry = makeRegistry();
    expect(registry.isEnabled(projectname::AppCommandIds::editUndo),
           "App command registry enables imported clip undo for placement edit");
    expect(!registry.isEnabled(projectname::AppCommandIds::editRedo),
           "App command registry leaves redo disabled before undo");
    auto undoPlacement = dispatcher.dispatch(registry, projectname::AppCommandIds::editUndo);
    expect(undoPlacement.status == projectname::AppCommandResultStatus::handledWithStatus,
           "App command dispatcher routes imported clip placement undo");
    auto* clip = findClipById(session.getProject(), clipId);
    expect(clip != nullptr && std::abs(clip->startBeats - 4.0) < 0.0001,
           "App command placement undo restores start beat");
    expect(session.getNextImportedClipRedoEditKind() == projectname::ImportedClipEditKind::placement,
           "App command undo test sees placement edit on redo stack");

    registry = makeRegistry();
    expect(!registry.isEnabled(projectname::AppCommandIds::editUndo),
           "App command registry disables undo after the only placement edit is undone");
    expect(registry.isEnabled(projectname::AppCommandIds::editRedo),
           "App command registry enables redo after placement undo");
    auto redoPlacement = dispatcher.dispatch(registry, projectname::AppCommandIds::editRedo);
    expect(redoPlacement.status == projectname::AppCommandResultStatus::handledWithStatus,
           "App command dispatcher routes imported clip placement redo");
    clip = findClipById(session.getProject(), clipId);
    expect(clip != nullptr && std::abs(clip->startBeats - 8.0) < 0.0001,
           "App command placement redo reapplies start beat");

    expect(session.replaceImportedAudioClipMedia(clipId,
                                                 "audio/command-relinked.wav",
                                                 "analysis/command-relinked.waveform.json",
                                                 3.25,
                                                 error),
           "App command undo test records imported clip media replacement edit");
    expect(session.getNextImportedClipUndoEditKind() == projectname::ImportedClipEditKind::mediaReplacement,
           "App command undo test sees media replacement edit on top of undo stack");
    expect(!session.canUndoImportedClipPlacementEdit()
               && session.canUndoImportedClipMediaReplacementEdit(),
           "App command undo test keeps top-of-stack type-specific enablement precise");

    registry = makeRegistry();
    auto undoMedia = dispatcher.dispatch(registry, projectname::AppCommandIds::editUndo);
    expect(undoMedia.status == projectname::AppCommandResultStatus::handledWithStatus,
           "App command dispatcher routes imported clip media replacement undo");
    clip = findClipById(session.getProject(), clipId);
    expect(clip != nullptr && clip->relativePath == "audio/timeline-clip.wav",
           "App command media undo restores media path");
    expect(clip != nullptr && clip->analysisPath == "analysis/timeline-clip.waveform.json",
           "App command media undo restores analysis path");
    expect(clip != nullptr && std::abs(clip->lengthBeats - 2.0) < 0.0001,
           "App command media undo restores clip length");
    expect(session.getNextImportedClipUndoEditKind() == projectname::ImportedClipEditKind::placement,
           "App command media undo reveals previous placement edit on undo stack");
    expect(session.getNextImportedClipRedoEditKind() == projectname::ImportedClipEditKind::mediaReplacement,
           "App command media undo moves media replacement edit to redo stack");

    registry = makeRegistry();
    auto redoMedia = dispatcher.dispatch(registry, projectname::AppCommandIds::editRedo);
    expect(redoMedia.status == projectname::AppCommandResultStatus::handledWithStatus,
           "App command dispatcher routes imported clip media replacement redo");
    clip = findClipById(session.getProject(), clipId);
    expect(clip != nullptr && clip->relativePath == "audio/command-relinked.wav",
           "App command media redo reapplies media path");
    expect(clip != nullptr && clip->analysisPath == "analysis/command-relinked.waveform.json",
           "App command media redo reapplies analysis path");
    expect(clip != nullptr && std::abs(clip->lengthBeats - 3.25) < 0.0001,
           "App command media redo reapplies clip length");
}

void appSessionSelectsImportedAudioClips()
{
    auto project = projectname::ProjectModel::createDefault();

    projectname::ProjectClip late;
    late.id = "clip-session-selection-late";
    late.name = "Session Selection Late";
    late.type = "audio-file";
    late.relativePath = "audio/session-selection-late.wav";
    late.analysisPath = "analysis/session-selection-late.waveform.json";
    late.startBeats = 8.0;
    late.lengthBeats = 1.0;

    projectname::ProjectClip middle;
    middle.id = "clip-session-selection-middle";
    middle.name = "Session Selection Middle";
    middle.type = "audio-file";
    middle.relativePath = "audio/session-selection-middle.wav";
    middle.analysisPath = "analysis/session-selection-middle.waveform.json";
    middle.startBeats = 4.0;
    middle.lengthBeats = 1.0;

    projectname::ProjectClip early;
    early.id = "clip-session-selection-early";
    early.name = "Session Selection Early";
    early.type = "audio-file";
    early.relativePath = "audio/session-selection-early.wav";
    early.analysisPath = "analysis/session-selection-early.waveform.json";
    early.startBeats = 1.0;
    early.lengthBeats = 1.0;

    expect(project.addClipToTrack("track-1", late),
           "Session selection test adds late imported clip");
    expect(project.addClipToTrack("track-1", middle),
           "Session selection test adds middle imported clip");
    expect(project.addClipToTrack("track-1", early),
           "Session selection test adds early imported clip");

    projectname::AppSession session(std::move(project));
    std::string error;
    expect(session.selectImportedAudioClip(middle.id, error),
           "Session selects imported audio clip");
    expect(session.getSelectedClipId() == middle.id,
           "Session exposes selected clip id");

    expect(!session.selectImportedAudioClip("clip-1", error),
           "Session rejects generated clip selection");
    expect(session.getSelectedClipId() == middle.id,
           "Session keeps previous selection after generated clip rejection");

    expect(session.selectAdjacentImportedAudioClip(projectname::ImportedAudioClipSelectionDirection::next, error),
           "Session selects next imported clip");
    expect(session.getSelectedClipId() == late.id,
           "Session next selection follows timeline order");

    expect(session.selectAdjacentImportedAudioClip(projectname::ImportedAudioClipSelectionDirection::next, error),
           "Session wraps next imported clip selection");
    expect(session.getSelectedClipId() == early.id,
           "Session next selection wraps to earliest imported clip");

    expect(session.selectAdjacentImportedAudioClip(projectname::ImportedAudioClipSelectionDirection::previous, error),
           "Session wraps previous imported clip selection");
    expect(session.getSelectedClipId() == late.id,
           "Session previous selection wraps to latest imported clip");

    session.clearSelectedClip();
    expect(session.getSelectedClipId().empty(),
           "Session clears selected clip id");

    expect(session.selectAdjacentImportedAudioClip(projectname::ImportedAudioClipSelectionDirection::next, error),
           "Session adjacent selection recovers from empty selection");
    expect(session.getSelectedClipId() == early.id,
           "Session empty next selection chooses earliest imported clip");

    projectname::AppSession noImportedSession;
    expect(!noImportedSession.selectAdjacentImportedAudioClip(projectname::ImportedAudioClipSelectionDirection::next,
                                                             error),
           "Session rejects adjacent selection with no imported clips");
    expect(noImportedSession.getSelectedClipId().empty(),
           "Session adjacent selection failure leaves selection empty");
}

void appSessionTracksImportedClipPlacementUndoHistory()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    projectname::AppSession session(std::move(project));
    std::string error;

    expect(!session.canUndoImportedClipPlacementEdit(),
           "Session starts with no imported clip placement undo history");
    expect(!session.canRedoImportedClipPlacementEdit(),
           "Session starts with no imported clip placement redo history");
    expect(!session.undoImportedClipPlacementEdit(error),
           "Session rejects imported clip placement undo with empty history");
    expect(!session.redoImportedClipPlacementEdit(error),
           "Session rejects imported clip placement redo with empty history");

    expect(!session.setImportedAudioClipStartBeats(clipId, -1.0, error),
           "Session placement undo history ignores failed placement edits");
    expect(!session.canUndoImportedClipPlacementEdit(),
           "Session failed placement edit does not record undo history");

    expect(session.setImportedAudioClipStartBeats(clipId, 4.0, error),
           "Session placement undo history accepts no-op placement edit");
    expect(!session.canUndoImportedClipPlacementEdit(),
           "Session no-op placement edit does not record undo history");

    expect(session.setImportedAudioClipStartBeats(clipId, 8.0, error),
           "Session placement undo history records moved imported clip");
    auto* movedClip = findClipById(session.getProject(), clipId);
    expect(movedClip != nullptr && std::abs(movedClip->startBeats - 8.0) < 0.0001,
           "Session placement undo history applies moved start beat");
    expect(session.canUndoImportedClipPlacementEdit() && !session.canRedoImportedClipPlacementEdit(),
           "Session placement edit enables undo and clears redo");

    expect(session.undoImportedClipPlacementEdit(error),
           "Session placement undo reverts moved imported clip");
    movedClip = findClipById(session.getProject(), clipId);
    expect(movedClip != nullptr && std::abs(movedClip->startBeats - 4.0) < 0.0001,
           "Session placement undo restores previous start beat");
    expect(!session.canUndoImportedClipPlacementEdit() && session.canRedoImportedClipPlacementEdit(),
           "Session placement undo moves edit to redo history");

    expect(session.redoImportedClipPlacementEdit(error),
           "Session placement redo reapplies moved imported clip");
    movedClip = findClipById(session.getProject(), clipId);
    expect(movedClip != nullptr && std::abs(movedClip->startBeats - 8.0) < 0.0001,
           "Session placement redo restores moved start beat");
    expect(session.canUndoImportedClipPlacementEdit() && !session.canRedoImportedClipPlacementEdit(),
           "Session placement redo moves edit back to undo history");

    expect(session.undoImportedClipPlacementEdit(error),
           "Session placement undo prepares redo clearing test");
    expect(session.setImportedAudioClipStartBeats(clipId, 12.0, error),
           "Session placement undo history records new edit after undo");
    movedClip = findClipById(session.getProject(), clipId);
    expect(movedClip != nullptr && std::abs(movedClip->startBeats - 12.0) < 0.0001,
           "Session placement new edit applies after undo");
    expect(session.canUndoImportedClipPlacementEdit() && !session.canRedoImportedClipPlacementEdit(),
           "Session new placement edit clears redo history");
    expect(!session.redoImportedClipPlacementEdit(error),
           "Session cleared placement redo history cannot be replayed");

    expect(session.setImportedAudioClipStartBeats(clipId, 16.0, error),
           "Session placement undo history records stale failure setup edit");
    session.getProject() = projectname::ProjectModel::createDefault();
    const auto staleProjectState = session.getProject();
    expect(!session.undoImportedClipPlacementEdit(error),
           "Session placement undo fails when edited imported clip is stale");
    expect(session.getProject() == staleProjectState,
           "Session stale placement undo leaves project state unchanged");
    expect(session.canUndoImportedClipPlacementEdit(),
           "Session stale placement undo keeps undo entry for retry");

    session.replaceProject(makeProjectWithImportedTimelineClip(2.0, 1.0));
    expect(!session.canUndoImportedClipPlacementEdit() && !session.canRedoImportedClipPlacementEdit(),
           "Session project replacement clears imported clip placement history");
}

void appSessionTracksImportedClipMediaReplacementUndoHistory()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    projectname::AppSession session(std::move(project));
    std::string error;

    expect(!session.canUndoImportedClipMediaReplacementEdit(),
           "Session starts with no imported clip media replacement undo history");
    expect(!session.canRedoImportedClipMediaReplacementEdit(),
           "Session starts with no imported clip media replacement redo history");
    expect(!session.undoImportedClipMediaReplacementEdit(error),
           "Session rejects imported clip media replacement undo with empty history");
    expect(!session.redoImportedClipMediaReplacementEdit(error),
           "Session rejects imported clip media replacement redo with empty history");

    expect(!session.replaceImportedAudioClipMedia("clip-1",
                                                  "audio/rejected-media.wav",
                                                  "analysis/rejected-media.waveform.json",
                                                  1.0,
                                                  error),
           "Session media replacement undo history ignores failed replacement edits");
    expect(!session.canUndoImportedClipMediaReplacementEdit(),
           "Session failed media replacement edit does not record undo history");

    expect(session.replaceImportedAudioClipMedia(clipId,
                                                 "audio/timeline-clip.wav",
                                                 "analysis/timeline-clip.waveform.json",
                                                 2.0,
                                                 error),
           "Session media replacement undo history accepts no-op replacement edit");
    expect(!session.canUndoImportedClipMediaReplacementEdit(),
           "Session no-op media replacement edit does not record undo history");

    expect(session.replaceImportedAudioClipMedia(clipId,
                                                 "audio/relinked.wav",
                                                 "analysis/relinked.waveform.json",
                                                 3.5,
                                                 error),
           "Session media replacement undo history records relinked imported clip");
    auto* relinkedClip = findClipById(session.getProject(), clipId);
    expect(relinkedClip != nullptr && relinkedClip->relativePath == "audio/relinked.wav",
           "Session media replacement undo history applies media path");
    expect(relinkedClip != nullptr && relinkedClip->analysisPath == "analysis/relinked.waveform.json",
           "Session media replacement undo history applies analysis path");
    expect(relinkedClip != nullptr && std::abs(relinkedClip->lengthBeats - 3.5) < 0.0001,
           "Session media replacement undo history applies clip length");
    expect(session.canUndoImportedClipMediaReplacementEdit()
               && !session.canRedoImportedClipMediaReplacementEdit(),
           "Session media replacement edit enables undo and clears redo");

    expect(session.undoImportedClipMediaReplacementEdit(error),
           "Session media replacement undo restores original imported clip media");
    relinkedClip = findClipById(session.getProject(), clipId);
    expect(relinkedClip != nullptr && relinkedClip->relativePath == "audio/timeline-clip.wav",
           "Session media replacement undo restores original media path");
    expect(relinkedClip != nullptr && relinkedClip->analysisPath == "analysis/timeline-clip.waveform.json",
           "Session media replacement undo restores original analysis path");
    expect(relinkedClip != nullptr && std::abs(relinkedClip->lengthBeats - 2.0) < 0.0001,
           "Session media replacement undo restores original clip length");
    expect(!session.canUndoImportedClipMediaReplacementEdit()
               && session.canRedoImportedClipMediaReplacementEdit(),
           "Session media replacement undo moves edit to redo history");

    expect(session.redoImportedClipMediaReplacementEdit(error),
           "Session media replacement redo reapplies relinked imported clip media");
    relinkedClip = findClipById(session.getProject(), clipId);
    expect(relinkedClip != nullptr && relinkedClip->relativePath == "audio/relinked.wav",
           "Session media replacement redo restores relinked media path");
    expect(relinkedClip != nullptr && relinkedClip->analysisPath == "analysis/relinked.waveform.json",
           "Session media replacement redo restores relinked analysis path");
    expect(relinkedClip != nullptr && std::abs(relinkedClip->lengthBeats - 3.5) < 0.0001,
           "Session media replacement redo restores relinked clip length");
    expect(session.canUndoImportedClipMediaReplacementEdit()
               && !session.canRedoImportedClipMediaReplacementEdit(),
           "Session media replacement redo moves edit back to undo history");

    expect(session.undoImportedClipMediaReplacementEdit(error),
           "Session media replacement undo prepares redo clearing test");
    expect(session.replaceImportedAudioClipMedia(clipId,
                                                 "audio/relinked-second.wav",
                                                 "analysis/relinked-second.waveform.json",
                                                 1.25,
                                                 error),
           "Session media replacement undo history records new edit after undo");
    relinkedClip = findClipById(session.getProject(), clipId);
    expect(relinkedClip != nullptr && relinkedClip->relativePath == "audio/relinked-second.wav",
           "Session media replacement new edit applies after undo");
    expect(relinkedClip != nullptr && std::abs(relinkedClip->lengthBeats - 1.25) < 0.0001,
           "Session media replacement new edit stores replacement length");
    expect(session.canUndoImportedClipMediaReplacementEdit()
               && !session.canRedoImportedClipMediaReplacementEdit(),
           "Session new media replacement edit clears redo history");
    expect(!session.redoImportedClipMediaReplacementEdit(error),
           "Session cleared media replacement redo history cannot be replayed");

    expect(session.replaceImportedAudioClipMedia(clipId,
                                                 "audio/relinked-stale.wav",
                                                 "analysis/relinked-stale.waveform.json",
                                                 5.0,
                                                 error),
           "Session media replacement undo history records stale failure setup edit");
    session.getProject() = projectname::ProjectModel::createDefault();
    const auto staleProjectState = session.getProject();
    expect(!session.undoImportedClipMediaReplacementEdit(error),
           "Session media replacement undo fails when edited imported clip is stale");
    expect(session.getProject() == staleProjectState,
           "Session stale media replacement undo leaves project state unchanged");
    expect(session.canUndoImportedClipMediaReplacementEdit(),
           "Session stale media replacement undo keeps undo entry for retry");

    session.replaceProject(makeProjectWithImportedTimelineClip(2.0, 1.0));
    expect(!session.canUndoImportedClipMediaReplacementEdit()
               && !session.canRedoImportedClipMediaReplacementEdit(),
           "Session project replacement clears imported clip media replacement history");
}

void appSessionMediaReplacementUndoInvalidatesPreparedCache()
{
    constexpr auto clipId = "clip-imported-playback";

    auto project = makeProjectWithImportedTimelineClip(4.0, 2.0);
    projectname::AppSession session(std::move(project));
    std::string error;

    const auto* originalClip = findClipById(session.getProject(), clipId);
    expect(originalClip != nullptr, "Session media replacement cache test has original imported clip");
    auto cachedOriginal = originalClip != nullptr
        ? session.cacheImportedTimelineClip(*originalClip, makePreparedMonoCacheClip({ 0.10f, 0.11f, 0.12f }))
        : nullptr;
    expect(cachedOriginal != nullptr && cachedOriginal->size() == 3,
           "Session media replacement cache test stores original prepared clip");

    session.getTransport().setPositionBeats(4.0);
    auto originalHit = session.playFromCachedTimeline(10.0, error);
    expect(originalHit.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session media replacement cache test hits original prepared clip");
    expect(originalHit.preparedSamples != nullptr && std::abs((*originalHit.preparedSamples)[0] - 0.10f) < 0.0001f,
           "Session media replacement cache test returns original prepared samples");

    expect(session.replaceImportedAudioClipMedia(clipId,
                                                 "audio/timeline-clip.wav",
                                                 "analysis/timeline-clip.waveform.json",
                                                 2.0,
                                                 error),
           "Session media replacement cache test accepts same-path no-op replacement");
    session.getTransport().setPositionBeats(4.0);
    auto noOpHit = session.playFromCachedTimeline(10.0, error);
    expect(noOpHit.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session media replacement no-op preserves prepared cache");
    expect(noOpHit.preparedSamples != nullptr && std::abs((*noOpHit.preparedSamples)[0] - 0.10f) < 0.0001f,
           "Session media replacement no-op keeps original prepared samples");

    expect(session.replaceImportedAudioClipMedia(clipId,
                                                 "audio/timeline-clip.wav",
                                                 "analysis/timeline-clip-replaced.waveform.json",
                                                 4.0,
                                                 error),
           "Session media replacement cache test records same-path replacement");
    session.getTransport().setPositionBeats(4.0);
    auto replacedMiss = session.playFromCachedTimeline(10.0, error);
    expect(replacedMiss.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session media replacement clears same-path stale cache on forward edit");
    expect(!replacedMiss.usedCachedBuffer,
           "Session media replacement forward cache invalidation does not reuse stale samples");

    const auto* replacedClip = findClipById(session.getProject(), clipId);
    expect(replacedClip != nullptr
               && replacedClip->analysisPath == "analysis/timeline-clip-replaced.waveform.json"
               && std::abs(replacedClip->lengthBeats - 4.0) < 0.0001,
           "Session media replacement cache test stores same-path replacement metadata");
    auto cachedReplacement = replacedClip != nullptr
        ? session.cacheImportedTimelineClip(*replacedClip, makePreparedMonoCacheClip({ 0.20f, 0.21f, 0.22f }))
        : nullptr;
    expect(cachedReplacement != nullptr && cachedReplacement->size() == 3,
           "Session media replacement cache test stores replacement prepared clip");

    session.getTransport().setPositionBeats(4.0);
    auto replacementHit = session.playFromCachedTimeline(10.0, error);
    expect(replacementHit.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session media replacement cache test hits replacement prepared clip");
    expect(replacementHit.preparedSamples != nullptr
               && std::abs((*replacementHit.preparedSamples)[0] - 0.20f) < 0.0001f,
           "Session media replacement cache test returns replacement prepared samples");

    expect(session.undoImportedClipMediaReplacementEdit(error),
           "Session media replacement cache test undoes same-path replacement");
    session.getTransport().setPositionBeats(4.0);
    auto undoMiss = session.playFromCachedTimeline(10.0, error);
    expect(undoMiss.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session media replacement undo clears same-path replacement cache");
    expect(!undoMiss.usedCachedBuffer,
           "Session media replacement undo cache invalidation does not reuse stale replacement samples");

    const auto* restoredClip = findClipById(session.getProject(), clipId);
    expect(restoredClip != nullptr
               && restoredClip->analysisPath == "analysis/timeline-clip.waveform.json"
               && std::abs(restoredClip->lengthBeats - 2.0) < 0.0001,
           "Session media replacement cache test restores original same-path metadata");
    auto cachedRestored = restoredClip != nullptr
        ? session.cacheImportedTimelineClip(*restoredClip, makePreparedMonoCacheClip({ 0.30f, 0.31f, 0.32f }))
        : nullptr;
    expect(cachedRestored != nullptr && cachedRestored->size() == 3,
           "Session media replacement cache test stores restored prepared clip");

    session.getTransport().setPositionBeats(4.0);
    auto restoredHit = session.playFromCachedTimeline(10.0, error);
    expect(restoredHit.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session media replacement cache test hits restored prepared clip");
    expect(restoredHit.preparedSamples != nullptr
               && std::abs((*restoredHit.preparedSamples)[0] - 0.30f) < 0.0001f,
           "Session media replacement cache test returns restored prepared samples");

    expect(session.redoImportedClipMediaReplacementEdit(error),
           "Session media replacement cache test redoes same-path replacement");
    session.getTransport().setPositionBeats(4.0);
    auto redoMiss = session.playFromCachedTimeline(10.0, error);
    expect(redoMiss.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session media replacement redo clears same-path restored cache");
    expect(!redoMiss.usedCachedBuffer,
           "Session media replacement redo cache invalidation does not reuse stale restored samples");
}

void appSessionUpdatesStaticTrackMixStateThroughCommand()
{
    projectname::AppSession session;
    std::string error;

    expect(session.setTrackMixState("track-1", 0.75f, -0.5f, true, false, error),
           "Session updates static track mix state");
    expect(error.empty(), "Session track mix update success leaves error empty");

    const auto& track = session.getProject().getTracks().front();
    expect(std::abs(track.volume - 0.75f) < 0.0001f,
           "Session track mix update stores volume");
    expect(std::abs(track.pan - -0.5f) < 0.0001f,
           "Session track mix update stores pan");
    expect(track.muted && !track.solo,
           "Session track mix update stores mute and solo flags");

    const auto previousTrack = track;
    expect(!session.setTrackMixState("track-1", -0.01f, 0.0f, false, false, error),
           "Session rejects invalid negative track volume");
    expect(session.getProject().getTracks().front() == previousTrack,
           "Session invalid volume leaves track mix state unchanged");

    expect(!session.setTrackMixState("track-1", 0.5f, 2.0f, false, false, error),
           "Session rejects invalid track pan");
    expect(session.getProject().getTracks().front() == previousTrack,
           "Session invalid pan leaves track mix state unchanged");

    expect(!session.setTrackMixState("missing-track", 0.5f, 0.0f, false, false, error),
           "Session reports missing track for static mix update");
    expect(error.find("Track") != std::string::npos,
           "Session missing-track mix update returns readable error");
}

void appSessionSavesAndLoadsProjectPackages()
{
    projectname::AppSession session;
    session.getProject().setName("Session Save Test");
    session.setTempoBpm(111.0);
    expect(session.setTimeSignature(5, 4), "Session save test signature accepted");
    session.play();
    session.advanceSeconds(2.0);

    const auto package = makeTemporaryPackagePath("projectname-session-test");

    std::string error;
    expect(session.saveProjectPackage(package, error), "Session project package saves");
    expect(std::filesystem::is_regular_file(package / "manifest.json"), "Session manifest exists");

    projectname::AppSession loadedSession;
    expect(loadedSession.loadProjectPackage(package, error), "Session project package loads");
    expect(loadedSession.getProject() == session.getProject(), "Loaded session project matches saved project");
    expect(!loadedSession.shouldPlayGeneratedTone(), "Loaded session does not resume generated tone");

    expect(std::filesystem::remove_all(package) > 0, "Temporary session project package deleted");
}

void appSessionSaveManifestSymlinkFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Before Session Manifest Symlink Failure");
    session.setTempoBpm(109.0);

    const auto package =
        makeTemporaryPackagePath("projectname-session-save-manifest-symlink-test");

    std::string error;
    expect(session.saveProjectPackage(package, error),
           "Initial session manifest-symlink package save succeeds");

    const auto manifestPath = package / "manifest.json";
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto linkedManifestTarget =
        makeTemporarySettingsPath("projectname-session-save-manifest-symlink-target-test");
    const auto originalManifestText = readTextFile(manifestPath);
    expect(originalManifestText.find("Before Session Manifest Symlink Failure") != std::string::npos,
           "Session manifest-symlink fixture starts with the original manifest state");

    writeTextFile(linkedManifestTarget, originalManifestText);
    writeTextFile(temporaryManifestPath, "stale temporary manifest before session save failure");
    expect(std::filesystem::remove(manifestPath),
           "Session manifest-symlink fixture removes the package manifest before linking");

    std::error_code symlinkError;
    std::filesystem::create_symlink(linkedManifestTarget, manifestPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        std::filesystem::remove(linkedManifestTarget);
        return;
    }

    session.getProject().setName("After Session Manifest Symlink Failure");
    session.setTempoBpm(159.0);
    const auto attemptedSessionProject = session.getProject();

    error = "stale session manifest symlink save error";
    expect(!session.saveProjectPackage(package, error),
           "Session save rejects a symlink manifest path");
    expect(error.find("manifest") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "Session manifest symlink save failure error is human-readable");
    expect(session.getProject() == attemptedSessionProject,
           "Session manifest symlink save failure leaves the session project unchanged");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(manifestPath)),
           "Session manifest symlink save failure leaves the manifest symlink unchanged");
    expect(readTextFile(linkedManifestTarget) == originalManifestText,
           "Session manifest symlink save failure preserves the linked manifest target");
    expect(readTextFile(linkedManifestTarget).find("After Session Manifest Symlink Failure") == std::string::npos,
           "Session manifest symlink save failure does not write the new project through the symlink");
    expect(!std::filesystem::exists(temporaryManifestPath),
           "Session manifest symlink save failure removes stale temporary manifest");
    expect(!std::filesystem::exists(package / "backups" / "manifest.previous.json"),
           "Session manifest symlink save failure happens before backup creation");

    expect(std::filesystem::remove(manifestPath),
           "Temporary session manifest symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary session manifest-symlink package deleted");
    expect(std::filesystem::remove(linkedManifestTarget),
           "Temporary session manifest-symlink target deleted");
}

void appSessionSaveBrokenManifestSymlinkFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Before Session Broken Manifest Symlink Failure");
    session.setTempoBpm(113.0);

    const auto package =
        makeTemporaryPackagePath("projectname-session-save-broken-manifest-symlink-test");

    std::string error;
    expect(session.saveProjectPackage(package, error),
           "Initial session broken-manifest-symlink package save succeeds");

    const auto manifestPath = package / "manifest.json";
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto brokenManifestTarget = package / "missing-session-manifest-target.json";
    const auto originalManifestText = readTextFile(manifestPath);
    expect(originalManifestText.find("Before Session Broken Manifest Symlink Failure") != std::string::npos,
           "Session broken-manifest-symlink fixture starts with the original manifest state");

    writeTextFile(package / "audio" / "session-sentinel.txt", "session audio sentinel");
    writeTextFile(temporaryManifestPath, "stale temporary manifest before session broken symlink failure");
    expect(std::filesystem::remove(manifestPath),
           "Session broken-manifest-symlink fixture removes the package manifest before linking");

    std::error_code symlinkError;
    std::filesystem::create_symlink(brokenManifestTarget, manifestPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        return;
    }

    session.getProject().setName("After Session Broken Manifest Symlink Failure");
    session.setTempoBpm(163.0);
    const auto attemptedSessionProject = session.getProject();

    error = "stale session broken manifest symlink save error";
    expect(!session.saveProjectPackage(package, error),
           "Session save rejects a broken symlink manifest path");
    expect(error.find("manifest") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "Session broken manifest symlink save failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "Session broken manifest symlink save failure is not reported as missing manifest");
    expect(session.getProject() == attemptedSessionProject,
           "Session broken manifest symlink save failure leaves the session project unchanged");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(manifestPath)),
           "Session broken manifest symlink save failure leaves the manifest symlink unchanged");
    expect(!std::filesystem::exists(brokenManifestTarget),
           "Session broken manifest symlink save failure does not create the missing target");
    expect(!std::filesystem::exists(temporaryManifestPath),
           "Session broken manifest symlink save failure removes stale temporary manifest");
    expect(!std::filesystem::exists(package / "backups" / "manifest.previous.json"),
           "Session broken manifest symlink save failure happens before backup creation");
    expect(readTextFile(package / "audio" / "session-sentinel.txt") == "session audio sentinel",
           "Session broken manifest symlink save failure preserves package audio contents");

    expect(std::filesystem::remove(manifestPath),
           "Temporary session broken manifest symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary session broken manifest-symlink package deleted");
}

void appSessionSaveBrokenAssetFolderSymlinkFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Before Session Broken Asset Folder Symlink Failure");
    session.setTempoBpm(119.0);

    const auto package =
        makeTemporaryPackagePath("projectname-session-save-broken-asset-folder-symlink-test");

    std::string error;
    expect(session.saveProjectPackage(package, error),
           "Initial session broken asset-folder symlink package save succeeds");

    const auto manifestPath = package / "manifest.json";
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto originalManifestText = readTextFile(manifestPath);
    expect(originalManifestText.find("Before Session Broken Asset Folder Symlink Failure") != std::string::npos,
           "Session broken asset-folder symlink fixture starts with the original manifest state");

    const auto audioSymlinkPath = package / "audio";
    const auto missingAudioTarget =
        makeTemporaryPackagePath("projectname-session-save-broken-asset-folder-symlink-target-test");
    expect(std::filesystem::remove_all(audioSymlinkPath) > 0,
           "Session broken asset-folder symlink fixture removes the original audio directory");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingAudioTarget, audioSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        return;
    }

    session.getProject().setName("After Session Broken Asset Folder Symlink Failure");
    session.setTempoBpm(167.0);
    const auto attemptedSessionProject = session.getProject();

    error = "stale session broken asset-folder symlink save error";
    expect(!session.saveProjectPackage(package, error),
           "Session save rejects a broken symlink asset folder path");
    expect(error.find("asset folder") != std::string::npos
               && error.find("symlink") != std::string::npos
               && error.find("audio") != std::string::npos,
           "Session broken asset-folder symlink failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "Session broken asset-folder symlink failure is not reported as a missing folder");
    expect(session.getProject() == attemptedSessionProject,
           "Session broken asset-folder symlink save failure leaves the session project unchanged");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(audioSymlinkPath)),
           "Session broken asset-folder symlink failure leaves the audio symlink unchanged");
    expect(!std::filesystem::exists(missingAudioTarget),
           "Session broken asset-folder symlink failure does not create the missing target");
    expect(!std::filesystem::exists(temporaryManifestPath),
           "Session broken asset-folder symlink failure does not create a temporary manifest");
    expect(readTextFile(manifestPath) == originalManifestText,
           "Session broken asset-folder symlink failure leaves the active manifest unchanged");
    expect(readTextFile(manifestPath).find("After Session Broken Asset Folder Symlink Failure") == std::string::npos,
           "Session broken asset-folder symlink failure does not commit the new project state");
    expect(!std::filesystem::exists(package / "backups" / "manifest.previous.json"),
           "Session broken asset-folder symlink failure happens before backup creation");
    expect(!std::filesystem::exists(missingAudioTarget / "manifest.json"),
           "Session broken asset-folder symlink failure does not write target manifest through the link");
    expect(!std::filesystem::exists(missingAudioTarget / "manifest.json.tmp"),
           "Session broken asset-folder symlink failure does not write target temporary manifest through the link");

    expect(std::filesystem::remove(audioSymlinkPath),
           "Temporary session broken asset-folder symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary session broken asset-folder symlink package deleted");
    std::filesystem::remove_all(missingAudioTarget);
}

void appSessionSaveLinkedAssetFolderSymlinkFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Before Session Linked Asset Folder Symlink Failure");
    session.setTempoBpm(123.0);

    const auto package =
        makeTemporaryPackagePath("projectname-session-save-linked-asset-folder-symlink-test");

    std::string error;
    expect(session.saveProjectPackage(package, error),
           "Initial session linked asset-folder symlink package save succeeds");

    const auto manifestPath = package / "manifest.json";
    const auto temporaryManifestPath = package / "manifest.json.tmp";
    const auto originalManifestText = readTextFile(manifestPath);
    expect(originalManifestText.find("Before Session Linked Asset Folder Symlink Failure") != std::string::npos,
           "Session linked asset-folder symlink fixture starts with the original manifest state");

    const auto audioSymlinkPath = package / "audio";
    const auto linkedAudioTarget =
        makeTemporaryPackagePath("projectname-session-save-linked-asset-folder-symlink-target-test");
    const auto linkedAudioSentinel = linkedAudioTarget / "sentinel.txt";
    writeTextFile(linkedAudioSentinel, "linked session audio target sentinel");
    expect(std::filesystem::remove_all(audioSymlinkPath) > 0,
           "Session linked asset-folder symlink fixture removes the original audio directory");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedAudioTarget, audioSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        std::filesystem::remove_all(linkedAudioTarget);
        return;
    }

    session.getProject().setName("After Session Linked Asset Folder Symlink Failure");
    session.setTempoBpm(171.0);
    const auto attemptedSessionProject = session.getProject();

    error = "stale session linked asset-folder symlink save error";
    expect(!session.saveProjectPackage(package, error),
           "Session save rejects a linked symlink asset folder path");
    expect(error.find("asset folder") != std::string::npos
               && error.find("symlink") != std::string::npos
               && error.find("audio") != std::string::npos,
           "Session linked asset-folder symlink failure error is human-readable");
    expect(session.getProject() == attemptedSessionProject,
           "Session linked asset-folder symlink save failure leaves the session project unchanged");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(audioSymlinkPath)),
           "Session linked asset-folder symlink failure leaves the audio symlink unchanged");
    expect(readTextFile(linkedAudioSentinel) == "linked session audio target sentinel",
           "Session linked asset-folder symlink failure preserves the linked target contents");
    expect(!std::filesystem::exists(temporaryManifestPath),
           "Session linked asset-folder symlink failure does not create a temporary manifest");
    expect(readTextFile(manifestPath) == originalManifestText,
           "Session linked asset-folder symlink failure leaves the active manifest unchanged");
    expect(readTextFile(manifestPath).find("After Session Linked Asset Folder Symlink Failure") == std::string::npos,
           "Session linked asset-folder symlink failure does not commit the new project state");
    expect(!std::filesystem::exists(package / "backups" / "manifest.previous.json"),
           "Session linked asset-folder symlink failure happens before backup creation");
    expect(!std::filesystem::exists(linkedAudioTarget / "manifest.json"),
           "Session linked asset-folder symlink failure does not write target manifest through the link");
    expect(!std::filesystem::exists(linkedAudioTarget / "manifest.json.tmp"),
           "Session linked asset-folder symlink failure does not write target temporary manifest through the link");

    expect(std::filesystem::remove(audioSymlinkPath),
           "Temporary session linked asset-folder symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary session linked asset-folder symlink package deleted");
    expect(std::filesystem::remove_all(linkedAudioTarget) > 0,
           "Temporary session linked asset-folder symlink target deleted");
}

void appSessionLoopRegionCommandsKeepTransportState()
{
    projectname::AppSession session;
    session.setTempoBpm(132.0);
    session.getTransport().setPositionBeats(5.5);
    session.play();

    std::string error;
    expect(session.setLoopRegion(4.0, 8.0, error), "Session sets loop region");
    expect(session.getLoopRegion().enabled, "Session loop region enables");
    expect(std::abs(session.getLoopRegion().startBeats - 4.0) < 0.0001,
           "Session loop region stores start beat");
    expect(std::abs(session.getLoopRegion().lengthBeats - 8.0) < 0.0001,
           "Session loop region stores length beats");
    expect(session.getTransport().isPlaying(), "Session loop set keeps transport playing");
    expect(std::abs(session.getTransport().getPositionBeats() - 5.5) < 0.0001,
           "Session loop set does not move transport");
    expect(session.shouldPlayGeneratedTone(), "Session loop set does not alter playback intent");

    expect(!session.setLoopRegion(2.0, -1.0, error), "Session rejects invalid loop region");
    expect(session.getLoopRegion().enabled
               && std::abs(session.getLoopRegion().startBeats - 4.0) < 0.0001
               && std::abs(session.getLoopRegion().lengthBeats - 8.0) < 0.0001,
           "Rejected session loop region leaves current loop unchanged");

    const auto package = makeTemporaryPackagePath("projectname-session-loop-region-test");
    expect(session.saveProjectPackage(package, error), "Session loop region project saves");

    projectname::AppSession loadedSession;
    expect(loadedSession.loadProjectPackage(package, error), "Session loop region project loads");
    expect(loadedSession.getProject() == session.getProject(), "Loaded session loop project matches saved project");
    expect(!loadedSession.shouldPlayGeneratedTone(), "Loaded session loop project does not resume playback intent");

    loadedSession.clearLoopRegion();
    expect(!loadedSession.getLoopRegion().enabled, "Session clears loop region");
    expect(std::abs(loadedSession.getTransport().getPositionBeats() - 5.5) < 0.0001,
           "Session loop clear does not move transport");

    expect(std::filesystem::remove_all(package) > 0, "Temporary session loop package deleted");
}

void appSessionAdvanceWrapsEnabledLoopRegion()
{
    {
        projectname::AppSession session;
        session.setTempoBpm(120.0);
        session.getTransport().setPositionBeats(7.5);
        session.play();

        session.advanceSeconds(1.0);
        expect(std::abs(session.getTransport().getPositionBeats() - 9.5) < 0.0001,
               "Disabled loop leaves session transport advance unchanged");
    }

    {
        projectname::AppSession session;
        session.setTempoBpm(120.0);
        std::string error;
        expect(session.setLoopRegion(4.0, 4.0, error), "Exact-boundary loop region set");
        session.getTransport().setPositionBeats(7.5);
        session.play();

        session.advanceSeconds(0.25);
        expect(std::abs(session.getTransport().getPositionBeats() - 4.0) < 0.0001,
               "Session loop wraps exact loop end to loop start");
    }

    {
        projectname::AppSession session;
        session.setTempoBpm(120.0);
        std::string error;
        expect(session.setLoopRegion(4.0, 4.0, error), "Overshoot loop region set");
        session.getTransport().setPositionBeats(7.5);
        session.play();

        session.advanceSeconds(1.0);
        expect(std::abs(session.getTransport().getPositionBeats() - 5.5) < 0.0001,
               "Session loop preserves overshoot after wrapping");
    }

    {
        projectname::AppSession session;
        session.setTempoBpm(120.0);
        std::string error;
        expect(session.setLoopRegion(4.0, 4.0, error), "Large-overshoot loop region set");
        session.getTransport().setPositionBeats(7.0);
        session.play();

        session.advanceSeconds(4.5);
        expect(std::abs(session.getTransport().getPositionBeats() - 4.0) < 0.0001,
               "Session loop handles multi-length overshoot deterministically");
    }

    {
        projectname::AppSession session;
        session.setTempoBpm(120.0);
        std::string error;
        expect(session.setLoopRegion(4.0, 4.0, error), "Stopped loop region set");
        session.getTransport().setPositionBeats(7.5);

        session.advanceSeconds(1.0);
        expect(std::abs(session.getTransport().getPositionBeats() - 7.5) < 0.0001,
               "Stopped session loop advance leaves transport unchanged");
    }
}

void appSessionPreparesImportedTimelinePlaybackFromPlay()
{
    const auto package = makeTemporaryPackagePath("projectname-session-timeline-playback-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-session-timeline-source-test");
    writePcm16Wav(wavPath, 10, 1, { 1000, 2000, 3000, 4000 });

    projectname::AppSession session;
    session.setTempoBpm(120.0);

    std::string error;
    auto imported = session.importPcm16WavIntoProjectPackage(package, wavPath, 2.0, error);
    expect(imported.has_value(), "Session timeline playback test imports PCM16 WAV: " + error);
    if (imported.has_value())
    {
        projectname::AppSession staleSession;
        auto staleCache = staleSession.cacheImportedTimelineClip(imported->clip, imported->preparedClip);
        expect(staleCache == nullptr, "Session timeline cache rejects clips outside the current project");
        expect(std::filesystem::remove(imported->copiedAudioPath),
               "Session timeline playback removes package audio to prove cache use");
    }

    session.getTransport().setPositionBeats(0.0);
    auto preRoll = session.playFromTimeline(package, 10.0, error);
    expect(preRoll.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session Play prepares imported clip before timeline entry");
    expect(session.getTransport().isPlaying(), "Session timeline playback starts transport");
    expect(!session.shouldPlayGeneratedTone(), "Session timeline playback disables generated-tone fallback");
    expect(preRoll.activation.has_value()
               && preRoll.activation->timelinePlaybackStartSample == 10
               && preRoll.activation->clipLocalStartOffsetSamples == 0,
           "Session timeline playback schedules future imported clip entry");
    expect(preRoll.transportTimelineSample == 0,
           "Session timeline playback records current pre-roll transport sample");
    expect(preRoll.usedCachedBuffer, "Session timeline playback uses cached imported audio after import");
    expect(preRoll.preparedSamples != nullptr && preRoll.preparedSamples->size() == 4,
           "Session timeline playback reuses prepared audio samples outside render callback");

    if (preRoll.activation.has_value() && preRoll.preparedSamples != nullptr && !preRoll.preparedSamples->empty())
    {
        projectname::AudioEngineStub audioEngine;
        audioEngine.prepare(10.0);
        audioEngine.setTimelinePositionSamples(preRoll.transportTimelineSample);
        audioEngine.setPreparedMonoClipSamples(preRoll.preparedSamples);
        audioEngine.startScheduledPreparedMonoClip(preRoll.activation->timelinePlaybackStartSample,
                                                   preRoll.activation->clipLocalStartOffsetSamples,
                                                   preRoll.activation->clipLengthSamples);

        std::vector<float> left(12, 1.0f);
        audioEngine.render(left.data(), nullptr, static_cast<int>(left.size()));

        auto preStartPeak = 0.0f;
        for (std::size_t index = 0; index < 10; ++index)
            preStartPeak = std::max(preStartPeak, std::abs(left[index]));

        expect(preStartPeak == 0.0f, "Session timeline playback renders silence before imported clip entry");
        expect(std::abs(left[10] - (*preRoll.preparedSamples)[0]) < 0.0001f,
               "Session timeline playback renders imported clip at scheduled entry");
    }

    session.getTransport().setPositionBeats(2.4);
    auto seek = session.playFromTimeline(package, 10.0, error);
    expect(seek.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session Play prepares imported clip from inside timeline region");
    expect(seek.activation.has_value()
               && seek.activation->timelinePlaybackStartSample == 12
               && seek.activation->clipLocalStartOffsetSamples == 2,
           "Session timeline playback schedules imported clip from clip-local seek offset");
    expect(seek.transportTimelineSample == 12,
           "Session timeline playback records current seek transport sample");
    expect(seek.usedCachedBuffer, "Session timeline playback seek reuses cached imported audio");

    if (seek.activation.has_value() && seek.preparedSamples != nullptr && seek.preparedSamples->size() >= 4)
    {
        projectname::AudioEngineStub audioEngine;
        audioEngine.prepare(10.0);
        audioEngine.setTimelinePositionSamples(seek.transportTimelineSample);
        audioEngine.setPreparedMonoClipSamples(seek.preparedSamples);
        audioEngine.startScheduledPreparedMonoClip(seek.activation->timelinePlaybackStartSample,
                                                   seek.activation->clipLocalStartOffsetSamples,
                                                   seek.activation->clipLengthSamples);

        std::vector<float> left(2);
        audioEngine.render(left.data(), nullptr, static_cast<int>(left.size()));

        expect(std::abs(left[0] - (*seek.preparedSamples)[2]) < 0.0001f,
               "Session timeline playback seek renders imported clip-local sample");
        expect(std::abs(left[1] - (*seek.preparedSamples)[3]) < 0.0001f,
               "Session timeline playback seek continues imported clip-local samples");
    }

    std::string loopError;
    expect(session.setLoopRegion(2.0, 0.8, loopError), "Session timeline playback test sets loop region");
    session.getTransport().setPositionBeats(2.6);
    session.play();
    session.advanceSeconds(0.1);
    expect(std::abs(session.getTransport().getPositionBeats() - 2.0) < 0.0001,
           "Session timeline playback test wraps transport to imported clip start");

    auto loopStart = session.playFromTimeline(package, 10.0, error);
    expect(loopStart.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session Play prepares imported clip after loop wrap");
    expect(loopStart.activation.has_value()
               && loopStart.activation->timelinePlaybackStartSample == 10
               && loopStart.activation->clipLocalStartOffsetSamples == 0,
           "Session timeline playback schedules imported clip from loop-wrapped position");

    if (imported.has_value())
    {
        expect(session.replaceImportedAudioClipMedia(imported->clip.id,
                                                     "audio/replaced-timeline.wav",
                                                     "analysis/replaced-timeline.waveform.json",
                                                     imported->clip.lengthBeats,
                                                     error),
               "Session media replacement invalidates cached imported timeline clip");
    }

    session.getTransport().setPositionBeats(2.0);
    auto invalidatedCache = session.playFromCachedTimeline(10.0, error);
    expect(invalidatedCache.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session cached playback requires background preparation after media replacement");
    expect(!invalidatedCache.usedCachedBuffer,
           "Session cached playback does not reuse stale prepared audio after media replacement");

    auto missingFileFallback = session.playFromTimeline(package, 10.0, error);
    expect(missingFileFallback.status == projectname::TimelinePlaybackPreparationStatus::generatedToneFallback,
           "Session Play falls back when replaced package audio is missing");
    expect(error.find("Could not prepare imported timeline clip") != std::string::npos,
           "Session replaced-media missing-file fallback reports preparation failure");

    session.getTransport().setPositionBeats(3.0);
    auto fallback = session.playFromTimeline(package, 10.0, error);
    expect(fallback.status == projectname::TimelinePlaybackPreparationStatus::generatedToneFallback,
           "Session Play falls back to generated tone after imported clips end");
    expect(session.shouldPlayGeneratedTone(), "Session fallback enables generated tone intent");
    expect(session.getTransport().isPlaying(), "Session fallback keeps transport playing");

    expect(std::filesystem::remove(wavPath), "Temporary session timeline source WAV deleted");
    expect(std::filesystem::remove_all(package) > 0, "Temporary session timeline package deleted");
}

void appSessionCachesMultipleImportedTimelineClips()
{
    auto project = projectname::ProjectModel::createDefault();
    project.getTransport().setTempoBpm(120.0);

    projectname::ProjectTrack track;
    track.id = "track-cache";
    track.name = "Cache Track";
    track.type = "audio";

    std::vector<projectname::ProjectClip> clips;
    for (auto index = 0; index < 5; ++index)
    {
        clips.push_back(makeImportedTimelineCacheClip(index, static_cast<double>(index * 2)));
        track.clips.push_back(clips.back());
    }

    project.addTrack(std::move(track));
    projectname::AppSession session(std::move(project));

    auto makeSamples = [](int index)
    {
        const auto base = static_cast<float>(index + 1) / 10.0f;
        return std::vector<float> { base, base + 0.01f, base + 0.02f, base + 0.03f, base + 0.04f };
    };

    for (auto index = 0; index < 4; ++index)
    {
        auto cached = session.cacheImportedTimelineClip(clips[static_cast<std::size_t>(index)],
                                                        makePreparedMonoCacheClip(makeSamples(index)));
        expect(cached != nullptr && cached->size() == 5,
               "Session multi-cache stores initial imported timeline clip");
    }

    std::string error;
    session.getTransport().setPositionBeats(4.0);
    auto hit = session.playFromCachedTimeline(10.0, error);
    expect(hit.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session multi-cache hits cached imported timeline clip");
    expect(hit.usedCachedBuffer, "Session multi-cache marks cached hit");
    expect(hit.clip.has_value() && hit.clip->clipId == clips[2].id,
           "Session multi-cache hit returns expected clip");
    expect(hit.preparedSamples != nullptr && std::abs((*hit.preparedSamples)[0] - 0.30f) < 0.0001f,
           "Session multi-cache hit returns expected prepared samples");

    session.getTransport().setPositionBeats(8.0);
    auto miss = session.playFromCachedTimeline(10.0, error);
    expect(miss.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session multi-cache reports a miss for uncached imported timeline clip");
    expect(!miss.usedCachedBuffer, "Session multi-cache miss does not report cached buffer use");
    expect(miss.clip.has_value() && miss.clip->clipId == clips[4].id,
           "Session multi-cache miss identifies uncached clip");

    auto newest = session.cacheImportedTimelineClip(clips[4], makePreparedMonoCacheClip(makeSamples(4)));
    expect(newest != nullptr && newest->size() == 5,
           "Session multi-cache stores newest imported timeline clip");

    session.getTransport().setPositionBeats(8.0);
    auto newestHit = session.playFromCachedTimeline(10.0, error);
    expect(newestHit.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session multi-cache hits newly cached imported timeline clip");
    expect(newestHit.preparedSamples != nullptr && std::abs((*newestHit.preparedSamples)[0] - 0.50f) < 0.0001f,
           "Session multi-cache returns newest prepared samples");

    session.getTransport().setPositionBeats(0.0);
    auto evicted = session.playFromCachedTimeline(10.0, error);
    expect(evicted.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session multi-cache evicts the oldest prepared timeline clip");
    expect(evicted.clip.has_value() && evicted.clip->clipId == clips[0].id,
           "Session multi-cache eviction keeps the requested clip in the miss result");

    session.getTransport().setPositionBeats(2.0);
    auto beforeStaleMutation = session.playFromCachedTimeline(10.0, error);
    expect(beforeStaleMutation.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session multi-cache has a cached clip before stale media-path mutation");
    expect(session.getProject().replaceImportedAudioClipMedia(clips[1].id,
                                                              "audio/cache-1-replaced.wav",
                                                              "analysis/cache-1-replaced.waveform.json",
                                                              1.0,
                                                              error),
           "Session multi-cache test mutates media path through project model");

    session.getTransport().setPositionBeats(2.0);
    auto staleRejected = session.playFromCachedTimeline(10.0, error);
    expect(staleRejected.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session multi-cache rejects stale entry when media path changes");
    expect(!staleRejected.usedCachedBuffer,
           "Session multi-cache stale rejection does not report cached buffer use");
    expect(staleRejected.clip.has_value() && staleRejected.clip->relativePath == "audio/cache-1-replaced.wav",
           "Session multi-cache stale rejection reports the current media path");
}

void appSessionPreparesCachedTimelineVoiceWindow()
{
    auto project = projectname::ProjectModel::createDefault();
    project.getTransport().setTempoBpm(120.0);

    projectname::ProjectClip clipA;
    clipA.id = "clip-window-a";
    clipA.name = "Window A";
    clipA.type = "audio-file";
    clipA.relativePath = "audio/window-a.wav";
    clipA.analysisPath = "analysis/window-a.waveform.json";
    clipA.startBeats = 0.0;
    clipA.lengthBeats = 2.0;

    projectname::ProjectClip clipB;
    clipB.id = "clip-window-b";
    clipB.name = "Window B";
    clipB.type = "audio-file";
    clipB.relativePath = "audio/window-b.wav";
    clipB.analysisPath = "analysis/window-b.waveform.json";
    clipB.startBeats = 1.0;
    clipB.lengthBeats = 2.0;

    projectname::ProjectTrack track;
    track.id = "track-window";
    track.name = "Voice Window";
    track.type = "audio";
    track.clips.push_back(clipA);
    track.clips.push_back(clipB);
    project.addTrack(std::move(track));

    projectname::AppSession session(std::move(project));
    const auto cachedA = session.cacheImportedTimelineClip(
        clipA,
        makePreparedMonoCacheClip({ 0.10f, 0.10f, 0.10f, 0.10f, 0.10f,
                                    0.10f, 0.10f, 0.10f, 0.10f, 0.10f }));
    const auto cachedB = session.cacheImportedTimelineClip(
        clipB,
        makePreparedMonoCacheClip({ 0.20f, 0.20f, 0.20f, 0.20f, 0.20f,
                                    0.20f, 0.20f, 0.20f, 0.20f, 0.20f }));
    expect(cachedA != nullptr && cachedB != nullptr,
           "Session voice-window test caches both overlapping clips");

    session.getTransport().setPositionBeats(0.0);
    std::string error;
    auto playback = session.playCachedTimelineVoiceWindow(10.0, 16, error);
    expect(playback.status == projectname::TimelineVoicePlaybackPreparationStatus::voiceWindowReady,
           "Session prepares cached timeline voice window");
    expect(error.empty(), "Session voice-window preparation leaves error empty on cache hit");
    expect(session.getTransport().isPlaying(), "Session voice-window preparation starts transport");
    expect(!session.shouldPlayGeneratedTone(), "Session voice-window preparation disables generated-tone fallback");
    expect(playback.transportTimelineSample == 0,
           "Session voice-window preparation records transport timeline sample");
    expect(playback.schedule.renderTimelineStartSample == 0 && playback.schedule.frameCount == 16,
           "Session voice-window preparation records requested render window");
    expect(playback.schedule.voices.size() == 2,
           "Session voice-window preparation schedules overlapping voices");
    expect(playback.preparedBuffers.size() == 2,
           "Session voice-window preparation returns immutable buffers for both voices");

    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(10.0);
    audioEngine.setPreparedTrackVoiceBuffers(std::move(playback.preparedBuffers));
    audioEngine.startPreparedVoiceSchedule(std::move(playback.schedule));

    std::vector<float> left(16, 1.0f);
    std::vector<float> right(16, 1.0f);
    audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

    expect(std::abs(left[0] - 0.10f) < 0.0001f && std::abs(right[0] - 0.10f) < 0.0001f,
           "Prepared voice window renders first clip before overlap");
    expect(std::abs(left[5] - 0.30f) < 0.0001f && std::abs(right[5] - 0.30f) < 0.0001f,
           "Prepared voice window sums overlapping imported clips");
    expect(std::abs(left[10] - 0.20f) < 0.0001f && std::abs(right[10] - 0.20f) < 0.0001f,
           "Prepared voice window continues second clip after first clip ends");
    expect(left[15] == 0.0f && right[15] == 0.0f,
           "Prepared voice window renders silence after scheduled voices");
}

void appSessionVoiceWindowReportsSampleRateMismatchMetadata()
{
    auto project = projectname::ProjectModel::createDefault();
    project.getTransport().setTempoBpm(120.0);

    projectname::ProjectClip clipA;
    clipA.id = "clip-rate-a";
    clipA.name = "Rate A";
    clipA.type = "audio-file";
    clipA.relativePath = "audio/rate-a.wav";
    clipA.analysisPath = "analysis/rate-a.waveform.json";
    clipA.startBeats = 0.0;
    clipA.lengthBeats = 2.0;

    projectname::ProjectClip clipB;
    clipB.id = "clip-rate-b";
    clipB.name = "Rate B";
    clipB.type = "audio-file";
    clipB.relativePath = "audio/rate-b.wav";
    clipB.analysisPath = "analysis/rate-b.waveform.json";
    clipB.startBeats = 1.0;
    clipB.lengthBeats = 2.0;

    projectname::ProjectTrack track;
    track.id = "track-rate";
    track.name = "Rate Track";
    track.type = "audio";
    track.clips.push_back(clipA);
    track.clips.push_back(clipB);
    project.addTrack(std::move(track));

    projectname::AppSession session(std::move(project));
    [[maybe_unused]] const auto cachedA =
        session.cacheImportedTimelineClip(clipA,
                                          makePreparedMonoCacheClip({ 0.10f, 0.10f, 0.10f,
                                                                     0.10f, 0.10f, 0.10f,
                                                                     0.10f, 0.10f, 0.10f,
                                                                     0.10f },
                                                                   10.0));
    [[maybe_unused]] const auto cachedB =
        session.cacheImportedTimelineClip(clipB,
                                          makePreparedMonoCacheClip({ 0.20f, 0.20f, 0.20f,
                                                                     0.20f, 0.20f, 0.20f,
                                                                     0.20f, 0.20f, 0.20f,
                                                                     0.20f },
                                                                   20.0));

    std::string error;
    auto playback = session.playCachedTimelineVoiceWindow(10.0, 16, error);
    expect(playback.status == projectname::TimelineVoicePlaybackPreparationStatus::voiceWindowReady,
           "Session sample-rate metadata test prepares voice window");
    expect(playback.sampleRateMismatches.size() == 1,
           "Session voice-window playback reports one sample-rate mismatch");

    if (!playback.sampleRateMismatches.empty())
    {
        const auto& mismatch = playback.sampleRateMismatches.front();
        expect(mismatch.clipId == clipB.id && mismatch.clipName == clipB.name,
               "Session voice-window mismatch metadata preserves clip identity");
        expect(mismatch.relativePath == clipB.relativePath,
               "Session voice-window mismatch metadata preserves package path");
        expect(std::abs(mismatch.sourceSampleRateHz - 20.0) < 0.0001
                   && std::abs(mismatch.outputSampleRateHz - 10.0) < 0.0001,
               "Session voice-window mismatch metadata records source and output rates");
    }

    expect(playback.message.find("sample-rate mismatch") != std::string::npos,
           "Session voice-window playback message surfaces sample-rate warning");
}

void appSessionVoiceWindowUsesPersistedTrackMixState()
{
    auto project = projectname::ProjectModel::createDefault();
    project.getTransport().setTempoBpm(120.0);

    projectname::ProjectClip clipA;
    clipA.id = "clip-mix-a";
    clipA.name = "Mix A";
    clipA.type = "audio-file";
    clipA.relativePath = "audio/mix-a.wav";
    clipA.analysisPath = "analysis/mix-a.waveform.json";
    clipA.startBeats = 0.0;
    clipA.lengthBeats = 2.0;

    projectname::ProjectTrack trackA;
    trackA.id = "track-mix-a";
    trackA.name = "Mix A";
    trackA.type = "audio";
    trackA.volume = 0.5f;
    trackA.pan = -1.0f;
    trackA.clips.push_back(clipA);

    projectname::ProjectClip clipB;
    clipB.id = "clip-mix-b";
    clipB.name = "Mix B";
    clipB.type = "audio-file";
    clipB.relativePath = "audio/mix-b.wav";
    clipB.analysisPath = "analysis/mix-b.waveform.json";
    clipB.startBeats = 1.0;
    clipB.lengthBeats = 2.0;

    projectname::ProjectTrack trackB;
    trackB.id = "track-mix-b";
    trackB.name = "Mix B";
    trackB.type = "audio";
    trackB.volume = 0.25f;
    trackB.pan = 1.0f;
    trackB.clips.push_back(clipB);

    project.addTrack(std::move(trackA));
    project.addTrack(std::move(trackB));

    projectname::AppSession session(std::move(project));
    const auto cachedA = session.cacheImportedTimelineClip(
        clipA,
        makePreparedMonoCacheClip({ 0.40f, 0.40f, 0.40f, 0.40f, 0.40f,
                                    0.40f, 0.40f, 0.40f, 0.40f, 0.40f }));
    const auto cachedB = session.cacheImportedTimelineClip(
        clipB,
        makePreparedMonoCacheClip({ 0.80f, 0.80f, 0.80f, 0.80f, 0.80f,
                                    0.80f, 0.80f, 0.80f, 0.80f, 0.80f }));
    expect(cachedA != nullptr && cachedB != nullptr,
           "Session track-mix voice-window test caches both clips");

    std::string error;
    auto playback = session.playCachedTimelineVoiceWindow(10.0, 16, error);
    expect(playback.status == projectname::TimelineVoicePlaybackPreparationStatus::voiceWindowReady,
           "Session track-mix voice-window playback is ready");
    expect(playback.schedule.voices.size() == 2,
           "Session track-mix voice-window schedules both voices");

    if (playback.schedule.voices.size() == 2)
    {
        const auto& first = playback.schedule.voices[0];
        const auto& second = playback.schedule.voices[1];
        expect(std::abs(first.gainLeft - 0.5f) < 0.0001f && first.gainRight == 0.0f,
               "Session voice-window uses persisted left pan and volume");
        expect(second.gainLeft == 0.0f && std::abs(second.gainRight - 0.25f) < 0.0001f,
               "Session voice-window uses persisted right pan and volume");
    }

    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(10.0);
    audioEngine.setPreparedTrackVoiceBuffers(std::move(playback.preparedBuffers));
    audioEngine.startPreparedVoiceSchedule(std::move(playback.schedule));

    std::vector<float> left(16, 1.0f);
    std::vector<float> right(16, 1.0f);
    audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

    expect(std::abs(left[0] - 0.20f) < 0.0001f && right[0] == 0.0f,
           "Session voice-window renders left-panned first track");
    expect(std::abs(left[5] - 0.20f) < 0.0001f && std::abs(right[5] - 0.20f) < 0.0001f,
           "Session voice-window renders overlapping tracks with persisted pan and volume");
    expect(left[10] == 0.0f && std::abs(right[10] - 0.20f) < 0.0001f,
           "Session voice-window renders right-panned second track after overlap");
}

void appSessionVoiceWindowFiltersPersistedMuteAndSoloState()
{
    auto makeSession = [](bool muteFirst, bool soloFirst)
    {
        auto project = projectname::ProjectModel::createDefault();
        project.getTransport().setTempoBpm(120.0);

        projectname::ProjectClip clipA;
        clipA.id = "clip-filter-a";
        clipA.name = "Filter A";
        clipA.type = "audio-file";
        clipA.relativePath = "audio/filter-a.wav";
        clipA.analysisPath = "analysis/filter-a.waveform.json";
        clipA.startBeats = 0.0;
        clipA.lengthBeats = 2.0;

        projectname::ProjectTrack trackA;
        trackA.id = "track-filter-a";
        trackA.name = "Filter A";
        trackA.type = "audio";
        trackA.muted = muteFirst;
        trackA.solo = soloFirst;
        trackA.clips.push_back(clipA);

        projectname::ProjectClip clipB;
        clipB.id = "clip-filter-b";
        clipB.name = "Filter B";
        clipB.type = "audio-file";
        clipB.relativePath = "audio/filter-b.wav";
        clipB.analysisPath = "analysis/filter-b.waveform.json";
        clipB.startBeats = 1.0;
        clipB.lengthBeats = 2.0;

        projectname::ProjectTrack trackB;
        trackB.id = "track-filter-b";
        trackB.name = "Filter B";
        trackB.type = "audio";
        trackB.clips.push_back(clipB);

        project.addTrack(std::move(trackA));
        project.addTrack(std::move(trackB));

        projectname::AppSession session(std::move(project));
        [[maybe_unused]] const auto cachedA =
            session.cacheImportedTimelineClip(clipA, makePreparedMonoCacheClip({ 0.25f, 0.25f, 0.25f,
                                                                                  0.25f, 0.25f, 0.25f,
                                                                                  0.25f, 0.25f, 0.25f,
                                                                                  0.25f }));
        [[maybe_unused]] const auto cachedB =
            session.cacheImportedTimelineClip(clipB, makePreparedMonoCacheClip({ 0.50f, 0.50f, 0.50f,
                                                                                  0.50f, 0.50f, 0.50f,
                                                                                  0.50f, 0.50f, 0.50f,
                                                                                  0.50f }));
        return session;
    };

    {
        auto session = makeSession(true, false);
        std::string error;
        auto playback = session.playCachedTimelineVoiceWindow(10.0, 16, error);
        expect(playback.status == projectname::TimelineVoicePlaybackPreparationStatus::voiceWindowReady,
               "Session muted-track voice-window playback is ready");
        expect(playback.schedule.voices.size() == 1
                   && playback.schedule.voices.front().trackId == "track-filter-b",
               "Session voice-window filters muted persisted track state");
    }

    {
        auto session = makeSession(false, true);
        std::string error;
        auto playback = session.playCachedTimelineVoiceWindow(10.0, 16, error);
        expect(playback.status == projectname::TimelineVoicePlaybackPreparationStatus::voiceWindowReady,
               "Session solo-track voice-window playback is ready");
        expect(playback.schedule.voices.size() == 1
                   && playback.schedule.voices.front().trackId == "track-filter-a",
               "Session voice-window filters non-solo persisted track state");
    }
}

void appSessionCachedTimelineVoiceWindowReportsMissingBuffers()
{
    auto project = projectname::ProjectModel::createDefault();
    project.getTransport().setTempoBpm(120.0);

    projectname::ProjectClip clipA;
    clipA.id = "clip-window-missing-a";
    clipA.name = "Window Missing A";
    clipA.type = "audio-file";
    clipA.relativePath = "audio/window-missing-a.wav";
    clipA.analysisPath = "analysis/window-missing-a.waveform.json";
    clipA.startBeats = 0.0;
    clipA.lengthBeats = 2.0;

    projectname::ProjectClip clipB;
    clipB.id = "clip-window-missing-b";
    clipB.name = "Window Missing B";
    clipB.type = "audio-file";
    clipB.relativePath = "audio/window-missing-b.wav";
    clipB.analysisPath = "analysis/window-missing-b.waveform.json";
    clipB.startBeats = 1.0;
    clipB.lengthBeats = 2.0;

    projectname::ProjectTrack track;
    track.id = "track-window-missing";
    track.name = "Voice Window Missing";
    track.type = "audio";
    track.clips.push_back(clipA);
    track.clips.push_back(clipB);
    project.addTrack(std::move(track));

    projectname::AppSession session(std::move(project));
    const auto cachedA = session.cacheImportedTimelineClip(
        clipA,
        makePreparedMonoCacheClip({ 0.10f, 0.10f, 0.10f, 0.10f, 0.10f,
                                    0.10f, 0.10f, 0.10f, 0.10f, 0.10f }));
    expect(cachedA != nullptr, "Session voice-window missing test caches first clip");

    std::string error;
    auto playback = session.playCachedTimelineVoiceWindow(10.0, 16, error);
    expect(playback.status == projectname::TimelineVoicePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session voice-window preparation reports missing overlapping clip cache");
    expect(playback.missingClips.size() == 1 && playback.missingClips.front().clipId == clipB.id,
           "Session voice-window preparation identifies missing clip");
    expect(playback.preparedBuffers.size() == 1,
           "Session voice-window preparation returns available cached buffers with miss result");
    expect(!session.getTransport().isPlaying(),
           "Session voice-window cache miss does not start transport");
}

void appSessionPreparedTimelineCacheHonorsMemoryBudget()
{
    auto project = projectname::ProjectModel::createDefault();
    project.getTransport().setTempoBpm(120.0);

    projectname::ProjectTrack track;
    track.id = "track-cache-budget";
    track.name = "Cache Budget Track";
    track.type = "audio";

    std::vector<projectname::ProjectClip> clips;
    for (auto index = 0; index < 4; ++index)
    {
        clips.push_back(makeImportedTimelineCacheClip(index, static_cast<double>(index * 2)));
        track.clips.push_back(clips.back());
    }

    project.addTrack(std::move(track));
    projectname::AppSession session(std::move(project));

    projectname::ImportedTimelineClipCacheLimits limits;
    limits.maxEntries = 4;
    limits.maxSampleBytes = 40;
    session.setImportedTimelineClipCacheLimits(limits);
    expect(session.getImportedTimelineClipCacheLimits().maxSampleBytes == 40,
           "Session prepared cache stores explicit byte budget");

    auto smallSamples = [](int index)
    {
        const auto base = static_cast<float>(index + 1) / 10.0f;
        return std::vector<float> { base, base + 0.01f, base + 0.02f, base + 0.03f, base + 0.04f };
    };

    auto cached0 = session.cacheImportedTimelineClip(clips[0], makePreparedMonoCacheClip(smallSamples(0)));
    auto cached1 = session.cacheImportedTimelineClip(clips[1], makePreparedMonoCacheClip(smallSamples(1)));
    expect(cached0 != nullptr && cached1 != nullptr,
           "Session prepared cache accepts entries within byte budget");

    auto cached2 = session.cacheImportedTimelineClip(clips[2], makePreparedMonoCacheClip(smallSamples(2)));
    expect(cached2 != nullptr, "Session prepared cache accepts new entry before trimming old entries");

    std::string error;
    session.getTransport().setPositionBeats(0.0);
    auto budgetEvicted = session.playFromCachedTimeline(10.0, error);
    expect(budgetEvicted.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session prepared cache evicts oldest entry to satisfy byte budget");
    expect(budgetEvicted.clip.has_value() && budgetEvicted.clip->clipId == clips[0].id,
           "Session prepared cache budget miss reports evicted clip");

    session.getTransport().setPositionBeats(2.0);
    auto stillCached = session.playFromCachedTimeline(10.0, error);
    expect(stillCached.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session prepared cache keeps newer entry after byte-budget trim");
    expect(stillCached.preparedSamples != nullptr && std::abs((*stillCached.preparedSamples)[0] - 0.20f) < 0.0001f,
           "Session prepared cache byte-budget hit returns expected samples");

    const auto wasPlaying = session.getTransport().isPlaying();
    const auto positionBeforeOversize = session.getTransport().getPositionBeats();
    auto oversized = session.cacheImportedTimelineClip(
        clips[3],
        makePreparedMonoCacheClip({ 0.4f, 0.41f, 0.42f, 0.43f, 0.44f, 0.45f,
                                    0.46f, 0.47f, 0.48f, 0.49f, 0.5f }));
    expect(oversized == nullptr,
           "Session prepared cache rejects a single buffer larger than the byte budget");
    expect(session.getTransport().isPlaying() == wasPlaying
               && std::abs(session.getTransport().getPositionBeats() - positionBeforeOversize) < 0.0001,
           "Session prepared cache oversized rejection leaves transport state unchanged");

    session.getTransport().setPositionBeats(6.0);
    auto oversizeMiss = session.playFromCachedTimeline(10.0, error);
    expect(oversizeMiss.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session prepared cache does not retain oversized rejected buffer");

    expect(session.replaceImportedAudioClipMedia(clips[1].id,
                                                 "audio/cache-budget-1-replaced.wav",
                                                 "analysis/cache-budget-1-replaced.waveform.json",
                                                 1.0,
                                                 error),
           "Session prepared cache invalidates matching entry under byte budget");

    session.getTransport().setPositionBeats(2.0);
    auto invalidated = session.playFromCachedTimeline(10.0, error);
    expect(invalidated.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
           "Session prepared cache byte-budget invalidation clears replaced clip");

    session.getTransport().setPositionBeats(4.0);
    auto unaffected = session.playFromCachedTimeline(10.0, error);
    expect(unaffected.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Session prepared cache byte-budget invalidation preserves other cached clips");
}

void backgroundTimelinePlaybackPreparationJobPreparesCacheMiss()
{
    const auto package = makeTemporaryPackagePath("projectname-background-timeline-playback-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-background-timeline-source-test");
    writePcm16Wav(wavPath, 10, 1, { 1000, 2000, 3000, 4000 });

    projectname::AppSession session;
    session.setTempoBpm(120.0);

    std::string error;
    auto imported = session.importPcm16WavIntoProjectPackage(package, wavPath, 2.0, error);
    expect(imported.has_value(), "Background timeline playback test imports PCM16 WAV");

    projectname::AppSession loadedSession;
    expect(loadedSession.loadProjectPackage(package, error),
           "Background timeline playback test loads project without prepared cache");
    loadedSession.getTransport().setPositionBeats(2.4);

    projectname::BackgroundTimelinePlaybackPreparationRequest request;
    request.project = loadedSession.getProject();
    request.packageDirectory = package;
    request.outputSampleRateHz = 10.0;

    projectname::BackgroundTimelinePlaybackPreparationJob job(std::move(request));
    job.start();
    auto result = job.waitForResult();
    const auto progress = job.getProgress();

    expect(!result.cancelled, "Background timeline playback preparation completes without cancellation");
    expect(result.preparation.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Background timeline playback preparation returns imported clip");
    expect(result.preparation.preparedSamples != nullptr && result.preparation.preparedSamples->size() == 4,
           "Background timeline playback preparation returns prepared samples");
    expect(result.preparation.activation.has_value()
               && result.preparation.activation->timelinePlaybackStartSample == 12
               && result.preparation.activation->clipLocalStartOffsetSamples == 2,
           "Background timeline playback preparation preserves seek activation");
    expect(progress.phase == projectname::BackgroundTimelinePlaybackPreparationPhase::completed,
           "Background timeline playback preparation reports completed progress");
    expect(progress.percent == 100, "Background timeline playback preparation reaches 100 percent");
    expect(progress.framesProcessed == 4 && progress.framesTotal == 4,
           "Background timeline playback preparation reports decode frames");

    expect(std::filesystem::remove(wavPath), "Temporary background timeline source WAV deleted");
    expect(std::filesystem::remove_all(package) > 0, "Temporary background timeline package deleted");
}

void backgroundTimelinePlaybackPreparationJobPreparesOverlappingVoiceWindowMisses()
{
    const auto package = makeTemporaryPackagePath("projectname-background-voice-window-test");
    const auto wavA = makeTemporaryAudioPath("projectname-background-voice-a-test");
    const auto wavB = makeTemporaryAudioPath("projectname-background-voice-b-test");

    writePcm16Wav(wavA, 10, 1, { 3277, 3277, 3277, 3277, 3277,
                                 3277, 3277, 3277, 3277, 3277 });
    writePcm16Wav(wavB, 10, 1, { 6553, 6553, 6553, 6553, 6553,
                                 6553, 6553, 6553, 6553, 6553 });

    projectname::AppSession importingSession;
    importingSession.setTempoBpm(120.0);

    std::string error;
    auto importedA = importingSession.importPcm16WavIntoProjectPackage(package, wavA, 0.0, error);
    auto importedB = importingSession.importPcm16WavIntoProjectPackage(package, wavB, 1.0, error);
    expect(importedA.has_value() && importedB.has_value(),
           "Background voice-window test imports overlapping PCM16 WAV clips");

    projectname::AppSession loadedSession;
    expect(loadedSession.loadProjectPackage(package, error),
           "Background voice-window test reopens project without prepared cache");
    loadedSession.getTransport().setPositionBeats(0.0);

    projectname::BackgroundTimelinePlaybackPreparationRequest request;
    request.project = loadedSession.getProject();
    request.packageDirectory = package;
    request.outputSampleRateHz = 10.0;
    request.minimumRenderFrameCount = 16;

    projectname::BackgroundTimelinePlaybackPreparationJob job(std::move(request));
    job.start();
    auto result = job.waitForResult();
    const auto progress = job.getProgress();

    expect(!result.cancelled, "Background voice-window preparation completes without cancellation");
    expect(result.preparedClips.size() == 2,
           "Background voice-window preparation decodes both missing overlapping clips");
    expect(result.preparation.status == projectname::TimelinePlaybackPreparationStatus::importedClipReady,
           "Background voice-window preparation preserves first relevant prepared clip result");
    expect(progress.phase == projectname::BackgroundTimelinePlaybackPreparationPhase::completed,
           "Background voice-window preparation reports completed progress");

    auto completion =
        projectname::completeBackgroundTimelinePlaybackPreparation(loadedSession, std::move(result), 10.0);
    expect(completion.status == projectname::TimelinePlaybackPreparationCompletionStatus::scheduledVoiceWindow,
           "Timeline completion schedules prepared overlapping voice window");
    expect(loadedSession.getTransport().isPlaying(),
           "Timeline completion starts transport for prepared voice window");
    expect(completion.voicePlayback.schedule.voices.size() == 2,
           "Timeline completion returns both overlapping scheduled voices");
    expect(completion.voicePlayback.preparedBuffers.size() == 2,
           "Timeline completion returns both prepared voice buffers");

    projectname::AudioEngineStub audioEngine;
    audioEngine.prepare(10.0);
    audioEngine.setPreparedTrackVoiceBuffers(std::move(completion.voicePlayback.preparedBuffers));
    audioEngine.startPreparedVoiceSchedule(std::move(completion.voicePlayback.schedule));

    std::vector<float> left(16, 1.0f);
    std::vector<float> right(16, 1.0f);
    audioEngine.render(left.data(), right.data(), static_cast<int>(left.size()));

    expect(std::abs(left[0] - (3277.0f / 32767.0f)) < 0.0001f
               && std::abs(right[0] - left[0]) < 0.0001f,
           "Background-prepared voice window renders first imported clip before overlap");
    expect(std::abs(left[5] - ((3277.0f + 6553.0f) / 32767.0f)) < 0.0001f
               && std::abs(right[5] - left[5]) < 0.0001f,
           "Background-prepared voice window sums overlapping imported clips");
    expect(std::abs(left[10] - (6553.0f / 32767.0f)) < 0.0001f
               && std::abs(right[10] - left[10]) < 0.0001f,
           "Background-prepared voice window continues second imported clip after first ends");
    expect(left[15] == 0.0f && right[15] == 0.0f,
           "Background-prepared voice window renders silence after overlap window");

    expect(std::filesystem::remove(wavA), "Temporary background voice source A WAV deleted");
    expect(std::filesystem::remove(wavB), "Temporary background voice source B WAV deleted");
    expect(std::filesystem::remove_all(package) > 0, "Temporary background voice package deleted");
}

void backgroundTimelineCompletionReportsPreparedVoiceWindowSampleRateMismatches()
{
    const auto package = makeTemporaryPackagePath("projectname-background-rate-warning-test");
    const auto wavA = makeTemporaryAudioPath("projectname-background-rate-a-test");
    const auto wavB = makeTemporaryAudioPath("projectname-background-rate-b-test");

    writePcm16Wav(wavA, 10, 1, { 3277, 3277, 3277, 3277, 3277,
                                 3277, 3277, 3277, 3277, 3277 });
    writePcm16Wav(wavB, 20, 1, { 6553, 6553, 6553, 6553, 6553,
                                 6553, 6553, 6553, 6553, 6553 });

    projectname::AppSession importingSession;
    importingSession.setTempoBpm(120.0);

    std::string error;
    auto importedA = importingSession.importPcm16WavIntoProjectPackage(package, wavA, 0.0, error);
    auto importedB = importingSession.importPcm16WavIntoProjectPackage(package, wavB, 1.0, error);
    expect(importedA.has_value() && importedB.has_value(),
           "Background sample-rate warning test imports mismatched PCM16 WAV clips");

    projectname::AppSession loadedSession;
    expect(loadedSession.loadProjectPackage(package, error),
           "Background sample-rate warning test reopens project without prepared cache");
    loadedSession.getTransport().setPositionBeats(0.0);

    projectname::BackgroundTimelinePlaybackPreparationRequest request;
    request.project = loadedSession.getProject();
    request.packageDirectory = package;
    request.outputSampleRateHz = 10.0;
    request.minimumRenderFrameCount = 16;

    projectname::BackgroundTimelinePlaybackPreparationJob job(std::move(request));
    job.start();
    auto result = job.waitForResult();

    expect(!result.cancelled, "Background sample-rate warning preparation completes");
    expect(result.preparedClips.size() == 2,
           "Background sample-rate warning preparation decodes both clips");

    auto completion =
        projectname::completeBackgroundTimelinePlaybackPreparation(loadedSession, std::move(result), 10.0);
    expect(completion.status == projectname::TimelinePlaybackPreparationCompletionStatus::scheduledVoiceWindow,
           "Background sample-rate warning completion schedules voice window");
    expect(completion.voicePlayback.sampleRateMismatches.size() == 1,
           "Background completion propagates voice-window sample-rate mismatch metadata");

    if (!completion.voicePlayback.sampleRateMismatches.empty())
    {
        const auto& mismatch = completion.voicePlayback.sampleRateMismatches.front();
        expect(importedB.has_value() && mismatch.clipId == importedB->clip.id,
               "Background completion mismatch metadata identifies mismatched clip");
        expect(std::abs(mismatch.sourceSampleRateHz - 20.0) < 0.0001
                   && std::abs(mismatch.outputSampleRateHz - 10.0) < 0.0001,
               "Background completion mismatch metadata records decoded and output rates");
    }

    expect(completion.message.find("sample-rate mismatch") != std::string::npos,
           "Background completion message surfaces sample-rate warning");

    expect(std::filesystem::remove(wavA), "Temporary background sample-rate source A WAV deleted");
    expect(std::filesystem::remove(wavB), "Temporary background sample-rate source B WAV deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary background sample-rate package deleted");
}

void backgroundTimelinePlaybackPreparationJobFallsBackForMissingAudio()
{
    const auto package = makeTemporaryPackagePath("projectname-background-timeline-missing-test");
    auto project = makeProjectWithImportedTimelineClip(2.0, 2.0);

    projectname::BackgroundTimelinePlaybackPreparationRequest request;
    request.project = std::move(project);
    request.packageDirectory = package;
    request.outputSampleRateHz = 10.0;

    projectname::BackgroundTimelinePlaybackPreparationJob job(std::move(request));
    job.start();
    auto result = job.waitForResult();
    const auto progress = job.getProgress();

    expect(!result.cancelled, "Background missing timeline playback preparation is not cancellation");
    expect(result.preparation.status == projectname::TimelinePlaybackPreparationStatus::generatedToneFallback,
           "Background missing timeline playback preparation preserves generated-tone fallback");
    expect(result.error.find("Could not prepare imported timeline clip") != std::string::npos,
           "Background missing timeline playback preparation reports readable error");
    expect(progress.phase == projectname::BackgroundTimelinePlaybackPreparationPhase::failed,
           "Background missing timeline playback preparation reports failed progress");

    if (std::filesystem::exists(package))
        expect(std::filesystem::remove_all(package) > 0, "Temporary background missing timeline package deleted");
}

void backgroundTimelinePlaybackPreparationJobCancelsBeforeStart()
{
    auto project = makeProjectWithImportedTimelineClip(2.0, 2.0);
    projectname::BackgroundTimelinePlaybackPreparationRequest request;
    request.project = std::move(project);
    request.packageDirectory = makeTemporaryPackagePath("projectname-background-timeline-cancel-test");
    request.outputSampleRateHz = 10.0;

    projectname::BackgroundTimelinePlaybackPreparationJob job(std::move(request));
    job.requestCancel();
    const auto cancelRequestedProgress = job.getProgress();
    expect(cancelRequestedProgress.cancelRequested,
           "Background timeline playback progress records cancellation request");
    expect(cancelRequestedProgress.phase == projectname::BackgroundTimelinePlaybackPreparationPhase::cancelled,
           "Background timeline playback pre-start cancellation reports cancelled progress");

    job.start();
    auto result = job.waitForResult();
    const auto cancelledProgress = job.getProgress();

    expect(result.cancelled, "Background timeline playback cancellation is reported");
    expect(result.preparation.status == projectname::TimelinePlaybackPreparationStatus::generatedToneFallback,
           "Cancelled background timeline playback leaves default fallback preparation");
    expect(cancelledProgress.phase == projectname::BackgroundTimelinePlaybackPreparationPhase::cancelled,
           "Background timeline playback cancellation stays cancelled");
    expect(cancelledProgress.percent == 0,
           "Background timeline playback pre-start cancellation stays at 0 percent");
}

void timelinePlaybackPreparationCompletionRejectsCancelledAndStaleResults()
{
    {
        auto project = makeProjectWithImportedTimelineClip(2.0, 1.0);
        const auto* clip = findClipById(project, "clip-imported-playback");
        expect(clip != nullptr, "Timeline completion cancellation test has imported clip");

        projectname::AppSession session(project);
        session.getTransport().setPositionBeats(2.0);

        auto cancelled = makeReadyTimelinePreparationResult(*clip, { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f });
        cancelled.cancelled = true;
        cancelled.error = "User cancelled timeline preparation.";

        auto completion =
            projectname::completeBackgroundTimelinePlaybackPreparation(session, std::move(cancelled), 10.0);
        expect(completion.status == projectname::TimelinePlaybackPreparationCompletionStatus::cancelled,
               "Timeline completion reports cancelled result before scheduling");
        expect(!session.getTransport().isPlaying(),
               "Timeline completion cancellation leaves transport stopped");

        std::string error;
        auto cacheMiss = session.playFromCachedTimeline(10.0, error);
        expect(cacheMiss.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
               "Timeline completion cancellation does not cache ready-looking stale audio");
        expect(!cacheMiss.usedCachedBuffer,
               "Timeline completion cancellation does not schedule cached audio");
    }

    {
        auto project = makeProjectWithImportedTimelineClip(2.0, 1.0);
        const auto* clip = findClipById(project, "clip-imported-playback");
        expect(clip != nullptr, "Timeline completion stale test has imported clip");

        projectname::AppSession session(project);
        std::string error;
        expect(session.replaceImportedAudioClipMedia("clip-imported-playback",
                                                     "audio/replaced-stale-completion.wav",
                                                     "analysis/replaced-stale-completion.waveform.json",
                                                     1.0,
                                                     error),
               "Timeline completion stale test replaces media before completion");
        session.getTransport().setPositionBeats(2.0);

        auto stale = makeReadyTimelinePreparationResult(*clip, { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f });
        auto completion = projectname::completeBackgroundTimelinePlaybackPreparation(session, std::move(stale), 10.0);
        expect(completion.status == projectname::TimelinePlaybackPreparationCompletionStatus::staleResult,
               "Timeline completion rejects prepared result for old media path");
        expect(!session.getTransport().isPlaying(),
               "Timeline completion stale result leaves transport stopped");

        auto cacheMiss = session.playFromCachedTimeline(10.0, error);
        expect(cacheMiss.status == projectname::TimelinePlaybackPreparationStatus::backgroundPreparationRequired,
               "Timeline completion stale result does not cache replaced media");
        expect(cacheMiss.clip.has_value()
                   && cacheMiss.clip->relativePath == "audio/replaced-stale-completion.wav",
               "Timeline completion stale result keeps current media path authoritative");
    }

    {
        auto project = makeProjectWithImportedTimelineClip(2.0, 1.0);
        const auto* clip = findClipById(project, "clip-imported-playback");
        expect(clip != nullptr, "Timeline completion scheduling test has imported clip");

        projectname::AppSession session(project);
        session.getTransport().setPositionBeats(2.0);

        auto ready = makeReadyTimelinePreparationResult(*clip, { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f });
        auto completion = projectname::completeBackgroundTimelinePlaybackPreparation(session, std::move(ready), 10.0);
        expect(completion.status == projectname::TimelinePlaybackPreparationCompletionStatus::scheduledImportedClip,
               "Timeline completion schedules matching prepared result");
        expect(session.getTransport().isPlaying(),
               "Timeline completion scheduling starts project transport");
        expect(completion.playback.usedCachedBuffer,
               "Timeline completion scheduling uses the prepared cache");
        expect(completion.playback.preparedSamples != nullptr
                   && std::abs((*completion.playback.preparedSamples)[0] - 0.1f) < 0.0001f,
               "Timeline completion scheduling returns expected prepared samples");
    }
}

void appSessionImportsAudioWithoutResumingGeneratedTone()
{
    projectname::AppSession session;
    session.play();
    expect(session.shouldPlayGeneratedTone(), "Session import test starts with generated tone active");

    const auto package = makeTemporaryPackagePath("projectname-session-import-test");
    const auto wavPath = makeTemporaryAudioPath("projectname-session-import-source-test");
    writePcm16Wav(wavPath, 44100, 1, { 4096, -4096, 2048 });

    std::string error;
    auto result = session.importPcm16WavIntoProjectPackage(package, wavPath, error);
    expect(result.has_value(), "Session imports PCM16 WAV into project package");
    expect(!session.shouldPlayGeneratedTone(), "Session import disables generated-tone playback intent");
    expect(result.has_value() && result->clip.relativePath.rfind("audio/", 0) == 0,
           "Session import returns package-relative audio path");
    expect(result.has_value() && !result->preparedClip.samples.empty(),
           "Session import returns prepared audio samples");
    expect(result.has_value() && std::abs(result->clip.startBeats - 4.0) < 0.0001,
           "Session import uses deterministic non-overlapping start beat");

    if (result.has_value())
    {
        expect(session.setImportedAudioClipStartBeats(result->clip.id, 6.5, error),
               "Session places imported audio clip");
        expect(session.saveProjectPackage(package, error),
               "Session saves placed imported audio clip");
    }

    projectname::AppSession loadedSession;
    expect(loadedSession.loadProjectPackage(package, error),
           "Session-imported project package loads");
    expect(loadedSession.getProject() == session.getProject(),
           "Session-imported project round trips through manifest");
    const auto* loadedClip = result.has_value() ? findClipById(loadedSession.getProject(), result->clip.id) : nullptr;
    expect(loadedClip != nullptr && std::abs(loadedClip->startBeats - 6.5) < 0.0001,
           "Session placement persists through save/load");

    expect(std::filesystem::remove(wavPath), "Temporary session import WAV deleted");
    expect(std::filesystem::remove_all(package) > 0, "Temporary session import package deleted");
}

void appSessionLoadFailureKeepsCurrentProject()
{
    projectname::AppSession session;
    session.getProject().setName("Keep Me");
    session.setTempoBpm(133.0);
    const auto originalProject = session.getProject();

    const auto invalidPackage = makeTemporaryPackagePath("projectname-session-invalid-test");
    writeManifestText(invalidPackage, "{ invalid");

    std::string error;
    expect(!session.loadProjectPackage(invalidPackage, error), "Session rejects invalid package");
    expect(session.getProject() == originalProject, "Session keeps current project after failed load");

    expect(std::filesystem::remove_all(invalidPackage) > 0, "Temporary invalid session package deleted");
}

void projectManifestDirectoryLoadFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Keep Manifest Directory Baseline");
    session.setTempoBpm(101.0);
    const auto originalProject = session.getProject();

    const auto package = makeTemporaryPackagePath("projectname-manifest-directory-load-test");
    const auto manifestDirectory = package / "manifest.json";
    const auto sentinelPath = manifestDirectory / "sentinel.txt";
    writeTextFile(sentinelPath, "manifest directory sentinel");

    std::string error;
    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(!loaded.has_value(),
           "Project load rejects a package whose manifest path is a directory");
    expect(error.find("manifest") != std::string::npos
               && error.find("not a regular file") != std::string::npos,
           "Manifest directory load failure error is human-readable");
    expect(error.find("JSON") == std::string::npos,
           "Manifest directory load failure does not parse the directory as JSON");
    expect(std::filesystem::is_directory(manifestDirectory),
           "Manifest directory load failure leaves the occupied manifest path unchanged");
    expect(readTextFile(sentinelPath) == "manifest directory sentinel",
           "Manifest directory load failure preserves directory contents");

    expect(!session.loadProjectPackage(package, error),
           "Session rejects a package whose manifest path is a directory");
    expect(session.getProject() == originalProject,
           "Session keeps current project after manifest directory load failure");

    expect(std::filesystem::remove_all(package) > 0,
           "Temporary manifest-directory load package deleted");
}

void projectDirectPackageSymlinkLoadFailureRejectsBeforeParsingManifest()
{
    const auto packageSymlink =
        makeTemporaryPackagePath("projectname-direct-package-load-symlink-link-test");
    const auto linkedTargetPackage =
        makeTemporaryPackagePath("projectname-direct-package-load-symlink-target-test");
    const auto linkedTargetSentinel = linkedTargetPackage / "sentinel.txt";
    const auto linkedManifestText =
        R"({"manifestVersion":1,"name":"Direct Package Symlink Manifest Should Not Load","transport":{"tempoBpm":88.0,"timeSignature":{"numerator":5,"denominator":4},"positionBeats":2.0},"tracks":[]})";

    writeManifestText(linkedTargetPackage, linkedManifestText);
    writeTextFile(linkedTargetSentinel, "direct package load sentinel");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedTargetPackage, packageSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(linkedTargetPackage);
        return;
    }

    std::string error = "stale direct package symlink load error";
    auto loaded = projectname::ProjectModel::loadPackage(packageSymlink, error);
    expect(!loaded.has_value(),
           "Project load rejects a direct package directory symlink");
    expect(error.find("Project package path contains a symlink") != std::string::npos
               && error.find(packageSymlink.generic_string()) != std::string::npos,
           "Direct package symlink load failure error is human-readable");
    expect(error.find("JSON") == std::string::npos,
           "Direct package symlink load failure does not parse JSON through the symlink");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(packageSymlink)),
           "Direct package symlink load failure leaves the package symlink unchanged");
    expect(readTextFile(linkedTargetPackage / "manifest.json") == linkedManifestText,
           "Direct package symlink load failure preserves the linked target manifest");
    expect(readTextFile(linkedTargetSentinel) == "direct package load sentinel",
           "Direct package symlink load failure preserves the linked target sentinel");
    expect(!std::filesystem::exists(packageSymlink / "manifest.json.tmp"),
           "Direct package symlink load failure does not create a temporary manifest through the link");
    expect(!std::filesystem::exists(linkedTargetPackage / "manifest.json.tmp"),
           "Direct package symlink load failure does not write target temporary manifest");

    expect(std::filesystem::remove(packageSymlink),
           "Temporary direct package load symlink deleted");
    expect(std::filesystem::remove_all(linkedTargetPackage) > 0,
           "Temporary direct package load symlink target deleted");
}

void projectBrokenDirectPackageSymlinkLoadFailureRejectsBeforeManifestWork()
{
    const auto packageSymlink =
        makeTemporaryPackagePath("projectname-broken-direct-package-load-symlink-link-test");
    const auto missingTargetPackage =
        makeTemporaryPackagePath("projectname-broken-direct-package-load-symlink-target-test");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingTargetPackage, packageSymlink, symlinkError);
    if (symlinkError)
        return;

    std::string error = "stale broken direct package symlink load error";
    auto loaded = projectname::ProjectModel::loadPackage(packageSymlink, error);
    expect(!loaded.has_value(),
           "Project load rejects a broken direct package directory symlink");
    expect(error.find("Project package path contains a symlink") != std::string::npos
               && error.find(packageSymlink.generic_string()) != std::string::npos,
           "Broken direct package symlink load failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "Broken direct package symlink load failure is not reported as a missing manifest");
    expect(error.find("JSON") == std::string::npos,
           "Broken direct package symlink load failure does not parse JSON through the symlink");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(packageSymlink)),
           "Broken direct package symlink load failure leaves the package symlink unchanged");
    expect(!std::filesystem::exists(missingTargetPackage),
           "Broken direct package symlink load failure does not create the missing target");
    expect(!std::filesystem::exists(packageSymlink / "manifest.json.tmp"),
           "Broken direct package symlink load failure does not create a temporary manifest through the link");
    expect(!std::filesystem::exists(missingTargetPackage / "manifest.json.tmp"),
           "Broken direct package symlink load failure does not write target temporary manifest");

    expect(std::filesystem::remove(packageSymlink),
           "Temporary broken direct package load symlink deleted");
    std::filesystem::remove_all(missingTargetPackage);
}

void appSessionDirectPackageSymlinkLoadFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Keep Direct Package Symlink Session Baseline");
    session.setTempoBpm(119.0);
    const auto originalProject = session.getProject();

    const auto packageSymlink =
        makeTemporaryPackagePath("projectname-session-direct-package-load-symlink-link-test");
    const auto linkedTargetPackage =
        makeTemporaryPackagePath("projectname-session-direct-package-load-symlink-target-test");
    const auto linkedTargetSentinel = linkedTargetPackage / "sentinel.txt";
    const auto linkedManifestText =
        R"({"manifestVersion":1,"name":"Direct Session Package Symlink Manifest Should Not Load","transport":{"tempoBpm":88.0,"timeSignature":{"numerator":5,"denominator":4},"positionBeats":2.0},"tracks":[]})";

    writeManifestText(linkedTargetPackage, linkedManifestText);
    writeTextFile(linkedTargetSentinel, "direct session package load sentinel");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedTargetPackage, packageSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(linkedTargetPackage);
        return;
    }

    std::string error = "stale session direct package symlink load error";
    expect(!session.loadProjectPackage(packageSymlink, error),
           "Session load rejects a direct package directory symlink");
    expect(error.find("Project package path contains a symlink") != std::string::npos
               && error.find(packageSymlink.generic_string()) != std::string::npos,
           "Session direct package symlink load failure error is human-readable");
    expect(error.find("JSON") == std::string::npos,
           "Session direct package symlink load failure does not parse JSON through the symlink");
    expect(session.getProject() == originalProject,
           "Session direct package symlink load failure keeps current session project");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(packageSymlink)),
           "Session direct package symlink load failure leaves the package symlink unchanged");
    expect(readTextFile(linkedTargetPackage / "manifest.json") == linkedManifestText,
           "Session direct package symlink load failure preserves the linked target manifest");
    expect(readTextFile(linkedTargetSentinel) == "direct session package load sentinel",
           "Session direct package symlink load failure preserves the linked target sentinel");
    expect(!std::filesystem::exists(packageSymlink / "manifest.json.tmp"),
           "Session direct package symlink load failure does not create a temporary manifest through the link");
    expect(!std::filesystem::exists(linkedTargetPackage / "manifest.json.tmp"),
           "Session direct package symlink load failure does not write target temporary manifest");

    expect(std::filesystem::remove(packageSymlink),
           "Temporary session direct package load symlink deleted");
    expect(std::filesystem::remove_all(linkedTargetPackage) > 0,
           "Temporary session direct package load symlink target deleted");
}

void appSessionBrokenDirectPackageSymlinkLoadFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Keep Broken Direct Package Symlink Session Baseline");
    session.setTempoBpm(121.0);
    const auto originalProject = session.getProject();

    const auto packageSymlink =
        makeTemporaryPackagePath("projectname-session-broken-direct-package-load-symlink-link-test");
    const auto missingTargetPackage =
        makeTemporaryPackagePath("projectname-session-broken-direct-package-load-symlink-target-test");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingTargetPackage, packageSymlink, symlinkError);
    if (symlinkError)
        return;

    std::string error = "stale session broken direct package symlink load error";
    expect(!session.loadProjectPackage(packageSymlink, error),
           "Session load rejects a broken direct package directory symlink");
    expect(error.find("Project package path contains a symlink") != std::string::npos
               && error.find(packageSymlink.generic_string()) != std::string::npos,
           "Session broken direct package symlink load failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "Session broken direct package symlink load failure is not reported as a missing manifest");
    expect(error.find("JSON") == std::string::npos,
           "Session broken direct package symlink load failure does not parse JSON through the symlink");
    expect(session.getProject() == originalProject,
           "Session broken direct package symlink load failure keeps current session project");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(packageSymlink)),
           "Session broken direct package symlink load failure leaves the package symlink unchanged");
    expect(!std::filesystem::exists(missingTargetPackage),
           "Session broken direct package symlink load failure does not create the missing target");
    expect(!std::filesystem::exists(packageSymlink / "manifest.json.tmp"),
           "Session broken direct package symlink load failure does not create a temporary manifest through the link");
    expect(!std::filesystem::exists(missingTargetPackage / "manifest.json.tmp"),
           "Session broken direct package symlink load failure does not write target temporary manifest");

    expect(std::filesystem::remove(packageSymlink),
           "Temporary session broken direct package load symlink deleted");
    std::filesystem::remove_all(missingTargetPackage);
}

void projectLinkedPackageParentSymlinkLoadFailureRejectsBeforeParsingManifest()
{
    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-linked-package-parent-load-symlink-link-test");
    const auto linkedParentTarget =
        makeTemporaryPackagePath("projectname-linked-package-parent-load-symlink-target-test");
    const auto linkedTargetPackage = linkedParentTarget / "Nested Load.project";
    const auto requestedPackage = parentSymlink / "Nested Load.project";
    const auto linkedParentSentinel = linkedParentTarget / "sentinel.txt";
    const auto linkedManifestText =
        R"({"manifestVersion":1,"name":"Linked Parent Manifest Should Not Load","transport":{"tempoBpm":88.0,"timeSignature":{"numerator":5,"denominator":4},"positionBeats":2.0},"tracks":[]})";

    writeManifestText(linkedTargetPackage, linkedManifestText);
    writeTextFile(linkedParentSentinel, "linked parent load sentinel");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    std::string error = "stale linked package-parent load error";
    auto loaded = projectname::ProjectModel::loadPackage(requestedPackage, error);
    expect(!loaded.has_value(),
           "Project load rejects a package through a linked intermediate parent");
    expect(error.find("Project package path contains a symlink") != std::string::npos
               && error.find(parentSymlink.generic_string()) != std::string::npos,
           "Linked package-parent load failure error is human-readable");
    expect(error.find("JSON") == std::string::npos,
           "Linked package-parent load failure does not parse JSON through the symlink");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Linked package-parent load failure leaves the parent symlink unchanged");
    expect(readTextFile(linkedTargetPackage / "manifest.json") == linkedManifestText,
           "Linked package-parent load failure preserves the linked target manifest");
    expect(readTextFile(linkedParentSentinel) == "linked parent load sentinel",
           "Linked package-parent load failure preserves the linked target sentinel");
    expect(!std::filesystem::exists(requestedPackage / "manifest.json.tmp"),
           "Linked package-parent load failure does not create a temporary manifest through the link");
    expect(!std::filesystem::exists(linkedTargetPackage / "manifest.json.tmp"),
           "Linked package-parent load failure does not write target temporary manifest");

    expect(std::filesystem::remove(parentSymlink),
           "Temporary linked package-parent load symlink deleted");
    expect(std::filesystem::remove_all(linkedParentTarget) > 0,
           "Temporary linked package-parent load target deleted");
}

void projectLinkedPackageParentSymlinkLoadFailureLeavesAppSettingsUntouched()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Linked Parent Isolation Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;
    settings.audioSetup.preferredOutput.juceDeviceStateXml =
        "<DEVICESETUP deviceType=\"Windows Audio\" audioOutputDeviceName=\"Linked Parent Isolation Output\"/>";

    const auto settingsPath =
        makeTemporarySettingsPath("projectname-linked-package-parent-load-settings-isolation-test");

    std::string error;
    expect(projectname::saveAppSettings(settings, settingsPath, error),
           "Linked package-parent settings-isolation fixture saves app settings");
    const auto originalSettingsText = readTextFile(settingsPath);

    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-linked-package-parent-load-settings-isolation-link-test");
    const auto linkedParentTarget =
        makeTemporaryPackagePath("projectname-linked-package-parent-load-settings-isolation-target-test");
    const auto linkedTargetPackage = linkedParentTarget / "Nested Load.project";
    const auto requestedPackage = parentSymlink / "Nested Load.project";
    const auto linkedParentSentinel = linkedParentTarget / "sentinel.txt";
    const auto linkedManifestText =
        R"({"manifestVersion":1,"name":"Linked Parent Settings Isolation Manifest Should Not Load","transport":{"tempoBpm":88.0,"timeSignature":{"numerator":5,"denominator":4},"positionBeats":2.0},"tracks":[]})";

    writeManifestText(linkedTargetPackage, linkedManifestText);
    writeTextFile(linkedParentSentinel, "linked parent settings-isolation sentinel");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(linkedParentTarget);
        std::filesystem::remove(settingsPath);
        return;
    }

    error = "stale linked package-parent settings-isolation load error";
    auto loaded = projectname::ProjectModel::loadPackage(requestedPackage, error);
    expect(!loaded.has_value(),
           "Project load rejects a linked intermediate parent during settings-isolation check");
    expect(error.find("Project package path contains a symlink") != std::string::npos
               && error.find(parentSymlink.generic_string()) != std::string::npos,
           "Linked package-parent settings-isolation load failure error is human-readable");
    expect(error.find("JSON") == std::string::npos,
           "Linked package-parent settings-isolation load failure does not parse JSON through the symlink");
    expect(readTextFile(settingsPath) == originalSettingsText,
           "Linked package-parent settings-isolation load failure leaves app settings unchanged");

    std::string settingsError;
    const auto reloadedSettings = projectname::loadAppSettings(settingsPath, settingsError);
    expect(reloadedSettings.has_value() && *reloadedSettings == settings,
           "Linked package-parent settings-isolation load failure leaves app settings loadable");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Linked package-parent settings-isolation load failure leaves the parent symlink unchanged");
    expect(readTextFile(linkedTargetPackage / "manifest.json") == linkedManifestText,
           "Linked package-parent settings-isolation load failure preserves the linked target manifest");
    expect(readTextFile(linkedParentSentinel) == "linked parent settings-isolation sentinel",
           "Linked package-parent settings-isolation load failure preserves the linked target sentinel");
    expect(!std::filesystem::exists(requestedPackage / "manifest.json.tmp"),
           "Linked package-parent settings-isolation load failure does not create a temporary manifest through the link");
    expect(!std::filesystem::exists(linkedTargetPackage / "manifest.json.tmp"),
           "Linked package-parent settings-isolation load failure does not write target temporary manifest");

    expect(std::filesystem::remove(parentSymlink),
           "Temporary linked package-parent settings-isolation symlink deleted");
    expect(std::filesystem::remove_all(linkedParentTarget) > 0,
           "Temporary linked package-parent settings-isolation target deleted");
    expect(std::filesystem::remove(settingsPath),
           "Temporary linked package-parent settings-isolation app settings file deleted");
}

void appSessionLinkedPackageParentSymlinkLoadFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Keep Linked Package Parent Session Baseline");
    session.setTempoBpm(123.0);
    const auto originalProject = session.getProject();

    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-session-linked-package-parent-load-symlink-link-test");
    const auto linkedParentTarget =
        makeTemporaryPackagePath("projectname-session-linked-package-parent-load-symlink-target-test");
    const auto linkedTargetPackage = linkedParentTarget / "Nested Session Load.project";
    const auto requestedPackage = parentSymlink / "Nested Session Load.project";
    const auto linkedParentSentinel = linkedParentTarget / "sentinel.txt";
    const auto linkedManifestText =
        R"({"manifestVersion":1,"name":"Linked Session Parent Manifest Should Not Load","transport":{"tempoBpm":88.0,"timeSignature":{"numerator":5,"denominator":4},"positionBeats":2.0},"tracks":[]})";

    writeManifestText(linkedTargetPackage, linkedManifestText);
    writeTextFile(linkedParentSentinel, "linked session parent load sentinel");

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(linkedParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(linkedParentTarget);
        return;
    }

    std::string error = "stale session linked package-parent load error";
    expect(!session.loadProjectPackage(requestedPackage, error),
           "Session load rejects a package through a linked intermediate parent");
    expect(error.find("Project package path contains a symlink") != std::string::npos
               && error.find(parentSymlink.generic_string()) != std::string::npos,
           "Session linked package-parent load failure error is human-readable");
    expect(error.find("JSON") == std::string::npos,
           "Session linked package-parent load failure does not parse JSON through the symlink");
    expect(session.getProject() == originalProject,
           "Session linked package-parent load failure keeps current session project");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Session linked package-parent load failure leaves the parent symlink unchanged");
    expect(readTextFile(linkedTargetPackage / "manifest.json") == linkedManifestText,
           "Session linked package-parent load failure preserves the linked target manifest");
    expect(readTextFile(linkedParentSentinel) == "linked session parent load sentinel",
           "Session linked package-parent load failure preserves the linked target sentinel");
    expect(!std::filesystem::exists(requestedPackage / "manifest.json.tmp"),
           "Session linked package-parent load failure does not create a temporary manifest through the link");
    expect(!std::filesystem::exists(linkedTargetPackage / "manifest.json.tmp"),
           "Session linked package-parent load failure does not write target temporary manifest");

    expect(std::filesystem::remove(parentSymlink),
           "Temporary session linked package-parent load symlink deleted");
    expect(std::filesystem::remove_all(linkedParentTarget) > 0,
           "Temporary session linked package-parent load target deleted");
}

void appSessionBrokenPackageParentSymlinkLoadFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Keep Broken Package Parent Session Baseline");
    session.setTempoBpm(127.0);
    const auto originalProject = session.getProject();

    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-session-broken-package-parent-load-symlink-link-test");
    const auto missingParentTarget =
        makeTemporaryPackagePath("projectname-session-broken-package-parent-load-symlink-target-test");
    const auto requestedPackage = parentSymlink / "Nested Session Load.project";

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
        return;

    std::string error = "stale session broken package-parent load error";
    expect(!session.loadProjectPackage(requestedPackage, error),
           "Session load rejects a package through a broken intermediate parent");
    expect(error.find("Project package path contains a symlink") != std::string::npos
               && error.find(parentSymlink.generic_string()) != std::string::npos,
           "Session broken package-parent load failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "Session broken package-parent load failure is not reported as a missing manifest");
    expect(error.find("JSON") == std::string::npos,
           "Session broken package-parent load failure does not parse JSON through the symlink");
    expect(session.getProject() == originalProject,
           "Session broken package-parent load failure keeps current session project");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Session broken package-parent load failure leaves the parent symlink unchanged");
    expect(!std::filesystem::exists(missingParentTarget),
           "Session broken package-parent load failure does not create the missing parent target");
    expect(!std::filesystem::exists(missingParentTarget / "Nested Session Load.project"),
           "Session broken package-parent load failure does not create a package through the broken link");
    expect(!std::filesystem::exists(requestedPackage / "manifest.json.tmp"),
           "Session broken package-parent load failure does not create a temporary manifest through the link");
    expect(!std::filesystem::exists(missingParentTarget / "Nested Session Load.project" / "manifest.json.tmp"),
           "Session broken package-parent load failure does not write target temporary manifest");

    expect(std::filesystem::remove(parentSymlink),
           "Temporary session broken package-parent load symlink deleted");
    std::filesystem::remove_all(missingParentTarget);
}

void projectBrokenPackageParentSymlinkLoadFailureRejectsBeforeManifestWork()
{
    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-broken-package-parent-load-symlink-link-test");
    const auto missingParentTarget =
        makeTemporaryPackagePath("projectname-broken-package-parent-load-symlink-target-test");
    const auto requestedPackage = parentSymlink / "Nested Load.project";

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
        return;

    std::string error = "stale broken package-parent load error";
    auto loaded = projectname::ProjectModel::loadPackage(requestedPackage, error);
    expect(!loaded.has_value(),
           "Project load rejects a package through a broken intermediate parent");
    expect(error.find("Project package path contains a symlink") != std::string::npos
               && error.find(parentSymlink.generic_string()) != std::string::npos,
           "Broken package-parent load failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "Broken package-parent load failure is not reported as a missing manifest");
    expect(error.find("JSON") == std::string::npos,
           "Broken package-parent load failure does not parse JSON through the symlink");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Broken package-parent load failure leaves the parent symlink unchanged");
    expect(!std::filesystem::exists(missingParentTarget),
           "Broken package-parent load failure does not create the missing parent target");
    expect(!std::filesystem::exists(missingParentTarget / "Nested Load.project"),
           "Broken package-parent load failure does not create a package through the broken link");
    expect(!std::filesystem::exists(requestedPackage / "manifest.json.tmp"),
           "Broken package-parent load failure does not create a temporary manifest through the link");
    expect(!std::filesystem::exists(missingParentTarget / "Nested Load.project" / "manifest.json.tmp"),
           "Broken package-parent load failure does not write target temporary manifest");

    expect(std::filesystem::remove(parentSymlink),
           "Temporary broken package-parent load symlink deleted");
    std::filesystem::remove_all(missingParentTarget);
}

void projectBrokenPackageParentSymlinkLoadFailureLeavesAppSettingsUntouched()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Broken Parent Isolation Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;
    settings.audioSetup.preferredOutput.juceDeviceStateXml =
        "<DEVICESETUP deviceType=\"Windows Audio\" audioOutputDeviceName=\"Broken Parent Isolation Output\"/>";

    const auto settingsPath =
        makeTemporarySettingsPath("projectname-broken-package-parent-load-settings-isolation-test");

    std::string error;
    expect(projectname::saveAppSettings(settings, settingsPath, error),
           "Broken package-parent settings-isolation fixture saves app settings");
    const auto originalSettingsText = readTextFile(settingsPath);

    const auto parentSymlink =
        makeTemporaryPackagePath("projectname-broken-package-parent-load-settings-isolation-link-test");
    const auto missingParentTarget =
        makeTemporaryPackagePath("projectname-broken-package-parent-load-settings-isolation-target-test");
    const auto requestedPackage = parentSymlink / "Nested Load.project";

    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(missingParentTarget, parentSymlink, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove(settingsPath);
        return;
    }

    error = "stale broken package-parent settings-isolation load error";
    auto loaded = projectname::ProjectModel::loadPackage(requestedPackage, error);
    expect(!loaded.has_value(),
           "Project load rejects a broken intermediate parent during settings-isolation check");
    expect(error.find("Project package path contains a symlink") != std::string::npos
               && error.find(parentSymlink.generic_string()) != std::string::npos,
           "Broken package-parent settings-isolation load failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "Broken package-parent settings-isolation load failure is not reported as a missing manifest");
    expect(error.find("JSON") == std::string::npos,
           "Broken package-parent settings-isolation load failure does not parse JSON through the symlink");
    expect(readTextFile(settingsPath) == originalSettingsText,
           "Broken package-parent settings-isolation load failure leaves app settings unchanged");

    std::string settingsError;
    const auto reloadedSettings = projectname::loadAppSettings(settingsPath, settingsError);
    expect(reloadedSettings.has_value() && *reloadedSettings == settings,
           "Broken package-parent settings-isolation load failure leaves app settings loadable");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(parentSymlink)),
           "Broken package-parent settings-isolation load failure leaves the parent symlink unchanged");
    expect(!std::filesystem::exists(missingParentTarget),
           "Broken package-parent settings-isolation load failure does not create the missing parent target");
    expect(!std::filesystem::exists(missingParentTarget / "Nested Load.project"),
           "Broken package-parent settings-isolation load failure does not create a package through the broken link");
    expect(!std::filesystem::exists(requestedPackage / "manifest.json.tmp"),
           "Broken package-parent settings-isolation load failure does not create a temporary manifest through the link");
    expect(!std::filesystem::exists(missingParentTarget / "Nested Load.project" / "manifest.json.tmp"),
           "Broken package-parent settings-isolation load failure does not write target temporary manifest");

    expect(std::filesystem::remove(parentSymlink),
           "Temporary broken package-parent settings-isolation symlink deleted");
    std::filesystem::remove_all(missingParentTarget);
    expect(std::filesystem::remove(settingsPath),
           "Temporary broken package-parent settings-isolation app settings file deleted");
}

void projectManifestDirectoryLoadFailureLeavesAppSettingsUntouched()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Manifest Directory Isolation Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;
    settings.audioSetup.preferredOutput.juceDeviceStateXml =
        "<DEVICESETUP deviceType=\"Windows Audio\" audioOutputDeviceName=\"Manifest Directory Isolation Output\"/>";

    const auto settingsPath =
        makeTemporarySettingsPath("projectname-manifest-directory-load-settings-isolation-test");

    std::string error;
    expect(projectname::saveAppSettings(settings, settingsPath, error),
           "Manifest-directory settings-isolation fixture saves app settings");
    const auto originalSettingsText = readTextFile(settingsPath);

    projectname::AppSession session;
    session.getProject().setName("Keep Manifest Directory Settings Isolation Baseline");
    session.setTempoBpm(113.0);
    const auto originalProject = session.getProject();

    const auto package =
        makeTemporaryPackagePath("projectname-manifest-directory-load-settings-isolation-test");
    const auto manifestDirectory = package / "manifest.json";
    const auto sentinelPath = manifestDirectory / "sentinel.txt";
    writeTextFile(sentinelPath, "manifest directory settings-isolation sentinel");

    expect(!session.loadProjectPackage(package, error),
           "Session rejects manifest-directory package during settings-isolation check");
    expect(error.find("manifest") != std::string::npos
               && error.find("not a regular file") != std::string::npos,
           "Manifest-directory settings-isolation load failure error is human-readable");
    expect(session.getProject() == originalProject,
           "Manifest-directory settings-isolation load failure keeps current session project");
    expect(readTextFile(settingsPath) == originalSettingsText,
           "Manifest-directory settings-isolation load failure leaves app settings unchanged");

    std::string settingsError;
    const auto reloadedSettings = projectname::loadAppSettings(settingsPath, settingsError);
    expect(reloadedSettings.has_value() && *reloadedSettings == settings,
           "Manifest-directory settings-isolation load failure leaves app settings loadable");
    expect(std::filesystem::is_directory(manifestDirectory),
           "Manifest-directory settings-isolation load failure leaves manifest directory unchanged");
    expect(readTextFile(sentinelPath) == "manifest directory settings-isolation sentinel",
           "Manifest-directory settings-isolation load failure preserves directory contents");
    expect(!std::filesystem::exists(package / "manifest.json.tmp"),
           "Manifest-directory settings-isolation load failure does not create a temporary manifest");

    expect(std::filesystem::remove_all(package) > 0,
           "Temporary manifest-directory settings-isolation package deleted");
    expect(std::filesystem::remove(settingsPath),
           "Temporary manifest-directory settings-isolation app settings file deleted");
}

void projectManifestSymlinkLoadFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Keep Manifest Symlink Baseline");
    session.setTempoBpm(104.0);
    const auto originalProject = session.getProject();

    const auto package = makeTemporaryPackagePath("projectname-manifest-symlink-load-test");
    const auto manifestSymlinkPath = package / "manifest.json";
    const auto symlinkTargetPath = package / "linked-manifest-target.json";
    const auto linkedManifestText =
        R"({"manifestVersion":1,"name":"Linked Manifest Should Not Load","transport":{"tempoBpm":88.0,"timeSignature":{"numerator":5,"denominator":4},"positionBeats":2.0},"tracks":[]})";
    writeTextFile(symlinkTargetPath, linkedManifestText);

    std::error_code symlinkError;
    std::filesystem::create_symlink(symlinkTargetPath, manifestSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        return;
    }

    std::string error;
    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(!loaded.has_value(),
           "Project load rejects a package whose manifest path is a symlink");
    expect(error.find("manifest") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "Manifest symlink load failure error is human-readable");
    expect(error.find("JSON") == std::string::npos,
           "Manifest symlink load failure does not parse JSON through the symlink");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(manifestSymlinkPath)),
           "Manifest symlink load failure leaves the manifest symlink unchanged");
    expect(readTextFile(symlinkTargetPath) == linkedManifestText,
           "Manifest symlink load failure preserves the symlink target contents");

    expect(!session.loadProjectPackage(package, error),
           "Session rejects a package whose manifest path is a symlink");
    expect(session.getProject() == originalProject,
           "Session keeps current project after manifest symlink load failure");
    expect(!std::filesystem::exists(package / "manifest.json.tmp"),
           "Manifest symlink load failure does not create a temporary manifest");

    expect(std::filesystem::remove_all(package) > 0,
           "Temporary manifest-symlink load package deleted");
}

void projectManifestBrokenSymlinkLoadFailureKeepsSessionProject()
{
    projectname::AppSession session;
    session.getProject().setName("Keep Manifest Broken Symlink Baseline");
    session.setTempoBpm(107.0);
    const auto originalProject = session.getProject();

    const auto package = makeTemporaryPackagePath("projectname-manifest-broken-symlink-load-test");
    const auto manifestSymlinkPath = package / "manifest.json";
    const auto brokenTargetPath = package / "missing-manifest-target.json";
    std::filesystem::create_directories(package);

    std::error_code symlinkError;
    std::filesystem::create_symlink(brokenTargetPath, manifestSymlinkPath, symlinkError);
    if (symlinkError)
    {
        std::filesystem::remove_all(package);
        return;
    }

    std::string error;
    auto loaded = projectname::ProjectModel::loadPackage(package, error);
    expect(!loaded.has_value(),
           "Project load rejects a package whose manifest path is a broken symlink");
    expect(error.find("manifest") != std::string::npos
               && error.find("symlink") != std::string::npos,
           "Manifest broken-symlink load failure error is human-readable");
    expect(error.find("not found") == std::string::npos,
           "Manifest broken-symlink load failure is not reported as a missing manifest");
    expect(std::filesystem::is_symlink(std::filesystem::symlink_status(manifestSymlinkPath)),
           "Manifest broken-symlink load failure leaves the manifest symlink unchanged");
    expect(!std::filesystem::exists(brokenTargetPath),
           "Manifest broken-symlink load failure does not create the missing target");

    expect(!session.loadProjectPackage(package, error),
           "Session rejects a package whose manifest path is a broken symlink");
    expect(session.getProject() == originalProject,
           "Session keeps current project after manifest broken-symlink load failure");
    expect(!std::filesystem::exists(brokenTargetPath),
           "Session broken-symlink load failure does not create the missing target");
    expect(!std::filesystem::exists(package / "manifest.json.tmp"),
           "Manifest broken-symlink load failure does not create a temporary manifest");

    expect(std::filesystem::remove(manifestSymlinkPath),
           "Temporary broken manifest symlink deleted");
    expect(std::filesystem::remove_all(package) > 0,
           "Temporary broken manifest-symlink load package deleted");
}

void projectManifestSymlinkLoadFailuresLeaveAppSettingsUntouched()
{
    projectname::AppSettings settings;
    settings.audioSetup.firstRunPromptDismissed = true;
    settings.audioSetup.preferredOutput.hasOutputDevice = true;
    settings.audioSetup.preferredOutput.deviceType = "Windows Audio";
    settings.audioSetup.preferredOutput.deviceName = "Project Load Isolation Output";
    settings.audioSetup.preferredOutput.sampleRateHz = 48000.0;
    settings.audioSetup.preferredOutput.bufferSizeSamples = 256;
    settings.audioSetup.preferredOutput.outputChannelCount = 2;
    settings.audioSetup.preferredOutput.juceDeviceStateXml =
        "<DEVICESETUP deviceType=\"Windows Audio\" audioOutputDeviceName=\"Project Load Isolation Output\"/>";

    const auto settingsPath =
        makeTemporarySettingsPath("projectname-manifest-symlink-load-settings-isolation-test");

    std::string error;
    expect(projectname::saveAppSettings(settings, settingsPath, error),
           "Project load settings-isolation fixture saves app settings");
    const auto originalSettingsText = readTextFile(settingsPath);

    auto exerciseLoadFailure = [&](bool brokenLink)
    {
        projectname::AppSession session;
        session.getProject().setName(brokenLink
                                         ? "Keep Broken Manifest Settings Isolation Baseline"
                                         : "Keep Manifest Settings Isolation Baseline");
        session.setTempoBpm(brokenLink ? 117.0 : 115.0);
        const auto originalProject = session.getProject();

        const auto package = makeTemporaryPackagePath(
            brokenLink
                ? "projectname-broken-manifest-symlink-load-settings-isolation-test"
                : "projectname-manifest-symlink-load-settings-isolation-test");
        const auto manifestSymlinkPath = package / "manifest.json";
        const auto symlinkTargetPath = brokenLink
            ? package / "missing-manifest-target.json"
            : package / "linked-manifest-target.json";

        std::filesystem::create_directories(package);
        if (!brokenLink)
        {
            writeTextFile(symlinkTargetPath,
                          R"({"manifestVersion":1,"name":"Linked Manifest Should Not Touch Settings","transport":{"tempoBpm":88.0,"timeSignature":{"numerator":5,"denominator":4},"positionBeats":2.0},"tracks":[]})");
        }

        std::error_code symlinkError;
        std::filesystem::create_symlink(symlinkTargetPath, manifestSymlinkPath, symlinkError);
        if (symlinkError)
        {
            std::filesystem::remove_all(package);
            return;
        }

        error = brokenLink
            ? "stale broken manifest load settings-isolation error"
            : "stale manifest load settings-isolation error";
        expect(!session.loadProjectPackage(package, error),
               brokenLink
                   ? "Session rejects broken manifest symlink package during settings-isolation check"
                   : "Session rejects manifest symlink package during settings-isolation check");
        expect(error.find("manifest") != std::string::npos
                   && error.find("symlink") != std::string::npos,
               brokenLink
                   ? "Broken manifest settings-isolation load failure error is human-readable"
                   : "Manifest settings-isolation load failure error is human-readable");
        expect(session.getProject() == originalProject,
               brokenLink
                   ? "Broken manifest settings-isolation load failure keeps current session project"
                   : "Manifest settings-isolation load failure keeps current session project");
        expect(readTextFile(settingsPath) == originalSettingsText,
               brokenLink
                   ? "Broken manifest settings-isolation load failure leaves app settings unchanged"
                   : "Manifest settings-isolation load failure leaves app settings unchanged");

        std::string settingsError;
        const auto reloadedSettings = projectname::loadAppSettings(settingsPath, settingsError);
        expect(reloadedSettings.has_value() && *reloadedSettings == settings,
               brokenLink
                   ? "Broken manifest settings-isolation load failure leaves app settings loadable"
                   : "Manifest settings-isolation load failure leaves app settings loadable");
        expect(!std::filesystem::exists(package / "manifest.json.tmp"),
               brokenLink
                   ? "Broken manifest settings-isolation load failure does not create a temporary manifest"
                   : "Manifest settings-isolation load failure does not create a temporary manifest");

        if (brokenLink)
        {
            expect(!std::filesystem::exists(symlinkTargetPath),
                   "Broken manifest settings-isolation load failure does not create the missing target");
            expect(std::filesystem::remove(manifestSymlinkPath),
                   "Temporary settings-isolation broken manifest symlink deleted");
        }

        expect(std::filesystem::remove_all(package) > 0,
               brokenLink
                   ? "Temporary settings-isolation broken manifest package deleted"
                   : "Temporary settings-isolation manifest package deleted");
    };

    exerciseLoadFailure(false);
    exerciseLoadFailure(true);

    expect(readTextFile(settingsPath) == originalSettingsText,
           "Project load settings-isolation fixture leaves app settings unchanged after all failures");
    expect(std::filesystem::remove(settingsPath),
           "Temporary project load settings-isolation app settings file deleted");
}
} // namespace

int main()
{
    appSessionKeepsTransportInsideProjectModel();
    appSessionTimelineViewportStateClampsValues();
    timelineViewportFitHelperFramesImportedAudioClips();
    timelineViewportCenterHelperFramesSelectedImportedAudioClip();
    workspaceCommandRouterPreservesFocusedWorkspaceShortcuts();
    appCommandRegistryDescribesPrototypeTopBarCommands();
    audioSetupStatusModelsFirstRunReadyAndErrorStates();
    appCommandRoutesImportedClipEditUndoRedo();
    appSessionSelectsImportedAudioClips();
    appSessionTracksImportedClipPlacementUndoHistory();
    appSessionTracksImportedClipMediaReplacementUndoHistory();
    appSessionMediaReplacementUndoInvalidatesPreparedCache();
    appSessionUpdatesStaticTrackMixStateThroughCommand();
    appSessionSavesAndLoadsProjectPackages();
    appSessionSaveManifestSymlinkFailureKeepsSessionProject();
    appSessionSaveBrokenManifestSymlinkFailureKeepsSessionProject();
    appSessionSaveBrokenAssetFolderSymlinkFailureKeepsSessionProject();
    appSessionSaveLinkedAssetFolderSymlinkFailureKeepsSessionProject();
    appSessionLoopRegionCommandsKeepTransportState();
    appSessionAdvanceWrapsEnabledLoopRegion();
    appSessionPreparesImportedTimelinePlaybackFromPlay();
    appSessionCachesMultipleImportedTimelineClips();
    appSessionPreparesCachedTimelineVoiceWindow();
    appSessionVoiceWindowReportsSampleRateMismatchMetadata();
    appSessionVoiceWindowUsesPersistedTrackMixState();
    appSessionVoiceWindowFiltersPersistedMuteAndSoloState();
    appSessionCachedTimelineVoiceWindowReportsMissingBuffers();
    appSessionPreparedTimelineCacheHonorsMemoryBudget();
    backgroundTimelinePlaybackPreparationJobPreparesCacheMiss();
    backgroundTimelinePlaybackPreparationJobPreparesOverlappingVoiceWindowMisses();
    backgroundTimelineCompletionReportsPreparedVoiceWindowSampleRateMismatches();
    backgroundTimelinePlaybackPreparationJobFallsBackForMissingAudio();
    backgroundTimelinePlaybackPreparationJobCancelsBeforeStart();
    timelinePlaybackPreparationCompletionRejectsCancelledAndStaleResults();
    appSessionImportsAudioWithoutResumingGeneratedTone();
    appSessionLoadFailureKeepsCurrentProject();
    projectManifestDirectoryLoadFailureKeepsSessionProject();
    projectDirectPackageSymlinkLoadFailureRejectsBeforeParsingManifest();
    projectBrokenDirectPackageSymlinkLoadFailureRejectsBeforeManifestWork();
    appSessionDirectPackageSymlinkLoadFailureKeepsSessionProject();
    appSessionBrokenDirectPackageSymlinkLoadFailureKeepsSessionProject();
    projectLinkedPackageParentSymlinkLoadFailureRejectsBeforeParsingManifest();
    projectLinkedPackageParentSymlinkLoadFailureLeavesAppSettingsUntouched();
    appSessionLinkedPackageParentSymlinkLoadFailureKeepsSessionProject();
    appSessionBrokenPackageParentSymlinkLoadFailureKeepsSessionProject();
    projectBrokenPackageParentSymlinkLoadFailureRejectsBeforeManifestWork();
    projectBrokenPackageParentSymlinkLoadFailureLeavesAppSettingsUntouched();
    projectManifestDirectoryLoadFailureLeavesAppSettingsUntouched();
    projectManifestSymlinkLoadFailureKeepsSessionProject();
    projectManifestBrokenSymlinkLoadFailureKeepsSessionProject();
    projectManifestSymlinkLoadFailuresLeaveAppSettingsUntouched();
    transportStateAdvancesOnlyWhilePlaying();
    projectManifestRoundTrips();
    appSettingsRoundTripsAudioSetupPreferences();
    appSettingsCommitFailureRemovesTemporaryFile();
    appSettingsTemporaryWriteFailureKeepsExistingSettings();
    appSettingsSaveRemovesStaleTemporarySymlinkWithoutFollowingIt();
    appSettingsSaveRemovesStaleBrokenTemporarySymlinkWithoutFollowingIt();
    appSettingsDirectoryCreationFailurePreservesOccupiedParentPath();
    appSettingsSaveSymlinkPathFailureLeavesTargetsUntouched();
    appSettingsSaveBrokenSymlinkPathFailureLeavesTargetsUntouched();
    appSettingsSaveBrokenParentSymlinkFailureLeavesTargetsUntouched();
    appSettingsEmptyPathFailsBeforeFilesystemWork();
    appSettingsLoadDirectoryPathKeepsCallerFallbackSettings();
    appSettingsLoadSymlinkPathKeepsCallerFallbackSettings();
    appSettingsLoadBrokenSymlinkPathKeepsCallerFallbackSettings();
    appSettingsLoadBrokenParentSymlinkPathKeepsCallerFallbackSettings();
    appSettingsLoadLinkedParentSymlinkPathKeepsCallerFallbackSettings();
    appSettingsLoadsAudioSetupDefaultsFromMinimalJson();
    appSettingsRejectsUnsupportedVersion();
    appSettingsResetClearsAudioSetupPreferences();
    projectPackageSaveAsPolicyBlocksManifestOnlyWhenPackageAssetsNeedCopy();
    projectPackageSaveAsPolicyAllowsSamePackageManifestSave();
    projectPackageSaveAsPolicyPreservesExternalReferences();
    projectPackageSaveAsCopyCommandCopiesPackageAssetsAndStartsFreshBackups();
    projectPackageSaveAsCopyCommandRejectsTargetConflictsBeforeCopy();
    projectPackageSaveAsCopyCommandRejectsTargetSymlinksBeforeCopy();
    projectPackageSaveAsCopyCommandRejectsBrokenTargetSymlinksBeforeCopy();
    projectPackageSaveAsCopyCommandRejectsSourceSymlinksBeforeCopy();
    projectPackageSaveAsCopyCommandRejectsBrokenSourceSymlinksBeforeCopy();
    projectPackageSaveAsCopyCommandRejectsTargetInsideSource();
    projectPackageSaveAsCopyCommandCancelsAndRollsBackPartialTarget();
    backgroundSaveAsPackageCopyJobCopiesAssetsAndReportsProgress();
    backgroundSaveAsPackageCopyJobRejectsLinkedTargetSymlinks();
    backgroundSaveAsPackageCopyJobRejectsBrokenTargetSymlinks();
    backgroundSaveAsPackageCopyJobRejectsBrokenSourceSymlinks();
    backgroundSaveAsPackageCopyJobCancelsBeforeStart();
    projectPackageSaveAsRetryPreflightAllowsManifestOnlyRetryAndStaleTempCleanup();
    projectPackageSaveAsRetryPreflightRejectsTargetManifestConflicts();
    projectPackageSaveAsRetryPreflightRejectsTargetManifestSymlinkConflicts();
    projectPackageSaveAsRetryPreflightRejectsCopiedAssetSymlinks();
    projectPackageSaveAsRetryPreflightRejectsMissingCopiedAssetsAndStaleState();
    projectLoopRegionValidatesAndRoundTrips();
    projectImportedClipSelectionValidatesAndRoundTrips();
    projectTrackMixStateRoundTripsAndLoadsLegacyDefaults();
    projectSavePackagePathFileFailureLeavesOccupiedPathUntouched();
    projectSavePackageDirectoryCreationFailureLeavesOccupiedParentUntouched();
    projectSavePackageSymlinkPathFailureLeavesTargetUntouched();
    projectSaveCreatesPreviousManifestBackup();
    projectSaveBackupFailureKeepsManifestAndRemovesTemporaryManifest();
    projectSaveRemovesStaleTemporaryManifestSymlinkWithoutFollowingIt();
    projectSaveRemovesStaleBrokenTemporaryManifestSymlinkWithoutFollowingIt();
    projectSaveManifestSymlinkFailureLeavesTargetUntouched();
    projectSaveBrokenManifestSymlinkFailureLeavesTargetUntouched();
    projectSaveTemporaryManifestOpenFailureKeepsManifestAndOccupiedPath();
    projectSaveFailsBeforeManifestCommitWhenAssetFolderPathIsFile();
    projectSaveFailsBeforeManifestCommitWhenAssetFolderPathIsSymlink();
    projectSaveFailsBeforeManifestCommitWhenAssetFolderPathIsBrokenSymlink();
    projectSaveRejectsBrokenLaterAssetFolderSymlinkBeforeManifestStaging();
    projectSaveRejectsLinkedLaterAssetFolderSymlinkBeforeManifestStaging();
    projectSaveCommitFailureRemovesTemporaryManifest();
    projectManifestLoadsLegacyTrackWithoutDevices();
    projectManifestFailuresAreRecoverable();
    toneRendererProducesBoundedStereoSignal();
    audioEngineStubRendersOnlyWhileEnabled();
    audioEngineStubRendersInterleavedInt16();
    audioEngineStubPlaysGeneratedClipForPreparedDuration();
    audioEngineStubRejectsInvalidGeneratedClipLengths();
    audioEngineStubSchedulesGeneratedClipOnTimeline();
    audioEngineStubCanSeekIntoScheduledGeneratedClip();
    audioEngineStubStopCancelsScheduledGeneratedClip();
    audioEngineStubSchedulesPreparedMonoClipBuffer();
    audioEngineStubCanSeekIntoPreparedMonoClipBuffer();
    audioEngineStubClampsPreparedMonoClipSamples();
    audioEngineStubRejectsEmptyPreparedMonoClip();
    audioEngineStubSumsPreparedTrackVoicesToStereo();
    timelinePlaybackPlanMapsImportedClipBeatsToSamples();
    timelinePlaybackPlanSchedulesPreparedClipRendering();
    trackVoiceScheduleBuildsMixerReadyVoiceWindows();
    wavAudioImporterLoadsStereoPcm16AsPreparedMono();
    wavAudioImporterCancelsDuringDecode();
    wavImportedPreparedClipStopsAndRestarts();
    wavAudioImporterRejectsUnsupportedFiles();
    projectAudioImportCopiesWavIntoPackageAndPersistsClip();
    importedMediaPackageInventoryClassifiesReferencesAndCandidates();
    importedMediaPackageInventoryReportsUnsafeAndMissingReferences();
    importedMediaPackageInventoryClassifiesStagingDirectories();
    packageMediaQuarantineRestoreManifestRoundTrips();
    packageMediaQuarantineRestoreManifestRejectsUnsafePaths();
    packageMediaQuarantineRestoreManifestRejectsDuplicateDestinations();
    packageMediaQuarantineRestoreManifestLoadsPartialFailureState();
    packageMediaRestoreEntrySelectionTracksExplicitRestorableChoices();
    packageMediaRestoreEntrySelectionModelsReviewAndEmptyStates();
    packageMediaQuarantinePreflightPlansCandidates();
    packageMediaQuarantinePreflightRejectsBlockedInventory();
    packageMediaQuarantinePreflightRejectsEmptyAndDuplicatePlans();
    packageMediaQuarantineCommandMovesCandidates();
    packageMediaQuarantineCommandRollsBackOnDestinationConflict();
    packageMediaQuarantineCommandCleansTempManifestOnMissingSource();
    packageMediaQuarantineRestoreCommandRestoresAllCandidates();
    packageMediaQuarantineRestoreCommandRestoresSelectedEntries();
    packageMediaQuarantineRestoreCommandMarksOccupiedOriginalConflicts();
    packageMediaQuarantineRestoreCommandPersistsMissingQuarantinePath();
    backgroundPackageMediaCleanupJobQuarantinesPackage();
    backgroundPackageMediaCleanupJobRestoresPackage();
    backgroundPackageMediaCleanupJobRestoresSelectedEntries();
    backgroundPackageMediaCleanupJobCancelsBeforeStart();
    backgroundPackageMediaCleanupJobRejectsActivePackageWork();
    backgroundPackageMediaCleanupJobPropagatesCommandFailure();
    packageMediaCleanupStatusMapsInventoryAndPreflightStates();
    packageMediaCleanupStatusMapsQuarantineRestoreAndCancellation();
    packageMediaCleanupBatchDiscoveryListsValidBatchesNewestFirst();
    packageMediaCleanupBatchDiscoveryReportsInvalidAndUnreadableBatches();
    packageMediaMaintenanceViewModelSummarizesEmptyAndCleanupCandidateStates();
    packageMediaMaintenanceViewModelCombinesBatchRowsAndRestoreEnablement();
    packageMediaMaintenanceViewModelCarriesDiscoveryIssues();
    packageMediaMaintenanceBrowserRowsRenderSelectableBatchState();
    importedClipInspectorReportsSelectedOrFirstImportedClipMetadata();
    waveformThumbnailLoaderReportsInvalidAnalysis();
    projectModelPlacesImportedAudioClips();
    timelineClipLaneScalesOrdersAndPreservesWaveformStates();
    timelineClipLaneHitTestsVisibleImportedClipsAndSelection();
    timelineClipLaneScalesAndClipsLoopRange();
    projectAudioImportPlacesClipsDeterministicallyAndAllowsExplicitStart();
    waveformAnalysisRegeneratorRestoresMissingAndInvalidSummaries();
    projectAudioImportUsesUniquePackageFileNames();
    projectAudioImportRejectsInvalidWavWithoutMutatingProject();
    projectAudioImportCancelsDuringDecodeWithoutMutatingProject();
    projectAudioImportCancelsDuringStagedCopyAndCleansUp();
    backgroundAudioImportJobImportsProjectPackage();
    backgroundAudioImportJobReportsFailureWithoutMutatingProject();
    backgroundAudioImportJobCancelsBeforeStart();
    importedClipInspectorEditDraftValidatesStartBeatCommitAndCancel();
    importedClipInspectorEditDraftValidatesMediaRelinkMetadata();
    importedClipMediaRelinkPreparationStagesValidatedMetadata();
    importedClipMediaRelinkPreparationRejectsInvalidSourceWithoutStaging();
    importedClipMediaRelinkPreparationCancelsAndCleansStaging();
    importedClipMediaRelinkCommitCleansStaleSelection();
    backgroundMediaRelinkPreparationJobPreparesSelectedClip();
    backgroundMediaRelinkPreparationJobReportsInvalidSource();
    backgroundMediaRelinkPreparationJobCancelsBeforeStart();

    if (failures == 0)
    {
        std::cout << "All Rabbington Studio tests passed.\n";
        return 0;
    }

    std::cerr << failures << " Rabbington Studio test(s) failed.\n";
    return 1;
}
