#import <AppKit/AppKit.h>
#include "KeyboardAnnouncement.h"

bool scbridge::announceKeyboardSetting(juce::Component& focus,const juce::String& text) {
    auto* peer=focus.getPeer();
    auto* view=peer ? static_cast<NSView*>(peer->getNativeHandle()) : nil;
    auto* window=[view window];
    if(!window) return false;
    // Use the control's actual window, even if AppKit has no main window.
    NSAccessibilityPostNotificationWithUserInfo(window,NSAccessibilityAnnouncementRequestedNotification,
        @{NSAccessibilityAnnouncementKey:[NSString stringWithUTF8String:text.toRawUTF8()],
          NSAccessibilityPriorityKey:@(NSAccessibilityPriorityMedium)});
    return true;
}
