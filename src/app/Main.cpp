// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MainComponent.h"

#include "core/ProductIdentity.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

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
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());

        if (smokeTestMode_)
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
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Colour(0xff101114),
                             juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow_;
    bool smokeTestMode_ = false;
};

START_JUCE_APPLICATION(RabbingtonStudioApplication)
