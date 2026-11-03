#pragma once
#include "Protocol.h"
#include "WorkerScheduling.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#ifndef _WIN32
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/sync_policy.h>
#include <mach/mach_time.h>
#include <pthread.h>
#endif
#include <algorithm>

namespace scbridge {
constexpr uint32_t renderQuantum = 32;
constexpr uint32_t queueCapacity = 128;
constexpr uint32_t maximumBatchQuanta = 32;
constexpr double realtimeWaitFraction = 0.75;
constexpr double maximumRealtimeWaitSeconds = 0.050;
constexpr double offlineWaitSeconds = 2.0;
constexpr int workerIdleWaitMilliseconds = 100;

// A coalesced kernel notification wakes the worker or its waiting consumer.
// No mutex, allocation, file access or socket operation runs in notify().
class WakeSignal {
public:
#ifdef _WIN32
    WakeSignal() : event(CreateEventW(nullptr,FALSE,FALSE,nullptr)) {}
    ~WakeSignal() { if(event) CloseHandle(event); }
    WakeSignal(const WakeSignal&) = delete;
    WakeSignal& operator=(const WakeSignal&) = delete;
    bool valid() const { return event!=nullptr; }
    void notify() { if(event && !pending.exchange(true,std::memory_order_acq_rel)) SetEvent(event); }
    bool waitUntil(std::chrono::steady_clock::time_point deadline) {
        const auto remaining=std::chrono::duration<double,std::milli>(deadline-std::chrono::steady_clock::now()).count();
        if(!event || remaining<=0) return false;
        const auto timeout=static_cast<DWORD>(std::min(2147483647.0,std::ceil(remaining)));
        if(WaitForSingleObject(event,timeout)!=WAIT_OBJECT_0) return false;
        pending.store(false,std::memory_order_release);return true;
    }
    void reset() { if(event) ResetEvent(event);pending.store(false,std::memory_order_release); }
private:
    HANDLE event=nullptr;
    std::atomic<bool> pending{false};
#else
    WakeSignal() { semaphore_create(mach_task_self(), &semaphore, SYNC_POLICY_FIFO, 0); }
    ~WakeSignal() { if (valid()) semaphore_destroy(mach_task_self(), semaphore); }
    WakeSignal(const WakeSignal&) = delete;
    WakeSignal& operator=(const WakeSignal&) = delete;
    bool valid() const { return semaphore != SEMAPHORE_NULL; }
    void notify() {
        if (valid() && !pending.exchange(true, std::memory_order_acq_rel))
            semaphore_signal(semaphore);
    }
    bool waitUntil(std::chrono::steady_clock::time_point deadline) {
        while (valid()) {
            auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) return false;
            mach_timespec_t timeout{static_cast<unsigned>(remaining.count() / 1000000000),
                                   static_cast<int>(remaining.count() % 1000000000)};
            auto result = semaphore_timedwait(semaphore, timeout);
            if (result == KERN_SUCCESS) { pending.store(false, std::memory_order_release); return true; }
            if (result != KERN_ABORTED) return false;
        }
        return false;
    }
    void reset() {
        if (valid()) while (semaphore_timedwait(semaphore, mach_timespec_t{0,0}) == KERN_SUCCESS) {}
        pending.store(false, std::memory_order_release);
    }
private:
    semaphore_t semaphore = SEMAPHORE_NULL;
    std::atomic<bool> pending{false};
#endif
};

// One producer and one consumer own each queue. Payloads are published only
// after construction; the audio callback never touches in-flight shared memory.
template <typename Packet> class PacketQueue {
public:
    Packet* beginWrite() {
        auto position = written.load(std::memory_order_relaxed);
        if (position - read.load(std::memory_order_acquire) == queueCapacity) return nullptr;
        return &packets[position % queueCapacity];
    }
    void endWrite() { written.fetch_add(1, std::memory_order_release); }
    Packet* beginRead(uint32_t offset = 0) {
        auto position = read.load(std::memory_order_relaxed);
        if (offset >= written.load(std::memory_order_acquire) - position) return nullptr;
        return &packets[(position + offset) % queueCapacity];
    }
    uint32_t writable() const {
        return queueCapacity - (written.load(std::memory_order_relaxed) - read.load(std::memory_order_acquire));
    }
    void endRead() { read.fetch_add(1, std::memory_order_release); }
    void reset() { written.store(0); read.store(0); }
private:
    std::array<Packet, queueCapacity> packets{};
    std::atomic<uint32_t> written{0}, read{0};
};

struct InputPacket {
    uint64_t start = 0;
    uint32_t count = 0, bytes = 0, reset = 0;
    uint32_t restoreSceneValid=0;
    uint32_t stateEpoch=0;
    uint64_t factoryRequest=0;
    uint64_t factoryExpected[4]{};
    EngineScene restoredScene;
    int32_t parameters[parameterCount]{};
    uint32_t generations[parameterCount]{};
    Event events[maxEvents]{};
    uint8_t midi[maxMidiBytes]{};
};
inline uint64_t packControl(uint32_t generation,int32_t value) {
    return (uint64_t(generation)<<32)|uint32_t(value);
}
inline uint32_t controlGeneration(uint64_t value) { return uint32_t(value>>32); }
inline int32_t controlValue(uint64_t value) { return int32_t(uint32_t(value)); }
struct OutputPacket {
    uint64_t start = 0;
    float left[renderQuantum]{}, right[renderQuantum]{};
};
}
