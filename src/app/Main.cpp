// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MainComponent.h"

#include "core/ProductIdentity.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>

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
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());

        if (projectChooserSmokeTestMode_)
        {
            juce::Timer::callAfterDelay(500,
                                        [this]
                                        {
                                            runProjectChooserSmokeTestAndQuit();
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
    void runProjectChooserSmokeTestAndQuit()
    {
        std::string error;
        std::error_code filesystemError;
        const auto tempDirectory = std::filesystem::temp_directory_path(filesystemError);
        auto passed = false;

        if (filesystemError)
        {
            error = "Could not locate a temporary directory: " + filesystemError.message();
        }
        else
        {
            const auto scratchRoot =
                tempDirectory
                / ("rabbington-studio-project-chooser-smoke-"
                   + std::to_string(juce::Time::currentTimeMillis()));
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

    void runAudioMidiResetSmokeTestAndQuit()
    {
        std::string error;
        std::error_code filesystemError;
        const auto tempDirectory = std::filesystem::temp_directory_path(filesystemError);
        auto passed = false;

        if (filesystemError)
        {
            error = "Could not locate a temporary directory: " + filesystemError.message();
        }
        else
        {
            const auto scratchRoot =
                tempDirectory
                / ("rabbington-studio-audio-midi-reset-smoke-"
                   + std::to_string(juce::Time::currentTimeMillis()));
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
};

START_JUCE_APPLICATION(RabbingtonStudioApplication)
