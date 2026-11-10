#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LegacyWindowsAccessibility.h"

namespace scbridge {
inline constexpr int keyboardChoicePageStep=10;
class KeyboardChoice : public juce::ComboBox {
public:
    void announceKeyboardValue() {
#ifdef SCBRIDGE_WINDOWS7
        if(!legacyAnnouncement.post(*this,getText()))
#endif
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
        if(!isEnabled()) return false;
        if(!key.getModifiers().isAnyModifierKeyDown()) {
            int next=getSelectedItemIndex();
            const int code=key.getKeyCode();
            if(code==juce::KeyPress::upKey || code==juce::KeyPress::leftKey) --next;
            else if(code==juce::KeyPress::downKey || code==juce::KeyPress::rightKey) ++next;
            else if(code==juce::KeyPress::homeKey) next=0;
            else if(code==juce::KeyPress::endKey) next=getNumItems()-1;
            else if(code==juce::KeyPress::pageUpKey) next-=keyboardChoicePageStep;
            else if(code==juce::KeyPress::pageDownKey) next+=keyboardChoicePageStep;
            else return juce::ComboBox::keyPressed(key);
            const auto before=getText();
            if(getNumItems()>0)
                setSelectedItemIndex(juce::jlimit(0,getNumItems()-1,next),juce::sendNotificationSync);
            if(getText()!=before) announceKeyboardValue();
            return true;
        }
        return juce::ComboBox::keyPressed(key);
    }
#ifdef SCBRIDGE_WINDOWS7
private:
    LegacyWindowsAnnouncement legacyAnnouncement;
#endif
};
}
