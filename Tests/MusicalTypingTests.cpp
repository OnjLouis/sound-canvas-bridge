#include "MusicalTyping.h"
#include <array>
#include <iostream>
#include <vector>

int main() {
    using namespace scbridge;
    using Event=std::array<int,3>;
    std::vector<Event> events;
    auto send=[&](int channel,int pitch,int velocity) { events.push_back({channel,pitch,velocity}); };
    auto check=[](bool ok,const char* message) { if(!ok) std::cerr<<"FAIL: "<<message<<'\n';return ok; };
    MusicalTyping keys;
    const int a=keys.keyIndex('A'),w=keys.keyIndex('w'),k=keys.keyIndex('k');
    bool passed=check(a==0 && w==1 && k==12 && keys.keyIndex('q')==-1,"White and sharp key layout");
    keys.press(a,a,2,send);keys.press(a,a,2,send);keys.press(w,w,2,send);
    keys.shiftOctave(1);keys.shiftVelocity(1);keys.release(w,send);keys.releaseAll(send);
    passed &= check(events==std::vector<Event>{{2,60,88},{2,61,88},{2,61,0},{2,60,0}},
                    "Chords and repeat suppression; release the original pitch and channel");
    events.clear();keys.start(false);
    keys.press(k,k,1,send);keys.shiftOctave(1);keys.press(a,a,1,send);
    keys.release(k,send);keys.release(a,send);
    passed &= check(events==std::vector<Event>{{1,72,96},{1,72,0}},"Overlapping pitches must not release early");
    events.clear();keys.start(true);
    keys.press(a,a,10,send);keys.press(MusicalTyping::mouseKey,a,10,send);
    keys.releaseAll(send);
    passed &= check(events==std::vector<Event>{{10,36,96},{10,36,0}},"Drum range and overlapping pointer/key release");
    for(int i=0;i<30;++i) {keys.shiftOctave(1);keys.shiftVelocity(1);}
    passed &= check(keys.octave()==8 && keys.velocity()==127,"Upper MIDI range");
    events.clear();
    for(const auto key : {'P',';','\''}) {
        const auto index=keys.keyIndex(key);
        keys.press(index,index,1,send);
        if(index>=0) keys.release(index,send);
    }
    passed &= check(events==std::vector<Event>{{1,123,127},{1,123,0},{1,124,127},{1,124,0},{1,125,127},{1,125,0}},
                    "P, semicolon and apostrophe play and release D sharp, E and F");
    for(int i=0;i<30;++i) {keys.shiftOctave(-1);keys.shiftVelocity(-1);}
    passed &= check(keys.octave()==-1 && keys.velocity()==1,"Lower MIDI range");
    return passed ? 0 : 1;
}
