// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ImportedClipInspectorEditDraft.h"

#include "PackagePath.h"

#include <cctype>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <utility>

namespace projectname
{
namespace
{
[[nodiscard]] ImportedClipInspectorEditValidation makeValidation(
    ImportedClipInspectorEditValidationCode code,
    std::string message)
{
    ImportedClipInspectorEditValidation validation;
    validation.code = code;
    validation.accepted = code == ImportedClipInspectorEditValidationCode::accepted;
    validation.message = std::move(message);
    return validation;
}

[[nodiscard]] std::string makeNotEditableMessage(
    ImportedClipInspectorEditAvailability availability)
{
    if (availability == ImportedClipInspectorEditAvailability::readOnlyFallback)
        return "Select an imported audio clip before editing inspector fallback metadata.";

    return "No selected imported audio clip is available for editing.";
}

[[nodiscard]] bool hasNonWhitespace(const std::string& text) noexcept
{
    for (const auto character : text)
    {
        if (!std::isspace(static_cast<unsigned char>(character)))
            return true;
    }

    return false;
}

[[nodiscard]] std::string formatBeatText(double value)
{
    if (!std::isfinite(value) || value < 0.0)
        value = 0.0;

    if (std::abs(value) < 0.0000001)
        value = 0.0;

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(6) << value;

    auto text = stream.str();
    while (!text.empty() && text.back() == '0')
        text.pop_back();

    if (!text.empty() && text.back() == '.')
        text.pop_back();

    return text.empty() ? std::string("0") : text;
}

[[nodiscard]] std::optional<double> parseStartBeatText(const std::string& text)
{
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());

    double value = 0.0;
    stream >> std::ws >> value;
    if (!stream)
        return std::nullopt;

    while (true)
    {
        const auto next = stream.peek();
        if (next == std::char_traits<char>::eof())
            break;

        if (!std::isspace(static_cast<unsigned char>(next)))
            return std::nullopt;

        [[maybe_unused]] const auto consumed = stream.get();
    }

    if (!std::isfinite(value) || value < 0.0)
        return std::nullopt;

    return value;
}
} // namespace

ImportedClipInspectorEditDraft ImportedClipInspectorEditDraft::fromInspectorState(
    const ImportedClipInspectorState& state)
{
    ImportedClipInspectorEditDraft draft;
    draft.clipId_ = state.clipId;
    draft.committedStartBeats_ = state.startBeats;
    draft.startBeatText_ = formatBeatText(state.startBeats);
    draft.committedRelativePath_ = state.relativePath;
    draft.committedAnalysisPath_ = state.analysisPath;
    draft.committedLengthBeats_ = state.lengthBeats;
    draft.mediaRelativePath_ = state.relativePath;
    draft.analysisPath_ = state.analysisPath;
    draft.lengthBeats_ = state.lengthBeats;

    if (state.clipId.empty())
        draft.availability_ = ImportedClipInspectorEditAvailability::unavailable;
    else if (!state.usingSelectedClip)
        draft.availability_ = ImportedClipInspectorEditAvailability::readOnlyFallback;
    else
        draft.availability_ = ImportedClipInspectorEditAvailability::editable;

    return draft;
}

ImportedClipInspectorEditAvailability ImportedClipInspectorEditDraft::getAvailability() const noexcept
{
    return availability_;
}

bool ImportedClipInspectorEditDraft::canEdit() const noexcept
{
    return availability_ == ImportedClipInspectorEditAvailability::editable && !clipId_.empty();
}

const std::string& ImportedClipInspectorEditDraft::getClipId() const noexcept
{
    return clipId_;
}

const std::string& ImportedClipInspectorEditDraft::getStartBeatText() const noexcept
{
    return startBeatText_;
}

void ImportedClipInspectorEditDraft::setStartBeatText(std::string text)
{
    startBeatText_ = std::move(text);
}

void ImportedClipInspectorEditDraft::cancelStartBeatEdit()
{
    startBeatText_ = formatBeatText(committedStartBeats_);
}

