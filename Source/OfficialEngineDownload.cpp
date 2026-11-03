#include "OfficialEngine.h"
#include <libunshield.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#include <setupapi.h>
#endif

namespace {
void require(bool value,const juce::String& message) { if(!value) throw std::runtime_error(message.toStdString()); }
void progress(const juce::File& job,const juce::String& phase,int percent) {
    auto value=std::make_unique<juce::DynamicObject>();value->setProperty("phase",phase);value->setProperty("percent",percent);
    require(job.getChildFile("progress.json").replaceWithText(juce::JSON::toString(juce::var(value.release()))),"Could not write download progress.");
}
void download(const juce::File& job,const juce::File& target) {
    int status=0;juce::StringPairArray headers;
    auto input=juce::URL(scbridge::officialEngineUrl).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(20000).withNumRedirectsToFollow(0).withStatusCode(&status).withResponseHeaders(&headers));
    require(input && status==200,"Roland's download is unavailable. Your current engine has not changed; Browse remains available.");
    juce::FileOutputStream output(target);require(output.openedOk(),"Could not create the temporary download.");
    char bytes[65536];int64_t total=0;int last=-1;
    while(!input->isExhausted()) {
        const int count=input->read(bytes,sizeof(bytes));require(count>=0,"Roland's download ended unexpectedly.");
        if(count==0) break;
        total+=count;require(total<=scbridge::officialArchiveBytes,"Roland's download has changed. It has not been installed.");
        require(output.write(bytes,static_cast<size_t>(count)),"Could not save the temporary download.");
        const int percent=static_cast<int>(total*100/scbridge::officialArchiveBytes)/5*5;
        if(percent!=last) { progress(job,"Downloading from Roland",percent);last=percent; }
    }
    output.flush();require(total==scbridge::officialArchiveBytes,"Roland's download was incomplete.");
}
#ifdef _WIN32
struct RuntimeSelection {
    const wchar_t* name;
    const juce::File target;
    const uint32_t size;
    bool found=false;
    DWORD error=ERROR_SUCCESS;
};
UINT CALLBACK runtimeFile(void* context,UINT notification,UINT_PTR first,UINT_PTR) {
    auto& selected=*static_cast<RuntimeSelection*>(context);
    if(notification==SPFILENOTIFY_FILEINCABINET) {
        auto& info=*reinterpret_cast<FILE_IN_CABINET_INFO_W*>(first);
        if(std::wcscmp(info.NameInCabinet,selected.name)!=0) return FILEOP_SKIP;
        const auto path=selected.target.getFullPathName();
        if(selected.found || info.FileSize!=selected.size || selected.target.exists() || std::wcslen(path.toWideCharPointer())>=MAX_PATH) {
            selected.error=ERROR_INVALID_DATA;return FILEOP_ABORT;
        }
        selected.found=true;
        std::wcscpy(info.FullTargetName,path.toWideCharPointer());return FILEOP_DOIT;
    }
    if(notification==SPFILENOTIFY_NEEDNEWCABINET) { selected.error=ERROR_INVALID_DATA;return FILEOP_ABORT; }
    if(notification==SPFILENOTIFY_FILEEXTRACTED) {
        selected.error=reinterpret_cast<FILEPATHS_W*>(first)->Win32Error;
        return selected.error;
    }
    return NO_ERROR;
}
#else
void extractCabinetEntry(const juce::File& cabinet,const char* name,const juce::File& target,int64_t expectedSize) {
    require(!target.exists(),"The runtime extraction destination is occupied.");
    // macOS ships libarchive through bsdtar. Stream one fixed member to our
    // output file; no archive pathname is ever used as a filesystem destination.
    juce::ChildProcess child;
    require(child.start({"/usr/bin/tar","-xOf",cabinet.getFullPathName(),"--",name},juce::ChildProcess::wantStdOut),"Could not open the runtime cabinet.");
    juce::FileOutputStream output(target);require(output.openedOk(),"Could not save the engine runtime.");
    char bytes[65536];int64_t total=0;
    const auto deadline=juce::Time::getMillisecondCounterHiRes()+10000;
    while(child.isRunning()) {
        if(juce::Time::getMillisecondCounterHiRes()>=deadline) {
            child.kill();child.waitForProcessToFinish(1000);require(false,"The runtime extraction timed out.");
        }
        const auto count=child.readProcessOutput(bytes,sizeof(bytes));
        total+=count;
        if(total>expectedSize || !output.write(bytes,static_cast<size_t>(count))) {
            child.kill();child.waitForProcessToFinish(1000);require(false,"Invalid runtime data size.");
        }
    }
    for(;;) {
        const auto count=child.readProcessOutput(bytes,sizeof(bytes));if(count<=0) break;
        total+=count;require(total<=expectedSize && output.write(bytes,static_cast<size_t>(count)),"Invalid runtime data size.");
    }
    require(child.getExitCode()==0 && total==expectedSize,"The required runtime data entry is absent or incomplete.");
}
#endif
void extractRuntime(Unshield* cabinet,const juce::File& job) {
    const auto* group=unshield_file_group_find(cabinet,"Run_Time");
    require(group!=nullptr,"The engine runtime archive is absent.");
    int index=-1;
    for(int i=group->first_file;i<=group->last_file;++i) {
        const auto* name=unshield_file_name(cabinet,i);
        if(name && juce::String(name)==
#ifdef _WIN32
           "vcredist_x64.exe"
#else
           "vcredist_x86.exe"
#endif
           ) { require(index<0,"Duplicate runtime archive.");index=i; }
    }
    require(index>=0 && unshield_file_is_valid(cabinet,index) && unshield_file_size(cabinet,index)==scbridge::officialRedistributableBytes,"Unexpected runtime archive.");
    const auto redist=job.getChildFile("runtime.exe");
    require(!redist.exists() && unshield_file_save(cabinet,index,"runtime.exe") &&
            scbridge::engineHashMatches(redist,scbridge::officialRedistributableHash),"The runtime archive failed verification.");
    const auto outer=job.getChildFile(scbridge::officialRuntimeCab.name);
    {
        juce::FileInputStream input(redist);juce::FileOutputStream output(outer);
        require(input.openedOk() && input.setPosition(scbridge::officialRuntimeCab.offset) && output.openedOk() &&
                output.writeFromInputStream(input,scbridge::officialRuntimeCab.size)==scbridge::officialRuntimeCab.size,"Could not read the runtime cabinet.");
    }
    require(scbridge::engineHashMatches(outer,scbridge::officialRuntimeCab.sha256),"The runtime cabinet failed verification.");
    const auto innerFile=job.getChildFile("runtime-inner.cab"),runtimeFileName=job.getChildFile("MSVCR100.dll");
#ifdef _WIN32
    RuntimeSelection inner{L".\\.\\.\\.\\vc_red.cab",innerFile,static_cast<uint32_t>(scbridge::officialRuntimeInnerBytes)};
    require(SetupIterateCabinetW(outer.getFullPathName().toWideCharPointer(),0,runtimeFile,&inner) && inner.found && !inner.error &&
            scbridge::engineHashMatches(inner.target,scbridge::officialRuntimeInnerHash),"Could not extract the verified runtime data cabinet.");
    RuntimeSelection runtime{L"F_CENTRAL_msvcr100_x64",runtimeFileName,static_cast<uint32_t>(scbridge::officialRuntimeBytes)};
    require(SetupIterateCabinetW(inner.target.getFullPathName().toWideCharPointer(),0,runtimeFile,&runtime) && runtime.found && !runtime.error &&
            scbridge::engineHashMatches(runtime.target,scbridge::officialRuntimeHash),"Could not extract the verified app-local engine runtime.");
#else
    extractCabinetEntry(outer,"././././vc_red.cab",innerFile,scbridge::officialRuntimeInnerBytes);
    require(scbridge::engineHashMatches(innerFile,scbridge::officialRuntimeInnerHash),"The runtime data cabinet failed verification.");
    extractCabinetEntry(innerFile,"F_CENTRAL_msvcr100_x86",runtimeFileName,scbridge::officialRuntimeBytes);
    require(scbridge::engineHashMatches(runtimeFileName,scbridge::officialRuntimeHash),"The engine runtime failed verification.");
    // Keep the dependency outside our bundles. Older front ends import only
    // SCCore.dll, so the helper commits its verified runtime before reporting success.
    const auto profile=juce::SystemStats::getEnvironmentVariable("SCBRIDGE_PROFILE_DIR",{});
    const auto root=juce::File::isAbsolutePath(profile) ? juce::File(profile) :
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Application Support/OnjMusic/SoundCanvasBridge");
    const auto destination=root.getChildFile("Downloaded Engines").getChildFile(scbridge::officialCoreHash).getChildFile("MSVCR100.dll");
    const auto imported=scbridge::importVerifiedEngine(runtimeFileName,destination,scbridge::officialRuntimeHash);
    require(imported.wasOk(),imported.getErrorMessage());
#endif
}
void extract(const juce::File& archive,const juce::File& job) {
    progress(job,"Verifying the download",100);
    require(archive.getSize()==scbridge::officialArchiveBytes && scbridge::engineHashMatches(archive,scbridge::officialArchiveHash),
            "Roland's archive is not the verified version. No engine or settings have changed.");
    juce::ZipFile zip(archive);require(zip.getNumEntries()>0 && zip.getNumEntries()<=100,"Unexpected archive contents.");
    const juce::String installerName="SOUND_Canvas_VA_Win_v103_Trial/SOUND_Canvas_VA_Installer.exe";
    int installerIndex=-1;int64_t expanded=0;
    for(int i=0;i<zip.getNumEntries();++i) {
        const auto& entry=*zip.getEntry(i);const auto& name=entry.filename;
        require(!name.startsWithChar('/') && !name.containsChar('\\') && !name.containsChar(':') &&
                !juce::StringArray::fromTokens(name,"/",{}).contains(".."),"Unsafe archive entry.");
        expanded+=entry.uncompressedSize;require(expanded<=300*1024*1024,"Unexpected expanded archive size.");
        if(name==installerName) { require(installerIndex<0,"Duplicate installer entry.");installerIndex=i; }
    }
    require(installerIndex>=0 && zip.getEntry(installerIndex)->uncompressedSize==scbridge::officialInstallerBytes,"The expected installer is absent.");
    const auto installer=job.getChildFile("payload.exe");
    { std::unique_ptr<juce::InputStream> input(zip.createStreamForEntry(installerIndex));juce::FileOutputStream output(installer);
      require(input && output.openedOk() && output.writeFromInputStream(*input,scbridge::officialInstallerBytes)==scbridge::officialInstallerBytes,"Could not read the installer as an archive."); }
    require(scbridge::engineHashMatches(installer,scbridge::officialInstallerHash),"The installer failed verification.");
    progress(job,"Extracting the engine",100);
    juce::FileInputStream input(installer);require(input.openedOk(),"Could not read the verified installer.");
    for(const auto& payload:scbridge::officialPayloads) {
        require(payload.offset>=0 && payload.size<=installer.getSize()-payload.offset && input.setPosition(payload.offset),"Cabinet range is invalid.");
        const auto file=job.getChildFile(payload.name);
        { juce::FileOutputStream output(file);require(output.openedOk() && output.writeFromInputStream(input,payload.size)==payload.size,"Could not extract the cabinet."); }
        require(scbridge::engineHashMatches(file,payload.sha256),"The cabinet failed verification.");
    }
    unshield_set_log_level(UNSHIELD_LOG_LEVEL_LOWEST);
    // Only this disposable helper changes directory. Short fixed filenames
    // avoid libunshield's legacy path-size and Windows ANSI-path limitations.
    std::unique_ptr<Unshield,decltype(&unshield_close)> cabinet(unshield_open("data1.cab"),unshield_close);
    require(cabinet!=nullptr,"Could not open the InstallShield cabinet.");
    const auto* group=unshield_file_group_find(cabinet.get(),scbridge::officialCoreGroup);require(group!=nullptr,"The platform engine group is absent.");
    int found=-1;
    for(int i=group->first_file;i<=group->last_file;++i) {
        const auto* name=unshield_file_name(cabinet.get(),i);
        if(name && juce::String(name)=="SCCore.dll") { require(found<0,"Duplicate engine entry.");found=i; }
    }
    require(found>=0 && unshield_file_is_valid(cabinet.get(),found) && unshield_file_size(cabinet.get(),found)==scbridge::officialCoreBytes,"Unexpected engine entry.");
    const auto core=job.getChildFile("SCCore.dll");require(!core.exists(),"The extraction destination is occupied.");
    require(unshield_file_save(cabinet.get(),found,"SCCore.dll"),"Engine extraction failed.");
    require(scbridge::engineHashMatches(core,scbridge::officialCoreHash),"The extracted engine failed verification.");
    extractRuntime(cabinet.get(),job);
    progress(job,"Engine verified",100);
}
}
int downloaderMain(int argc,char** argv) {
    auto result=std::make_unique<juce::DynamicObject>();int code=1;
    try {
        require((argc==3 && std::string(argv[1])=="--download") ||
                (argc==4 && std::string(argv[1])=="--extract-archive"),"Unsupported engine download command.");
        const juce::File job(argv[argc-1]);
        require(job.isDirectory() && !scbridge::enginePathIsRedirected(job) && job.findChildFiles(juce::File::findFilesAndDirectories,false).isEmpty(),"Expected a fresh download directory.");
        const auto archive=argc==4 ? juce::File(argv[2]) : job.getChildFile("roland-trial.zip");
        require(job.setAsCurrentWorkingDirectory(),"Could not enter the temporary extraction directory.");
        if(argc==3) download(job,archive);
        extract(archive,job);result->setProperty("status","ok");code=0;
    } catch(const std::exception& error) { result->setProperty("status","error");result->setProperty("message",juce::String(error.what())); }
    std::cout<<juce::JSON::toString(juce::var(result.release()),true)<<std::endl;return code;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    std::vector<std::string> arguments;arguments.reserve(static_cast<size_t>(argc));
    for(int i=0;i<argc;++i) arguments.push_back(juce::String(argv[i]).toStdString());
    std::vector<char*> pointers;pointers.reserve(arguments.size());
    for(auto& value:arguments) pointers.push_back(value.data());
    return downloaderMain(argc,pointers.data());
}
#else
int main(int argc,char** argv) { return downloaderMain(argc,argv); }
#endif
