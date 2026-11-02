#include "Controls.h"
#include "ControlMessage.h"
#include "Scene.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    const char* required[]={"mfxType","mfxParameter","mfxValue","mfxToReverb","mfxToChorus","mfxToDelay","mfxEq"};
    for(auto* id:required) {
        bool found=false;
        for(const auto& control:scbridge::controls) if(!std::strcmp(control.id,id)) found=true;
        if(!found) { std::printf("FAIL insertion control missing: %s\n",id);return 1; }
    }
    static_assert(scbridge::parameterCount==93 && scbridge::insertionTypeCount==65);
    assert(!std::strcmp(scbridge::controls[0].id,"part"));
    assert(!std::strcmp(scbridge::controls[58].id,"drumNote"));
    assert(!std::strcmp(scbridge::controls[65].id,"drumDelay"));
    auto validMessage=[](const scbridge::ControlMessage& message,unsigned size) {
        assert(message.size==size && message.bytes[0]==0xf0 && message.bytes[size-1]==0xf7);
        unsigned sum=0;for(unsigned i=5;i<size-1;++i) sum+=message.bytes[i];
        assert((sum&127)==0);
    };
    for(unsigned type=0;type<scbridge::insertionTypeCount;++type) {
        const auto& definition=scbridge::insertionTypes[type];
        assert(scbridge::insertionTypeIndexForCode(definition.msb,definition.lsb)==static_cast<int>(type));
        auto message=scbridge::controlMessage(scbridge::insertionTypeIndex,type,0,36,0);
        validMessage(message,12);
        assert(message.bytes[5]==0x40 && message.bytes[6]==3 && message.bytes[7]==0);
        assert(message.bytes[8]==definition.msb && message.bytes[9]==definition.lsb);
        for(unsigned other=type+1;other<scbridge::insertionTypeCount;++other)
            assert(definition.msb!=scbridge::insertionTypes[other].msb || definition.lsb!=scbridge::insertionTypes[other].lsb);
    }
    assert(scbridge::insertionTypeIndexForCode(0x7f,0x7f)==-1);
    for(unsigned slot=1;slot<=scbridge::insertionParameterCount;++slot) for(int value:{0,64,127}) {
        auto selected=scbridge::controlMessage(scbridge::insertionValueIndex,value,0,36,0,slot);
        auto direct=scbridge::controlMessage(scbridge::insertionFirstDirectIndex+slot-1,value,0,36,0);
        validMessage(selected,11);validMessage(direct,11);
        assert(selected.bytes==direct.bytes);
        assert(selected.bytes[7]==slot+2 && selected.bytes[8]==value);
    }
    assert(!scbridge::controlMessage(scbridge::insertionSelectorIndex,2,0,36,0).size);
    assert(!scbridge::controlMessage(scbridge::insertionValueIndex,64,0,36,0,0).size);
    assert(!scbridge::controlMessage(scbridge::insertionValueIndex,64,0,36,0,21).size);
    assert(!scbridge::controlMessage(scbridge::insertionTypeIndex,65,0,36,0).size);
    scbridge::EngineScene initial;
    for(unsigned part=0;part<16;++part) {
        for(unsigned i=1;i<15;++i) initial.legacy[part][i-1]=scbridge::controls[i].initial;
        for(unsigned i=16;i<24;++i) initial.parts[part][i-16]=scbridge::controls[i].initial;
    }
    for(unsigned i=24;i<58;++i) initial.globals[i-24]=scbridge::controls[i].initial;
    assert(scbridge::validScene(initial));
    initial.insertion.valid=1;initial.insertion.type=6;initial.insertion.parameters[0]=110;
    scbridge::EngineScene restored;
    assert(scbridge::decodeScene(1,&initial,scbridge::legacySceneSize,restored));
    assert(!restored.insertion.valid && !std::memcmp(&initial,&restored,scbridge::legacySceneSize));
    assert(scbridge::decodeScene(scbridge::sceneVersion,&initial,sizeof(initial),restored));
    assert(!std::memcmp(&initial,&restored,sizeof(initial)));
    assert(!scbridge::decodeScene(1,&initial,sizeof(initial),restored));
    assert(!scbridge::decodeScene(scbridge::sceneVersion,&initial,scbridge::legacySceneSize,restored));
    assert(!scbridge::decodeScene(3,&initial,sizeof(initial),restored));
    assert(!scbridge::decodeScene(1,nullptr,scbridge::legacySceneSize,restored));
    initial.insertion.eq=2;
    assert(!scbridge::decodeScene(scbridge::sceneVersion,&initial,sizeof(initial),restored));
    std::puts("PASS 65 type mappings, 20 selected/direct controls, DT1 checksums, ranges and old/new scene migration");
}
