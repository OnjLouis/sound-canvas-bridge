#pragma once
#include "AppPreferences.h"
#include "StartupService.h"
#include "UpdateService.h"
#include "TransientTooltip.h"
#ifdef _WIN32
#include "WindowsInstallation.h"
#endif

namespace scbridge {
class GeneralPreferences final : public juce::Component,private juce::Timer {
public:
    GeneralPreferences(AppPreferences& value,StartupService& login,UpdateService& updater)
        :preferences(value),startup(login),updates(updater) {
        setName("General preferences");setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        frequencyLabel.setText("Check for updates",juce::dontSendNotification);frequencyLabel.setAccessible(false);
        frequency.addItemList({"Never","At startup","Hourly","Daily"},1);
        frequency.setSelectedId(static_cast<int>(preferences.frequency()),juce::dontSendNotification);
        describe(frequency,"Check for updates","Never disables automatic checks. At startup checks on each launch. Hourly and Daily also check on launch when overdue. Updates never install automatically.",1);
        frequency.setComponentID("updateFrequency");
        frequency.onChange=[this] {
            if(!preferences.setFrequency(static_cast<CheckFrequency>(frequency.getSelectedId()))) { showError("Could not save the update frequency.");return; }
            updates.preferencesChanged();
        };
        check.setButtonText("Check for Updates...");describe(check,"Check for Updates","Check GitHub now, regardless of the automatic schedule. Shift+F1 also checks for updates.",2);
        check.onClick=[this] { updates.checkNow(); };
        runLogin.setButtonText("Run at login");runLogin.setToggleState(startup.enabled(),juce::dontSendNotification);
        describe(runLogin,"Run at login","Start Sound Canvas Bridge when you sign in. Off by default. Disabling this removes only the bridge's own startup item.",3);
        runLogin.setComponentID("runAtLogin");
        runLogin.onClick=[this] {
#ifdef _WIN32
            if(runLogin.getToggleState()) { useLocalApplication(true);return; }
            juce::String error;
            const auto current=juce::File::getSpecialLocation(juce::File::currentExecutableFile);
            if(!startup.setEnabled(false,WindowsInstallation().executable(),error) && !startup.setEnabled(false,current,error)) showError(error);
#else
            juce::String error;
            const auto app=juce::File::getSpecialLocation(juce::File::currentApplicationFile);
            if(!startup.setEnabled(runLogin.getToggleState(),app,error)) showError(error);
#endif
            runLogin.setToggleState(startup.enabled(),juce::dontSendNotification);
        };
        const char* hiddenLabel=
#ifdef _WIN32
            "Start hidden in system tray";
#else
            "Start hidden in menu bar";
#endif
        hidden.setButtonText(hiddenLabel);hidden.setToggleState(preferences.startHidden(),juce::dontSendNotification);
        describe(hidden,hiddenLabel,"Keep the controls hidden when the app starts, with saved MIDI and audio settings active. Open the status icon or launch the app again to restore them. Settings are shown if audio or the engine cannot start.",4);
        hidden.setComponentID("startHidden");
        hidden.onClick=[this] { if(!preferences.setStartHidden(hidden.getToggleState())) { hidden.setToggleState(preferences.startHidden(),juce::dontSendNotification);showError("Could not save the startup setting."); } };
        status.setName("Update status");status.setTitle(status.getName());status.setWantsKeyboardFocus(true);status.setExplicitFocusOrder(5);
        for(auto* control:std::initializer_list<juce::Component*>{&frequencyLabel,&frequency,&check,&runLogin,&hidden,&status}) addAndMakeVisible(control);
#ifdef _WIN32
        desktop.setButtonText("Add desktop shortcut");desktop.setName("Add desktop shortcut");desktop.setComponentID("desktopShortcut");
        desktop.setWantsKeyboardFocus(true);desktop.setExplicitFocusOrder(5);status.setExplicitFocusOrder(6);
        desktop.onClick=[this] { useLocalApplication(false); };addAndMakeVisible(desktop);
#endif
        setSize(650,630);startTimer(500);timerCallback();
    }
    void resized() override {
        frequencyLabel.setBounds(12,16,150,32);frequency.setBounds(170,16,getWidth()-182,32);
        check.setBounds(170,64,210,32);runLogin.setBounds(12,124,getWidth()-24,32);
        hidden.setBounds(12,170,getWidth()-24,32);status.setBounds(12,222,getWidth()-24,100);
#ifdef _WIN32
        desktop.setBounds(12,220,210,32);status.setBounds(12,266,getWidth()-24,100);
#endif
    }
#ifdef _WIN32
    ~GeneralPreferences() override { installationWorkers.removeAllJobs(false,-1); }
#endif
private:
#ifdef _WIN32
    void completeLocalAction(bool login,const juce::File& app) {
        juce::String error;
        const bool ok=login ? startup.setEnabled(true,app,error) : StartupService::addDesktopShortcut(app,error);
        runLogin.setToggleState(startup.enabled(),juce::dontSendNotification);
        actionStatus=ok ? (login ? "Run at login enabled for the local installation." : "Desktop shortcut added.") : error;
        if(!ok) showError(error);
        timerCallback();
    }
    void useLocalApplication(bool login) {
        if(installing) return;
        const auto app=juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        WindowsInstallation installation;
        if(const auto local=installation.existingExecutable();local!=juce::File{}) { completeLocalAction(login,local);return; }
        if(installation.isInstalled(app)) { showError("The local installation is incomplete. Repair it before adding shortcuts.");return; }
        runLogin.setToggleState(startup.enabled(),juce::dontSendNotification);
        juce::Component::SafePointer<GeneralPreferences> safe(this);
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,"Install a local copy?",
            "This option needs a local installation so it remains available when a portable drive is removed.\n\nInstall the bridge in "+
            installation.executable().getParentDirectory().getFullPathName()+
            "? Your portable copy and settings will be kept. Roland files are not copied; the engine must remain available at its configured location.",
            "Install Locally","Cancel",this,juce::ModalCallbackFunction::create([safe,installation,app,login](int choice) {
                if(!choice || !safe) return;
                safe->installing=true;safe->actionStatus="Installing and verifying the local copy...";safe->timerCallback();
                juce::Logger::writeToLog("Local application installation requested");
                safe->installationWorkers.addJob([safe,installation,app,login] {
                    const auto result=installation.install(app);
                    juce::Logger::writeToLog(result.wasOk() ? "Local application copy verified" : "Local application installation failed: "+result.getErrorMessage());
                    juce::MessageManager::callAsync([safe,installation,login,result] {
                        if(!safe) return;
                        safe->installing=false;
                        if(result.failed()) { safe->actionStatus=result.getErrorMessage();showError(result.getErrorMessage());safe->timerCallback();return; }
                        safe->completeLocalAction(login,installation.executable());
                    });
                });
            }));
    }
#endif
    template<class Control> static void describe(Control& control,const char* name,const char* help,int order) {
        control.setName(name);control.setTitle(name);control.setDescription(name);control.setHelpText(help);control.setTooltip(help);
        control.setWantsKeyboardFocus(true);control.setExplicitFocusOrder(order);
    }
    void timerCallback() override {
        const auto text=
#ifdef _WIN32
            actionStatus.isNotEmpty() ? actionStatus :
#endif
            updates.status().substring(0,350);if(status.getText()!=text) status.setText(text,juce::dontSendNotification);
        check.setEnabled(!updates.busy());
#ifdef _WIN32
        runLogin.setEnabled(!installing);desktop.setEnabled(!installing);
#endif
    }
    static void showError(const juce::String& text) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Preferences",text); }
    AppPreferences& preferences;StartupService& startup;UpdateService& updates;
    juce::Label frequencyLabel,status;juce::ComboBox frequency;juce::TextButton check;juce::ToggleButton runLogin,hidden;
    TransientTooltip tips{this,700};
#ifdef _WIN32
    juce::TextButton desktop;
    juce::String actionStatus;
    bool installing=false;
    juce::ThreadPool installationWorkers{1};
#endif
};
}
