#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace scbridge {
#if !defined(SCBRIDGE_WINDOWS7)
bool announceKeyboardSetting(juce::Component& focus,const juce::String& text);
#endif
}
