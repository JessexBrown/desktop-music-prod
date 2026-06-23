// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AppSettings.h"

#include <cmath>
#include <fstream>
#include <system_error>

namespace projectname
{
namespace
{
[[nodiscard]] bool readBoolOrDefault(const nlohmann::json& json,
                                     const char* key,
                                     bool defaultValue) noexcept
{
    const auto value = json.find(key);
    return value != json.end() && value->is_boolean() ? value->get<bool>() : defaultValue;
}

[[nodiscard]] std::string readStringOrDefault(const nlohmann::json& json,
                                              const char* key,
                                              std::string defaultValue = {})
{
    const auto value = json.find(key);
    return value != json.end() && value->is_string() ? value->get<std::string>() : std::move(defaultValue);
}

[[nodiscard]] double readNonNegativeFiniteDoubleOrDefault(const nlohmann::json& json,
                                                          const char* key,
                                                          double defaultValue) noexcept
{
    const auto value = json.find(key);
    if (value == json.end() || !value->is_number())
        return defaultValue;

    const auto number = value->get<double>();
    return std::isfinite(number) && number >= 0.0 ? number : defaultValue;
}

[[nodiscard]] int readNonNegativeIntOrDefault(const nlohmann::json& json,
                                              const char* key,
                                              int defaultValue) noexcept
{
    const auto value = json.find(key);
    if (value == json.end() || !value->is_number_integer())
        return defaultValue;

    const auto number = value->get<int>();
    return number >= 0 ? number : defaultValue;
}

[[nodiscard]] nlohmann::json makeAudioOutputPreferenceJson(
    const AudioOutputPreference& preference)
{
    return {
        { "hasOutputDevice", preference.hasOutputDevice },
        { "deviceType", preference.deviceType },
        { "deviceName", preference.deviceName },
        { "sampleRateHz", preference.sampleRateHz },
        { "bufferSizeSamples", preference.bufferSizeSamples },
        { "outputChannelCount", preference.outputChannelCount },
        { "juceDeviceStateXml", preference.juceDeviceStateXml },
    };
}

[[nodiscard]] AudioOutputPreference parseAudioOutputPreference(
    const nlohmann::json& preferenceJson)
{
    AudioOutputPreference preference;
    preference.hasOutputDevice = readBoolOrDefault(preferenceJson, "hasOutputDevice", false);
    preference.deviceType = readStringOrDefault(preferenceJson, "deviceType");
    preference.deviceName = readStringOrDefault(preferenceJson, "deviceName");
    preference.sampleRateHz =
        readNonNegativeFiniteDoubleOrDefault(preferenceJson, "sampleRateHz", 0.0);
    preference.bufferSizeSamples =
        readNonNegativeIntOrDefault(preferenceJson, "bufferSizeSamples", 0);
    preference.outputChannelCount =
        readNonNegativeIntOrDefault(preferenceJson, "outputChannelCount", 0);
    preference.juceDeviceStateXml = readStringOrDefault(preferenceJson, "juceDeviceStateXml");
    return preference;
}

[[nodiscard]] bool isMissingPathError(const std::error_code& error) noexcept
{
    return error == std::errc::no_such_file_or_directory
        || error == std::errc::not_a_directory;
}

[[nodiscard]] bool rejectSymlinkedAppSettingsSavePath(const std::filesystem::path& settingsPath,
                                                      std::string& error)
{
    auto current = settingsPath;

    while (!current.empty())
    {
        std::error_code filesystemError;
        const auto currentStatus = std::filesystem::symlink_status(current, filesystemError);
        if (filesystemError)
        {
            if (!isMissingPathError(filesystemError))
            {
                error = "Could not inspect app settings path: "
                    + current.generic_string() + ": " + filesystemError.message() + ".";
                return false;
            }
        }
        else if (std::filesystem::is_symlink(currentStatus))
        {
            error = "App settings path contains a symlink: "
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
} // namespace

nlohmann::json makeAppSettingsJson(const AppSettings& settings)
{
    return {
        { "settingsVersion", appSettingsSchemaVersion },
        {
            "audioSetup",
            {
                { "firstRunPromptDismissed", settings.audioSetup.firstRunPromptDismissed },
                { "preferredOutput", makeAudioOutputPreferenceJson(settings.audioSetup.preferredOutput) },
            },
        },
    };
}

std::optional<AppSettings> parseAppSettingsJson(const nlohmann::json& settingsJson,
                                                std::string& error)
{
    error.clear();

    if (!settingsJson.is_object())
    {
        error = "App settings root is not an object.";
        return std::nullopt;
    }

    const auto version = settingsJson.find("settingsVersion");
    if (version != settingsJson.end())
    {
        if (!version->is_number_integer())
        {
            error = "App settings version is not an integer.";
            return std::nullopt;
        }

        if (version->get<int>() != appSettingsSchemaVersion)
        {
            error = "Unsupported app settings version.";
            return std::nullopt;
        }
    }

    AppSettings settings;

    const auto audioSetup = settingsJson.find("audioSetup");
    if (audioSetup == settingsJson.end())
        return settings;

    if (!audioSetup->is_object())
    {
        error = "Audio setup settings are not an object.";
        return std::nullopt;
    }

    settings.audioSetup.firstRunPromptDismissed =
        readBoolOrDefault(*audioSetup, "firstRunPromptDismissed", false);

    const auto preferredOutput = audioSetup->find("preferredOutput");
    if (preferredOutput == audioSetup->end())
        return settings;

    if (!preferredOutput->is_object())
    {
        error = "Preferred audio output settings are not an object.";
        return std::nullopt;
    }

    settings.audioSetup.preferredOutput = parseAudioOutputPreference(*preferredOutput);
    return settings;
}

std::optional<AppSettings> loadAppSettings(const std::filesystem::path& settingsPath,
                                           std::string& error)
{
    error.clear();

    std::error_code filesystemError;
    const auto settingsStatus = std::filesystem::symlink_status(settingsPath, filesystemError);
    if (filesystemError)
    {
        error = "Could not inspect app settings file: " + filesystemError.message();
        return std::nullopt;
    }

    if (settingsStatus.type() == std::filesystem::file_type::not_found)
        return std::nullopt;

    if (std::filesystem::is_symlink(settingsStatus))
    {
        error = "App settings path is a symlink.";
        return std::nullopt;
    }

    if (!std::filesystem::is_regular_file(settingsStatus))
        return std::nullopt;

    std::ifstream settingsFile(settingsPath);
    if (!settingsFile)
    {
        error = "Could not open app settings file.";
        return std::nullopt;
    }

    try
    {
        const auto settingsJson = nlohmann::json::parse(settingsFile);
        return parseAppSettingsJson(settingsJson, error);
    }
    catch (const nlohmann::json::exception& exception)
    {
        error = std::string("Could not parse app settings file: ") + exception.what();
        return std::nullopt;
    }
}

bool saveAppSettings(const AppSettings& settings,
                     const std::filesystem::path& settingsPath,
                     std::string& error)
{
    error.clear();

    if (settingsPath.empty())
    {
        error = "App settings path is empty.";
        return false;
    }

    if (!rejectSymlinkedAppSettingsSavePath(settingsPath, error))
        return false;

    std::error_code filesystemError;
    if (settingsPath.has_parent_path())
    {
        std::filesystem::create_directories(settingsPath.parent_path(), filesystemError);
        if (filesystemError)
        {
            error = "Could not create app settings directory: " + filesystemError.message();
            return false;
        }
    }

    auto temporaryPath = settingsPath;
    temporaryPath += ".tmp";
    std::filesystem::remove(temporaryPath, filesystemError);

    {
        std::ofstream settingsFile(temporaryPath, std::ios::trunc);
        if (!settingsFile)
        {
            error = "Could not open temporary app settings file.";
            return false;
        }

        settingsFile << makeAppSettingsJson(settings).dump(2) << '\n';
        if (!settingsFile)
        {
            error = "Could not write temporary app settings file.";
            std::filesystem::remove(temporaryPath, filesystemError);
            return false;
        }
    }

    std::filesystem::copy_file(temporaryPath,
                               settingsPath,
                               std::filesystem::copy_options::overwrite_existing,
                               filesystemError);
    if (filesystemError)
    {
        error = "Could not commit app settings file: " + filesystemError.message();
        std::filesystem::remove(temporaryPath, filesystemError);
        return false;
    }

    std::filesystem::remove(temporaryPath, filesystemError);
    return true;
}

void resetAudioSetupPreferences(AppSettings& settings)
{
    settings.audioSetup = {};
}
} // namespace projectname
