#pragma once
#include "AppPreferences.h"
#include "StartupService.h"
#include "UpdateService.h"
#include "TransientTooltip.h"

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
        describe(runLogin,"Run at login","Start Sound Canvas Bridge when you sign in to this Mac. Off by default. Disabling this removes only the bridge's own login item.",3);
        runLogin.setComponentID("runAtLogin");
        runLogin.onClick=[this] {
            juce::String error;
            const auto app=juce::File::getSpecialLocation(juce::File::currentApplicationFile);
            if(!startup.setEnabled(runLogin.getToggleState(),app,error)) showError(error);
            runLogin.setToggleState(startup.enabled(),juce::dontSendNotification);
        };
        hidden.setButtonText("Start hidden in menu bar");hidden.setToggleState(preferences.startHidden(),juce::dontSendNotification);
        describe(hidden,"Start hidden in menu bar","Keep the controls hidden when the app starts, with saved MIDI and audio settings active. Open the menu-bar item or launch the app again to restore them. Settings are shown if audio or the engine cannot start.",4);
        hidden.setComponentID("startHidden");
        hidden.onClick=[this] { if(!preferences.setStartHidden(hidden.getToggleState())) { hidden.setToggleState(preferences.startHidden(),juce::dontSendNotification);showError("Could not save the startup setting."); } };
        status.setName("Update status");status.setTitle(status.getName());status.setWantsKeyboardFocus(true);status.setExplicitFocusOrder(5);
        for(auto* control:std::initializer_list<juce::Component*>{&frequencyLabel,&frequency,&check,&runLogin,&hidden,&status}) addAndMakeVisible(control);
        setSize(650,630);startTimer(500);timerCallback();
    }
    void resized() override {
        frequencyLabel.setBounds(12,16,150,32);frequency.setBounds(170,16,getWidth()-182,32);
        check.setBounds(170,64,210,32);runLogin.setBounds(12,124,getWidth()-24,32);
        hidden.setBounds(12,170,getWidth()-24,32);status.setBounds(12,222,getWidth()-24,100);
    }
private:
    template<class Control> static void describe(Control& control,const char* name,const char* help,int order) {
        control.setName(name);control.setTitle(name);control.setDescription(name);control.setHelpText(help);control.setTooltip(help);
        control.setWantsKeyboardFocus(true);control.setExplicitFocusOrder(order);
    }
    void timerCallback() override {
        const auto text=updates.status().substring(0,350);if(status.getText()!=text) status.setText(text,juce::dontSendNotification);
        check.setEnabled(!updates.busy());
    }
    static void showError(const juce::String& text) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Preferences",text); }
    AppPreferences& preferences;StartupService& startup;UpdateService& updates;
    juce::Label frequencyLabel,status;juce::ComboBox frequency;juce::TextButton check;juce::ToggleButton runLogin,hidden;
    TransientTooltip tips{this,700};
};
}
