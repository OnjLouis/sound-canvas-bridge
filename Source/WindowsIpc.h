#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <cstring>
#include "Protocol.h"

namespace scbridge {
struct WindowsShared {
    Shared audio;
    volatile LONG command = 0;
    volatile LONG reply = 0;
};

// Anonymous handles are inherited only by this instance's engine helper.
// The job closes the helper if its owning host exits unexpectedly.
class WindowsIpc {
public:
    ~WindowsIpc() { close(); }
    WindowsIpc() = default;
    WindowsIpc(const WindowsIpc&) = delete;
    WindowsIpc& operator=(const WindowsIpc&) = delete;
    bool start(const std::wstring& executable, const std::wstring& core) {
        close();
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,sizeof(WindowsShared),nullptr);
        request=CreateEventW(&security,FALSE,FALSE,nullptr);
        response=CreateEventW(&security,FALSE,FALSE,nullptr);
        if(!mapping || !request || !response) { close();return false; }
        block=static_cast<WindowsShared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(WindowsShared)));
        if(!block) { close();return false; }
        std::memset(block,0,sizeof(*block));
        job=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {
            close();return false;
        }
        SIZE_T bytes=0;
        InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        std::vector<unsigned char> storage(bytes);
        auto* attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes)) { close();return false; }
        HANDLE inherited[]{mapping,request,response};
        const bool configured=UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited,sizeof(inherited),nullptr,nullptr)!=FALSE;
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
        PROCESS_INFORMATION created{};
        auto number=[](HANDLE h) { return std::to_wstring(reinterpret_cast<uintptr_t>(h)); };
        auto command=L"\""+executable+L"\" \""+core+L"\" "+number(mapping)+L" "+number(request)+L" "+number(response);
        const bool launched=configured && CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
            EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup.StartupInfo,&created);
        DeleteProcThreadAttributeList(attributes);
        for(auto handle:inherited) SetHandleInformation(handle,HANDLE_FLAG_INHERIT,0);
        if(!launched) { close();return false; }
        process=created.hProcess;
        const bool assigned=AssignProcessToJobObject(job,process)!=FALSE;
        if(assigned) ResumeThread(created.hThread);
        else TerminateProcess(process,1);
        CloseHandle(created.hThread);
        if(!assigned || !receive('R',5000)) { close();return false; }
        return true;
    }
    Shared* shared() const { return block ? &block->audio : nullptr; }
    bool exchange(char command,int milliseconds) {
        if(!block || !process || WaitForSingleObject(process,0)!=WAIT_TIMEOUT) return false;
        InterlockedExchange(&block->command,command);
        if(!SetEvent(request) || !receive('D',milliseconds)) return false;
        MemoryBarrier();return block->audio.result==0;
    }
    void close() {
        if(process) {
            if(block) { InterlockedExchange(&block->command,'Q');SetEvent(request); }
            if(WaitForSingleObject(process,1000)==WAIT_TIMEOUT && job) {
                TerminateJobObject(job,1);WaitForSingleObject(process,1000);
            }
            CloseHandle(process);process=nullptr;
        }
        if(job) { CloseHandle(job);job=nullptr; }
        if(block) { UnmapViewOfFile(block);block=nullptr; }
        for(auto* handle:{&mapping,&request,&response}) if(*handle) { CloseHandle(*handle);*handle=nullptr; }
    }
private:
    bool receive(char expected,int milliseconds) {
        HANDLE waits[]{response,process};
        if(WaitForMultipleObjects(2,waits,FALSE,static_cast<DWORD>(milliseconds))!=WAIT_OBJECT_0) return false;
        return InterlockedExchange(&block->reply,0)==expected;
    }
    WindowsShared* block=nullptr;
    HANDLE mapping=nullptr,request=nullptr,response=nullptr,process=nullptr,job=nullptr;
};
}
#endif
