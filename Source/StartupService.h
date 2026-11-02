#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#ifdef _WIN32
#include "StartupServiceWindows.h"
#else
#import <Foundation/Foundation.h>
#include <unistd.h>
#include <signal.h>

namespace scbridge {
class StartupService {
public:
    using Runner=std::function<bool(const juce::StringArray&)>;
    explicit StartupService(juce::File directory=juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/LaunchAgents"),Runner command=run)
        :file(directory.getChildFile("me.onj.soundcanvasbridge.login.plist")),invoke(std::move(command)) {}
    bool enabled() const { return file.existsAsFile(); }
    bool setEnabled(bool enabled,const juce::File& app,juce::String& error) {
        error.clear();
        if(file.isSymbolicLink() || file.getParentDirectory().getLinkedTarget()!=file.getParentDirectory()) {
            error="The login settings folder is redirected. No login item was changed.";return false;
        }
        const bool existed=file.existsAsFile();juce::MemoryBlock previous;
        if(existed && (file.getSize()>65536 || !file.loadFileAsData(previous))) {
            error="Could not read the existing login item. Nothing was changed.";return false;
        }
        if(existed) {
            NSData* bytes=[NSData dataWithBytes:previous.getData() length:previous.getSize()];
            id registration=[NSPropertyListSerialization propertyListWithData:bytes options:NSPropertyListImmutable format:nil error:nil];
            if(![registration isKindOfClass:[NSDictionary class]] || ![registration[@"Label"] isEqual:@"me.onj.soundcanvasbridge.login"]) {
                error="The existing login file is not recognised. Nothing was changed.";return false;
            }
        }
        const auto domain="gui/"+juce::String(static_cast<int>(getuid()));
        if(!enabled) {
            if(!existed) return true;
            if(!invoke({"bootout",domain,file.getFullPathName()}) && invoke({"print",domain+"/me.onj.soundcanvasbridge.login"})) {
                error="Could not unload the login item. Its existing setting was kept.";return false;
            }
            if(file.deleteFile()) return true;
            invoke({"bootstrap",domain,file.getFullPathName()});
            error="Could not remove the login item. Its existing setting was restored.";return false;
        }
        if(!app.isDirectory() || !app.hasFileExtension("app")) { error="Run the installed application before enabling Run at login.";return false; }
        NSString* path=[NSString stringWithUTF8String:app.getFullPathName().toRawUTF8()];
        NSDictionary* registration=@{@"Label":@"me.onj.soundcanvasbridge.login",@"ProgramArguments":@[@"/usr/bin/open",@"-g",path],@"RunAtLoad":@YES};
        NSError* failure=nil;NSData* data=[NSPropertyListSerialization dataWithPropertyList:registration format:NSPropertyListXMLFormat_v1_0 options:0 error:&failure];
        if(!data || !file.getParentDirectory().createDirectory().wasOk() || !file.replaceWithData(data.bytes,data.length)) {
            error="Could not save the login item.";return false;
        }
        if(existed) invoke({"bootout",domain,file.getFullPathName()});
        if(invoke({"bootstrap",domain,file.getFullPathName()})) return true;
        invoke({"bootout",domain,file.getFullPathName()});
        if(existed) {
            if(!file.replaceWithData(previous.getData(),previous.getSize()) || !invoke({"bootstrap",domain,file.getFullPathName()}))
                error="Login registration failed and its previous registration could not be fully restored.";
        } else if(!file.deleteFile()) error="Login registration failed and its unused settings file could not be removed.";
        if(error.isEmpty()) error="macOS did not accept Run at login. The previous setting was restored.";
        return false;
    }
private:
    static bool run(const juce::StringArray& arguments) {
        NSTask* task=[[[NSTask alloc] init] autorelease];task.executableURL=[NSURL fileURLWithPath:@"/bin/launchctl"];
        NSMutableArray* values=[NSMutableArray array];for(const auto& value:arguments) [values addObject:[NSString stringWithUTF8String:value.toRawUTF8()]];
        task.arguments=values;task.standardOutput=[NSFileHandle fileHandleWithNullDevice];task.standardError=[NSFileHandle fileHandleWithNullDevice];
        NSError* error=nil;if(![task launchAndReturnError:&error]) return false;
        auto deadline=juce::Time::getMillisecondCounterHiRes()+3000;
        while(task.running && juce::Time::getMillisecondCounterHiRes()<deadline) juce::Thread::sleep(10);
        if(task.running) {
            [task terminate];deadline=juce::Time::getMillisecondCounterHiRes()+1000;
            while(task.running && juce::Time::getMillisecondCounterHiRes()<deadline) juce::Thread::sleep(10);
            if(task.running) kill(task.processIdentifier,SIGKILL);
            [task waitUntilExit];return false;
        }
        return task.terminationStatus==0;
    }
    juce::File file;
    Runner invoke;
};
}
#endif
