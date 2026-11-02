#pragma once
#include <cstdint>
namespace scbridge {
constexpr uint32_t virtualMidiMagic=0x5343564d;
struct VirtualMidiFrame {
    uint32_t magic=virtualMidiMagic;
    uint32_t words=0;
    uint32_t data[4]{};
};
static_assert(sizeof(VirtualMidiFrame)==24);
}
