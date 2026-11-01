#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#import <AppKit/AppKit.h>

@interface OnjSCBridgeMenuTarget : NSObject <NSApplicationDelegate> {
@public
    std::function<void(int)> invoke;
    int showCommand;
    id<NSApplicationDelegate> originalDelegate;
    bool controlsHidden;
}
- (void)perform:(id)sender;
@end
@implementation OnjSCBridgeMenuTarget
- (void)perform:(id)sender { if(invoke) invoke(static_cast<int>([sender tag])); }
- (BOOL)applicationShouldHandleReopen:(NSApplication*)app hasVisibleWindows:(BOOL)visible {
    juce::ignoreUnused(app,visible);
    controlsHidden=false;
    juce::Logger::writeToLog("Application reopen requested");
    if(invoke) invoke(showCommand);
    return NO;
}
- (void)applicationDidBecomeActive:(NSNotification*)notification {
    if([originalDelegate respondsToSelector:_cmd]) [originalDelegate applicationDidBecomeActive:notification];
    // Initial activation is not a user request to reopen hidden controls.
}
- (BOOL)respondsToSelector:(SEL)selector {
    return [super respondsToSelector:selector] || [originalDelegate respondsToSelector:selector];
}
- (id)forwardingTargetForSelector:(SEL)selector {
    if([originalDelegate respondsToSelector:selector]) return originalDelegate;
    return [super forwardingTargetForSelector:selector];
}
@end

namespace scbridge {
class MenuBarStatus final : public juce::SystemTrayIconComponent {
public:
    MenuBarStatus(std::function<void(int)> action,int show,int settings,int panic,int quit,bool handleReopen=false)
        :handlesReopen(handleReopen) {
        setName("Sound Canvas Bridge");setTitle(getName());
        juce::Image icon(juce::Image::ARGB,20,20,true);juce::Graphics graphics(icon);graphics.setColour(juce::Colours::black);
        graphics.fillRect(3,7,3,10);graphics.fillRect(8,3,3,14);graphics.fillRect(13,6,3,11);
        setIconImage(icon,icon);
        target=[[OnjSCBridgeMenuTarget alloc] init];target->invoke=std::move(action);target->showCommand=show;
        // Preserve JUCE's delegate for termination, files and focus events.
        // Finder can activate a hidden accessory app without a reopen event.
        if(handlesReopen) {
            target->originalDelegate=[NSApp.delegate retain];
            NSApp.delegate=target;
        }
        auto* item=(NSStatusItem*)getNativeHandle();
        [item.button setAccessibilityLabel:@"Sound Canvas Bridge"];
        [item.button setAccessibilityHelp:nil];
        item.button.toolTip=@"Sound Canvas Bridge";
        NSMenu* menu=[[[NSMenu alloc] initWithTitle:@"Sound Canvas Bridge"] autorelease];
        add(menu,@"Show Controls",show);add(menu,@"Preferences...",settings);
        [menu addItem:[NSMenuItem separatorItem]];add(menu,@"Panic: All Sounds Off",panic);
        [menu addItem:[NSMenuItem separatorItem]];add(menu,@"Quit Sound Canvas Bridge",quit);
        // Attach a native menu: status-button AXPress must not depend on a
        // synthetic mouse event reaching JUCE's mouse-only event forwarder.
        item.menu=menu;
    }
    ~MenuBarStatus() override {
        if(handlesReopen) {
            if(NSApp.delegate==target) NSApp.delegate=target->originalDelegate;
            [target->originalDelegate release];
        }
        auto* item=(NSStatusItem*)getNativeHandle();item.menu=nil;
        [[NSStatusBar systemStatusBar] removeStatusItem:item];
        [target release];
    }
    bool available() const { return getNativeHandle()!=nullptr && [(NSStatusItem*)getNativeHandle() button]!=nil; }
    void setControlsHidden(bool hidden) { target->controlsHidden=hidden; }
private:
    void add(NSMenu* menu,NSString* title,int command) {
        NSMenuItem* item=[[[NSMenuItem alloc] initWithTitle:title action:@selector(perform:) keyEquivalent:@""] autorelease];
        item.target=target;item.tag=command;[menu addItem:item];
    }
    OnjSCBridgeMenuTarget* target=nil;
    const bool handlesReopen;
};
}
