#pragma once
#include "EngineLocation.h"
#include "TransientTooltip.h"
#if defined(_WIN32) || defined(SCBRIDGE_TRANSLATED_ENGINE)
#include "EngineDownloadService.h"
#endif

namespace scbridge {
class EnginePreferences final : public juce::Component,private juce::Timer {
public:
    explicit EnginePreferences(std::function<void()> changed={}) :onEngineChanged(std::move(changed)) {
        setName("Sound Canvas engine");setTitle(getName());setComponentID("enginePreferences");
        setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
        engineHeading.setText("Sound Canvas VA location",juce::dontSendNotification);engineHeading.setAccessible(false);
        enginePath.setName("Sound Canvas VA location");enginePath.setTitle(enginePath.getName());enginePath.setComponentID("engineLocation");
        enginePath.setReadOnly(true);enginePath.setMultiLine(false);enginePath.setTabKeyUsedAsCharacter(false);enginePath.setExplicitFocusOrder(1);
        enginePath.setDescription(enginePath.getName());enginePath.setHelpText("Location of the original Sound Canvas VA engine. Select and copy this path, or use Browse to choose another installation.");
        enginePath.setTooltip(enginePath.getHelpText());
        browse.setButtonText("Browse...");browse.setName("Browse for Sound Canvas VA");browse.setExplicitFocusOrder(2);
        browse.setDescription(browse.getName());browse.setHelpText("Choose the original Sound Canvas VA engine, not the bridge. A valid selection is saved and offers an engine restart.");
        browse.setTooltip(browse.getHelpText());browse.onClick=[this] { chooseEngine(); };
#ifdef _WIN64
        enginePath.setHelpText("The 64-bit Windows Sound Canvas VA SCCore.dll. This selection is shared by the standalone and VST3 and stays outside the bridge installation.");
        enginePath.setTooltip(enginePath.getHelpText());
        browse.setHelpText("Choose your own 64-bit SCCore.dll, not the bridge VST3 or a 32-bit engine. Reload existing instances after changing it.");
        browse.setTooltip(browse.getHelpText());
#elif defined(_WIN32)
        enginePath.setHelpText("The 32-bit Windows Sound Canvas VA SCCore.dll. This selection is shared by the standalone and VST3 and stays outside the bridge installation.");
        enginePath.setTooltip(enginePath.getHelpText());
        browse.setHelpText("Choose your own 32-bit SCCore.dll, not the bridge VST3 or a 64-bit engine. Reload existing instances after changing it.");
        browse.setTooltip(browse.getHelpText());
#elif defined(SCBRIDGE_TRANSLATED_ENGINE)
        enginePath.setHelpText("The 32-bit Windows Sound Canvas VA SCCore.dll used by the translated engine. This selection is shared by the standalone, AU and VST3. The DLL is kept outside the bridge bundles.");
        enginePath.setTooltip(enginePath.getHelpText());
        browse.setHelpText("Choose your own 32-bit SCCore.dll. A 64-bit DLL or Mac VST cannot be used by this backend. Loading a new engine requires restarting existing instances.");
        browse.setTooltip(browse.getHelpText());
#endif
        engineStatus.setName("Engine location status");engineStatus.setTitle(engineStatus.getName());engineStatus.setWantsKeyboardFocus(true);engineStatus.setExplicitFocusOrder(4);
#if defined(_WIN32) || defined(SCBRIDGE_TRANSLATED_ENGINE)
        download.setButtonText("Download from Roland...");download.setName("Download from Roland");download.setComponentID("downloadEngine");
        download.setExplicitFocusOrder(3);engineStatus.setExplicitFocusOrder(4);
        download.setHelpText("Downloads and extracts the verified engine from Roland's official trial archive without running its installer. Roland may change or remove this download. Browse remains available.");
        download.setDescription(download.getName());download.setTooltip(download.getHelpText());
        download.onClick=[this] { downloadEngine(); };
        downloadProgress.setName("Engine download progress");downloadProgress.setWantsKeyboardFocus(false);
        downloadProgress.setVisible(false);addAndMakeVisible(download);addChildComponent(downloadProgress);
#endif
        for(auto* control:std::initializer_list<juce::Component*>{&engineHeading,&enginePath,&browse,&engineStatus}) addAndMakeVisible(control);
        updateEnginePath();setSize(626,190);startTimer(150);
    }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(*this,juce::AccessibilityRole::group);
    }
    void resized() override {
        engineHeading.setBounds(0,0,getWidth(),24);enginePath.setBounds(0,30,getWidth(),32);
        browse.setBounds(0,72,108,32);engineStatus.setBounds(0,112,getWidth(),66);
#if defined(_WIN32) || defined(SCBRIDGE_TRANSLATED_ENGINE)
        download.setBounds(120,72,220,32);
        downloadProgress.setBounds(354,76,std::max(0,getWidth()-354),24);
#endif
    }
private:
#if defined(_WIN32) || defined(SCBRIDGE_TRANSLATED_ENGINE)
    void downloadEngine() {
        if(downloader.busy()) { downloader.cancel();return; }
        juce::Component::SafePointer<EnginePreferences> safe(this);
        juce::AlertWindow::showYesNoCancelBox(juce::MessageBoxIconType::QuestionIcon,"Download Sound Canvas Engine",
            "Download the engine from Roland's official Sound Canvas VA trial archive? Roland's software licence applies. You can read it on Roland's download page before proceeding.\n\nThe Windows installer will be read as an archive, not run. Only the verified engine and any required app-local runtime are retained. Your current engine stays available. The official download may change or disappear; Browse will remain available.",
            "Agree and Download","Read Roland Licence","Cancel",nullptr,juce::ModalCallbackFunction::create([safe](int choice) {
                if(!safe) return;
                if(choice==2) { juce::URL(scbridge::officialEngineLicence).launchInDefaultBrowser();return; }
                if(choice!=1 || safe->downloader.busy()) return;
                safe->downloader.start([safe](juce::Result result,juce::File engine) {
                    if(!safe) return;
                    bool sameEngine=false;
                    if(result.wasOk()) {
                        scbridge::EngineLocation location;juce::String error;
                        sameEngine=location.bundle()==engine;
                        if(!location.save(engine,error)) result=juce::Result::fail(error);
                    }
                    safe->downloadProgress.setVisible(false);safe->browse.setEnabled(true);
                    safe->download.setButtonText("Download from Roland...");safe->download.setName("Download from Roland");
                    safe->lastDownloadPhase.clear();
                    if(result.failed()) {
                        safe->engineStatus.setText(result.getErrorMessage().contains("cancelled") ? "Download cancelled. Your current engine is unchanged." :
                            "Download failed. Your current engine is unchanged.",juce::dontSendNotification);
                        juce::Logger::writeToLog("Engine download: "+result.getErrorMessage());
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Engine Download",result.getErrorMessage());
                        return;
                    }
                    safe->updateEnginePath();
                    juce::AccessibilityHandler::postAnnouncement("Sound Canvas engine downloaded and verified",juce::AccessibilityHandler::AnnouncementPriority::medium);
                    if(!sameEngine && safe->onEngineChanged) safe->onEngineChanged();
                });
                safe->browse.setEnabled(false);safe->downloadProgress.setVisible(true);
                safe->download.setButtonText("Cancel download");safe->download.setName("Cancel download");
            }));
    }
#endif
    void updateEnginePath() {
        scbridge::EngineLocation location;const auto path=location.bundle();juce::String error;
        enginePath.setText(path.getFullPathName(),false);
        engineStatus.setText(scbridge::EngineLocation::validate(path,error) ? "Sound Canvas VA installation found." : "Engine not found. Choose Download from Roland or Browse.",juce::dontSendNotification);
    }
    void chooseEngine() {
        chooser=std::make_unique<juce::FileChooser>("Choose Sound Canvas VA",scbridge::EngineLocation().bundle().getParentDirectory(),
#if defined(_WIN32) || defined(SCBRIDGE_TRANSLATED_ENGINE)
            "SCCore.dll"
#else
            "*.vst"
#endif
        );
        juce::Component::SafePointer<EnginePreferences> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
            [safe](const juce::FileChooser& dialog) {
                if(!safe || dialog.getResult()==juce::File()) return;
                scbridge::EngineLocation location;juce::String error;
                if(!location.save(dialog.getResult(),error)) {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Sound Canvas VA location",error);return;
                }
                safe->updateEnginePath();if(safe->onEngineChanged) safe->onEngineChanged();
            });
    }
    void timerCallback() override {
#if defined(_WIN32) || defined(SCBRIDGE_TRANSLATED_ENGINE)
        if(downloader.busy()) {
            const auto progress=downloader.snapshot();downloadValue=progress.progress;
            engineStatus.setText(progress.phase,juce::dontSendNotification);
            if(progress.phase!=lastDownloadPhase) {
                lastDownloadPhase=progress.phase;
                juce::AccessibilityHandler::postAnnouncement(progress.phase,juce::AccessibilityHandler::AnnouncementPriority::medium);
            }
        }
#endif
    }
    std::function<void()> onEngineChanged;
    juce::Label engineHeading,engineStatus;
    juce::TextEditor enginePath;
    juce::TextButton browse;
#if defined(_WIN32) || defined(SCBRIDGE_TRANSLATED_ENGINE)
    scbridge::EngineDownloadService downloader;
    juce::TextButton download;
    double downloadValue=-1;
    juce::ProgressBar downloadProgress{downloadValue};
    juce::String lastDownloadPhase;
#endif
    std::unique_ptr<juce::FileChooser> chooser;

};
}
