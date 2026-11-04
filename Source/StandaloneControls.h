#pragma once
#include "ControlMessage.h"
#include "Scene.h"

namespace scbridge {
inline std::array<ControlMessage,16> allPartMapMessages(int map) {
    std::array<ControlMessage,16> result{};
    if(map<1 || map>4) return result;
    for(unsigned part=0;part<result.size();++part) {
        const unsigned block=part==9 ? 0 : part<9 ? part+1 : part;
        // Map 0 follows the persistent TONE MAP-0 NUMBER across GS Reset.
        result[part]=dt1Message(0x404000+(block<<8),0,static_cast<uint8_t>(map));
    }
    return result;
}
inline std::array<ControlMessage,32> allPartMapSelection(int map,const EngineScene& scene) {
    std::array<ControlMessage,32> result{};
    if(map<1 || map>4) return result;
    const auto maps=allPartMapMessages(map);
    std::copy(maps.begin(),maps.end(),result.begin());
    // A map-register write alone does not reselect an already loaded voice.
    // Native tone selection is channel-routing independent, including part 10.
    for(unsigned part=0;part<16;++part) {
        const unsigned block=part==9 ? 0 : part<9 ? part+1 : part;
        result[16+part]=dt1Message(0x401000+(block<<8),scene.legacy[part][1],scene.legacy[part][2]);
    }
    return result;
}
}
