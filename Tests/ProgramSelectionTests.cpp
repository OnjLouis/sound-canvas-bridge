#include "ProgramSelection.h"
#include "ControlMessage.h"
#include "ControllerMirror.h"
#include <cstdlib>
#include <iostream>

static void require(bool value, const char* text) {
    if (!value) { std::cerr << "FAIL: " << text << '\n'; std::exit(1); }
}
static void workerSelectionTests() {
    using namespace scbridge;
    ProgramRequests requests;
    const auto reserved=requests.reserve(12,9);
    require(requests.commit(reserved), "Commit queued model request");
    InputPacket packet;
    packet.stateEpoch=7;packet.parameters[0]=3;packet.generations[0]=20;
    packet.parameters[15]=1;packet.generations[15]=4;
    packet.events[0]={0,3,0};packet.count=1;packet.bytes=3;
    packet.midi[0]=0x90;packet.midi[1]=60;packet.midi[2]=100;
    require(queueProgramSelection(packet,requests.load(),4,0,0), "Queue factory pair before host notes");
    require(packet.parameters[0]==3 && packet.generations[0]==20 &&
            packet.factoryExpected[0]==packControl(20,3) && packet.parameters[15]==1 && packet.generations[15]==4 &&
            packet.events[2].position==0 && packet.midi[packet.events[2].position]==0x90,
            "Queuing must not change Part, its generation or host MIDI order");

    EngineScene actual;
    actual.partModes[9]=0;actual.partModes[2]=2;
    const auto completedRoles=sceneRhythmParts(actual);
    require(programRolesBeforePacket(packet,completedRoles,false)==completedRoles,
            "Completed mirror roles remain authoritative without advanced scene capture");
    packet.reset=1;packet.restoreSceneValid=1;packet.restoredScene=actual;
    require(programRolesBeforePacket(packet,completedRoles,false)==defaultRhythmParts,
            "Reset must ignore saved roles when advanced restoration is unavailable");
    require(programRolesBeforePacket(packet,defaultRhythmParts,true)==completedRoles,
            "Reset must use restored roles when advanced restoration is available");
    packet.restoreSceneValid=0;
    require(programRolesBeforePacket(packet,completedRoles,true)==defaultRhythmParts,
            "Reset without a restoration returns to default roles");
    packet.reset=0;

    ProgramPartState parts;
    const auto original=packControl(20,3);
    std::atomic<uint64_t> command{original};
    auto effective=parts.resolve(packet.stateEpoch,original);
    const bool roleMatches=rhythmPart(programRolesBeforePacket(packet,completedRoles,false),9);
    require(!roleMatches, "Preceding MIDI made the requested drum part melodic");
    if(roleMatches) parts.commit(packet.stateEpoch,9,command,effective);
    require(command.load()==original && effective==original && parts.resolve(packet.stateEpoch,original)==original,
            "Rejected factory must leave routing and the helper Part generation unchanged");

    require(parts.commit(packet.stateEpoch,2,command,effective) && effective==packControl(21,2),
            "An accepted factory commits Part on the worker");
    require(parts.resolve(packet.stateEpoch,original)==effective,
            "Already queued packets inherit an accepted Part instead of reverting it");
    const auto firstAccepted=effective;
    effective=parts.resolve(packet.stateEpoch,original);
    require(parts.commit(packet.stateEpoch,4,command,effective) && effective==packControl(22,4),
            "A second queued factory resolves its superseded source Part before committing");
    require(parts.resolve(packet.stateEpoch,original)==effective &&
            parts.resolve(packet.stateEpoch,firstAccepted)==effective,
            "All superseded factory generations carry forward through FIFO packets");
    const auto accepted=effective;
    require(parts.resolve(packet.stateEpoch,original)==accepted && command.load()==accepted,
            "A later rejected factory must retain the last accepted Part");

    command.store(packControl(23,4));
    require(!parts.commit(packet.stateEpoch,4,command,effective) && effective==accepted && command.load()==packControl(23,4),
            "Same-Part requests must still reject an intervening automation generation");

    const auto automation=packControl(23,6);
    command.store(automation);
    require(parts.resolve(packet.stateEpoch,automation)==automation,
            "Factory aliases must not mask a newer automation generation");
    effective=parts.resolve(packet.stateEpoch,original);
    require(!parts.commit(packet.stateEpoch,9,command,effective) && effective==accepted && command.load()==automation,
            "Automation racing worker commit must win without adding an alias");
    effective=parts.resolve(packet.stateEpoch,automation);
    require(parts.commit(packet.stateEpoch,8,command,effective) && effective==packControl(24,8),
            "A later factory may commit after the automation packet reaches the worker");
    require(parts.resolve(packet.stateEpoch,automation)==effective &&
            parts.resolve(packet.stateEpoch,original)==original,
            "A fresh alias chain must not span intervening automation generations");
    require(parts.resolve(packet.stateEpoch+1,automation)==automation,
            "Part aliases must not cross restored-state epochs");
    effective=packControl(30,8);command.store(effective);
    require(parts.commit(packet.stateEpoch+1,8,command,effective) && effective==packControl(30,8),
            "Loading on the current Part must not advance its generation or trigger part-change AllSoundOff");

    ProgramPartState wrapped;
    const auto beforeWrap=packControl(UINT32_MAX,0);
    command.store(beforeWrap);effective=beforeWrap;
    require(wrapped.commit(1,9,command,effective) && effective==packControl(0,9) &&
            wrapped.resolve(1,beforeWrap)==effective && wrapped.resolve(1,packControl(1,5))==packControl(1,5),
            "Factory aliases must survive generation wrap without consuming future automation");
}
int main() {
    using namespace scbridge;
    workerSelectionTests();
    ProgramRequests requests;
    const auto reserved=requests.reserve(12,9);
    require(programRequestIndex(reserved)==12 && programRequestPart(reserved)==9 && !(reserved&programRequestReady),
            "Selection is not consumable before its UI values are ready");
    requests.cancel();
    require(!requests.commit(reserved) && !requests.load(), "Intervening automation must win over publication");
    const auto next=requests.reserve(12,9);
    require(next!=reserved && requests.commit(next) && (requests.load()&programRequestReady), "Repeated presets have distinct committed requests");
    require(!requests.cancelIf(reserved) && requests.load(), "An old cancellation cannot cancel a newer request");
    ControllerMirror mirror;
    auto observe=[&](const uint8_t* data,size_t size) {
        mirror.observeLong(data,static_cast<uint32_t>(size));
        EngineScene scene;mirror.capture(scene);return sceneRhythmParts(scene);
    };
    InputPacket packet;
    require(appendProgramMessages(packet, 9, 4, 8, 16), "Append a coherent map and tone transaction");
    require(packet.count == 2 && packet.events[0].offset == 0 && packet.events[1].offset == 0,
            "Map and tone selection must share the same sample offset");
    const auto messages = programMessages(9, 4, 8, 16);
    require(!std::memcmp(packet.midi, messages[0].bytes.data(), messages[0].size) &&
            !std::memcmp(packet.midi + messages[0].size, messages[1].bytes.data(), messages[1].size),
            "Preserve exact native map then bank/program SysEx order");
    packet.count = maxEvents - 1;
    const auto previousBytes = packet.bytes;
    require(!appendProgramMessages(packet, 0, 1, 0, 0) && packet.count == maxEvents - 1 && packet.bytes == previousBytes,
            "Defer a complete selection when event space is insufficient");
    packet.count = 0; packet.bytes = maxMidiBytes - 1;
    require(!appendProgramMessages(packet, 0, 1, 0, 0) && packet.count == 0,
            "Defer a complete selection when byte space is insufficient");
    require(!programMessages(-1, 1, 0, 0)[0].size && !programMessages(16, 1, 0, 0)[0].size,
            "Invalid targets must not create MIDI");
    require(programPart(defaultRhythmParts, 4, false) == 4, "Preserve a selected melodic part");
    require(programPart(defaultRhythmParts, 9, false) == 0, "Do not load melodic addresses on a rhythm part");
    require(programPart(defaultRhythmParts, 0, true) == 9, "Use default drum part for kits");
    auto parts = defaultRhythmParts;
    for (int part = 0; part < 16; ++part) {
        const unsigned block = part == 9 ? 0 : part < 9 ? part + 1 : part;
        for (int mode : {0, 1, 2}) {
            auto message = dt1Message(0x401015 + (block << 8), mode);
            parts = observe(message.bytes.data(), message.size);
            require(rhythmPart(parts, part) == (mode != 0), "Observe native rhythm assignment on every part");
        }
    }
    require(programPart(0xffff, 0, false) == -1, "No invented melodic part when all parts are drums");
    require(programPart(0, 9, true) == -1, "No forced drums on channel 10");
    require(programPart(0, 9, false) == 9, "Melodic channel 10 remains melodic");
    auto message = dt1Message(0x401015, 0);
    auto broken = message; broken.bytes[broken.size - 2] ^= 1;
    mirror.reset();
    require(observe(broken.bytes.data(), broken.size) == defaultRhythmParts,
            "Ignore invalid checksums");
    require(observe(message.bytes.data(), message.size) == 0,
            "Observe channel 10 leaving rhythm mode");
    message = dt1Message(0x40007f, 0);
    require(observe(message.bytes.data(), message.size) == defaultRhythmParts, "GS reset");
    const uint8_t gm[] = {0xf0, 0x7e, 0x7f, 9, 1, 0xf7};
    require(observe(gm, sizeof(gm)) == defaultRhythmParts, "GM reset");
    std::cout << "PASS: existing part roles preserved, relocated drums, melodic channel 10 and reset observation\n";
}
