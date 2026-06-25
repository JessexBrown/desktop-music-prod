// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ProjectModel.h"

#include "ProductIdentity.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace projectname
{
namespace
{
const std::array<const char*, 5> assetFolders { "audio", "samples", "presets", "analysis", "backups" };
constexpr const char* manifestFileName = "manifest.json";
constexpr const char* previousManifestBackupFileName = "manifest.previous.json";

[[nodiscard]] bool isMissingPathError(const std::error_code& error) noexcept
{
    return error == std::errc::no_such_file_or_directory
        || error == std::errc::not_a_directory;
}

[[nodiscard]] bool rejectSymlinkedProjectPackagePath(const std::filesystem::path& packageDirectory,
                                                     std::string& error)
{
    auto current = packageDirectory;

    while (!current.empty())
    {
        std::error_code filesystemError;
        const auto currentStatus = std::filesystem::symlink_status(current, filesystemError);
        if (filesystemError)
        {
            if (!isMissingPathError(filesystemError))
            {
                error = "Could not inspect project package path: "
                    + current.generic_string() + ": " + filesystemError.message() + ".";
                return false;
            }
        }
        else if (std::filesystem::is_symlink(currentStatus))
        {
            error = "Project package path contains a symlink: "
                + current.generic_string() + ".";
            return false;
        }

        const auto parent = current.parent_path();
        if (parent == current)
            break;

        current = parent;
    }

    return true;
}

[[nodiscard]] nlohmann::json makeTimeSignatureJson(TimeSignature timeSignature)
{
    return {
        { "numerator", timeSignature.numerator },
        { "denominator", timeSignature.denominator },
    };
}

[[nodiscard]] bool isValidLoopRegion(double startBeats, double lengthBeats) noexcept
{
    return std::isfinite(startBeats)
        && std::isfinite(lengthBeats)
        && startBeats >= 0.0
        && lengthBeats > 0.0
        && std::isfinite(startBeats + lengthBeats);
}

[[nodiscard]] bool isValidTrackVolume(float volume) noexcept
{
    return std::isfinite(volume) && volume >= 0.0f && volume <= 4.0f;
}

[[nodiscard]] bool isValidTrackPan(float pan) noexcept
{
    return std::isfinite(pan) && pan >= -1.0f && pan <= 1.0f;
}

[[nodiscard]] float sanitizeManifestTrackVolume(float volume) noexcept
{
    if (!std::isfinite(volume))
        return 1.0f;

    return std::clamp(volume, 0.0f, 4.0f);
}

[[nodiscard]] float sanitizeManifestTrackPan(float pan) noexcept
{
    if (!std::isfinite(pan))
        return 0.0f;

    return std::clamp(pan, -1.0f, 1.0f);
}

[[nodiscard]] nlohmann::json makeLoopRegionJson(ProjectLoopRegion loopRegion)
{
    return {
        { "enabled", loopRegion.enabled },
        { "startBeats", loopRegion.startBeats },
        { "lengthBeats", loopRegion.lengthBeats },
    };
}

[[nodiscard]] nlohmann::json makeSelectionJson(const std::string& selectedClipId)
{
    return {
        { "clipId", selectedClipId },
    };
}

[[nodiscard]] nlohmann::json makeClipJson(const ProjectClip& clip)
{
    auto json = nlohmann::json {
        { "id", clip.id },
        { "name", clip.name },
        { "type", clip.type },
        { "relativePath", clip.relativePath },
        { "startBeats", clip.startBeats },
        { "lengthBeats", clip.lengthBeats },
    };

    if (!clip.analysisPath.empty())
        json["analysisPath"] = clip.analysisPath;

    return json;
}

[[nodiscard]] nlohmann::json makeDeviceJson(const ProjectDevice& device)
{
    return {
        { "id", device.id },
        { "name", device.name },
        { "type", device.type },
        { "bypassed", device.bypassed },
    };
}

[[nodiscard]] nlohmann::json makeTrackJson(const ProjectTrack& track)
{
    auto devices = nlohmann::json::array();
    for (const auto& device : track.devices)
        devices.push_back(makeDeviceJson(device));

    auto clips = nlohmann::json::array();
    for (const auto& clip : track.clips)
        clips.push_back(makeClipJson(clip));

    return {
        { "id", track.id },
        { "name", track.name },
        { "type", track.type },
        { "volume", track.volume },
        { "pan", track.pan },
        { "muted", track.muted },
        { "solo", track.solo },
        { "devices", devices },
        { "clips", clips },
    };
}

[[nodiscard]] float readFiniteFloat(const nlohmann::json& json,
                                    const char* key,
                                    float defaultValue) noexcept
{
    const auto value = json.find(key);
    if (value == json.end() || !value->is_number())
        return defaultValue;

    const auto number = value->get<double>();
    if (!std::isfinite(number))
        return defaultValue;

    return static_cast<float>(number);
}

[[nodiscard]] std::optional<ProjectClip> readClip(const nlohmann::json& clipJson, std::string& error)
{
    if (!clipJson.is_object())
    {
        error = "Clip entry is not an object.";
        return std::nullopt;
    }

    ProjectClip clip;
    clip.id = clipJson.value("id", "");
    clip.name = clipJson.value("name", "Untitled Clip");
    clip.type = clipJson.value("type", "generated-audio");
    clip.relativePath = clipJson.value("relativePath", "");
    clip.analysisPath = clipJson.value("analysisPath", "");
    clip.startBeats = clipJson.value("startBeats", 0.0);
    clip.lengthBeats = clipJson.value("lengthBeats", 4.0);

    if (clip.id.empty())
    {
        error = "Clip is missing an id.";
        return std::nullopt;
    }

    return clip;
}

[[nodiscard]] std::optional<ProjectDevice> readDevice(const nlohmann::json& deviceJson, std::string& error)
{
    if (!deviceJson.is_object())
    {
        error = "Device entry is not an object.";
        return std::nullopt;
    }

    ProjectDevice device;
    device.id = deviceJson.value("id", "");
    device.name = deviceJson.value("name", "Untitled Device");
    device.type = deviceJson.value("type", "builtin/generic");
    device.bypassed = deviceJson.value("bypassed", false);

    if (device.id.empty())
    {
        error = "Device is missing an id.";
        return std::nullopt;
    }

    return device;
}

[[nodiscard]] std::optional<ProjectTrack> readTrack(const nlohmann::json& trackJson, std::string& error)
{
    if (!trackJson.is_object())
    {
        error = "Track entry is not an object.";
        return std::nullopt;
    }

    ProjectTrack track;
    track.id = trackJson.value("id", "");
    track.name = trackJson.value("name", "Untitled Track");
    track.type = trackJson.value("type", "audio");
    track.volume = sanitizeManifestTrackVolume(readFiniteFloat(trackJson, "volume", 1.0f));
    track.pan = sanitizeManifestTrackPan(readFiniteFloat(trackJson, "pan", 0.0f));
    track.muted = trackJson.value("muted", false);
    track.solo = trackJson.value("solo", false);

    if (track.id.empty())
    {
        error = "Track is missing an id.";
        return std::nullopt;
    }

    const auto devices = trackJson.find("devices");
    if (devices != trackJson.end())
    {
        if (!devices->is_array())
        {
            error = "Track devices must be an array.";
            return std::nullopt;
        }

        for (const auto& deviceJson : *devices)
        {
            auto device = readDevice(deviceJson, error);
            if (!device.has_value())
                return std::nullopt;

            track.devices.push_back(std::move(*device));
        }
    }

    const auto clips = trackJson.find("clips");
    if (clips != trackJson.end())
    {
        if (!clips->is_array())
        {
            error = "Track clips must be an array.";
            return std::nullopt;
        }

        for (const auto& clipJson : *clips)
        {
            auto clip = readClip(clipJson, error);
            if (!clip.has_value())
                return std::nullopt;

            track.clips.push_back(std::move(*clip));
        }
    }

    return track;
}

[[nodiscard]] bool isImportedAudioClip(const ProjectClip& clip) noexcept
{
    return clip.type == "audio-file";
}

[[nodiscard]] bool hasImportedAudioClip(const std::vector<ProjectTrack>& tracks,
                                        const std::string& clipId) noexcept
{
    if (clipId.empty())
        return false;

    for (const auto& track : tracks)
    {
        for (const auto& clip : track.clips)
        {
            if (clip.id == clipId && isImportedAudioClip(clip))
                return true;
        }
    }

    return false;
}

struct ImportedAudioClipReference
{
    const ProjectClip* clip = nullptr;
    std::size_t trackIndex = 0;
    std::size_t clipIndex = 0;
    std::size_t sourceOrder = 0;
};

[[nodiscard]] std::vector<ImportedAudioClipReference> collectTimelineOrderedImportedAudioClips(
    const std::vector<ProjectTrack>& tracks)
{
    std::vector<ImportedAudioClipReference> clips;
    auto sourceOrder = std::size_t { 0 };

    for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex)
    {
        const auto& track = tracks[trackIndex];
        for (std::size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex)
        {
            const auto& clip = track.clips[clipIndex];
            if (!isImportedAudioClip(clip))
                continue;

            ImportedAudioClipReference reference;
            reference.clip = &clip;
            reference.trackIndex = trackIndex;
            reference.clipIndex = clipIndex;
            reference.sourceOrder = sourceOrder++;
            clips.push_back(reference);
        }
    }

    std::stable_sort(clips.begin(),
                     clips.end(),
                     [](const ImportedAudioClipReference& left, const ImportedAudioClipReference& right)
                     {
                         const auto leftFinite = std::isfinite(left.clip->startBeats);
                         const auto rightFinite = std::isfinite(right.clip->startBeats);
                         if (leftFinite != rightFinite)
                             return leftFinite;

                         if (leftFinite && left.clip->startBeats != right.clip->startBeats)
                             return left.clip->startBeats < right.clip->startBeats;

                         return left.sourceOrder < right.sourceOrder;
                     });

    return clips;
}
} // namespace

