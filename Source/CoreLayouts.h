#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace scbridge {
struct WindowsCoreLayout {
    const char* sha256;
    unsigned bits;
    uint32_t dataRva,partsRva,drumsRva;
    ptrdiff_t firstPart,partStride,masterVolume,insertionDelta;
};
// Read-only layouts are selected by exact file identity, never by a filename
// or a guessed product version. MIDI/SysEx addresses remain the GS addresses.
constexpr WindowsCoreLayout windowsCoreLayouts[]{
    {"117e6aa147a96fbde5e10d2caf16c89965acc1e44235fd245992216cc620bdb1",64,
     0x1a136b0,0x1a222a0,0x1a1e838,0x3dc,0x488,0xb04a,0},
    {"e95e7f8f97667c247cccb8b15735cba949a07e92673a6a5411116d18e2268c47",64,
     0x1a1a2a0,0x1a252e0,0x1a252d8,0x3dc,0x488,0xb80a,-0x100},
    {"b8ce7c1f1db3402c2b85883ddc6449915ade44db89a4ad9982b13ab943e76879",32,
     0x1a06f1c,0x1a127ec,0x1a12034,0x3a4,0x450,0xb082,0},
    {"8ddb29d5e1c20ba5a262084e96fbc4f4de8c70db988873b80a1a6bd3ee244f29",32,
     0x1a0eecc,0x1a19f20,0x1a19f1c,0x3a4,0x450,0xb806,-0x80}
};
inline const WindowsCoreLayout* windowsCoreLayout(const char* sha256,unsigned bits) {
    if(!sha256) return nullptr;
    for(const auto& layout:windowsCoreLayouts)
        if(layout.bits==bits && !std::strcmp(layout.sha256,sha256)) return &layout;
    return nullptr;
}
}
