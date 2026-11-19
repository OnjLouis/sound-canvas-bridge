#import <AppKit/AppKit.h>
#import <Security/SecTask.h>
#include "MacEngineLaunch.h"
#include "MacEngineSession.h"
#include <sys/un.h>
#include <fcntl.h>
#include <memory>
#include <atomic>

namespace scbridge {
bool sandboxedHost() {
    const auto task=SecTaskCreateFromSelf(kCFAllocatorDefault);
    if(!task) return false;
    const auto value=SecTaskCopyValueForEntitlement(task,CFSTR("com.apple.security.app-sandbox"),nullptr);
    const bool result=value && CFEqual(value,kCFBooleanTrue);
    if(value) CFRelease(value);
    CFRelease(task);return result;
}
int createSandboxEngineMemory() {
    auto path=std::string(NSTemporaryDirectory().fileSystemRepresentation)+"scbridge-memory.XXXXXX";
    const int memory=mkstemp(path.data());
    if(memory>=0) {unlink(path.c_str());fcntl(memory,F_SETFD,FD_CLOEXEC);}
    return memory;
}
int launchSandboxEngine(const juce::File& launcher,int memory,int& resultSocket,pid_t& process) {
    @autoreleasepool {
        auto folder=std::string(NSTemporaryDirectory().fileSystemRepresentation)+"scbridge.XXXXXX";
        if(!mkdtemp(folder.data())) return 4;
        const auto path=folder+"/s",request=folder+"/session.scbridgesession";
        const int server=socket(AF_UNIX,SOCK_STREAM,0);
        struct Cleanup {
            int server;const std::string &folder,&path,&request;
            ~Cleanup() {if(server>=0) close(server);unlink(request.c_str());unlink(path.c_str());rmdir(folder.c_str());}
        } cleanup{server,folder,path,request};
        sockaddr_un address{};address.sun_family=AF_UNIX;
        if(server<0 || path.size()>=sizeof(address.sun_path)) return 4;
        strlcpy(address.sun_path,path.c_str(),sizeof(address.sun_path));
        if(bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address)) || listen(server,1) || chmod(path.c_str(),0600)) return 4;
        macSession::Hello hello;[[NSUUID UUID] getUUIDBytes:hello.nonce.data()];
        auto* nonce=[NSData dataWithBytes:hello.nonce.data() length:hello.nonce.size()];
        auto* document=[NSPropertyListSerialization dataWithPropertyList:@{@"nonce":nonce,@"pid":@(getpid())}
            format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
        auto* requestURL=[NSURL fileURLWithPath:[NSString stringWithUTF8String:request.c_str()]];
        if(![document writeToURL:requestURL options:NSDataWritingAtomic error:nil] || chmod(request.c_str(),0600)) return 4;
        auto* helperURL=[NSURL fileURLWithPath:[NSString stringWithUTF8String:launcher.getFullPathName().toRawUTF8()]];
        struct Launch {std::atomic<pid_t> pid{0};std::atomic<bool> complete{false};};
        auto launch=std::make_shared<Launch>();
        auto start=^{
            auto* config=[NSWorkspaceOpenConfiguration configuration];
            config.activates=NO;config.createsNewApplicationInstance=YES;config.allowsRunningApplicationSubstitution=NO;
            [NSWorkspace.sharedWorkspace openURLs:@[requestURL] withApplicationAtURL:helperURL configuration:config
                completionHandler:^(NSRunningApplication* app,NSError* error) {
                    if(!app) NSLog(@"Sound Canvas sandbox helper launch failed: %@ (%ld)",error.domain,(long)error.code);
                    launch->pid.store(app ? app.processIdentifier : -1);launch->complete.store(true);
                }];
        };
        if([NSThread isMainThread]) start();else dispatch_async(dispatch_get_main_queue(),start);
        const auto deadline=juce::Time::getMillisecondCounterHiRes()+macSession::startupTimeoutMs;
        int connection=-1;
        while(juce::Time::getMillisecondCounterHiRes()<deadline) {
            if(launch->complete.load() && launch->pid.load()<=0) return 4;
            if(launch->pid.load()>0 && macSession::wait(server,POLLIN,5)) {
                connection=accept(server,nullptr,nullptr);break;
            }
            if([NSThread isMainThread]) [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.005]];
            else usleep(5000);
        }
        if(connection<0) return 4;
        int yes=1;setsockopt(connection,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
        fcntl(connection,F_SETFD,FD_CLOEXEC);
        uid_t uid{};gid_t gid{};pid_t peer{};socklen_t size=sizeof(peer);macSession::Hello received;
        if(getpeereid(connection,&uid,&gid) || uid!=geteuid() ||
           getsockopt(connection,SOL_LOCAL,LOCAL_PEERPID,&peer,&size) || peer!=launch->pid.load() ||
           !macSession::transfer(connection,&received,sizeof(received),false) || received.marker!=hello.marker || received.nonce!=hello.nonce ||
           !macSession::sendMemory(connection,memory)) {close(connection);return 4;}
        char status=0;
        if(!macSession::transfer(connection,&status,1,false) || status!='L') {close(connection);return status=='M' ? 3 : 4;}
        resultSocket=connection;process=peer;
        NSLog(@"Sound Canvas sandbox engine connected");return 1;
    }
}
}
