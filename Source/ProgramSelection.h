#pragma once
#include <cstddef>
#include <cstdint>
#include "ControlMessage.h"
#include "Pipeline.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace scbridge {
constexpr uint16_t defaultRhythmParts = 1u << 9;
constexpr uint32_t programSelectionEventCount = 2;
constexpr uint64_t programRequestReady = uint64_t{1} << 31;
constexpr int programRequestIndex(uint64_t value) noexcept { return static_cast<int>(value&0xffff)-1; }
constexpr int programRequestPart(uint64_t value) noexcept { return static_cast<int>((value>>16)&15); }
class ProgramRequests {
public:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    uint64_t reserve(int index,int part) {
        if(index<0 || index>=65535 || part<0 || part>=16) return 0;
        const auto next=(uint64_t(serial.fetch_add(1)+1)<<32)|(uint64_t(part)<<16)|uint64_t(index+1);
        requested.store(next);return next;
    }
    bool commit(uint64_t reserved) {
        return reserved && requested.compare_exchange_strong(reserved,reserved|programRequestReady);
    }
    void cancel() { requested.store(0); }
    bool cancelIf(uint64_t expected) { return requested.compare_exchange_strong(expected,0); }
    uint64_t load() const { return requested.load(); }
private:
    std::atomic<uint32_t> serial{0};
    std::atomic<uint64_t> requested{0};
};
inline uint16_t sceneRhythmParts(const EngineScene& scene) noexcept {
    uint16_t result=0;
    for(unsigned part=0;part<16;++part) if(scene.partModes[part]) result=static_cast<uint16_t>(result|(1u<<part));
    return result;
}
inline uint16_t programRolesBeforePacket(const InputPacket& packet,uint16_t completedRoles,bool advancedReady) noexcept {
    if(!packet.reset) return completedRoles;
    return packet.restoreSceneValid && advancedReady ? sceneRhythmParts(packet.restoredScene) : defaultRhythmParts;
}

// Only the worker commits Part. Older FIFO packets may still contain generations
// superseded by consecutive factory commits, but automation starts a new chain.
class ProgramPartState {
public:
    uint64_t resolve(uint32_t packetEpoch,uint64_t command) const noexcept {
        if(packetEpoch!=epoch) return command;
        const auto distance=uint32_t(controlGeneration(command)-controlGeneration(first));
        const auto span=uint32_t(controlGeneration(latest)-controlGeneration(first));
        return distance<span ? latest : command;
    }
    bool commit(uint32_t packetEpoch,int part,std::atomic<uint64_t>& command,uint64_t& packetPart) noexcept {
        if(part<0 || part>=16) return false;
        const auto previous=packetPart;
        const auto selected=controlValue(previous)==part ? previous : packControl(controlGeneration(previous)+1,part);
        auto expected=previous;
        if(!command.compare_exchange_strong(expected,selected)) return false;
        if(packetEpoch!=epoch || previous!=latest) first=previous;
        epoch=packetEpoch;latest=selected;packetPart=selected;
        return true;
    }
private:
    uint32_t epoch=0;
    uint64_t first=0,latest=0;
};

inline std::array<ControlMessage, programSelectionEventCount> programMessages(int part, int map, int bank, int program) {
    if (part < 0 || part >= 16 || map < 1 || map > 4 || bank < 0 || bank > 127 || program < 0 || program > 127) return {};
    const unsigned block = part == 9 ? 0 : part < 9 ? part + 1 : part;
    return {dt1Message(0x404000 + (block << 8), static_cast<uint8_t>(map)),
            dt1Message(0x401000 + (block << 8), static_cast<uint8_t>(bank), static_cast<uint8_t>(program))};
}
inline bool appendProgramMessages(InputPacket& packet, int part, int map, int bank, int program) {
    const auto messages = programMessages(part, map, bank, program);
    if (!messages[0].size || !messages[1].size || packet.count > maxEvents - programSelectionEventCount ||
        packet.bytes > maxMidiBytes - messages[0].size - messages[1].size) return false;
    for (const auto& message : messages) {
        packet.events[packet.count++] = {0, message.size, packet.bytes};
        std::memcpy(packet.midi + packet.bytes, message.bytes.data(), message.size);
        packet.bytes += message.size;
    }
    return true;
}
inline bool queueProgramSelection(InputPacket& packet,uint64_t request,int map,int bank,int program) {
    if(!(request&programRequestReady) || packet.factoryRequest) return false;
    const auto previousCount=packet.count;
    if(!appendProgramMessages(packet,programRequestPart(request),map,bank,program)) return false;
    std::rotate(packet.events,packet.events+previousCount,packet.events+packet.count);
    packet.factoryRequest=request;
    for(unsigned i=0;i<4;++i) packet.factoryExpected[i]=packControl(packet.generations[i],packet.parameters[i]);
    return true;
}
constexpr bool rhythmPart(uint16_t parts, int part) noexcept {
    return part >= 0 && part < 16 && (parts & (1u << part)) != 0;
}
constexpr int programPart(uint16_t parts, int selected, bool drums) noexcept {
    if (selected >= 0 && selected < 16 && rhythmPart(parts, selected) == drums) return selected;
    const int preferred = drums ? 9 : 0;
    if (rhythmPart(parts, preferred) == drums) return preferred;
    for (int part = 0; part < 16; ++part) if (rhythmPart(parts, part) == drums) return part;
    return -1;
}

}
