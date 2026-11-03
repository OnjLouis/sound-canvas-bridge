#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace scbridge {
class KeyboardChoice : public juce::ComboBox {
public:
    void announceKeyboardValue() {
        if(auto* handler=getAccessibilityHandler()) handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
        const auto text=getName()+", "+getText();
#ifdef SCBRIDGE_TEST_ANNOUNCEMENTS
        getProperties().set("lastKeyboardAnnouncement",text);
#endif
#ifndef _WIN32
        // VoiceOver does not reliably speak combo-box value notifications.
        // Use the same native announcement path as our numeric controls.
        juce::AccessibilityHandler::postAnnouncement(text,juce::AccessibilityHandler::AnnouncementPriority::medium);
#endif
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if(!key.getModifiers().isAnyModifierKeyDown() &&
           (key==juce::KeyPress::upKey || key==juce::KeyPress::leftKey ||
            key==juce::KeyPress::downKey || key==juce::KeyPress::rightKey)) {
            const auto before=getText();
            const int direction=(key==juce::KeyPress::upKey || key==juce::KeyPress::leftKey) ? -1 : 1;
            if(getNumItems()>0)
                setSelectedItemIndex(juce::jlimit(0,getNumItems()-1,getSelectedItemIndex()+direction),juce::sendNotificationSync);
            if(getText()!=before) announceKeyboardValue();
            return true;
        }
        return juce::ComboBox::keyPressed(key);
    }
};
}
