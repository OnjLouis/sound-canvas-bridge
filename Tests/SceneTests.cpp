#include "Scene.h"
#include <thread>
#include <iostream>
#include <cstring>

int main() {
    scbridge::StateMailbox<scbridge::StateFrame> mailbox;
    std::atomic<bool> done{false},bad{false};std::atomic<unsigned> reads{0};
    auto reader=[&] {
        uint32_t previous=0;
        while(!done.load()) {
            scbridge::StateFrame frame;
            if(!mailbox.read(frame)) { std::this_thread::yield();continue; }
            const auto byte=static_cast<uint8_t>(frame.epoch%128);
            if(frame.epoch<previous) bad.store(true);
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
