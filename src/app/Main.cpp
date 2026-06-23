// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MainComponent.h"

#include "core/ProductIdentity.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

class RabbingtonStudioApplication final : public juce::JUCEApplication
{
public:
    RabbingtonStudioApplication() = default;

    const juce::String getApplicationName() override
    {
        return projectname::productName;
    }

    const juce::String getApplicationVersion() override
    {
        return "0.1.0";
    }

    bool moreThanOneInstanceAllowed() override
    {
        return true;
    }

    void initialise(const juce::String& commandLine) override
    {
        smokeTestMode_ = commandLine.contains("--smoke-test");
        projectChooserSmokeTestMode_ = commandLine.contains("--smoke-project-choosers");
        audioMidiResetSmokeTestMode_ = commandLine.contains("--smoke-audio-midi-reset");
        appSettingsCorruptionSmokeTestMode_ = commandLine.contains("--smoke-app-settings-corruption");
        restoreDetailSmokeTestMode_ = commandLine.contains("--smoke-restore-details");
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());

        if (projectChooserSmokeTestMode_)
        {
            juce::Timer::callAfterDelay(500,
                                        [this]
                                        {
                                            runProjectChooserSmokeTestAndQuit();
                                        });
        }
        else if (appSettingsCorruptionSmokeTestMode_)
        {
            juce::Timer::callAfterDelay(500,
                                        [this]
                                        {
                                            runAppSettingsCorruptionSmokeTestAndQuit();
                                        });
        }
        else if (restoreDetailSmokeTestMode_)
        {
            juce::Timer::callAfterDelay(500,
                                        [this]
                                        {
                                            runRestoreDetailSmokeTestAndQuit();
                                        });
        }
        else if (audioMidiResetSmokeTestMode_)
        {
            juce::Timer::callAfterDelay(500,
                                        [this]
                                        {
                                            runAudioMidiResetSmokeTestAndQuit();
                                        });
        }
        else if (smokeTestMode_)
        {
            juce::Timer::callAfterDelay(500, [] {
                if (auto* app = juce::JUCEApplication::getInstance())
                    app->systemRequestedQuit();
            });
        }
    }

    void shutdown() override
    {
        mainWindow_ = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        juce::ignoreUnused(commandLine);
    }

private:
    static std::filesystem::path makeSmokeScratchRoot(const std::string& prefix,
                                                      std::error_code& filesystemError)
    {
        auto tempDirectory = std::filesystem::temp_directory_path(filesystemError);
        if (filesystemError)
            return {};

        std::error_code canonicalError;
        auto canonicalTempDirectory = std::filesystem::weakly_canonical(tempDirectory,
                                                                        canonicalError);
        if (!canonicalError && !canonicalTempDirectory.empty())
            tempDirectory = std::move(canonicalTempDirectory);

        return tempDirectory / (prefix + std::to_string(juce::Time::currentTimeMillis()));
    }

    void runProjectChooserSmokeTestAndQuit()
    {
        std::string error;
        std::error_code filesystemError;
        const auto scratchRoot =
            makeSmokeScratchRoot("rabbington-studio-project-chooser-smoke-",
                                 filesystemError);
        auto passed = false;

        if (filesystemError)
        {
            error = "Could not locate a temporary directory: " + filesystemError.message();
        }
        else
        {
            passed = mainWindow_ != nullptr
                && mainWindow_->getMainComponent().runProjectChooserSmokeTest(scratchRoot, error);
        }

        if (!passed)
        {
            std::cerr << "Project chooser smoke failed: " << error << '\n';
            setApplicationReturnValue(1);
        }

        systemRequestedQuit();
    }

    void runAppSettingsCorruptionSmokeTestAndQuit()
    {
        std::string error;
        std::error_code filesystemError;
        const auto scratchRoot =
            makeSmokeScratchRoot("rabbington-studio-app-settings-corruption-smoke-",
                                 filesystemError);
        auto passed = false;

        if (filesystemError)
        {
            error = "Could not locate a temporary directory: " + filesystemError.message();
        }
        else
        {
            passed = mainWindow_ != nullptr
                && mainWindow_->getMainComponent().runAppSettingsCorruptionSmokeTest(scratchRoot, error);
        }

        if (!passed)
        {
            std::cerr << "App settings corruption smoke failed: " << error << '\n';
            setApplicationReturnValue(1);
        }

        systemRequestedQuit();
    }

    void runRestoreDetailSmokeTestAndQuit()
    {
        std::string error;
        std::error_code filesystemError;
        const auto scratchRoot =
            makeSmokeScratchRoot("rabbington-studio-restore-detail-smoke-",
                                 filesystemError);
        auto passed = false;

        if (filesystemError)
        {
            error = "Could not locate a temporary directory: " + filesystemError.message();
        }
        else
        {
            passed = mainWindow_ != nullptr
                && mainWindow_->getMainComponent().runPackageMediaRestoreDetailSmokeTest(scratchRoot, error);
        }

        if (!passed)
        {
            std::cerr << "Restore detail smoke failed: " << error << '\n';
            setApplicationReturnValue(1);
        }

        systemRequestedQuit();
    }

    void runAudioMidiResetSmokeTestAndQuit()
    {
        std::string error;
        std::error_code filesystemError;
        const auto scratchRoot =
            makeSmokeScratchRoot("rabbington-studio-audio-midi-reset-smoke-",
                                 filesystemError);
        auto passed = false;

        if (filesystemError)
        {
            error = "Could not locate a temporary directory: " + filesystemError.message();
        }
        else
        {
            passed = mainWindow_ != nullptr
                && mainWindow_->getMainComponent().runAudioMidiResetSmokeTest(scratchRoot, error);
        }

        if (!passed)
        {
            std::cerr << "Audio/MIDI reset smoke failed: " << error << '\n';
            setApplicationReturnValue(1);
        }

        systemRequestedQuit();
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Colour(0xff101114),
                             juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            mainComponent_ = new MainComponent();
            setContentOwned(mainComponent_, true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        MainComponent& getMainComponent() const noexcept
        {
            return *mainComponent_;
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        MainComponent* mainComponent_ = nullptr;
    };

    std::unique_ptr<MainWindow> mainWindow_;
    bool smokeTestMode_ = false;
    bool projectChooserSmokeTestMode_ = false;
    bool audioMidiResetSmokeTestMode_ = false;
    bool appSettingsCorruptionSmokeTestMode_ = false;
    bool restoreDetailSmokeTestMode_ = false;
};

START_JUCE_APPLICATION(RabbingtonStudioApplication)
