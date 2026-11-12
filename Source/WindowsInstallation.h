#pragma once
#include <shlobj.h>

namespace scbridge {
class WindowsInstallation {
public:
    static juce::File localDirectory() {
        PWSTR path=nullptr;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path))) return {};
        const juce::File base{juce::String(path)};CoTaskMemFree(path);
        return base.getChildFile("Programs/OnjMusic").getChildFile(installationFolderName());
    }
    static juce::File defaultDirectory() {
        const auto shared=juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory).getChildFile("OnjMusic").getChildFile(installationFolderName());
        return WindowsInstallation(shared).existingExecutable()!=juce::File{} ? shared : localDirectory();
    }
    explicit WindowsInstallation(juce::File directory=defaultDirectory()):destination(std::move(directory)) {}
    juce::File executable() const { return destination==juce::File{} ? juce::File{} : destination.getChildFile("Sound Canvas Bridge.exe"); }
    bool isInstalled(const juce::File& app) const { return executable()!=juce::File{} && app==executable(); }
    juce::File existingExecutable() const {
        if(destination==juce::File{} || !safeAncestors(destination)) return {};
        for(const auto& name:requiredFiles()) {
            const auto file=destination.getChildFile(name);
            if(!safeAncestors(file) || !file.existsAsFile() || file.getSize()<=0 || file.getSize()>maxFileSize) return {};
        }
        if(destination.getChildFile("SoundCanvasMaintenance.exe").existsAsFile()!=destination.getChildFile("WindowsUpdateKey.xml").existsAsFile()) return {};
        return executable();
    }
    juce::Result install(const juce::File& app) const {
        if(destination==juce::File{} || !safeAncestors(destination)) return juce::Result::fail("The local installation folder is unavailable or redirected.");
        if(isInstalled(app)) return app.existsAsFile() ? juce::Result::ok() : juce::Result::fail("The installed application is missing.");
        const auto source=app.getParentDirectory();
        const juce::StringArray optional{"SoundCanvasMidiHost.exe","Windows.Devices.Midi2.dll","SoundCanvasMaintenance.exe","WindowsUpdateKey.xml",
            "SoundCanvasEngineDownload.exe","Unshield Licence.txt","Zlib Licence.txt"
#ifdef SCBRIDGE_WINDOWS7
            ,"SoundCanvasUpdateCheck.exe"
#endif
        };
        if(source.getChildFile("SoundCanvasEngineDownload.exe").existsAsFile() &&
           (!source.getChildFile("Unshield Licence.txt").existsAsFile() || !source.getChildFile("Zlib Licence.txt").existsAsFile()))
            return juce::Result::fail("The engine download helper's licence files are missing.");
        auto names=requiredFiles();
        for(const auto& name:optional) if(source.getChildFile(name).existsAsFile()) names.add(name);
        if(source.getChildFile("SoundCanvasMaintenance.exe").existsAsFile()!=source.getChildFile("WindowsUpdateKey.xml").existsAsFile())
            return juce::Result::fail("The application's update files are incomplete.");
        struct Handles {
            std::vector<HANDLE> values;
            ~Handles() { for(auto value:values) CloseHandle(value); }
        } handles;
        for(const auto& name:names) {
            const auto file=source.getChildFile(name);
            if(!file.existsAsFile() || file.isSymbolicLink() || file.getSize()<=0 || file.getSize()>maxFileSize)
                return juce::Result::fail("The application package is missing or has an invalid file: "+name);
            const auto handle=CreateFileW(file.getFullPathName().toWideCharPointer(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            if(handle==INVALID_HANDLE_VALUE) return juce::Result::fail("An application file is busy. Try again after its update has finished.");
            handles.values.push_back(handle);
        }
        if(destination.exists()) {
            for(const auto& name:names) if(!source.getChildFile(name).hasIdenticalContentTo(destination.getChildFile(name)))
                return juce::Result::fail("A different local installation already exists. Update that copy before enabling this option.");
            return juce::Result::ok();
        }
        if(!destination.getParentDirectory().createDirectory().wasOk()) return juce::Result::fail("Could not create the local application folder.");
        const auto staging=destination.getSiblingFile(".SoundCanvasBridge-"+juce::Uuid().toString()+".staging");
        if(!staging.createDirectory().wasOk()) return juce::Result::fail("Could not prepare the local installation.");
        auto fail=[&](const juce::String& message) {
            for(const auto& name:names) staging.getChildFile(name).deleteFile();
            staging.deleteFile();
            return juce::Result::fail(message);
        };
        for(const auto& name:names) {
            const auto original=source.getChildFile(name),copy=staging.getChildFile(name);
            if(!original.copyFileTo(copy) || !original.hasIdenticalContentTo(copy)) return fail("Could not copy and verify "+name+". The existing installation was not changed.");
        }
        if(!safeAncestors(destination) || !MoveFileExW(staging.getFullPathName().toWideCharPointer(),destination.getFullPathName().toWideCharPointer(),MOVEFILE_WRITE_THROUGH))
            return fail("Could not complete the local installation. Nothing was overwritten.");
        return juce::Result::ok();
    }
private:
    static constexpr const char* installationFolderName() {
#ifdef SCBRIDGE_WINDOWS7
        return "Sound Canvas Bridge Windows 7";
#else
        return "Sound Canvas Bridge";
#endif
    }
    static constexpr int64_t maxFileSize=64*1024*1024;
    static juce::StringArray requiredFiles() { return {"Sound Canvas Bridge.exe","SoundCanvasHelper.exe","Manual.html","VersionHistory.html"}; }
    static bool safeAncestors(juce::File path) {
        for(;;) {
            const auto attributes=GetFileAttributesW(path.getFullPathName().toWideCharPointer());
            if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) return false;
            const auto parent=path.getParentDirectory();
            if(parent==path) return true;
            path=parent;
        }
    }
    juce::File destination;
};
}
