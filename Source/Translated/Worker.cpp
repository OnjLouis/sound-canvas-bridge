#include "Protocol.h"
#include "CoreLayouts.h"
#include "WorkerScheduling.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <poll.h>
#include <pthread.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <CommonCrypto/CommonDigest.h>


extern int boxedmain(int, const char**);
extern "C" void SDL_SetMainReady(void);
namespace fs = std::filesystem;

static void pruneFinishedInstances(const fs::path& cache) {
    unsigned inspected=0,removed=0;
    for(const auto& entry:fs::directory_iterator(cache)) {
        if(++inspected>512 || removed>=32) break;
        const auto name=entry.path().filename().string();
        if(name.rfind("ipc-",0)!=0 || name.size()<=4) continue;
        const auto separator=name.find_first_not_of("0123456789",4);
        if(separator==4 || (separator!=std::string::npos && name[separator]!='-')) continue;
        const auto pid=std::strtoll(name.c_str()+4,nullptr,10);
        if(pid<=0 || pid>INT32_MAX || kill(static_cast<pid_t>(pid),0)==0 || errno!=ESRCH) continue;
        struct stat info{};
        const auto marker=entry.path()/"bridge-owned";
        if(lstat(entry.path().c_str(),&info) || !S_ISDIR(info.st_mode) || info.st_uid!=getuid() ||
           fs::is_symlink(marker) || !fs::is_regular_file(marker) || fs::file_size(marker)!=sizeof(uint32_t)) continue;
        uint32_t version=0;FILE* file=std::fopen(marker.c_str(),"rb");
        if(!file) continue;
        const bool valid=std::fread(&version,sizeof(version),1,file)==1 && version==scbridge::protocolVersion;
        std::fclose(file);if(!valid) continue;
        bool safe=true;unsigned children=0;
        for(const auto& child:fs::recursive_directory_iterator(entry.path()))
            if(++children>4096 || child.is_symlink()) {safe=false;break;}
        if(!safe) continue;
        std::error_code error;fs::remove_all(entry.path(),error);if(!error) ++removed;
    }
}

static std::string verifiedCore(const fs::path& path) {
    FILE* file=std::fopen(path.c_str(),"rb");if(!file)return {};
    CC_SHA256_CTX hash;CC_SHA256_Init(&hash);
    unsigned char bytes[65536],digest[32];size_t total=0,count=0;
    while((count=std::fread(bytes,1,sizeof(bytes),file))) {
        total+=count;
        if(total>32*1024*1024) {std::fclose(file);return {};}
        CC_SHA256_Update(&hash,bytes,static_cast<CC_LONG>(count));
    }
    const bool ok=!std::ferror(file);std::fclose(file);
    CC_SHA256_Final(digest,&hash);
    char text[65]{};
    for(size_t i=0;i<32;++i)std::snprintf(text+i*2,3,"%02x",digest[i]);
    return ok && scbridge::windowsCoreLayout(text,32) ? std::string(text) : std::string{};
}

