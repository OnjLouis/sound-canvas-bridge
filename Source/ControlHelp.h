#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace scbridge {
inline constexpr auto explicitHelpProperty="scbridgeContextHelp";
inline juce::String contextHelp(const juce::Component& component) {
    const auto explicitHelp=component.getProperties()[explicitHelpProperty].toString();
    return explicitHelp.isNotEmpty() ? explicitHelp : component.getHelpText();
}
}
