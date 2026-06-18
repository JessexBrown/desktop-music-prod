#include "core/AppSession.h"
#include "core/AppCommandRegistry.h"
#include "core/AudioEngineStub.h"
#include "core/BackgroundAudioImportJob.h"
#include "core/BackgroundTimelinePlaybackPreparationJob.h"
#include "core/BackgroundWaveformAnalysisJob.h"
#include "core/ImportedClipInspector.h"
#include "core/ProjectAudioImport.h"
#include "core/ProjectModel.h"
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

std::filesystem::path makeTemporaryPackagePath(const std::string& prefix)
{
    return std::filesystem::temp_directory_path()
        / (prefix + "-" + std::to_string(std::random_device {}()) + ".project");
}

std::filesystem::path makeTemporaryAudioPath(const std::string& prefix)
{
    return std::filesystem::temp_directory_path()
        / (prefix + "-" + std::to_string(std::random_device {}()) + ".wav");
}

void writeManifestText(const std::filesystem::path& package, const std::string& manifestText)
{
    std::filesystem::create_directories(package);
    std::ofstream manifest(package / "manifest.json", std::ios::trunc);
    manifest << manifestText;
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
    expect(secondResult.has_value(), "Imported clip inspector test imports second PCM16 WAV");

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
    expect(registry.size() == 8, "App command registry exposes the prototype top-bar actions");

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
    expectCommand(projectname::AppCommandIds::projectSave,
                  "Save",
                  projectname::AppCommandScope::project,
                  true,
                  "App command registry contains Save");
    expectCommand(projectname::AppCommandIds::projectOpen,
                  "Open",
                  projectname::AppCommandScope::project,
                  true,
                  "App command registry contains Open");
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

    expect(registry.findCommand("missing.command") == nullptr,
           "App command registry returns null for unknown commands");
    expect(!registry.isEnabled("missing.command"),
           "App command registry reports unknown commands unavailable");

    projectname::AppCommandAvailability availability;
    availability.canImportAudio = false;
    availability.canCancelImport = true;
    availability.canCancelTimelinePreparation = true;

    const auto busyRegistry = projectname::makePrototypeAppCommandRegistry(availability);
    expect(!busyRegistry.isEnabled(projectname::AppCommandIds::audioImport),
           "App command registry disables import while import UI or job is active");
    expect(busyRegistry.isEnabled(projectname::AppCommandIds::audioImportCancel),
           "App command registry enables cancel import from availability");
    expect(busyRegistry.isEnabled(projectname::AppCommandIds::timelinePreparationCancel),
           "App command registry enables cancel timeline preparation from availability");

    const auto* disabledImport = busyRegistry.findCommand(projectname::AppCommandIds::audioImport);
    expect(disabledImport != nullptr && !disabledImport->disabledReason.empty(),
           "Disabled import command keeps a status reason");

    const auto* enabledCancel = busyRegistry.findCommand(projectname::AppCommandIds::audioImportCancel);
    expect(enabledCancel != nullptr && enabledCancel->disabledReason.empty(),
           "Enabled cancel command clears stale disabled status text");

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
    expect(imported.has_value(), "Session timeline playback test imports PCM16 WAV");
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
} // namespace

int main()
{
    appSessionKeepsTransportInsideProjectModel();
    appSessionTimelineViewportStateClampsValues();
    timelineViewportFitHelperFramesImportedAudioClips();
    workspaceCommandRouterPreservesFocusedWorkspaceShortcuts();
    appCommandRegistryDescribesPrototypeTopBarCommands();
    appSessionSelectsImportedAudioClips();
    appSessionUpdatesStaticTrackMixStateThroughCommand();
    appSessionSavesAndLoadsProjectPackages();
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
    transportStateAdvancesOnlyWhilePlaying();
    projectManifestRoundTrips();
    projectLoopRegionValidatesAndRoundTrips();
    projectImportedClipSelectionValidatesAndRoundTrips();
    projectTrackMixStateRoundTripsAndLoadsLegacyDefaults();
    projectSaveCreatesPreviousManifestBackup();
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

    if (failures == 0)
    {
        std::cout << "All ProjectName tests passed.\n";
        return 0;
    }

    std::cerr << failures << " ProjectName test(s) failed.\n";
    return 1;
}
