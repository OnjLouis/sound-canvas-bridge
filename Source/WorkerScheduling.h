#pragma once
#include <algorithm>
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <avrt.h>
#else
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#endif

namespace scbridge {
inline bool configureAudioWorker(double rate, uint32_t frames = 32) {
#ifdef _WIN32
    struct Registration {
        HANDLE handle=nullptr;
        ~Registration() { if(handle) AvRevertMmThreadCharacteristics(handle); }
    };
    static thread_local Registration registration;
    if(!registration.handle) {
        DWORD index=0;
        registration.handle=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);
    }
    return registration.handle && AvSetMmThreadPriority(registration.handle,AVRT_PRIORITY_HIGH);
#else
    mach_timebase_info_data_t timebase{};
    if (mach_timebase_info(&timebase) != KERN_SUCCESS) return false;
    // Apply to a running thread so a rejected policy cannot strand a JUCE
    // thread handle. The ordinary high-QoS thread remains the fallback.
    constexpr double minimumPeriodSeconds = 0.0005;
    const double period = std::max(minimumPeriodSeconds, frames / rate);
    auto ticks = [&](double seconds) {
        return static_cast<uint32_t>(seconds * 1000000000.0 * timebase.denom / timebase.numer);
    };
    thread_time_constraint_policy_data_t policy{ticks(period),ticks(period*0.25),ticks(period),true};
    return thread_policy_set(pthread_mach_thread_np(pthread_self()),THREAD_TIME_CONSTRAINT_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
#endif
}
}
