// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ImportedClipInspector.h"

#include <optional>
#include <string>

namespace projectname
{
enum class ImportedClipInspectorEditAvailability
{
    unavailable,
    readOnlyFallback,
    editable,
};

enum class ImportedClipInspectorEditValidationCode
{
    accepted,
    notEditable,
    invalidStartBeat,
    missingMediaPath,
    unsafeMediaPath,
    unsafeAnalysisPath,
    invalidLengthBeats,
};

struct ImportedClipInspectorEditValidation
{
    ImportedClipInspectorEditValidationCode code =
        ImportedClipInspectorEditValidationCode::accepted;
    bool accepted = true;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept { return accepted; }
};

struct ImportedClipStartBeatCommit
{
    std::string clipId;
    double startBeats = 0.0;
};

struct ImportedClipMediaRelinkCommit
{
    std::string clipId;
    std::string relativePath;
    std::string analysisPath;
    double lengthBeats = 0.0;
};

class ImportedClipInspectorEditDraft
{
public:
    [[nodiscard]] static ImportedClipInspectorEditDraft fromInspectorState(
        const ImportedClipInspectorState& state);

    [[nodiscard]] ImportedClipInspectorEditAvailability getAvailability() const noexcept;
    [[nodiscard]] bool canEdit() const noexcept;
    [[nodiscard]] const std::string& getClipId() const noexcept;

    [[nodiscard]] const std::string& getStartBeatText() const noexcept;
    void setStartBeatText(std::string text);
    void cancelStartBeatEdit();
    [[nodiscard]] ImportedClipInspectorEditValidation validateStartBeat() const;
    [[nodiscard]] std::optional<ImportedClipStartBeatCommit> makeStartBeatCommit(
        std::string& error) const;

    [[nodiscard]] const std::string& getMediaRelativePath() const noexcept;
    [[nodiscard]] const std::string& getAnalysisPath() const noexcept;
    [[nodiscard]] double getLengthBeats() const noexcept;
    void setMediaRelinkDraft(std::string relativePath,
                             std::string analysisPath,
                             double lengthBeats);
    void cancelMediaRelinkEdit();
    [[nodiscard]] ImportedClipInspectorEditValidation validateMediaRelink() const;
    [[nodiscard]] std::optional<ImportedClipMediaRelinkCommit> makeMediaRelinkCommit(
        std::string& error) const;

private:
    ImportedClipInspectorEditAvailability availability_ =
        ImportedClipInspectorEditAvailability::unavailable;
    std::string clipId_;
    double committedStartBeats_ = 0.0;
    std::string startBeatText_;
    std::string committedRelativePath_;
    std::string committedAnalysisPath_;
    double committedLengthBeats_ = 0.0;
    std::string mediaRelativePath_;
    std::string analysisPath_;
    double lengthBeats_ = 0.0;
};
} // namespace projectname
