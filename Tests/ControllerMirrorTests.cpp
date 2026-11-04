#include "ControllerMirror.h"
#include "StandaloneControls.h"
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
int main() {
    scbridge::ControllerMirror mirror;
    for(int map=1;map<=4;++map) {
        scbridge::ControllerMirror all;
        for(unsigned part=0;part<16;++part) {
            all.observeShort((0xb0|part)|(0<<8)|((part+2)<<16));
            all.observeShort((0xc0|part)|((part+7)<<8));
        }
        scbridge::EngineScene before;all.capture(before);
        const auto messages=scbridge::allPartMapSelection(map,before);
        require(messages.size()==32,"All-parts map must include native tone reselection");
        for(const auto& message:messages) all.observeLong(message.bytes.data(),message.size);
        for(unsigned part=0;part<16;++part) {
            int32_t pending[scbridge::parameterCount]{},actual[scbridge::parameterCount]{};
            pending[0]=part;all.snapshot(part,pending,actual);
            require(actual[1]==map && actual[2]==int(part+2) && actual[3]==int(part+7),
                "All-parts map must cover each GS part block without changing bank/program");
        }
        const uint8_t reset[]{0xf0,0x41,0x10,0x42,0x12,0x40,0,0x7f,0,0x41,0xf7};
        all.observeLong(reset,sizeof(reset));
        for(unsigned part=0;part<16;++part) {
            int32_t pending[scbridge::parameterCount]{},actual[scbridge::parameterCount]{};
            all.snapshot(part,pending,actual);
            require(actual[1]==map,"GS reset must preserve the selected default map on every part");
            all.observeShort((0xb0|part)|(32<<8)|(4<<16));
            all.observeShort((0xc0|part)|(5<<8));
            all.snapshot(part,pending,actual);
            require(actual[1]==4,"Explicit MIDI maps must override the default map");
            all.observeShort((0xb0|part)|(32<<8));
            all.observeShort((0xc0|part)|(5<<8));
            all.snapshot(part,pending,actual);
            require(actual[1]==map,"Bank LSB zero must return to the selected default map");
        }
    }
    for(const auto& message:scbridge::allPartMapMessages(0)) require(message.size==0,"Invalid map must not generate MIDI");
    for(const auto& message:scbridge::allPartMapSelection(5,{})) require(message.size==0,"Invalid map must not reselect tones");
    int32_t commands[scbridge::parameterCount]{},values[scbridge::parameterCount]{};
    auto cc=[&](unsigned part,unsigned controller,unsigned value) {
        mirror.observeShort((0xb0|part)|(controller<<8)|(value<<16));
    };
    auto read=[&](unsigned part) { commands[0]=part; mirror.snapshot(part,commands,values); };
    cc(0,1,93); cc(0,11,47); cc(0,7,82); cc(0,10,23);
    cc(0,91,55);cc(0,93,66);cc(0,94,77);cc(0,74,91);cc(0,71,81);cc(0,73,71);cc(0,72,61);
    read(0);
    const int expected[]={82,23,47,93,55,66,77,91,81,71,61};
    for(unsigned i=0;i<11;++i) require(values[i+4]==expected[i],"Controller readback mismatch");
    cc(1,1,12);read(0);require(values[7]==93,"Another MIDI part changed the selected readback");
    read(1);require(values[7]==12,"Independent part was not tracked");
    cc(0,0,8);cc(0,32,3);read(0);
    require(values[1]==4 && values[2]==0,"Bank selection committed before program change");
    mirror.observeShort(0xc0|(5<<8));read(0);
    require(values[1]==3 && values[2]==8 && values[3]==5,"Program/bank commit mismatch");
    cc(0,99,1);cc(0,98,0x20);cc(0,6,102);read(0);
    require(values[11]==102,"Tone NRPN was not tracked");
    cc(0,101,127);cc(0,6,3);read(0);
    require(values[11]==102,"RPN selection leaked into tone NRPN");
    cc(0,121,0);read(0);
    require(values[6]==127 && values[7]==0 && values[4]==82 && values[5]==23,"Controller reset mismatch");
    auto sysex=[&](unsigned b,unsigned c,unsigned value,bool valid=true) {
        std::vector<uint8_t> message{0xf0,0x41,0x10,0x42,0x12,0x40,uint8_t(b),uint8_t(c),uint8_t(value),0,0xf7};
        message[9]=uint8_t((128-((0x40+b+c+value)%128))%128);
        if(!valid) message[9]^=1;
        mirror.observeLong(message.data(),message.size());
    };
    sysex(0x11,0x19,75);read(0);require(values[4]==75,"GS part1 block mismatch");
    sysex(0x10,0x19,35);read(9);require(values[4]==35,"GS drum-part block mismatch");
    sysex(0x1a,0x19,15);read(10);require(values[4]==15,"GS part11 block mismatch");
    sysex(0x1a,0x19,99,false);read(10);require(values[4]==15,"Invalid GS checksum was mirrored");
    sysex(0x11,0x0b,0);cc(0,1,81);read(0);require(values[7]==0,"Disabled modulation reception was mirrored");
    sysex(0x11,0x0b,1);cc(0,1,81);read(0);require(values[7]==81,"Enabled modulation reception was not mirrored");
    sysex(0,0x7f,0);read(0);require(values[4]==100 && values[6]==127 && values[7]==0,"GS reset was not reflected");
    std::cout<<"All supported CCs, independent parts, bank commit, NRPN, reset and GS checksum/block readbacks pass\n";
}