ProjectModel ProjectModel::createDefault()
{
    ProjectModel project;

    ProjectTrack track;
    track.id = "track-1";
    track.name = "Generated Tone";
    track.type = "audio";

    ProjectDevice device;
    device.id = "device-1";
    device.name = "Generated Tone Source";
    device.type = "builtin/generated-tone-source";
    device.bypassed = false;
    track.devices.push_back(device);

    ProjectClip clip;
    clip.id = "clip-1";
    clip.name = "Starter Tone";
    clip.type = "generated-audio";
    clip.relativePath = "audio/generated-tone.wav";
    clip.startBeats = 0.0;
    clip.lengthBeats = 4.0;
    track.clips.push_back(clip);

    project.tracks_.push_back(track);
    return project;
}

std::optional<ProjectModel> ProjectModel::loadPackage(const std::filesystem::path& packageDirectory,
                                                      std::string& error)
{
    if (!rejectSymlinkedProjectPackagePath(packageDirectory, error))
        return std::nullopt;

    std::error_code filesystemError;
    const auto packageStatus = std::filesystem::symlink_status(packageDirectory, filesystemError);
    if (filesystemError)
    {
        if (!isMissingPathError(filesystemError))
        {
            error = "Could not inspect project package path: " + filesystemError.message();
            return std::nullopt;
        }
    }
    else if (std::filesystem::is_regular_file(packageStatus))
    {
        error = "Project package path points to a file.";
        return std::nullopt;
    }

    const auto manifestPath = packageDirectory / manifestFileName;

    filesystemError.clear();
    const auto manifestStatus = std::filesystem::symlink_status(manifestPath, filesystemError);
    if (filesystemError)
    {
        if (isMissingPathError(filesystemError))
        {
            error = "Project manifest was not found.";
            return std::nullopt;
        }

        error = "Project manifest could not be inspected: " + filesystemError.message();
        return std::nullopt;
    }

    if (manifestStatus.type() == std::filesystem::file_type::not_found)
    {
        error = "Project manifest was not found.";
        return std::nullopt;
    }

    if (std::filesystem::is_symlink(manifestStatus))
    {
        error = "Project manifest path is a symlink.";
        return std::nullopt;
    }

    if (!std::filesystem::is_regular_file(manifestStatus))
    {
        error = "Project manifest path is not a regular file.";
        return std::nullopt;
    }

    std::ifstream manifestFile(manifestPath);
    if (!manifestFile)
    {
        error = "Project manifest could not be opened.";
        return std::nullopt;
    }

    try
    {
        auto manifest = nlohmann::json::parse(manifestFile);
        return fromManifestJson(manifest, error);
    }
    catch (const nlohmann::json::exception& exception)
    {
        error = std::string("Project manifest is not valid JSON: ") + exception.what();
        return std::nullopt;
    }
}

