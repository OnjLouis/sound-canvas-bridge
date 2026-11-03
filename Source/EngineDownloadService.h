#pragma once
#include "OfficialEngine.h"
#include "Performance.h"
#include <atomic>

namespace scbridge {
class EngineDownloadService final : private juce::Thread {
public:
    struct Snapshot { juce::String phase; double progress=-1; };
    EngineDownloadService():Thread("Sound Canvas engine download") {}
    ~EngineDownloadService() override { shared->alive=false;cancel();stopThread(3000); }
    bool busy() const { return shared->pending.load() || isThreadRunning(); }
    Snapshot snapshot() const { const juce::ScopedLock lock(shared->lock);return shared->snapshot; }
    void cancel() { shared->cancelled=true;signalThreadShouldExit(); }
    void start(std::function<void(juce::Result,juce::File)> finished) {
        if(isThreadRunning()) return;
        if(shared->pending.exchange(true)) return;
        shared->cancelled=false;completion=std::move(finished);setStatus("Connecting to Roland",-1);startThread();
    }
private:
    struct Shared {
        std::atomic<bool> alive{true},pending{false},cancelled{false};juce::CriticalSection lock;Snapshot snapshot;
    };
    void setStatus(juce::String phase,double progress) {
        const juce::ScopedLock lock(shared->lock);shared->snapshot={std::move(phase),progress};
    }
    void run() override {
        const auto isolated=juce::SystemStats::getEnvironmentVariable("SCBRIDGE_PROFILE_DIR",{});
        const auto cache=juce::File::isAbsolutePath(isolated) ? juce::File(isolated).getChildFile("Download Cache") :
            juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#ifdef _WIN32
            .getChildFile("OnjMusic/SoundCanvasBridge/Download Cache")
#else
            .getChildFile("Caches/OnjMusic/SoundCanvasBridge")
#endif
            ;
        const auto job=cache.getNonexistentChildFile("OnjMusic-SoundCanvas-Engine",{});
        const auto helper=juce::File::getSpecialLocation(juce::File::currentExecutableFile)
#ifdef _WIN32
            .getParentDirectory().getChildFile("SoundCanvasEngineDownload.exe");
#else
            .getParentDirectory().getParentDirectory().getChildFile("Resources/SoundCanvasEngineDownload");
#endif
        auto result=enginePathIsRedirected(job) ? juce::Result::fail("The download cache contains a symbolic link. Use Browse instead.") : job.createDirectory();
        const bool ownsJob=result.wasOk();
        juce::File installed;juce::ChildProcess child;
        if(result.wasOk() && (!helper.existsAsFile() || !child.start({helper.getFullPathName(),"--download",job.getFullPathName()})))
            result=juce::Result::fail("The engine download helper could not start. Browse remains available.");
        if(result.wasOk()) {
            const auto deadline=juce::Time::getMillisecondCounterHiRes()+180000;
            while(child.isRunning() && !threadShouldExit() && juce::Time::getMillisecondCounterHiRes()<deadline) {
                child.waitForProcessToFinish(100);
                const auto progressFile=job.getChildFile("progress.json");
                if(progressFile.existsAsFile() && progressFile.getSize()<=2048) {
                    const auto update=juce::JSON::parse(progressFile);
                    const auto phase=update.getProperty("phase",{}).toString();
                    if(phase.isNotEmpty()) setStatus(phase,juce::jlimit(0.0,1.0,double(update.getProperty("percent",0))/100.0));
                }
            }
            if(child.isRunning()) {
                child.kill();child.waitForProcessToFinish(1000);
                result=juce::Result::fail(threadShouldExit() ? "Engine download cancelled. Your current engine has not changed." :
                    "The engine download timed out. Your current engine has not changed; Browse remains available.");
            } else {
                char bytes[4097]{};const int count=child.readProcessOutput(bytes,sizeof(bytes));
                const auto reply=count<=4096 ? juce::JSON::parse(juce::String::fromUTF8(bytes,count)) : juce::var{};
                if(child.getExitCode()!=0 || reply.getProperty("status",{}).toString()!="ok") {
                    const auto message=reply.getProperty("message",{}).toString();
                    result=juce::Result::fail(message.isNotEmpty() ? message : "The engine download or extraction failed. Your current engine has not changed.");
                }
            }
        }
        if(threadShouldExit()) result=juce::Result::fail("Engine download cancelled. Your current engine has not changed.");
        if(result.wasOk()) {
            setStatus("Installing the verified engine",1);
            installed=performanceFile().getParentDirectory().getChildFile("Downloaded Engines")
                .getChildFile(officialCoreHash).getChildFile("SCCore.dll");
#ifdef _WIN32
            // Install the app-local dependency before making this engine selectable.
            result=importVerifiedEngine(job.getChildFile("MSVCR100.dll"),installed.getSiblingFile("MSVCR100.dll"),officialRuntimeHash);
            if(result.wasOk())
#endif
                result=importVerifiedEngine(job.getChildFile("SCCore.dll"),installed,officialCoreHash);
        }
        if(ownsJob && !job.deleteRecursively()) juce::Logger::writeToLog("Engine download: temporary extraction cleanup failed.");
        const auto state=shared;const auto callback=completion;
        juce::MessageManager::callAsync([state,callback,result,installed] {
            if(!state->alive.load()) return;
            state->pending=false;
            if(callback) callback(state->cancelled.load() ? juce::Result::fail("Engine download cancelled. Your current engine has not changed.") : result,installed);
        });
    }
    std::shared_ptr<Shared> shared=std::make_shared<Shared>();
    std::function<void(juce::Result,juce::File)> completion;
};
}
