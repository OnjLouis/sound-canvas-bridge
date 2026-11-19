#pragma once
#include "Backend.h"

namespace scbridge {
inline constexpr const char* engineSetupGuidance=
#if !defined(_WIN32) && !defined(SCBRIDGE_TRANSLATED_ENGINE)
    "Open the Sound Canvas Bridge standalone app, choose Preferences > General, then Browse to your Sound Canvas VA VST installation. After setup, reload this plug-in instance.";
#else
    "Open the Sound Canvas Bridge standalone app, choose Preferences > General, then Download from Roland or Browse. After setup, reload this plug-in instance.";
#endif
// Report missing-engine setup through the editor's status control, not an
// unowned modal window: headless hosts can unload an AU on a decoder thread.
}
