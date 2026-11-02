#include <algorithm>
#include <cstdint>
namespace { bool sounding=false; }
#define EXPORT extern "C" __declspec(dllexport)
EXPORT int TG_initialize(int) { sounding=false;return 0; }
EXPORT void TG_activate(float,int) {}
EXPORT int TG_setSampleRate(float) { return 0; }
EXPORT int TG_setMaxBlockSize(unsigned) { return 0; }
EXPORT void TG_setInterruptThreadIdAtThisTime() {}
EXPORT void TG_ShortMidiIn(unsigned packed,unsigned) {
    if((packed&0xf0)==0x90) sounding=((packed>>16)&127)!=0;
    if((packed&0xf0)==0x80 || ((packed&0xf0)==0xb0 && ((packed>>8)&127)==120)) sounding=false;
}
EXPORT void TG_LongMidiIn(const unsigned char*,unsigned) { sounding=false; }
EXPORT void TG_Process(float* left,float* right,unsigned frames) {
    std::fill_n(left,frames,sounding ? 0.25f : 0.f);std::fill_n(right,frames,sounding ? -0.25f : 0.f);
}
EXPORT void TG_deactivate() {}
EXPORT void TG_terminate() {}
