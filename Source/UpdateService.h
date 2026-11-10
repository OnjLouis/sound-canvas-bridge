#pragma once
#include "AppPreferences.h"
#include "UpdatePresentation.h"
#include <atomic>

namespace scbridge {
#ifdef SCBRIDGE_WINDOWS7
class UpdateService final {
public:
    UpdateService(AppPreferences&,juce::String) {}
    void stop() {}
    void start() {}
    void preferencesChanged() {}
    bool busy() const { return false; }
    juce::String status() const { return "Automatic updates are unavailable in the Windows 7 edition."; }
    void checkNow() {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,"Check for Updates",status());
    }
};
#else
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
    static constexpr int maximumReplyBytes=3072;
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
#ifdef _WIN32
            .getParentDirectory().getChildFile("SoundCanvasMaintenance.exe");
#else
            .getParentDirectory().getParentDirectory().getChildFile("Resources/SoundCanvasMaintenance");
#endif
        juce::ChildProcess child;
#ifdef _WIN32
        juce::StringArray arguments{helper.getFullPathName(),preparing ? "--prepare-base64" : "--check",
            preparing ? juce::Base64::toBase64(releaseJSON.toRawUTF8(),releaseJSON.getNumBytesAsUTF8()) : current};
#else
        juce::StringArray arguments{helper.getFullPathName(),preparing ? "--prepare-json" : "--check",preparing ? releaseJSON : current};
#endif
        juce::String output;bool failed=false;
        if(!helper.existsAsFile() || !child.start(arguments)) { output="The update helper could not start.";failed=true; }
        else {
            const auto deadline=juce::Time::getMillisecondCounterHiRes()+(preparing ? 150000 : 40000);
            while(child.isRunning() && !threadShouldExit() && juce::Time::getMillisecondCounterHiRes()<deadline) {
                child.waitForProcessToFinish(20);
            }
            if(child.isRunning()) { child.kill();child.waitForProcessToFinish(1000);output="The update operation was cancelled or timed out.";failed=true; }
            else {
                // The helpers enforce this UTF-8 reply limit before writing, so
                // waiting for exit cannot fill the platform's output pipe.
                char bytes[maximumReplyBytes+2];
                const int count=child.readProcessOutput(bytes,sizeof(bytes));
                if(count>maximumReplyBytes+1) { output="The update helper reply exceeded its limit.";failed=true; }
                else output=juce::String::fromUTF8(bytes,count);
                juce::Logger::writeToLog("Update helper exited with code "+juce::String(child.getExitCode())+"; reply bytes "+juce::String(count));
            }
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
#ifdef _WIN32
                    "Download the signed update and open its installer? The installer updates the standalone and VST3 together. Save your work and stop playback before installing.\n\n"+release.getProperty("notes",{}).toString(),
#else
                    "Download the verified disk image and open its installer? The installer updates the standalone, AU and VST3 together. Playback will not stop unless you choose Install.",
#endif
                    "Download and Open Installer","Later",nullptr,juce::ModalCallbackFunction::create([this,token,release](int choice) {
                        if(!token->load() || !choice || busy()) return;
                        releaseJSON=juce::JSON::toString(release,true);preparing=true;manualRequest=true;
                        lastStatus="Downloading and verifying the update...";startThread();
                    }));
                return;
            }
            lastStatus=message.isNotEmpty() ? message : failed ? output : "The update server returned an invalid response.";
            if(state=="error" || state.isEmpty()) juce::Logger::writeToLog("Update operation: "+lastStatus.substring(0,4096));
            // The installer is already open; a modal acknowledgement here can
            // prevent its graceful shutdown request from reaching this app.
            if(shouldShowUpdateResult(wasManual,wasPreparing,failed,state.toStdString())) juce::AlertWindow::showMessageBoxAsync(
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
#endif
}