bool ProjectModel::savePackage(const std::filesystem::path& packageDirectory, std::string& error) const
{
    if (!rejectSymlinkedProjectPackagePath(packageDirectory, error))
        return false;

    std::error_code filesystemError;
    const auto packageStatus = std::filesystem::symlink_status(packageDirectory, filesystemError);
    if (filesystemError)
    {
        if (!isMissingPathError(filesystemError))
        {
            error = "Could not inspect project package path: " + filesystemError.message();
            return false;
        }
    }
    else if (std::filesystem::is_regular_file(packageStatus))
    {
        error = "Project package path points to a file.";
        return false;
    }

    filesystemError.clear();
    std::filesystem::create_directories(packageDirectory, filesystemError);
    if (filesystemError)
    {
        error = "Could not create project package directory: " + filesystemError.message();
        return false;
    }

    for (const auto* folderName : assetFolders)
    {
        const auto assetFolderPath = packageDirectory / folderName;
        filesystemError.clear();
        const auto assetFolderStatus = std::filesystem::symlink_status(assetFolderPath, filesystemError);
        if (filesystemError)
        {
            if (!isMissingPathError(filesystemError))
            {
                error = std::string("Could not inspect asset folder: ")
                    + folderName + " (" + filesystemError.message() + ")";
                return false;
            }
        }
        else if (std::filesystem::is_symlink(assetFolderStatus))
        {
            error = std::string("Project asset folder path is a symlink: ") + folderName + ".";
            return false;
        }

        filesystemError.clear();
        std::filesystem::create_directories(assetFolderPath, filesystemError);
        if (filesystemError)
        {
            error = std::string("Could not create asset folder: ") + folderName + " (" + filesystemError.message() + ")";
            return false;
        }
    }

    const auto manifestPath = packageDirectory / manifestFileName;
    const auto temporaryManifestPath = packageDirectory / "manifest.json.tmp";
    std::filesystem::remove(temporaryManifestPath, filesystemError);
    filesystemError.clear();

    const auto manifestStatus = std::filesystem::symlink_status(manifestPath, filesystemError);
    if (filesystemError)
    {
        if (!isMissingPathError(filesystemError))
        {
            error = "Could not inspect project manifest path: " + filesystemError.message();
            return false;
        }
    }
    else if (std::filesystem::is_symlink(manifestStatus))
    {
        error = "Project manifest path is a symlink.";
        return false;
    }
    filesystemError.clear();

    std::ofstream temporaryManifestFile(temporaryManifestPath, std::ios::trunc);
    if (!temporaryManifestFile)
    {
        error = "Could not write temporary project manifest.";
        return false;
    }

    temporaryManifestFile << toManifestJson().dump(2) << '\n';
    temporaryManifestFile.close();
    if (!temporaryManifestFile)
    {
        error = "Temporary project manifest write failed.";
        std::filesystem::remove(temporaryManifestPath, filesystemError);
        return false;
    }

    if (std::filesystem::is_regular_file(manifestPath))
    {
        const auto backupPath = packageDirectory / "backups" / previousManifestBackupFileName;
        std::filesystem::copy_file(manifestPath,
                                   backupPath,
                                   std::filesystem::copy_options::overwrite_existing,
                                   filesystemError);
        if (filesystemError)
        {
            const auto backupErrorMessage = filesystemError.message();
            std::filesystem::remove(temporaryManifestPath, filesystemError);
            error = "Could not create project manifest backup: " + backupErrorMessage;
            return false;
        }
    }

    std::filesystem::copy_file(temporaryManifestPath,
                               manifestPath,
                               std::filesystem::copy_options::overwrite_existing,
                               filesystemError);
    if (filesystemError)
    {
        error = "Could not commit staged project manifest: " + filesystemError.message();
        std::filesystem::remove(temporaryManifestPath, filesystemError);
        return false;
    }

    std::filesystem::remove(temporaryManifestPath, filesystemError);
    if (filesystemError)
    {
        error = "Could not remove temporary project manifest: " + filesystemError.message();
        return false;
    }

    error.clear();
    return true;
}

