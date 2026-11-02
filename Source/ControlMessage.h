#pragma once
#include "Controls.h"
#include <array>

namespace scbridge {
struct ControlMessage { std::array<uint8_t,12> bytes{};unsigned size=0; };
inline ControlMessage dt1Message(uint32_t address,uint8_t first,int second=-1) {
    ControlMessage message;
    message.bytes={0xf0,0x41,0x10,0x42,0x12,uint8_t(address>>16),uint8_t((address>>8)&127),uint8_t(address&127),first};
    unsigned end=9;
    if(second>=0) message.bytes[end++]=static_cast<uint8_t>(second);
    unsigned sum=0;for(unsigned i=5;i<end;++i) sum+=message.bytes[i];
    message.bytes[end++]=static_cast<uint8_t>((128-(sum&127))&127);
    message.bytes[end++]=0xf7;message.size=end;
    return message;
}
inline ControlMessage controlMessage(unsigned index,int value,int part,int note,int setup,int insertionParameter=1) {
    if(index>=parameterCount || value<controls[index].minimum || value>controls[index].maximum ||
       controls[index].scope==ControlScope::Selector || controls[index].scope==ControlScope::Legacy) return {};
    if(controls[index].scope==ControlScope::InsertionParameter &&
       (insertionParameter<1 || insertionParameter>static_cast<int>(insertionParameterCount))) return {};
    if(controls[index].scope==ControlScope::InsertionType) {
        const auto& type=insertionTypes[value];
        return dt1Message(insertionTypeAddress,type.msb,type.lsb);
    }
    return dt1Message(controlAddress(index,part,note,setup,insertionParameter),static_cast<uint8_t>(value));
}
}