ImportedClipInspectorEditValidation ImportedClipInspectorEditDraft::validateStartBeat() const
{
    if (!canEdit())
    {
        return makeValidation(ImportedClipInspectorEditValidationCode::notEditable,
                              makeNotEditableMessage(availability_));
    }

    if (!parseStartBeatText(startBeatText_).has_value())
    {
        return makeValidation(ImportedClipInspectorEditValidationCode::invalidStartBeat,
                              "Clip start beat must be a finite non-negative value.");
    }

    return makeValidation(ImportedClipInspectorEditValidationCode::accepted, {});
}

std::optional<ImportedClipStartBeatCommit> ImportedClipInspectorEditDraft::makeStartBeatCommit(
    std::string& error) const
{
    const auto validation = validateStartBeat();
    if (!validation.accepted)
    {
        error = validation.message;
        return std::nullopt;
    }

    auto parsedStartBeat = parseStartBeatText(startBeatText_);
    if (!parsedStartBeat.has_value())
    {
        error = "Clip start beat must be a finite non-negative value.";
        return std::nullopt;
    }

    error.clear();
    return ImportedClipStartBeatCommit { clipId_, *parsedStartBeat };
}

const std::string& ImportedClipInspectorEditDraft::getMediaRelativePath() const noexcept
{
    return mediaRelativePath_;
}

const std::string& ImportedClipInspectorEditDraft::getAnalysisPath() const noexcept
{
    return analysisPath_;
}

double ImportedClipInspectorEditDraft::getLengthBeats() const noexcept
{
    return lengthBeats_;
}

void ImportedClipInspectorEditDraft::setMediaRelinkDraft(std::string relativePath,
                                                         std::string analysisPath,
                                                         double lengthBeats)
{
    mediaRelativePath_ = std::move(relativePath);
    analysisPath_ = std::move(analysisPath);
    lengthBeats_ = lengthBeats;
}

void ImportedClipInspectorEditDraft::cancelMediaRelinkEdit()
{
    mediaRelativePath_ = committedRelativePath_;
    analysisPath_ = committedAnalysisPath_;
    lengthBeats_ = committedLengthBeats_;
}

ImportedClipInspectorEditValidation ImportedClipInspectorEditDraft::validateMediaRelink() const
{
    if (!canEdit())
    {
        return makeValidation(ImportedClipInspectorEditValidationCode::notEditable,
                              makeNotEditableMessage(availability_));
    }

    if (!hasNonWhitespace(mediaRelativePath_))
    {
        return makeValidation(ImportedClipInspectorEditValidationCode::missingMediaPath,
                              "Imported audio clip media path is required.");
    }

    if (!isSafePackageRelativePath(std::filesystem::path(mediaRelativePath_)))
    {
        return makeValidation(ImportedClipInspectorEditValidationCode::unsafeMediaPath,
                              "Imported audio clip media path must be package-relative.");
    }

    if (!analysisPath_.empty()
        && (!hasNonWhitespace(analysisPath_)
            || !isSafePackageRelativePath(std::filesystem::path(analysisPath_))))
    {
        return makeValidation(ImportedClipInspectorEditValidationCode::unsafeAnalysisPath,
                              "Imported audio clip analysis path must be package-relative when present.");
    }

    if (!std::isfinite(lengthBeats_) || lengthBeats_ <= 0.0)
    {
        return makeValidation(ImportedClipInspectorEditValidationCode::invalidLengthBeats,
                              "Clip length must be a finite positive value.");
    }

    return makeValidation(ImportedClipInspectorEditValidationCode::accepted, {});
}

std::optional<ImportedClipMediaRelinkCommit> ImportedClipInspectorEditDraft::makeMediaRelinkCommit(
    std::string& error) const
{
    const auto validation = validateMediaRelink();
    if (!validation.accepted)
    {
        error = validation.message;
        return std::nullopt;
    }

    error.clear();
    return ImportedClipMediaRelinkCommit {
        clipId_,
        mediaRelativePath_,
        analysisPath_,
        lengthBeats_,
    };
}
} // namespace projectname
