#pragma once
#include "AppPreferences.h"
#include <atomic>

namespace scbridge {
class UpdateService final : private juce::Timer,private juce::Thread {
public:
    UpdateService(AppPreferences& value,juce::String version)
        :Thread("Sound Canvas update check"),preferences(value),current(std::move(version)) {}
    ~UpdateService() override {
        stop();
    }
    void stop() { stopTimer();alive->store(false);signalThreadShouldExit();stopThread(3000); }
    void start() { launching=true;startTimer(2000); }
    void preferencesChanged() { startTimer(1000); }
    bool busy() const { return isThreadRunning(); }
    juce::String status() const { return lastStatus; }
    void checkNow() { check(true); }
private:
    void timerCallback() override {
        startTimer(60000);
        const auto now=juce::Time::currentTimeMillis();
        if(!busy() && updateCheckDue(preferences.frequency(),preferences.lastCheck(),now,launching)) check(false);
        launching=false;
    }
    void check(bool manual) {
        if(busy()) {
            if(manual) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,"Check for Updates","An update check or download is already running.");
            return;
        }
        manualRequest=manual;preparing=false;
        if(!preferences.recordCheck(juce::Time::currentTimeMillis())) {
            lastStatus="Could not save the last update check time.";
            juce::Logger::writeToLog(lastStatus);return;
        }
        lastStatus="Checking for updates...";startThread();
    }
    void run() override {
        const auto helper=juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory().getParentDirectory().getChildFile("Resources/SoundCanvasMaintenance");
        juce::ChildProcess child;
        juce::StringArray arguments{helper.getFullPathName(),preparing ? "--prepare-json" : "--check",preparing ? releaseJSON : current};
        juce::String output;bool failed=false;
        if(!helper.existsAsFile() || !child.start(arguments)) { output="The update helper could not start.";failed=true; }
        else {
            const auto deadline=juce::Time::getMillisecondCounterHiRes()+(preparing ? 150000 : 40000);
            while(child.isRunning() && !threadShouldExit() && juce::Time::getMillisecondCounterHiRes()<deadline) {
                child.waitForProcessToFinish(20);
            }
            if(child.isRunning()) { child.kill();child.waitForProcessToFinish(1000);output="The update operation was cancelled or timed out.";failed=true; }
            else output+=child.readAllProcessOutput();
        }
        if(threadShouldExit()) return;
        auto token=alive;const bool wasManual=manualRequest,wasPreparing=preparing;
        juce::MessageManager::callAsync([this,token,output,failed,wasManual,wasPreparing] {
            if(!token->load()) return;
            auto result=failed ? juce::var{} : juce::JSON::parse(output);
            const auto state=result.getProperty("status",{}).toString();
            const auto message=result.getProperty("message",{}).toString();
            if(state=="available") {
                auto release=result.getProperty("release",{});
                const auto version=release.getProperty("version",{}).toString();
                lastStatus="Version "+version+" is available.";
                juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::InfoIcon,"Sound Canvas Bridge "+version+" Is Available",
                    "Download the verified disk image and open its installer? The installer updates the standalone, AU and VST3 together. Playback will not stop unless you choose Install.",
                    "Download and Open Installer","Later",nullptr,juce::ModalCallbackFunction::create([this,token,release](int choice) {
                        if(!token->load() || !choice || busy()) return;
                        releaseJSON=juce::JSON::toString(release,true);preparing=true;manualRequest=true;
                        lastStatus="Downloading and verifying the update...";startThread();
                    }));
                return;
            }
            lastStatus=message.isNotEmpty() ? message : failed ? output : "The update server returned an invalid response.";
            if(state=="error" || state.isEmpty()) juce::Logger::writeToLog("Update operation: "+lastStatus.substring(0,4096));
            if(wasManual || wasPreparing) juce::AlertWindow::showMessageBoxAsync(
                state=="error" || state.isEmpty() ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                "Check for Updates",lastStatus);
        });
    }
    AppPreferences& preferences;
    const juce::String current;
    std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);
    bool launching=false,manualRequest=false,preparing=false;
    juce::String releaseJSON,lastStatus="Automatic checks notify you; installation always requires confirmation.";
};
}
