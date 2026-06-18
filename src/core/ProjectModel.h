#pragma once

#include "TransportState.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace projectname
{
struct ProjectClip
{
    std::string id;
    std::string name;
    std::string type;
    std::string relativePath;
    std::string analysisPath;
    double startBeats = 0.0;
    double lengthBeats = 4.0;

    [[nodiscard]] bool operator==(const ProjectClip& other) const = default;
};

struct ProjectDevice
{
    std::string id;
    std::string name;
    std::string type;
    bool bypassed = false;

    [[nodiscard]] bool operator==(const ProjectDevice& other) const = default;
};

struct ProjectTrack
{
    std::string id;
    std::string name;
    std::string type;
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    std::vector<ProjectDevice> devices;
    std::vector<ProjectClip> clips;

    [[nodiscard]] bool operator==(const ProjectTrack& other) const = default;
};

struct ProjectLoopRegion
{
    bool enabled = false;
    double startBeats = 0.0;
    double lengthBeats = 0.0;

    [[nodiscard]] bool operator==(const ProjectLoopRegion& other) const = default;
};

enum class ImportedAudioClipSelectionDirection
{
    previous,
    next,
};

class ProjectModel
{
public:
    static constexpr int currentManifestVersion = 1;

    [[nodiscard]] static ProjectModel createDefault();
    [[nodiscard]] static std::optional<ProjectModel> loadPackage(const std::filesystem::path& packageDirectory,
                                                                 std::string& error);

    [[nodiscard]] bool savePackage(const std::filesystem::path& packageDirectory, std::string& error) const;

    void setName(std::string name);
    [[nodiscard]] const std::string& getName() const noexcept;

    [[nodiscard]] TransportState& getTransport() noexcept;
    [[nodiscard]] const TransportState& getTransport() const noexcept;
    [[nodiscard]] const ProjectLoopRegion& getLoopRegion() const noexcept;
    [[nodiscard]] bool setLoopRegion(double startBeats, double lengthBeats, std::string& error);
    void clearLoopRegion() noexcept;
    [[nodiscard]] const std::string& getSelectedClipId() const noexcept;
    [[nodiscard]] bool selectImportedAudioClip(const std::string& clipId, std::string& error);
    [[nodiscard]] bool selectAdjacentImportedAudioClip(ImportedAudioClipSelectionDirection direction,
                                                       std::string& error);
    void clearSelectedClip() noexcept;

    void addTrack(ProjectTrack track);
    [[nodiscard]] bool setTrackMixState(const std::string& trackId,
                                        float volume,
                                        float pan,
                                        bool muted,
                                        bool solo,
                                        std::string& error);
    [[nodiscard]] bool addClipToTrack(const std::string& trackId, ProjectClip clip);
    [[nodiscard]] bool setImportedAudioClipStartBeats(const std::string& clipId,
                                                      double startBeats,
                                                      std::string& error);
    [[nodiscard]] bool replaceImportedAudioClipMedia(const std::string& clipId,
                                                     std::string relativePath,
                                                     std::string analysisPath,
                                                     double lengthBeats,
                                                     std::string& error);
    [[nodiscard]] const std::vector<ProjectTrack>& getTracks() const noexcept;

    [[nodiscard]] bool operator==(const ProjectModel& other) const;

private:
    [[nodiscard]] nlohmann::json toManifestJson() const;
    [[nodiscard]] static std::optional<ProjectModel> fromManifestJson(const nlohmann::json& manifest,
                                                                      std::string& error);

    std::string name_ = "Untitled Song";
    TransportState transport_;
    ProjectLoopRegion loopRegion_;
    std::string selectedClipId_;
    std::vector<ProjectTrack> tracks_;
};
} // namespace projectname
