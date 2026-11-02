#pragma once
#include <cstdint>
#include <cstddef>
#include "Insertion.h"

namespace scbridge {
enum class ControlScope { Legacy, Part, Global, Drum, Selector, InsertionType, InsertionParameter };
enum class ControlUnit { Raw, Signed64, Pan, Switch, ReverbType, ChorusType, DelayType, DrumPitch, DrumNote, Setup, LowFrequency, HighFrequency, InsertionType };
struct ControlDefinition {
    const char* id;
    const char* name;
    int minimum, maximum, initial;
    ControlScope scope;
    uint32_t address;
    ptrdiff_t offset;
    ControlUnit unit = ControlUnit::Raw;
};
constexpr unsigned legacyParameterCount = 16;
constexpr ControlDefinition controls[] = {
    {"part","Part",0,15,0,ControlScope::Legacy,0,0},
    {"map","Sound Map",1,4,4,ControlScope::Legacy,0,0},
    {"bank","Bank",0,127,0,ControlScope::Legacy,0,0},
    {"program","Program",0,127,0,ControlScope::Legacy,0,0},
    {"volume","Volume",0,127,100,ControlScope::Legacy,0,0},
    {"pan","Pan",0,127,64,ControlScope::Legacy,0,0},
    {"expression","Expression",0,127,127,ControlScope::Legacy,0,0},
    {"modulation","Modulation",0,127,0,ControlScope::Legacy,0,0},
    {"reverb","Reverb",0,127,40,ControlScope::Legacy,0,0},
    {"chorus","Chorus",0,127,0,ControlScope::Legacy,0,0},
    {"delay","Delay",0,127,0,ControlScope::Legacy,0,0},
    {"cutoff","Brightness",0,127,64,ControlScope::Legacy,0,0},
    {"resonance","Resonance",0,127,64,ControlScope::Legacy,0,0},
    {"attack","Attack",0,127,64,ControlScope::Legacy,0,0},
    {"release","Release",0,127,64,ControlScope::Legacy,0,0},
    {"routing","Keyboard Routing",0,1,0,ControlScope::Legacy,0,0},
    {"decay","Decay",0,127,64,ControlScope::Part,0x401035,0x0d,ControlUnit::Signed64},
    {"vibratoRate","Vibrato Rate",0,127,64,ControlScope::Part,0x401030,0x08,ControlUnit::Signed64},
    {"vibratoDepth","Vibrato Depth",0,127,64,ControlScope::Part,0x401031,0x09,ControlUnit::Signed64},
    {"vibratoDelay","Vibrato Delay",0,127,64,ControlScope::Part,0x401037,0x0f,ControlUnit::Signed64},
    {"transpose","Part Transpose",40,88,64,ControlScope::Part,0x401016,-2,ControlUnit::Signed64},
    {"bendRange","Pitch Bend Range",64,88,66,ControlScope::Part,0x402010,0x2c,ControlUnit::Signed64},
    {"partEq","Part EQ",0,1,0,ControlScope::Part,0x404020,0x74,ControlUnit::Switch},
    {"partMfx","Part MFX",0,1,0,ControlScope::Part,0x404022,0x76,ControlUnit::Switch},
    {"masterVolume","Master Volume",0,127,127,ControlScope::Global,0x400004,0xc10c},
    {"masterPan","Master Pan",1,127,64,ControlScope::Global,0x400006,0xc110,ControlUnit::Pan},
    {"masterTranspose","Master Transpose",40,88,64,ControlScope::Global,0x400005,0xc10f,ControlUnit::Signed64},
    {"reverbType","Reverb Type",0,7,4,ControlScope::Global,0x400130,0xb906,ControlUnit::ReverbType},
    {"reverbCharacter","Reverb Character",0,7,4,ControlScope::Global,0x400131,0xb907},
    {"reverbLowPass","Reverb Pre Low Pass",0,7,0,ControlScope::Global,0x400132,0xb908},
    {"reverbLevel","Reverb Level",0,127,64,ControlScope::Global,0x400133,0xb909},
    {"reverbTime","Reverb Time",0,127,64,ControlScope::Global,0x400134,0xb90a},
    {"reverbFeedback","Reverb Feedback",0,127,0,ControlScope::Global,0x400135,0xb90b},
    {"reverbPredelay","Reverb Predelay",0,127,0,ControlScope::Global,0x400137,0xb90d},
    {"chorusType","Chorus Type",0,7,2,ControlScope::Global,0x400138,0xb90e,ControlUnit::ChorusType},
    {"chorusLowPass","Chorus Pre Low Pass",0,7,0,ControlScope::Global,0x400139,0xb90f},
    {"chorusLevel","Chorus Level",0,127,64,ControlScope::Global,0x40013a,0xb910},
    {"chorusFeedback","Chorus Feedback",0,127,8,ControlScope::Global,0x40013b,0xb911},
    {"chorusDelay","Chorus Delay",0,127,80,ControlScope::Global,0x40013c,0xb912},
    {"chorusRate","Chorus Rate",0,127,3,ControlScope::Global,0x40013d,0xb913},
    {"chorusDepth","Chorus Depth",0,127,19,ControlScope::Global,0x40013e,0xb914},
    {"chorusToReverb","Chorus To Reverb",0,127,0,ControlScope::Global,0x40013f,0xb915},
    {"chorusToDelay","Chorus To Delay",0,127,0,ControlScope::Global,0x400140,0xb916},
    {"delayType","Delay Type",0,9,0,ControlScope::Global,0x400150,0xb917,ControlUnit::DelayType},
    {"delayLowPass","Delay Pre Low Pass",0,7,0,ControlScope::Global,0x400151,0xb918},
    {"delayTime","Delay Centre Time",1,115,97,ControlScope::Global,0x400152,0xb919},
    {"delayLeftRatio","Delay Left Ratio",1,120,1,ControlScope::Global,0x400153,0xb91a},
    {"delayRightRatio","Delay Right Ratio",1,120,1,ControlScope::Global,0x400154,0xb91b},
    {"delayCentre","Delay Centre Level",0,127,127,ControlScope::Global,0x400155,0xb91c},
    {"delayLeft","Delay Left Level",0,127,0,ControlScope::Global,0x400156,0xb91d},
    {"delayRight","Delay Right Level",0,127,0,ControlScope::Global,0x400157,0xb91e},
    {"delayLevel","Delay Level",0,127,64,ControlScope::Global,0x400158,0xb91f},
    {"delayFeedback","Delay Feedback",0,127,80,ControlScope::Global,0x400159,0xb920,ControlUnit::Signed64},
    {"delayToReverb","Delay To Reverb",0,127,0,ControlScope::Global,0x40015a,0xb921},
    {"eqLowFrequency","EQ Low Frequency",0,1,0,ControlScope::Global,0x400200,0xb924,ControlUnit::LowFrequency},
    {"eqLowGain","EQ Low Gain",52,76,64,ControlScope::Global,0x400201,0xb925,ControlUnit::Signed64},
    {"eqHighFrequency","EQ High Frequency",0,1,0,ControlScope::Global,0x400202,0xb926,ControlUnit::HighFrequency},
    {"eqHighGain","EQ High Gain",52,76,64,ControlScope::Global,0x400203,0xb927,ControlUnit::Signed64},
    {"drumNote","Drum Note",0,127,36,ControlScope::Selector,0,0,ControlUnit::DrumNote},
    {"drumSetup","Drum Setup",0,1,0,ControlScope::Selector,0,0,ControlUnit::Setup},
    {"drumPitch","Drum Pitch",0,127,60,ControlScope::Drum,0x410100,0x180,ControlUnit::DrumPitch},
    {"drumLevel","Drum Level",0,127,100,ControlScope::Drum,0x410200,0x100},
    {"drumPan","Drum Pan",0,127,64,ControlScope::Drum,0x410400,0x280,ControlUnit::Pan},
    {"drumReverb","Drum Reverb",0,127,0,ControlScope::Drum,0x410500,0x300},
    {"drumChorus","Drum Chorus",0,127,0,ControlScope::Drum,0x410600,0x380},
    {"drumDelay","Drum Delay",0,127,0,ControlScope::Drum,0x410900,0x400},
    {"mfxType","Insertion Type",0,64,0,ControlScope::InsertionType,insertionTypeAddress,insertionTypeOffset,ControlUnit::InsertionType},
    {"mfxParameter","Insertion Parameter",1,20,1,ControlScope::Selector,0,0},
    {"mfxValue","Insertion Value",0,127,0,ControlScope::InsertionParameter,insertionFirstParameterAddress,insertionFirstParameterOffset},
    {"mfxToReverb","Insertion To Reverb",0,127,40,ControlScope::Global,0x400317,0xb940},
    {"mfxToChorus","Insertion To Chorus",0,127,0,ControlScope::Global,0x400318,0xb941},
    {"mfxToDelay","Insertion To Delay",0,127,0,ControlScope::Global,0x400319,0xb942},
    {"mfxEq","Insertion EQ",0,1,1,ControlScope::Global,0x40031f,0xb946,ControlUnit::Switch},
    {"mfxParameter1","EFX Parameter 1",0,127,0,ControlScope::Global,0x400303,0xb92c},
    {"mfxParameter2","EFX Parameter 2",0,127,0,ControlScope::Global,0x400304,0xb92d},
    {"mfxParameter3","EFX Parameter 3",0,127,0,ControlScope::Global,0x400305,0xb92e},
    {"mfxParameter4","EFX Parameter 4",0,127,0,ControlScope::Global,0x400306,0xb92f},
    {"mfxParameter5","EFX Parameter 5",0,127,0,ControlScope::Global,0x400307,0xb930},
    {"mfxParameter6","EFX Parameter 6",0,127,0,ControlScope::Global,0x400308,0xb931},
    {"mfxParameter7","EFX Parameter 7",0,127,0,ControlScope::Global,0x400309,0xb932},
    {"mfxParameter8","EFX Parameter 8",0,127,0,ControlScope::Global,0x40030a,0xb933},
    {"mfxParameter9","EFX Parameter 9",0,127,0,ControlScope::Global,0x40030b,0xb934},
    {"mfxParameter10","EFX Parameter 10",0,127,0,ControlScope::Global,0x40030c,0xb935},
    {"mfxParameter11","EFX Parameter 11",0,127,0,ControlScope::Global,0x40030d,0xb936},
    {"mfxParameter12","EFX Parameter 12",0,127,0,ControlScope::Global,0x40030e,0xb937},
    {"mfxParameter13","EFX Parameter 13",0,127,0,ControlScope::Global,0x40030f,0xb938},
    {"mfxParameter14","EFX Parameter 14",0,127,0,ControlScope::Global,0x400310,0xb939},
    {"mfxParameter15","EFX Parameter 15",0,127,0,ControlScope::Global,0x400311,0xb93a},
    {"mfxParameter16","EFX Parameter 16",0,127,0,ControlScope::Global,0x400312,0xb93b},
    {"mfxParameter17","EFX Parameter 17",0,127,0,ControlScope::Global,0x400313,0xb93c},
    {"mfxParameter18","EFX Parameter 18",0,127,0,ControlScope::Global,0x400314,0xb93d},
    {"mfxParameter19","EFX Parameter 19",0,127,0,ControlScope::Global,0x400315,0xb93e},
    {"mfxParameter20","EFX Parameter 20",0,127,0,ControlScope::Global,0x400316,0xb93f}
};
constexpr unsigned parameterCount = sizeof(controls) / sizeof(controls[0]);
constexpr unsigned drumNoteIndex = 58, drumSetupIndex = 59;
inline unsigned gsPart(int part) { return part == 9 ? 0u : part < 9 ? part + 1u : part; }
inline uint32_t controlAddress(unsigned index, int part, int note, int setup, int insertionParameter=1) {
    const auto& control = controls[index];
    if (control.scope == ControlScope::Part) return control.address + (gsPart(part) << 8);
    if (control.scope == ControlScope::Drum) return control.address + (setup << 12) + note;
    if (control.scope == ControlScope::InsertionParameter) return control.address + insertionParameter - 1;
    return control.address;
}
}