static bool transfer(int descriptor, void* data, size_t bytes, bool writing, int timeout = 5000) {
    auto* cursor = static_cast<unsigned char*>(data);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while (bytes) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) return false;
        pollfd event{descriptor, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
        const int ready = poll(&event, 1, static_cast<int>(remaining));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0 || !(event.revents & event.events)) return false;
        const ssize_t count = writing ? write(descriptor, cursor, bytes) : read(descriptor, cursor, bytes);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count <= 0) return false;
        cursor += count; bytes -= static_cast<size_t>(count);
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    for(int fd=0;fd<3;++fd) if(fcntl(fd,F_GETFD)<0) {
        const int replacement=open("/dev/null",fd ? O_WRONLY : O_RDONLY);
        if(replacement<0) return 9;
        if(replacement!=fd) {dup2(replacement,fd);close(replacement);}
    }
    using namespace scbridge;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    signal(SIGPIPE, SIG_IGN);
    auto* shared = static_cast<Shared*>(mmap(nullptr, sizeof(Shared), PROT_READ|PROT_WRITE, MAP_SHARED, 4, 0));
    if (shared == MAP_FAILED) return 3;
    uint32_t size = 8192;
    std::vector<char> executable(size);
    if (_NSGetExecutablePath(executable.data(), &size)) return 4;
    const auto resources = fs::canonical(executable.data()).parent_path();
    const auto guestResources = resources / "guest";
    const auto runtime = resources / "boxedwine.zip";
    const auto selectedCore=fs::path(argv[1]);
    const auto selectedHash=verifiedCore(selectedCore);
    if(!selectedCore.is_absolute() || selectedHash.empty()) {
        std::fprintf(stderr,"Unsupported or missing selected 32-bit Sound Canvas engine\n");return 10;
    }
    if (!fs::is_regular_file(guestResources / "SoundCanvasGuest.exe") || !fs::is_regular_file(runtime)) return 5;
    const char* home=getenv("HOME");if(!home || !*home)return 11;
    const char* profile=getenv("SCBRIDGE_PROFILE_DIR");
    const auto cache=profile && fs::path(profile).is_absolute() ? fs::path(profile)/"Worker Cache" :
        fs::path(home)/"Library/Caches/OnjMusic/SoundCanvasExperimental";
    for(auto parent=cache;!parent.empty() && parent!=parent.parent_path();parent=parent.parent_path())
        if(fs::is_symlink(parent)) return 13;
    fs::create_directories(cache);fs::permissions(cache,fs::perms::owner_all);
    pruneFinishedInstances(cache);
    auto pattern=(cache/("ipc-"+std::to_string(getpid())+"-XXXXXX")).string();
    if(!mkdtemp(pattern.data())) return 14;
    const fs::path instance=pattern;
    FILE* marker=std::fopen((instance/"bridge-owned").c_str(),"wb");
    if(!marker) return 12;
    const bool marked=std::fwrite(&protocolVersion,sizeof(protocolVersion),1,marker)==1;
    std::fclose(marker);if(!marked) return 12;
    const auto guest=instance/"guest";
    fs::create_directory(guest);fs::permissions(guest,fs::perms::owner_all);
    fs::copy_file(guestResources/"SoundCanvasGuest.exe",guest/"SoundCanvasGuest.exe");
    // Runtime DLLs belong to the user's engine installation, never our package.
    for(const auto* name:{"MSVCR100.dll","vcruntime140.dll"}) {
        const auto dependency=selectedCore.parent_path()/name;
        if(fs::is_regular_file(dependency)) fs::copy_file(dependency,guest/name);
    }
    const auto root = instance / "root";
    const auto schema = root / "opt/wine/lib/wine/i386-windows/apisetschema.dll";
    fs::create_directories(schema.parent_path());
    fs::copy_file(resources / "apisetschema.dll", schema);
    const auto commands = instance / "commands", responses = instance / "responses";
    if (mkfifo(commands.c_str(), 0600) || mkfifo(responses.c_str(), 0600)) return 6;
    const int input = open(commands.c_str(), O_RDWR);
    const int output = open(responses.c_str(), O_RDWR);
    if (input < 0 || output < 0) return 7;
    setenv("SDL_AUDIODRIVER", "dummy", 1); setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_SetMainReady();
    std::atomic<bool> completed{false};
    std::thread runtimeThread([&] {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        const std::string ipc = "e:\\";
        std::vector<std::string> args{"boxedwine", "-root", root.string(), "-zip", runtime.string(),
            "-mount_drive", guest.string(), "d", "-mount_drive", instance.string(), "e",
            "-mount_drive", selectedCore.parent_path().string(), "f",
            "-env", "WINEDEBUG=-all", "-env", "SCBRIDGE_VERIFIED_CORE_SHA256="+selectedHash, "-novideo", "/bin/wine",
            "d:\\SoundCanvasGuest.exe", "f:\\"+selectedCore.filename().string(), ipc + "commands", ipc + "responses"};
        std::vector<const char*> values;
        for (const auto& arg : args) values.push_back(arg.c_str());
        const auto status=boxedmain(static_cast<int>(values.size()), values.data());
        std::fprintf(stderr,"Experimental guest runtime ended: %d\n",status);
        completed.store(true);
    });
    char reply = 0;
    bool ok = transfer(output, &reply, 1, false);
    std::fprintf(stderr,"Guest ready transport=%d byte=%d\n",ok,int(reply));
    const bool transportRealtime=ok && configureAudioWorker(shared->sampleRate,128);
    std::fprintf(stderr,"Native transport realtime=%d\n",transportRealtime);
    ok=ok && reply=='R' && transfer(3,&reply,1,true);
    std::fprintf(stderr,"Parent ready transport=%d\n",ok);
    Shared response{};
    while (ok) {
        char command = 0;
        if (read(3, &command, 1) != 1) break;
        if (command == 'Q') break;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (shared->version != protocolVersion || shared->frames > maxFrames ||
            shared->eventCount > maxEvents || shared->midiBytes > maxMidiBytes) break;
        ok = transfer(input, &command, 1, true) && transfer(input, shared, offsetof(Shared, events), true) &&
            transfer(input, shared->events, shared->eventCount*sizeof(Event), true) &&
            transfer(input, shared->midi, shared->midiBytes, true) && transfer(output, &reply, 1, false) && reply == 'D' &&
            transfer(output, &response, offsetof(Shared, events), false) &&
            transfer(output, shared->left, shared->frames*sizeof(float), false) &&
            transfer(output, shared->right, shared->frames*sizeof(float), false);
        if(command=='S') std::fprintf(stderr,"Parent S exchange=%d reply=%d\n",ok,int(reply));
        if (!ok) break;
        shared->result = response.result; shared->helperRealtime = response.helperRealtime;
        shared->advancedReady = response.advancedReady;
        shared->capturedSceneValid = response.capturedSceneValid;
        shared->capturedScene = response.capturedScene;
        std::copy(std::begin(response.reported), std::end(response.reported), shared->reported);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        ok = transfer(3, &reply, 1, true);
    }
    char quit = 'Q'; transfer(input, &quit, 1, true, 250);
    for (int i = 0; i < 200 && !completed.load(); ++i) usleep(10000);
    if (!completed.load()) {
        runtimeThread.detach();
        // The worker owns no user files; OS teardown releases the guest threads.
        _exit(ok ? 0 : 8);
    }
    runtimeThread.join();
    close(input); close(output); munmap(shared, sizeof(Shared));
    fs::remove_all(instance);
    return ok ? 0 : 8;
}
