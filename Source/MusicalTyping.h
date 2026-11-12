#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace scbridge {
struct TypingKey { char key; int semitone; bool sharp; };
inline constexpr std::array<TypingKey,18> typingKeys{{
    {'a',0,false},{'w',1,true},{'s',2,false},{'e',3,true},{'d',4,false},
    {'f',5,false},{'t',6,true},{'g',7,false},{'y',8,true},{'h',9,false},
    {'u',10,true},{'j',11,false},{'k',12,false},{'o',13,true},{'l',14,false},
    {'p',15,true},{';',16,false},{'\'',17,false}
}};
inline constexpr int typingWhiteKeyCount=[] {
    int count=0;
    for(const auto& key:typingKeys) if(!key.sharp) ++count;
    return count;
}();
inline constexpr int lowestTypingOctave=-1, highestTypingOctave=8;
inline constexpr int lowestTypingVelocity=1, highestTypingVelocity=127, typingVelocityStep=8;

class MusicalTyping {
public:
    struct Note { int channel=0, pitch=0; };
    static constexpr int mouseKey=static_cast<int>(typingKeys.size());
    int octave() const { return octaveValue; }
    int velocity() const { return velocityValue; }
    void start(bool drums) { octaveValue=drums ? 2 : 4; }
    bool shiftOctave(int step) { return setOctave(octaveValue+step); }
    bool shiftVelocity(int step) { return setVelocity(velocityValue+step*typingVelocityStep); }
    bool setOctave(int value) {
        const auto next=std::clamp(value,lowestTypingOctave,highestTypingOctave);
        if(next==octaveValue) return false;
        octaveValue=next;return true;
    }
    bool setVelocity(int value) {
        const auto next=std::clamp(value,lowestTypingVelocity,highestTypingVelocity);
        if(next==velocityValue) return false;
        velocityValue=next;return true;
    }
    static int keyIndex(int key) {
        if(key>='A' && key<='Z') key+='a'-'A';
        for(size_t i=0;i<typingKeys.size();++i) if(typingKeys[i].key==key) return static_cast<int>(i);
        return -1;
    }
    bool held(int slot) const { return notes[static_cast<size_t>(slot)].channel!=0; }
    bool pitchHeld(int semitone) const {
        const int pitch=(octaveValue+1)*12+semitone;
        for(const auto& n:notes) if(n.channel && n.pitch==pitch) return true;
        return false;
    }
    template<class Send> void press(int slot,int index,int channel,Send&& send) {
        if(slot<0 || slot>mouseKey || index<0 || index>=mouseKey || channel<1 || channel>16 || held(slot)) return;
        const int pitch=(octaveValue+1)*12+typingKeys[static_cast<size_t>(index)].semitone;
        const bool alreadySounding=contains(channel,pitch);
        notes[static_cast<size_t>(slot)]={channel,pitch};
        if(!alreadySounding) send(channel,pitch,velocityValue);
    }
    template<class Send> void release(int slot,Send&& send) {
        auto& note=notes[static_cast<size_t>(slot)];
        if(!note.channel) return;
        const auto previous=note;note={};
        if(!contains(previous.channel,previous.pitch)) send(previous.channel,previous.pitch,0);
    }
    template<class Send> void releaseAll(Send&& send) {
        for(int slot=0;slot<=mouseKey;++slot) release(slot,send);
    }
private:
    bool contains(int channel,int pitch) const {
        for(const auto& n:notes) if(n.channel==channel && n.pitch==pitch) return true;
        return false;
    }
    std::array<Note,typingKeys.size()+1> notes{};
    int octaveValue=4,velocityValue=88;
};
}
