#pragma once
#include <shobjidl.h>
namespace scbridge {
class StartupService {
public:
    explicit StartupService(juce::File directory=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Microsoft/Windows/Start Menu/Programs/Startup"),juce::String arguments={})
        :file(directory.getChildFile("Sound Canvas Bridge.lnk")),launchArguments(std::move(arguments)) {}
    static bool addDesktopShortcut(const juce::File& app,juce::String& error,
        juce::File desktop=juce::File::getSpecialLocation(juce::File::userDesktopDirectory)) {
        return StartupService(std::move(desktop),"--show-controls").setEnabled(true,app,error);
    }
    bool enabled() const { return file.existsAsFile(); }
    bool setEnabled(bool enabled,const juce::File& app,juce::String& error) {
        error.clear();
        if(file.isSymbolicLink() || file.getParentDirectory().getLinkedTarget()!=file.getParentDirectory()) {
            error="The shortcut location is redirected. Nothing was changed.";return false;
        }
        IShellLinkW* link=nullptr;
        if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link)))) {
            error="Windows could not open the shortcut service.";return false;
        }
        struct Release { IShellLinkW* p;~Release(){p->Release();} } release{link};
        IPersistFile* persistence=nullptr;
        if(FAILED(link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&persistence)))) return false;
        struct ReleaseFile { IPersistFile* p;~ReleaseFile(){p->Release();} } releaseFile{persistence};
        if(file.existsAsFile()) {
            wchar_t target[32768]{},arguments[32768]{};
            if(FAILED(persistence->Load(file.getFullPathName().toWideCharPointer(),STGM_READ)) ||
               FAILED(link->GetPath(target,32768,nullptr,SLGP_RAWPATH)) || juce::File(juce::String(target))!=app ||
               FAILED(link->GetArguments(arguments,32768)) || juce::String(arguments)!=launchArguments) {
                error="The existing shortcut has a different target or arguments. Nothing was changed.";return false;
            }
        }
        if(!enabled) {
            if(!file.existsAsFile() || file.deleteFile()) return true;
            error="Could not remove the startup shortcut.";return false;
        }
        if(!app.existsAsFile() || !app.hasFileExtension("exe")) {
            error="Run the installed application before enabling Run at login.";return false;
        }
        if(file.existsAsFile()) return true;
        if(!file.getParentDirectory().createDirectory().wasOk() ||
           FAILED(link->SetPath(app.getFullPathName().toWideCharPointer())) ||
           FAILED(link->SetArguments(launchArguments.toWideCharPointer())) ||
           FAILED(link->SetWorkingDirectory(app.getParentDirectory().getFullPathName().toWideCharPointer())) ||
           FAILED(link->SetDescription(L"Sound Canvas Bridge")) ||
           FAILED(persistence->Save(file.getFullPathName().toWideCharPointer(),TRUE))) {
            error="Could not save the startup shortcut.";return false;
        }
        return true;
    }
private:
    juce::File file;
    juce::String launchArguments;
};
}
