#include "Scene.h"
#include <thread>
#include <iostream>
#include <cstring>
#include <array>

int main() {
    scbridge::EngineScene original;
    for(auto& part:original.legacy) for(unsigned i=1;i<15;++i) part[i-1]=static_cast<uint8_t>(scbridge::controls[i].initial);
    for(auto& part:original.parts) for(unsigned i=16;i<24;++i) part[i-16]=static_cast<uint8_t>(scbridge::controls[i].initial);
    for(unsigned i=24;i<58;++i) original.globals[i-24]=static_cast<uint8_t>(scbridge::controls[i].initial);
    if(!scbridge::validScene(original)) return 8;
    if(scbridge::encodedSceneVersion(original)!=2 || scbridge::encodedSceneSize(original)!=1952) return 9;
    std::array<uint8_t,1968> version3{};
    std::memcpy(version3.data(),&original,1952);
    version3[1952+2]=2;
    scbridge::EngineScene restored;
    if(!scbridge::decodeScene(3,version3.data(),version3.size(),restored)) {
        std::cerr<<"FAIL: version 3 must retain custom rhythm-part assignments\n";return 3;
    }
    if(sizeof(restored)!=version3.size() || std::memcmp(&restored,version3.data(),version3.size())) return 4;
    if(scbridge::encodedSceneVersion(restored)!=3 || scbridge::encodedSceneSize(restored)!=1968) return 10;
    if(!scbridge::decodeScene(2,&original,scbridge::version2SceneSize,restored) ||
       restored.partModes[9]!=1 || restored.partModes[2]!=0) return 5;
    if(!scbridge::decodeScene(1,&original,scbridge::legacySceneSize,restored) || restored.partModes[9]!=1) return 6;
    version3[1952]=3;
    if(scbridge::decodeScene(3,version3.data(),version3.size(),restored)) return 7;
    scbridge::StateMailbox<scbridge::StateFrame> mailbox;
    std::atomic<bool> done{false},bad{false};std::atomic<unsigned> reads{0};
    auto reader=[&] {
        uint32_t previous=0;
        while(!done.load()) {
            scbridge::StateFrame frame;
            if(!mailbox.read(frame)) { std::this_thread::yield();continue; }
            const auto byte=static_cast<uint8_t>(frame.epoch%128);
            if(frame.epoch<previous) bad.store(true);
            if(frame.factoryRequest!=frame.epoch) bad.store(true);
            previous=frame.epoch;
            const auto* scene=reinterpret_cast<const uint8_t*>(&frame.scene);
            for(size_t i=0;i<sizeof(frame.scene);++i) if(scene[i]!=byte) bad.store(true);
            for(unsigned i=0;i<scbridge::parameterCount;++i)
                if(frame.generations[i]!=frame.epoch || frame.values[i]!=static_cast<int32_t>(frame.epoch)) bad.store(true);
            ++reads;
        }
    };
    std::thread first(reader),second(reader);
    for(uint32_t epoch=1;epoch<=100000;++epoch) {
        scbridge::StateFrame frame;frame.epoch=epoch;
        frame.factoryRequest=epoch;
        std::memset(&frame.scene,epoch%128,sizeof(frame.scene));
        for(unsigned i=0;i<scbridge::parameterCount;++i) { frame.generations[i]=epoch;frame.values[i]=epoch; }
        while(!mailbox.publish(frame)) std::this_thread::yield();
    }
    done.store(true);first.join();second.join();
    scbridge::StateFrame last;
    if(bad.load() || !reads.load() || !mailbox.read(last) || last.epoch!=100000) return 1;
    mailbox.clear();if(mailbox.read(last)) return 2;
    std::cout<<"PASS 100000 scene publications, "<<reads.load()<<" concurrent reads; no torn or regressing snapshots\n";
}