void ProjectModel::setName(std::string name)
{
    name_ = !name.empty() ? std::move(name) : std::string("Untitled Song");
}

const std::string& ProjectModel::getName() const noexcept
{
    return name_;
}

TransportState& ProjectModel::getTransport() noexcept
{
    return transport_;
}

const TransportState& ProjectModel::getTransport() const noexcept
{
    return transport_;
}

const ProjectLoopRegion& ProjectModel::getLoopRegion() const noexcept
{
    return loopRegion_;
}

bool ProjectModel::setLoopRegion(double startBeats, double lengthBeats, std::string& error)
{
    error.clear();

    if (!isValidLoopRegion(startBeats, lengthBeats))
    {
        error = "Loop region must have finite non-negative start beats and positive length beats.";
        return false;
    }

    loopRegion_.enabled = true;
    loopRegion_.startBeats = startBeats;
    loopRegion_.lengthBeats = lengthBeats;
    return true;
}

void ProjectModel::clearLoopRegion() noexcept
{
    loopRegion_ = {};
}

const std::string& ProjectModel::getSelectedClipId() const noexcept
{
    return selectedClipId_;
}

bool ProjectModel::selectImportedAudioClip(const std::string& clipId, std::string& error)
{
    error.clear();

    if (clipId.empty())
    {
        error = "Clip id is required.";
        return false;
    }

    if (!hasImportedAudioClip(tracks_, clipId))
    {
        error = "Imported audio clip was not found.";
        return false;
    }

    selectedClipId_ = clipId;
    return true;
}

