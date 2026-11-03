#pragma once
#include "Backend.h"
#include <cstdint>

namespace scbridge {
inline constexpr const char* engineSetupGuidance=
#if !defined(_WIN32) && !defined(SCBRIDGE_TRANSLATED_ENGINE)
    "Open the Sound Canvas Bridge standalone app, choose Preferences > General, then Browse to your Sound Canvas VA VST installation. After setup, reload this plug-in instance.";
#else
    "Open the Sound Canvas Bridge standalone app, choose Preferences > General, then Download from Roland or Browse. After setup, reload this plug-in instance.";
#endif
inline bool requestEngineSetupNotice(bool hosted,bool offline,bool missing,const uint8_t* bytes,int size) {
    return hosted && !offline && missing && bytes && size>=3 && (bytes[0]&0xf0)==0x90 && bytes[2]!=0;
}
}
