#include "boxedwine.h"
#include "pixelformat.h"

#include <cstring>
#include <cstdlib>
#include "WorkerScheduling.h"
#include <AudioToolbox/AudioWorkInterval.h>
#include <os/workgroup.h>

namespace {
constexpr U32 audioHypercall = 0x53434155;
struct AudioThread {
    os_workgroup_interval_t group=nullptr;
    os_workgroup_join_token_s token{};
    bool realtime=false,joined=false,started=false;
    mach_timebase_info_data_t timebase{};
    ~AudioThread() {
        if(started) (void)os_workgroup_interval_finish(group,nullptr);
        if(joined) os_workgroup_leave(group,&token);
        if(group) os_release(group);
    }
};
thread_local AudioThread audio;
}

void callOpenGL(CPU* cpu, U32 operation) {
    if(operation!=audioHypercall) return;
    const U32 command=cpu->reg[2].u32;
    const double rate=cpu->reg[3].u32;
    const U32 frames=cpu->reg[1].u32;
    cpu->reg[0].u32=0;
    if(command==0) {
        const char* mode=std::getenv("SCBRIDGE_EXPERIMENT_RT");
        if(mode && !std::strcmp(mode,"0")) return;
        if(rate<8000 || rate>192000 || frames>4096) return;
        audio.realtime=scbridge::configureAudioWorker(rate,128);
        if(!audio.group && audio.realtime) {
            audio.group=AudioWorkIntervalCreate("Sound Canvas emulated synthesis",OS_CLOCK_MACH_ABSOLUTE_TIME,nullptr);
            if(audio.group) audio.joined=os_workgroup_join(audio.group,&audio.token)==0;
            mach_timebase_info(&audio.timebase);
        }
        cpu->reg[0].u32=audio.realtime && audio.joined;
    } else if(command==1 && audio.joined && !audio.started && rate>=8000 && frames>0 && frames<=4096) {
        const auto start=mach_absolute_time();
        const auto duration=static_cast<uint64_t>((frames/rate)*1e9*audio.timebase.denom/audio.timebase.numer);
        audio.started=os_workgroup_interval_start(audio.group,start,start+duration,nullptr)==0;
        cpu->reg[0].u32=audio.started;
    } else if(command==2 && audio.started) {
        const int result=os_workgroup_interval_finish(audio.group,nullptr);
        audio.started=false;
        cpu->reg[0].u32=result==0;
    }
}

int getPixelFormats(PixelFormat *formats, int maximum) {
    if (!formats || maximum < 3) return 0;
    std::memset(formats, 0, sizeof(PixelFormat) * maximum);
    formats[1].nSize = sizeof(PixelFormat);
    formats[1].nVersion = 1;
    formats[1].dwFlags = K_PFD_SUPPORT_OPENGL | K_PFD_DRAW_TO_WINDOW | K_PFD_DOUBLEBUFFER;
    formats[1].iPixelType = K_PFD_TYPE_RGBA;
    formats[1].cColorBits = 32;
    formats[1].cRedBits = 8;
    formats[1].cGreenBits = 8;
    formats[1].cBlueBits = 8;
    formats[1].cDepthBits = 24;
    formats[1].cStencilBits = 8;
    formats[2] = formats[1];
    formats[1].dwFlags |= K_PFD_GENERIC_FORMAT;
    return 2;
}

extern "C" {
void MacPlatormSetThreadPriority() {}
void MacPlatformOpenFileLocation(const char *) {}
const char *MacPlatformGetResourcePath(const char *path) { return path; }
}