bool ProjectModel::selectAdjacentImportedAudioClip(ImportedAudioClipSelectionDirection direction,
                                                   std::string& error)
{
    error.clear();

    const auto importedClips = collectTimelineOrderedImportedAudioClips(tracks_);
    if (importedClips.empty())
    {
        error = "No imported audio clips are available.";
        return false;
    }

    auto selectedIndex = importedClips.size();
    for (std::size_t index = 0; index < importedClips.size(); ++index)
    {
        if (importedClips[index].clip->id == selectedClipId_)
        {
            selectedIndex = index;
            break;
        }
    }

    if (selectedIndex == importedClips.size())
    {
        selectedIndex = direction == ImportedAudioClipSelectionDirection::next
            ? 0
            : importedClips.size() - 1;
    }
    else if (direction == ImportedAudioClipSelectionDirection::next)
    {
        selectedIndex = (selectedIndex + 1) % importedClips.size();
    }
    else
    {
        selectedIndex = selectedIndex == 0 ? importedClips.size() - 1 : selectedIndex - 1;
    }

    selectedClipId_ = importedClips[selectedIndex].clip->id;
    return true;
}

void ProjectModel::clearSelectedClip() noexcept
{
    selectedClipId_.clear();
}

void ProjectModel::addTrack(ProjectTrack track)
{
    tracks_.push_back(std::move(track));
}

