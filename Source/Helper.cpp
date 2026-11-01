#include "Protocol.h"
#include "WorkerScheduling.h"
#include "ControllerMirror.h"
#include "CoreState.h"
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <pthread.h>

template <typename T> T function(void* library, const char* name) {
    auto result = reinterpret_cast<T>(dlsym(library, name));
    if (!result) throw std::runtime_error(name);
    return result;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    using namespace scbridge;
    auto* shared = static_cast<Shared*>(mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, 4, 0));
    if (shared == MAP_FAILED) return 3;
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) return 4;
    try {
        auto initialize = function<int (*)(int)>(library, "TG_initialize");
        auto activate = function<void (*)(float, int)>(library, "TG_activate");
        auto setRate = function<int (*)(float)>(library, "TG_setSampleRate");
        auto setBlock = function<int (*)(unsigned)>(library, "TG_setMaxBlockSize");
        auto thread = function<void (*)()>(library, "TG_setInterruptThreadIdAtThisTime");
        auto shortMidi = function<void (*)(unsigned, unsigned)>(library, "TG_ShortMidiIn");
        auto longMidi = function<void (*)(const unsigned char*, unsigned)>(library, "TG_LongMidiIn");
        auto render = function<void (*)(float*, float*, unsigned)>(library, "TG_Process");
        auto deactivate = function<void (*)()>(library, "TG_deactivate");
        auto terminate = function<void (*)()>(library, "TG_terminate");
        if (initialize(0) < 0) return 5;
        ControllerMirror mirror;
        CoreState coreState;
        bool advancedReady=false;
        auto send = [&](int status, int a, int b) {
            const auto packed=status | (a << 8) | (b << 16);
            shortMidi(packed, 0); mirror.observeShort(packed);
        };
        std::array<int32_t, parameterCount> previous; previous.fill(-1);
        std::array<uint32_t, parameterCount> previousGenerations; previousGenerations.fill(UINT32_MAX);
        const unsigned char gsReset[] = {0xf0,0x41,0x10,0x42,0x12,0x40,0,0x7f,0,0x41,0xf7};
        bool active = false;
        char reply = 'R';
        if (sendto(3, &reply, 1, 0, nullptr, 0) != 1) return 6;
        char command;
        while (read(3, &command, 1) == 1 && command != 'Q') {
            __sync_synchronize();
            shared->result = 1;
            if (shared->version != protocolVersion || shared->frames > maxFrames ||
                shared->eventCount > maxEvents || shared->midiBytes > maxMidiBytes) return 7;
            for (uint32_t i = 0; i < shared->eventCount; ++i) {
                const auto& event = shared->events[i];
                if (!event.size || event.size > shared->midiBytes || event.position > shared->midiBytes - event.size ||
                    event.offset >= shared->frames) return 8;
            }
            if (command == 'S') {
                if (active) deactivate();
                activate(static_cast<float>(shared->sampleRate), 1024);
                setRate(static_cast<float>(shared->sampleRate)); setBlock(maxFrames); thread(); active = true;
                shared->helperRealtime = configureAudioWorker(shared->sampleRate, 128) ? 1 : 0;
            } else if (command != 'P' || !active) return 9;
            if (shared->reset) {
                longMidi(gsReset, 0); previous.fill(-1); previousGenerations.fill(UINT32_MAX); mirror.reset();
                for (int warm = 0; warm < 32; ++warm) render(shared->left, shared->right, 512);
                advancedReady=coreState.attach(argv[1],library);
            }
            shared->advancedReady=advancedReady ? 1 : 0;
            const auto* p = shared->parameters;
            for (unsigned i = 0; i < parameterCount; ++i)
                if (p[i] < controls[i].minimum || p[i] > controls[i].maximum) return 10;
            int part = p[0];
            auto changed=[&](unsigned index) { return shared->generations[index]!=previousGenerations[index]; };
            if(shared->reset && shared->restoreSceneValid && advancedReady) {
                const auto& scene=shared->restoredScene;
                if(!validScene(scene)) return 14;
                std::array<std::array<uint8_t,11>,64> messages{};
                unsigned messageCount=0;
                auto dt1=[&](unsigned address,int value) {
                    auto& data=messages[messageCount++];
                    data={0xf0,0x41,0x10,0x42,0x12,static_cast<uint8_t>(address>>16),
                        static_cast<uint8_t>((address>>8)&127),static_cast<uint8_t>(address&127),
                        static_cast<uint8_t>(value),0,0xf7};
                    data[9]=(128-((data[5]+data[6]+data[7]+data[8])&127))&127;
                    longMidi(data.data(),0);
                    if(messageCount==messages.size()) { render(shared->left,shared->right,32);messageCount=0; }
                };
                const int controllers[]={7,10,11,1,91,93,94};
                const int toneNrpn[]={0x20,0x21,0x63,0x66};
                for(int channel=0;channel<16;++channel) {
                    const auto* saved=scene.legacy[channel];
                    send(0xb0|channel,0,saved[1]);send(0xb0|channel,32,saved[0]);send(0xc0|channel,saved[2],0);
                    for(unsigned i=4;i<11;++i) send(0xb0|channel,controllers[i-4],saved[i-1]);
                    for(unsigned i=11;i<15;++i) {
                        send(0xb0|channel,99,1);send(0xb0|channel,98,toneNrpn[i-11]);send(0xb0|channel,6,saved[i-1]);
                    }
                    send(0xb0|channel,99,127);send(0xb0|channel,98,127);
                    render(shared->left,shared->right,128);
                }
                auto restore=[&](unsigned index,int channel,int note,int setup,int value) {
                    dt1(controlAddress(index,channel,note,setup),value);
                };
                for(unsigned i=24;i<58;++i) restore(i,0,36,0,scene.globals[i-24]);
                for(int channel=0;channel<16;++channel) for(unsigned i=16;i<24;++i)
                    restore(i,channel,36,0,scene.parts[channel][i-16]);
                for(int setup=0;setup<2;++setup) for(int field=0;field<6;++field) for(int note=0;note<128;++note)
                    restore(field+60,0,note,setup,scene.drums[setup][field][note]);
                render(shared->left,shared->right,512);
                // Avoid resetting a restored kit by redundantly sending its PC.
                // A genuinely newer pending edit still wins over the snapshot.
                int32_t actual[parameterCount]{};mirror.snapshot(part,p,actual);
                for(unsigned i=legacyParameterCount;i<parameterCount;++i)
                    actual[i]=coreState.read(i,part,p[drumNoteIndex],p[drumSetupIndex]);
                for(unsigned i=0;i<parameterCount;++i) {
                    previous[i]=p[i];
                    if(actual[i]==p[i]) previousGenerations[i]=shared->generations[i];
                }
            }
            bool changedPart = changed(0);
            if(changedPart && p[15] && previous[0]>=0) send(0xb0|previous[0],120,0);
            if (changed(1) || changed(2) || changed(3)) {
                if (previous[0] >= 0) send(0xb0 | previous[0], 120, 0);
                send(0xb0 | part, 0, p[2]); send(0xb0 | part, 32, p[1]); send(0xc0 | part, p[3], 0);
            }
            const int controllers[] = {7,10,11,1,91,93,94};
            for (unsigned i = 4; i < 11; ++i)
                if (changed(i)) send(0xb0 | part, controllers[i - 4], p[i]);
            const int toneNrpn[] = {0x20, 0x21, 0x63, 0x66};
            for (unsigned i = 11; i < 15; ++i) {
                if (!changed(i)) continue;
                send(0xb0 | part, 99, 1); send(0xb0 | part, 98, toneNrpn[i - 11]);
                send(0xb0 | part, 6, p[i]);
                send(0xb0 | part, 99, 127); send(0xb0 | part, 98, 127);
            }
            std::array<std::array<uint8_t,11>,parameterCount> advancedMessages{};
            bool advancedCommandsPending=false;
            if(advancedReady) for(unsigned i=legacyParameterCount;i<parameterCount;++i) {
                if(!shared->generations[i] || !changed(i) || controls[i].scope==ControlScope::Selector) continue;
                const auto address=controlAddress(i,part,p[drumNoteIndex],p[drumSetupIndex]);
                auto& data=advancedMessages[i];
                data={0xf0,0x41,0x10,0x42,0x12,static_cast<uint8_t>(address>>16),
                    static_cast<uint8_t>((address>>8)&127),static_cast<uint8_t>(address&127),
                    static_cast<uint8_t>(p[i]),0,0xf7};
                data[9]=(128-((data[5]+data[6]+data[7]+data[8])&127))&127;
                longMidi(data.data(),0);
                advancedCommandsPending=true;
            }
            std::copy(p, p + parameterCount, previous.begin());
            std::copy(std::begin(shared->generations),std::end(shared->generations),previousGenerations.begin());
            if (command == 'P') {
                uint32_t rendered = 0;
                for (uint32_t i = 0; i < shared->eventCount; ++i) {
                    const auto& event = shared->events[i];
                    if (event.offset < rendered) return 8;
                    if (event.offset > rendered) {
                        render(shared->left + rendered, shared->right + rendered, event.offset - rendered);
                        rendered = event.offset;
                    }
                    const auto* data = shared->midi + event.position;
                    if (data[0] == 0xf0) {
                        if (event.size < 2 || data[event.size - 1] != 0xf7) return 11;
                        longMidi(data, 0);
                        mirror.observeLong(data,event.size);
                    } else if (event.size <= 3 && data[0] >= 0x80) {
                        unsigned status = data[0];
                        if (p[15] && status < 0xf0) status = (status & 0xf0) | part;
                        unsigned packed = status | ((event.size > 1 ? data[1] : 0) << 8) |
                                          ((event.size > 2 ? data[2] : 0) << 16);
                        shortMidi(packed, 0);
                        mirror.observeShort(packed);
                    } else return 12;
                }
                if (rendered < shared->frames)
                    render(shared->left + rendered, shared->right + rendered, shared->frames - rendered);
            } else if(advancedCommandsPending) render(shared->left,shared->right,512);
            mirror.snapshot(part,p,shared->reported);
            for(unsigned i=legacyParameterCount;i<parameterCount;++i) {
                const int actual=advancedReady ? coreState.read(i,part,p[drumNoteIndex],p[drumSetupIndex]) : -1;
                shared->reported[i]=actual>=0 ? actual : p[i];
            }
            mirror.capture(shared->capturedScene);
            shared->capturedSceneValid=advancedReady && coreState.capture(shared->capturedScene) && validScene(shared->capturedScene) ? 1 : 0;
            shared->result = 0;
            __sync_synchronize();
            reply = 'D';
            if (write(3, &reply, 1) != 1) break;
        }
        if (active) deactivate();
        terminate();
    } catch (...) { return 13; }
    dlclose(library); munmap(shared, sizeof(Shared)); close(3); close(4);
    return 0;
}
