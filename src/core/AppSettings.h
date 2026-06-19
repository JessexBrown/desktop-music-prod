// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace projectname
{
inline constexpr int appSettingsSchemaVersion = 1;
inline constexpr const char* appSettingsFileName = "settings.json";

struct AudioOutputPreference
{
    bool hasOutputDevice = false;
    std::string deviceType;
    std::string deviceName;
    double sampleRateHz = 0.0;
    int bufferSizeSamples = 0;
    int outputChannelCount = 0;
    std::string juceDeviceStateXml;

    friend bool operator==(const AudioOutputPreference&, const AudioOutputPreference&) = default;
};

struct AudioSetupSettings
{
    bool firstRunPromptDismissed = false;
    AudioOutputPreference preferredOutput;

    friend bool operator==(const AudioSetupSettings&, const AudioSetupSettings&) = default;
};

struct AppSettings
{
    int settingsVersion = appSettingsSchemaVersion;
    AudioSetupSettings audioSetup;

    friend bool operator==(const AppSettings&, const AppSettings&) = default;
};

[[nodiscard]] nlohmann::json makeAppSettingsJson(const AppSettings& settings);
[[nodiscard]] std::optional<AppSettings> parseAppSettingsJson(const nlohmann::json& settingsJson,
                                                              std::string& error);
[[nodiscard]] std::optional<AppSettings> loadAppSettings(const std::filesystem::path& settingsPath,
                                                         std::string& error);
[[nodiscard]] bool saveAppSettings(const AppSettings& settings,
                                   const std::filesystem::path& settingsPath,
                                   std::string& error);
} // namespace projectname