bool ProjectModel::setTrackMixState(const std::string& trackId,
                                    float volume,
                                    float pan,
                                    bool muted,
                                    bool solo,
                                    std::string& error)
{
    error.clear();

    if (trackId.empty())
    {
        error = "Track id is required.";
        return false;
    }

    if (!isValidTrackVolume(volume))
    {
        error = "Track volume must be finite and between 0.0 and 4.0.";
        return false;
    }

    if (!isValidTrackPan(pan))
    {
        error = "Track pan must be finite and between -1.0 and 1.0.";
        return false;
    }

    for (auto& track : tracks_)
    {
        if (track.id != trackId)
            continue;

        track.volume = volume;
        track.pan = pan;
        track.muted = muted;
        track.solo = solo;
        return true;
    }

    error = "Track was not found.";
    return false;
}

bool ProjectModel::addClipToTrack(const std::string& trackId, ProjectClip clip)
{
    for (auto& track : tracks_)
    {
        if (track.id == trackId)
        {
            track.clips.push_back(std::move(clip));
            return true;
        }
    }

    return false;
}

bool ProjectModel::setImportedAudioClipStartBeats(const std::string& clipId,
                                                  double startBeats,
                                                  std::string& error)
{
    error.clear();

    if (clipId.empty())
    {
        error = "Clip id is required.";
        return false;
    }

    if (!std::isfinite(startBeats) || startBeats < 0.0)
    {
        error = "Clip start beat must be a finite non-negative value.";
        return false;
    }

    for (auto& track : tracks_)
    {
        for (auto& clip : track.clips)
        {
            if (clip.id != clipId)
                continue;

            if (clip.type != "audio-file")
            {
                error = "Only imported audio clips can be placed on the timeline.";
                return false;
            }

            clip.startBeats = startBeats;
            return true;
        }
    }

    error = "Imported audio clip was not found.";
    return false;
}

bool ProjectModel::replaceImportedAudioClipMedia(const std::string& clipId,
                                                 std::string relativePath,
                                                 std::string analysisPath,
                                                 double lengthBeats,
                                                 std::string& error)
{
    error.clear();

    if (clipId.empty())
    {
        error = "Clip id is required.";
        return false;
    }

    if (relativePath.empty())
    {
        error = "Imported audio clip media path is required.";
        return false;
    }

    if (!std::isfinite(lengthBeats) || lengthBeats <= 0.0)
    {
        error = "Clip length must be a finite positive value.";
        return false;
    }

    for (auto& track : tracks_)
    {
        for (auto& clip : track.clips)
        {
            if (clip.id != clipId)
                continue;

            if (clip.type != "audio-file")
            {
                error = "Only imported audio clips can replace media.";
                return false;
            }

            clip.relativePath = std::move(relativePath);
            clip.analysisPath = std::move(analysisPath);
            clip.lengthBeats = lengthBeats;
            return true;
        }
    }

    error = "Imported audio clip was not found.";
    return false;
}

const std::vector<ProjectTrack>& ProjectModel::getTracks() const noexcept
{
    return tracks_;
}

bool ProjectModel::operator==(const ProjectModel& other) const
{
    return name_ == other.name_
        && transport_.getTempoBpm() == other.transport_.getTempoBpm()
        && transport_.getTimeSignature() == other.transport_.getTimeSignature()
        && transport_.getPositionBeats() == other.transport_.getPositionBeats()
        && loopRegion_ == other.loopRegion_
        && selectedClipId_ == other.selectedClipId_
        && tracks_ == other.tracks_;
}

