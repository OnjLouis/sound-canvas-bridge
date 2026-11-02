#include "WindowsIpc.h"
#include <iostream>
#include <stdexcept>
static void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
static void initialise(scbridge::Shared& shared) {
    shared.version=scbridge::protocolVersion;shared.sampleRate=48000;shared.reset=1;
    for(unsigned i=0;i<scbridge::parameterCount;++i) {
        shared.parameters[i]=scbridge::controls[i].initial;
        shared.generations[i]=i<scbridge::legacyParameterCount ? 1 : 0;
    }
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3) return 2;
        scbridge::WindowsIpc first,second;
        require(first.start(argv[1],argv[2]),"First helper did not start");
        require(second.start(argv[1],argv[2]),"Second helper did not start");
        initialise(*first.shared());initialise(*second.shared());
        require(first.exchange('S',5000) && second.exchange('S',5000),"Initialisation handshake failed");
        auto& state=*first.shared();state.frames=128;state.reset=0;
        state.eventCount=1;state.midiBytes=3;state.events[0]={16,3,0};
        state.midi[0]=0x90;state.midi[1]=60;state.midi[2]=100;
        require(first.exchange('P',1000),"Note render failed");
        for(unsigned i=0;i<128;++i) require(state.left[i]==(i<16 ? 0.f : 0.25f) && state.right[i]==(i<16 ? 0.f : -0.25f),"MIDI sample timing changed");
        second.shared()->frames=128;second.shared()->reset=0;
        require(second.exchange('P',1000),"Second render failed");
        for(auto sample:second.shared()->left) require(sample==0.f,"Independent helper shared a voice");
        state.events[0]={0,3,0};state.midi[0]=0xb0;state.midi[1]=1;state.midi[2]=93;
        require(first.exchange('P',1000) && state.reported[7]==93,"Controller readback lost");
        require(state.advancedReady==0,"Unknown mock engine was accepted as a vendor layout");
        state.frames=scbridge::maxFrames+1;
        require(!first.exchange('P',1000),"Invalid request did not fail closed");
        first.close();second.close();
        std::cout<<"Independent helpers, exact MIDI offsets, controller feedback, invalid request and cleanup passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
