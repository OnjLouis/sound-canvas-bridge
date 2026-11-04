#pragma once
#include "Protocol.h"
#include <array>
#include <algorithm>

namespace scbridge {
// A readback model only: observing MIDI must never change the engine's input.
class ControllerMirror {
public:
    ControllerMirror() { reset(); }
    void reset(bool preserveDefaultMaps=false) {
        for (int part=0;part<16;++part) {
            if(!preserveDefaultMaps) selectedMap[part]=4;
            values[part]={part,static_cast<int32_t>(selectedMap[part]),0,0,100,64,127,0,40,0,0,64,64,64,64,0};
            bank[part]=0; map[part]=0;
            nrpnMsb[part]=nrpnLsb[part]=127;
            receive[part]={};
            partModes[part]=part==9 ? 1 : 0;
        }
    }
    void observeShort(unsigned packed) {
        const unsigned status=packed&0xff, command=status&0xf0, part=status&15;
        const unsigned first=(packed>>8)&127, second=(packed>>16)&127;
        if (command==0xc0) {
            if(!receive[part].program) return;
            values[part][1]=map[part] ? map[part] : selectedMap[part]; values[part][2]=bank[part]; values[part][3]=first;
            return;
        }
        if (command!=0xb0) return;
        auto& rx=receive[part];
        if(first<120 && !rx.controllers) return;
        if (first==0) { if(rx.bank) bank[part]=second; return; }
        if (first==32) { if(rx.bank && second<=4) map[part]=rx.bankLsb ? second : 0; return; }
        if (first==99) { if(rx.nrpn) nrpnMsb[part]=second; return; }
        if (first==98) { if(rx.nrpn) nrpnLsb[part]=second; return; }
        if (first==100 || first==101) { if(rx.rpn) nrpnMsb[part]=nrpnLsb[part]=127; return; }
        if (first==6 && rx.nrpn && nrpnMsb[part]==1) {
            const unsigned codes[]={0x20,0x21,0x63,0x66};
            for (unsigned i=0;i<4;++i) if (nrpnLsb[part]==codes[i]) values[part][11+i]=second;
            return;
        }
        if ((first==1 && !rx.modulation) || (first==7 && !rx.volume) ||
            (first==10 && !rx.pan) || (first==11 && !rx.expression)) return;
        const unsigned codes[]={7,10,11,1,91,93,94,74,71,73,72};
        for (unsigned i=0;i<11;++i) if(first==codes[i]) { values[part][4+i]=second; return; }
        if (first==121) {
            values[part][6]=127; values[part][7]=0;
            nrpnMsb[part]=nrpnLsb[part]=127;
        }
    }
    void snapshot(unsigned part,const int32_t* commands,int32_t* output) const {
        std::copy(values[part].begin(),values[part].end(),output);
        output[0]=commands[0]; output[15]=commands[15];
    }
    void capture(EngineScene& scene) const {
        for(unsigned part=0;part<16;++part) for(unsigned i=1;i<15;++i)
            scene.legacy[part][i-1]=static_cast<uint8_t>(values[part][i]);
        std::copy(partModes.begin(),partModes.end(),scene.partModes);
        std::copy(selectedMap.begin(),selectedMap.end(),scene.defaultMaps);
    }
    void observeLong(const uint8_t* data,uint32_t size) {
        if (size==6 && data[0]==0xf0 && data[1]==0x7e && (data[2]==0x7f || data[2]==0x10) && data[3]==9 &&
            (data[4]==1 || data[4]==3) && data[5]==0xf7) {
            reset(true);
            for(auto& rx:receive) { rx.bank=data[4]==3; rx.nrpn=false; }
            return;
        }
        if (size<11 || data[0]!=0xf0 || data[1]!=0x41 || data[2]!=0x10 ||
            data[3]!=0x42 || data[4]!=0x12 || data[size-1]!=0xf7) return;
        unsigned checksum=0;
        for (unsigned i=5;i<size-1;++i) {
            if (data[i]>127) return;
            checksum+=data[i];
        }
        if (checksum%128) return;
        unsigned address=(unsigned(data[5])<<14)|(unsigned(data[6])<<7)|data[7];
        for (unsigned i=8;i<size-2;++i,++address) {
            const auto a=(address>>14)&127,b=(address>>7)&127,c=address&127;
            if (a!=0x40) continue;
            if (b==0 && c==0x7f && data[i]==0) { reset(true); continue; }
            const auto block=b&15;
            const auto part=block==0 ? 9 : block<10 ? block-1 : block;
            if ((b&0x70)==0x10) {
                if(c==0x15 && data[i]<=2) partModes[part]=data[i];
                if(data[i]<=1) {
                    auto& rx=receive[part];const bool enabled=data[i]!=0;
                    switch(c) {
                        case 5: rx.program=enabled;break;
                        case 7: rx.controllers=enabled;break;
                        case 9: rx.rpn=enabled;break;
                        case 10: rx.nrpn=enabled;break;
                        case 11: rx.modulation=enabled;break;
                        case 12: rx.volume=enabled;break;
                        case 13: rx.pan=enabled;break;
                        case 14: rx.expression=enabled;break;
                        case 0x23: rx.bank=enabled;break;
                        case 0x24: rx.bankLsb=enabled;break;
                        default: break;
                    }
                }
                if (c==0) values[part][2]=bank[part]=data[i];
                if (c==1) values[part][3]=data[i];
                const unsigned offsets[]={0x19,0x1c,0x21,0x22,0x2c,0x32,0x33,0x34,0x36};
                const unsigned indices[]={4,5,9,8,10,11,12,13,14};
                for(unsigned j=0;j<9;++j) if(c==offsets[j]) values[part][indices[j]]=data[i];
            }
            if ((b&0x70)==0x40 && c==0 && data[i]<=4) {
                map[part]=data[i];
                values[part][1]=map[part] ? map[part] : selectedMap[part];
            }
            if ((b&0x70)==0x40 && c==1 && data[i]>=1 && data[i]<=4) {
                selectedMap[part]=data[i];
                if(map[part]==0) values[part][1]=selectedMap[part];
            }
        }
    }
private:
    struct Receive {
        bool program=true,controllers=true,rpn=true,nrpn=true,modulation=true,
             volume=true,pan=true,expression=true,bank=true,bankLsb=true;
    };
    std::array<Receive,16> receive{};
    std::array<std::array<int32_t,legacyParameterCount>,16> values{};
    std::array<unsigned,16> bank{},map{},nrpnMsb{},nrpnLsb{};
    std::array<unsigned,16> selectedMap{};
    std::array<uint8_t,16> partModes{};
};
}