nlohmann::json ProjectModel::toManifestJson() const
{
    auto tracks = nlohmann::json::array();
    for (const auto& track : tracks_)
        tracks.push_back(makeTrackJson(track));

    return {
        { "manifestVersion", currentManifestVersion },
        { "application", productName },
        { "name", name_ },
        { "transport",
          {
              { "tempoBpm", transport_.getTempoBpm() },
              { "timeSignature", makeTimeSignatureJson(transport_.getTimeSignature()) },
              { "positionBeats", transport_.getPositionBeats() },
          } },
        { "loopRegion", makeLoopRegionJson(loopRegion_) },
        { "selection", makeSelectionJson(selectedClipId_) },
        { "assets",
          {
              { "audioFolder", "audio" },
              { "samplesFolder", "samples" },
              { "presetsFolder", "presets" },
              { "analysisFolder", "analysis" },
              { "backupsFolder", "backups" },
          } },
        { "tracks", tracks },
    };
}

std::optional<ProjectModel> ProjectModel::fromManifestJson(const nlohmann::json& manifest,
                                                           std::string& error)
{
    if (!manifest.is_object())
    {
        error = "Project manifest is not a JSON object.";
        return std::nullopt;
    }

    const auto version = manifest.value("manifestVersion", 0);
    if (version != currentManifestVersion)
    {
        error = "Unsupported project manifest version.";
        return std::nullopt;
    }

    ProjectModel project;
    project.name_ = manifest.value("name", "Untitled Song");

    const auto transport = manifest.find("transport");
    if (transport == manifest.end() || !transport->is_object())
    {
        error = "Project transport is missing.";
        return std::nullopt;
    }

    project.transport_.setTempoBpm(transport->value("tempoBpm", 120.0));

    const auto timeSignature = transport->find("timeSignature");
    if (timeSignature == transport->end() || !timeSignature->is_object())
    {
        error = "Project time signature is missing.";
        return std::nullopt;
    }

    if (!project.transport_.setTimeSignature(timeSignature->value("numerator", 4),
                                             timeSignature->value("denominator", 4)))
    {
        error = "Project time signature is invalid.";
        return std::nullopt;
    }

    project.transport_.setPositionBeats(transport->value("positionBeats", 0.0));

    const auto loopRegion = manifest.find("loopRegion");
    if (loopRegion != manifest.end())
    {
        if (!loopRegion->is_object())
        {
            error = "Project loop region must be an object.";
            return std::nullopt;
        }

        const auto enabled = loopRegion->value("enabled", false);
        const auto startBeats = loopRegion->value("startBeats", 0.0);
        const auto lengthBeats = loopRegion->value("lengthBeats", 0.0);
        if (enabled)
        {
            if (!isValidLoopRegion(startBeats, lengthBeats))
            {
                error = "Project loop region is invalid.";
                return std::nullopt;
            }

            project.loopRegion_.enabled = true;
            project.loopRegion_.startBeats = startBeats;
            project.loopRegion_.lengthBeats = lengthBeats;
        }
    }

    const auto tracks = manifest.find("tracks");
    if (tracks == manifest.end() || !tracks->is_array())
    {
        error = "Project tracks must be an array.";
        return std::nullopt;
    }

    for (const auto& trackJson : *tracks)
    {
        auto track = readTrack(trackJson, error);
        if (!track.has_value())
            return std::nullopt;

        project.tracks_.push_back(std::move(*track));
    }

    const auto selection = manifest.find("selection");
    if (selection != manifest.end())
    {
        if (!selection->is_object())
        {
            error = "Project selection must be an object.";
            return std::nullopt;
        }

        const auto selectedClip = selection->find("clipId");
        if (selectedClip != selection->end())
        {
            if (!selectedClip->is_string())
            {
                error = "Project selected clip id must be a string.";
                return std::nullopt;
            }

            project.selectedClipId_ = selectedClip->get<std::string>();
        }
    }

    error.clear();
    return project;
}
} // namespace projectname
