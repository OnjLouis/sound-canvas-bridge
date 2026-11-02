#pragma once
#include <cstdint>
#include <cstddef>

namespace scbridge {
// Algorithm numbers and GS DT1 values from the Sound Canvas VA owner's manual.
// The displayed ordinal is not the two-byte wire value.
struct InsertionType { const char* name; uint8_t msb, lsb; };
constexpr InsertionType insertionTypes[] = {
    {"Thru",0x00,0x00}, {"Stereo EQ",0x01,0x00}, {"Spectrum",0x01,0x01},
    {"Enhancer",0x01,0x02}, {"Humanizer",0x01,0x03}, {"Overdrive",0x01,0x10},
    {"Distortion",0x01,0x11}, {"Phaser",0x01,0x20}, {"Auto Wah",0x01,0x21},
    {"Rotary",0x01,0x22}, {"Stereo Flanger",0x01,0x23}, {"Step Flanger",0x01,0x24},
    {"Tremolo",0x01,0x25}, {"Auto Pan",0x01,0x26}, {"Compressor",0x01,0x30},
    {"Limiter",0x01,0x31}, {"Hexa Chorus",0x01,0x40}, {"Tremolo Chorus",0x01,0x41},
    {"Stereo Chorus",0x01,0x42}, {"Space D",0x01,0x43}, {"3D Chorus",0x01,0x44},
    {"Stereo Delay",0x01,0x50}, {"Modulation Delay",0x01,0x51}, {"Triple Tap Delay",0x01,0x52},
    {"Quadruple Tap Delay",0x01,0x53}, {"Time Controllable Delay",0x01,0x54},
    {"Reverb",0x01,0x55}, {"Gate Reverb",0x01,0x56}, {"3D Delay",0x01,0x57},
    {"2 Voice Pitch Shifter",0x01,0x60}, {"Feedback Pitch Shifter",0x01,0x61},
    {"3D Auto",0x01,0x70}, {"3D Manual",0x01,0x71}, {"Lo Fi 1",0x01,0x72},
    {"Lo Fi 2",0x01,0x73}, {"Overdrive To Chorus",0x02,0x00},
    {"Overdrive To Flanger",0x02,0x01}, {"Overdrive To Delay",0x02,0x02},
    {"Distortion To Chorus",0x02,0x03}, {"Distortion To Flanger",0x02,0x04},
    {"Distortion To Delay",0x02,0x05}, {"Enhancer To Chorus",0x02,0x06},
    {"Enhancer To Flanger",0x02,0x07}, {"Enhancer To Delay",0x02,0x08},
    {"Chorus To Delay",0x02,0x09}, {"Flanger To Delay",0x02,0x0a},
    {"Chorus To Flanger",0x02,0x0b}, {"Rotary Multi",0x03,0x00},
    {"Guitar Multi 1",0x04,0x00}, {"Guitar Multi 2",0x04,0x01},
    {"Guitar Multi 3",0x04,0x02}, {"Clean Guitar Multi 1",0x04,0x03},
    {"Clean Guitar Multi 2",0x04,0x04}, {"Bass Multi",0x04,0x05}, {"EP Multi",0x04,0x06},
    {"Keyboard Multi",0x05,0x00}, {"Chorus And Delay",0x11,0x00},
    {"Flanger And Delay",0x11,0x01}, {"Chorus And Flanger",0x11,0x02},
    {"Overdrive 1 And Overdrive 2",0x11,0x03}, {"Overdrive And Rotary",0x11,0x04},
    {"Overdrive And Phaser",0x11,0x05}, {"Overdrive And Auto Wah",0x11,0x06},
    {"Phaser And Rotary",0x11,0x07}, {"Phaser And Auto Wah",0x11,0x08}
};
constexpr unsigned insertionTypeCount=sizeof(insertionTypes)/sizeof(insertionTypes[0]);
constexpr unsigned insertionParameterCount=20;
constexpr unsigned insertionTypeIndex=66, insertionSelectorIndex=67, insertionValueIndex=68;
constexpr unsigned insertionReverbIndex=69, insertionChorusIndex=70, insertionDelayIndex=71, insertionEqIndex=72;
constexpr unsigned insertionFirstDirectIndex=73;
constexpr uint32_t insertionTypeAddress=0x400300, insertionFirstParameterAddress=0x400303;
constexpr ptrdiff_t insertionTypeOffset=0xb928, insertionFirstParameterOffset=0xb92c;
inline int insertionTypeIndexForCode(uint8_t msb,uint8_t lsb) {
    for(unsigned i=0;i<insertionTypeCount;++i)
        if(insertionTypes[i].msb==msb && insertionTypes[i].lsb==lsb) return static_cast<int>(i);
    return -1;
}
struct InsertionState {
    uint8_t valid=0,type=0,parameters[insertionParameterCount]{};
    uint8_t toReverb=40,toChorus=0,toDelay=0,eq=1;
    uint8_t controlSource1=0,controlDepth1=64,controlSource2=0,controlDepth2=64;
};
static_assert(sizeof(InsertionState)==30);
inline bool validInsertionState(const InsertionState& state) {
    if(state.valid>1) return false;
    if(!state.valid) return true;
    if(state.type>=insertionTypeCount || state.eq>1) return false;
    for(auto value:state.parameters) if(value>127) return false;
    return state.toReverb<=127 && state.toChorus<=127 && state.toDelay<=127 &&
        state.controlSource1<=127 && state.controlDepth1<=127 && state.controlSource2<=127 && state.controlDepth2<=127;
}
}
