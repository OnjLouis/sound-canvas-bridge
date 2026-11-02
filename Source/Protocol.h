#pragma once
#include <cstdint>
#include "Controls.h"
#include "Scene.h"

namespace scbridge {
constexpr uint32_t protocolVersion = 6;
constexpr uint32_t maxFrames = 4096;
constexpr uint32_t maxEvents = 1024;
constexpr uint32_t maxMidiBytes = 65536;
struct Event { uint32_t offset, size, position; };
struct Shared {
    uint32_t version, frames, eventCount, midiBytes, reset, result;
    uint32_t helperRealtime;
    uint32_t advancedReady;
    uint32_t restoreSceneValid,capturedSceneValid;
    uint32_t stateEpoch;
    EngineScene restoredScene,capturedScene;
    double sampleRate;
    int32_t parameters[parameterCount];
    uint32_t generations[parameterCount];
    int32_t reported[parameterCount];
    Event events[maxEvents];
    uint8_t midi[maxMidiBytes];
    float left[maxFrames], right[maxFrames];
};
}
