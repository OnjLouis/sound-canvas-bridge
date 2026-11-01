#pragma once
#include "Controls.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace scbridge {
// Only exposed bridge settings, never sample data or vendor code. The byte-only
// layout is stable across native and Intel processes and in saved projects.
struct EngineScene {
    uint8_t legacy[16][14]{};
    uint8_t parts[16][8]{};
    uint8_t globals[34]{};
    uint8_t drums[2][6][128]{};
};
static_assert(sizeof(EngineScene)==1922);
inline bool validScene(const EngineScene& scene) {
    for(int part=0;part<16;++part) {
        for(unsigned i=1;i<15;++i) {
            const auto value=scene.legacy[part][i-1];
            if(value<controls[i].minimum || value>controls[i].maximum) return false;
        }
        for(unsigned i=16;i<24;++i) {
            const auto value=scene.parts[part][i-16];
            if(value<controls[i].minimum || value>controls[i].maximum) return false;
        }
    }
    for(unsigned i=24;i<58;++i)
        if(scene.globals[i-24]<controls[i].minimum || scene.globals[i-24]>controls[i].maximum) return false;
    for(const auto& setup:scene.drums) for(const auto& field:setup)
        for(auto value:field) if(value>127) return false;
    return true;
}
struct StateFrame {
    uint32_t sceneValid=0;
    uint32_t epoch=0;
    EngineScene scene;
    uint32_t generations[parameterCount]{};
    int32_t values[parameterCount]{};
};

// Fixed storage with bounded non-blocking handoff. A slot is never overwritten
// while a reader copies it. No allocation or OS lock is used by the audio path.
template<class T> class StateMailbox {
public:
    static_assert(std::atomic<int>::is_always_lock_free);
    bool publish(const T& value) {
        const int current=latest.load(std::memory_order_acquire);
        for(int i=0;i<3;++i) {
            if(i==current) continue;
            int expected=0;
            if(!slots[i].gate.compare_exchange_strong(expected,-1,std::memory_order_acq_rel)) continue;
            slots[i].value=value;
            latest.store(i,std::memory_order_release);
            slots[i].gate.store(0,std::memory_order_release);
            return true;
        }
        return false;
    }
    bool read(T& value) const {
        for(int attempt=0;attempt<64;++attempt) {
            const int selected=latest.load(std::memory_order_acquire);
            if(selected<0) return false;
            int expected=0;
            if(!slots[selected].gate.compare_exchange_strong(expected,1,std::memory_order_acq_rel)) continue;
            const bool current=latest.load(std::memory_order_acquire)==selected;
            if(current) value=slots[selected].value;
            slots[selected].gate.store(0,std::memory_order_release);
            if(current) return true;
        }
        return false;
    }
    void clear() { latest.store(-1,std::memory_order_release); }
private:
    struct Slot { mutable std::atomic<int> gate{0};T value{}; };
    std::array<Slot,3> slots{};
    std::atomic<int> latest{-1};
};
}
