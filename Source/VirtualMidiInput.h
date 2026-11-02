#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#ifdef _WIN32
#include "WindowsIpc.h"
#include "VirtualMidiProtocol.h"
namespace scbridge {
class VirtualMidiInput final : private juce::Thread {
public:
    static std::unique_ptr<VirtualMidiInput> createNewDevice(const juce::String&,juce::MidiInputCallback* callback) {
        auto result=std::unique_ptr<VirtualMidiInput>(new VirtualMidiInput(callback));
        if(!result->launch()) return {};
        return result;
    }
    ~VirtualMidiInput() override {
        enabled.store(false);signalThreadShouldExit();
        if(stopEvent) SetEvent(stopEvent);
        if(process && WaitForSingleObject(process,1500)==WAIT_TIMEOUT && job) {
            juce::Logger::writeToLog("Virtual MIDI helper did not close within 1.5 seconds; closing its isolated job.");
            TerminateJobObject(job,1);WaitForSingleObject(process,1000);
        }
        if(isThreadRunning()) waitForThreadToExit(-1);
        for(auto handle:{process,job,pipe,stopEvent}) if(handle) CloseHandle(handle);
    }
    void start() { enabled.store(true); }
    void stop() { enabled.store(false); }
private:
    explicit VirtualMidiInput(juce::MidiInputCallback* target) :Thread("Sound Canvas virtual MIDI"),callback(target),converter(65536) {}
    bool launch() {
        auto directory=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        const auto executable=directory.getChildFile("SoundCanvasMidiHost.exe");
        if(!executable.existsAsFile()) return false;
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE output=nullptr;
        if(!CreatePipe(&pipe,&output,&security,65536)) return false;
        SetHandleInformation(pipe,HANDLE_FLAG_INHERIT,0);
        stopEvent=CreateEventW(&security,TRUE,FALSE,nullptr);job=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!stopEvent || !job || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) { CloseHandle(output);return false; }
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        std::vector<unsigned char> storage(bytes);
        auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes)) { CloseHandle(output);return false; }
        HANDLE inherited[]{output,stopEvent};
        bool configured=UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)!=FALSE;
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
        auto command=L"\""+std::wstring(executable.getFullPathName().toWideCharPointer())+L"\" "+
            std::to_wstring(reinterpret_cast<uintptr_t>(output))+L" "+std::to_wstring(reinterpret_cast<uintptr_t>(stopEvent));
        PROCESS_INFORMATION child{};
        const bool launched=configured && CreateProcessW(executable.getFullPathName().toWideCharPointer(),command.data(),nullptr,nullptr,TRUE,
            EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup.StartupInfo,&child);
        DeleteProcThreadAttributeList(attributes);CloseHandle(output);SetHandleInformation(stopEvent,HANDLE_FLAG_INHERIT,0);
        if(!launched) return false;
        process=child.hProcess;
        const bool assigned=AssignProcessToJobObject(job,process)!=FALSE;
        if(assigned) ResumeThread(child.hThread);else TerminateProcess(process,1);
        CloseHandle(child.hThread);
        if(!assigned || !startThread()) return false;
        if(!ready.wait(8000) || !available.load()) return false;
        return true;
    }
    void run() override {
        while(!threadShouldExit()) {
            VirtualMidiFrame frame{};DWORD total=0;
            while(total<sizeof(frame)) {
                DWORD received=0;
                if(!ReadFile(pipe,reinterpret_cast<unsigned char*>(&frame)+total,sizeof(frame)-total,&received,nullptr) || !received) { ready.signal();return; }
                total+=received;
            }
            if(frame.magic!=virtualMidiMagic || frame.words>4) { ready.signal();return; }
            if(!frame.words) {
                if(frame.data[0]==2) { juce::Logger::writeToLog("Native MIDI startup stage "+juce::String(frame.data[1]));continue; }
                available.store(frame.data[0]==1);ready.signal();
                if(!available.load()) {
                    juce::Logger::writeToLog("Native virtual MIDI unavailable, error 0x"+juce::String::toHexString(frame.data[1]));return;
                }
                continue;
            }
            if((frame.data[0]>>28)==3) {
                const auto status=(frame.data[0]>>20)&15,count=(frame.data[0]>>16)&15;
                if(status<=1) { sysexBytes=0;discardSysex=false; }
                sysexBytes+=count;
                if(count>6 || sysexBytes>65534) { converter.reset();discardSysex=true; }
                if(discardSysex) continue;
            }
            converter.dispatch({frame.data,frame.words},juce::Time::getMillisecondCounterHiRes()*0.001,
                [this](juce::universal_midi_packets::BytesOnGroup bytes,double time) {
                    if(enabled.load() && callback && bytes.group==0 && !bytes.bytes.empty() && bytes.bytes.size()<=65536)
                        callback->handleIncomingMidiMessage(nullptr,juce::MidiMessage(bytes.bytes.data(),static_cast<int>(bytes.bytes.size()),time));
                });
        }
    }
    juce::MidiInputCallback* callback=nullptr;
    juce::universal_midi_packets::ToBytestreamDispatcher converter;
    std::atomic<bool> enabled{false},available{false};
    juce::WaitableEvent ready;
    size_t sysexBytes=0;
    bool discardSysex=false;
    HANDLE process=nullptr,job=nullptr,pipe=nullptr,stopEvent=nullptr;
};
}
#else
namespace scbridge { using VirtualMidiInput=juce::MidiInput; }
#endif
