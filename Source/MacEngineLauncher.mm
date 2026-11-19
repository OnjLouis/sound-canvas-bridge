#import <AppKit/AppKit.h>
#include "EngineLocation.h"
#include "MacEngineSession.h"
#include "Protocol.h"
#include <sys/un.h>
#include <fcntl.h>

@interface SCBridgeSessionDelegate : NSObject<NSApplicationDelegate>
@property(strong) NSURL* session;
@end
@implementation SCBridgeSessionDelegate
- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls {
    if(urls.count==1 && urls.firstObject.isFileURL && !self.session) self.session=urls.firstObject;
}
@end

int main() {
    using namespace scbridge;
    @autoreleasepool {
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        auto* delegate=[SCBridgeSessionDelegate new];NSApp.delegate=delegate;[NSApp finishLaunching];
        auto* deadline=[NSDate dateWithTimeIntervalSinceNow:macSession::startupTimeoutMs/1000.0];
        while(!delegate.session && deadline.timeIntervalSinceNow>0) {
            auto* event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate dateWithTimeIntervalSinceNow:.005]
                inMode:NSDefaultRunLoopMode dequeue:YES];
            if(event) [NSApp sendEvent:event];
        }
        auto* url=delegate.session;
        if(!url || ![url.lastPathComponent isEqualToString:@"session.scbridgesession"]) return 2;
        struct stat file{},directory{};auto* folder=url.path.stringByDeletingLastPathComponent;
        if(lstat(url.fileSystemRepresentation,&file) || !S_ISREG(file.st_mode) || file.st_uid!=geteuid() ||
           (file.st_mode&0777)!=0600 || file.st_size<=0 || file.st_size>4096 ||
           lstat(folder.fileSystemRepresentation,&directory) || !S_ISDIR(directory.st_mode) ||
           directory.st_uid!=geteuid() || (directory.st_mode&0777)!=0700) return 3;
        id document=[NSPropertyListSerialization propertyListWithData:[NSData dataWithContentsOfURL:url]
            options:NSPropertyListImmutable format:nil error:nil];
        if(![document isKindOfClass:NSDictionary.class]) return 3;
        NSData* nonce=document[@"nonce"];NSNumber* parent=document[@"pid"];
        if(![nonce isKindOfClass:NSData.class] || nonce.length!=16 ||
           ![parent isKindOfClass:NSNumber.class] || parent.intValue<=0) return 3;
        const auto path=std::string(folder.fileSystemRepresentation)+"/s";
        sockaddr_un address{};address.sun_family=AF_UNIX;
        if(path.size()>=sizeof(address.sun_path)) return 3;
        const int connection=socket(AF_UNIX,SOCK_STREAM,0);if(connection<0) return 4;
        strlcpy(address.sun_path,path.c_str(),sizeof(address.sun_path));
        if(connect(connection,reinterpret_cast<sockaddr*>(&address),sizeof(address))) {close(connection);return 5;}
        uid_t uid{};gid_t gid{};pid_t peer{};socklen_t size=sizeof(peer);
        if(getpeereid(connection,&uid,&gid) || uid!=geteuid() ||
           getsockopt(connection,SOL_LOCAL,LOCAL_PEERPID,&peer,&size) || peer!=parent.intValue) {close(connection);return 6;}
        macSession::Hello hello;memcpy(hello.nonce.data(),nonce.bytes,hello.nonce.size());
        if(!macSession::transfer(connection,&hello,sizeof(hello),true)) {close(connection);return 7;}
        const int memory=macSession::receiveMemory(connection,sizeof(Shared));
        if(memory<0) {close(connection);return 8;}
        auto core=EngineLocation().core();
        char status=core.existsAsFile() ? 'L' : 'M';
        if(!macSession::transfer(connection,&status,1,true) || status!='L') {close(memory);close(connection);return 9;}
        const auto executable=juce::File([NSBundle mainBundle].bundlePath.UTF8String).getParentDirectory()
            .getSiblingFile("Resources").getChildFile("SoundCanvasHelper");
        auto helperPath=executable.getFullPathName().toStdString(),corePath=core.getFullPathName().toStdString();
        // Duplicate above the inherited protocol descriptors before replacing
        // either one. exec keeps the authenticated launch PID and no sandbox.
        const int audio=fcntl(connection,F_DUPFD_CLOEXEC,10),shared=fcntl(memory,F_DUPFD_CLOEXEC,10);
        if(audio<0 || shared<0 || dup2(audio,3)<0 || dup2(shared,4)<0) return 10;
        close(audio);close(shared);
        if(connection!=3 && connection!=4) close(connection);
        if(memory!=3 && memory!=4) close(memory);
        char* arguments[]{helperPath.data(),corePath.data(),nullptr};
        execv(helperPath.c_str(),arguments);return 11;
    }
}
