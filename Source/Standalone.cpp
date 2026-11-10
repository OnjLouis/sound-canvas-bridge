#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include "MidiActivity.h"
#include "EngineTestPhrase.h"
#include "PersistentEngineOutput.h"
#include "EngineLocation.h"
#include "ReviewTextEditor.h"
#include "MenuBarStatus.h"
#include "TransientTooltip.h"
#include "GeneralPreferences.h"
#include "VirtualMidiInput.h"
#include "StandaloneEditor.h"
#include "StandaloneControls.h"
#include "StandaloneAudio.h"
#include "ControlHelp.h"
#include "AccessibleToggleButton.h"
#include "DawPresets.h"
#ifdef _WIN32
#include "WindowsMenuBar.h"
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
enum Command : int { audioSettingsCommand=0x20001,contextHelpCommand,manualCommand,historyCommand,
                    updatesCommand,projectCommand,contactCommand,donateCommand,allNotesOffCommand,
                    panicCommand,gsResetCommand,gmResetCommand,restartEngineCommand,activityDisplayCommand,
                    readActivityCommand,showControlsCommand,hideControlsCommand,dawPresetsCommand,aboutCommand };
constexpr const char* virtualPortName="Sound Canvas Bridge";
constexpr const char* donateUrl="https://onj.me/donate";
constexpr const char* contactUrl="https://onj.me/contact";
constexpr const char* projectUrl="https://github.com/OnjLouis/sound-canvas-bridge";
constexpr int actionModifiers=juce::ModifierKeys::commandModifier;
#ifdef _WIN32
scbridge::WindowsMenuBar::Section windowsMidiMenu() {
    return {"&MIDI",{allNotesOffCommand,panicCommand,0,gsResetCommand,gmResetCommand,restartEngineCommand},
        {"&All Notes Off","&Panic: All Sounds Off",{},"&GS Reset...","G&M Reset...","&Restart Engine with Defaults..."}};
}
#endif

bool commandInfo(juce::CommandID command,juce::ApplicationCommandInfo& result) {
    switch(command) {
        case audioSettingsCommand:
            result.setInfo("Preferences...","Audio, MIDI, update and startup preferences","Audio",0);
            result.addDefaultKeypress(',',juce::ModifierKeys::commandModifier);
            break;
        case contextHelpCommand:
            result.setInfo("Help for Focused Control","Read the focused control's help","Help",0);
            result.addDefaultKeypress(juce::KeyPress::F1Key,0);break;
        case updatesCommand:
            result.setInfo("Check for Updates...","Check the signed release channel","Help",0);
            result.addDefaultKeypress(juce::KeyPress::F1Key,juce::ModifierKeys::shiftModifier);break;
        case projectCommand:
            result.setInfo("Product Page","Open the Sound Canvas Bridge GitHub page","Help",0);
            result.addDefaultKeypress(juce::KeyPress::F1Key,juce::ModifierKeys::commandModifier);break;
        case manualCommand: result.setInfo("Sound Canvas Bridge Help","Open the offline manual","Help",0);break;
        case historyCommand: result.setInfo("Version History...","Read the bundled version history","Help",0);break;
        case donateCommand: result.setInfo("Donate","Open the donation page","Help",0);break;
        case contactCommand: result.setInfo("Contact","Open the contact page","Help",0);break;
        case allNotesOffCommand:
            result.setInfo("All Notes Off","Release notes on all 16 MIDI channels; sustain may continue","MIDI",0);
            result.addDefaultKeypress(juce::KeyPress::F9Key,0);
            break;
        case panicCommand:
            result.setInfo("Panic: All Sounds Off","Immediately stop voices without changing programs or effects","MIDI",0);
            result.addDefaultKeypress('p',actionModifiers);break;
        case gsResetCommand:
            result.setInfo("Send GS Reset...","Reset all parts and effects to native GS defaults","MIDI",0);
            result.addDefaultKeypress('g',actionModifiers);break;
        case gmResetCommand:
            result.setInfo("Send GM Reset...","Reset the original engine to General MIDI mode","MIDI",0);
            result.addDefaultKeypress('m',actionModifiers
#ifndef _WIN32
                |juce::ModifierKeys::shiftModifier
#endif
                );break;
        case restartEngineCommand:
            result.setInfo("Restart Engine with Defaults...","Recover even when normal MIDI reset messages are disabled","MIDI",0);
            result.addDefaultKeypress('r',actionModifiers);break;
        case activityDisplayCommand:
            result.setInfo("Show MIDI Activity","Show or hide the coloured channel activity display","View",0);
            result.addDefaultKeypress('d',actionModifiers);break;
        case readActivityCommand:
            result.setInfo("Read MIDI Activity","Read a snapshot without continuous announcements","View",0);
            result.addDefaultKeypress('i',actionModifiers);break;
        case aboutCommand: result.setInfo("About Sound Canvas Bridge","Show application information","Help",0);break;
        case dawPresetsCommand: result.setInfo("Install DAW Presets","Install the grouped native AU/VST3 preset files for this user","Help",0);break;
        case showControlsCommand: result.setInfo("Show Controls","Show the Sound Canvas controls window","View",0);break;
        case hideControlsCommand:
            result.setInfo(
#ifdef _WIN32
                "Hide to System Tray",
#else
                "Hide to Menu Bar",
#endif
                "Keep playback running with only the status icon visible","View",0);
            result.addDefaultKeypress('h',actionModifiers
#ifndef _WIN32
                |juce::ModifierKeys::shiftModifier
#endif
                );break;
#ifdef _WIN32
        case juce::StandardApplicationCommandIDs::quit:
            result.setInfo("Exit","Stop playback and close Sound Canvas Bridge","File",0);
            result.addDefaultKeypress('q',juce::ModifierKeys::ctrlModifier);break;
#endif
        default: return false;
    }
    return true;
}

juce::String appVersion() {
#ifdef SCBRIDGE_APP_VERSION
    return SCBRIDGE_APP_VERSION;
#else
    return "development";
#endif
}

juce::File applicationData() {
    return scbridge::performanceFile().getParentDirectory();
}

class MidiInputChoices final : public juce::Component {
public:
    explicit MidiInputChoices(juce::AudioDeviceManager& manager):devices(manager) {
        setName("MIDI inputs");setTitle(getName());setComponentID("midiInputs");
        viewport.setViewedComponent(&content,false);viewport.setScrollBarsShown(true,false);
        viewport.setName("Available MIDI inputs");viewport.setWantsKeyboardFocus(false);
        addAndMakeVisible(viewport);refresh();
    }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(*this,juce::AccessibilityRole::group);
    }
    void refresh() {
        const auto available=juce::MidiInput::getAvailableDevices();
        if(available!=ports) {
            ports=available;buttons.clear();
            for(const auto& port:ports) {
                auto button=std::make_unique<scbridge::AccessibleToggleButton>(port.name);
                button->setName(port.name);button->setComponentID(port.identifier);
                button->setTooltip("Receives all MIDI channels and SysEx from this input alongside the virtual port. Release held keys before disabling an input.");
                button->setHelpText(button->getTooltip());button->setDescription(button->getName());
                auto* control=button.get();
                button->onClick=[this,id=port.identifier,control] {
                    const bool requested=control->getToggleState();
                    devices.setMidiInputDeviceEnabled(id,requested);
                    const bool enabled=devices.isMidiInputDeviceEnabled(id);
                    control->setToggleState(enabled,juce::dontSendNotification);
                    juce::Logger::writeToLog("MIDI input "+control->getName()+(enabled ? " enabled" : " disabled"));
                    if(requested && !enabled)
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"MIDI input","Could not open "+control->getName()+". Check its connection, then refresh devices.");
                };
                content.addAndMakeVisible(*button);buttons.push_back(std::move(button));
            }
            resized();
        }
        for(size_t i=0;i<buttons.size();++i)
            buttons[i]->setToggleState(devices.isMidiInputDeviceEnabled(ports[static_cast<int>(i)].identifier),juce::dontSendNotification);
    }
    void resized() override {
        viewport.setBounds(getLocalBounds());
        const int width=std::max(0,getWidth()-viewport.getScrollBarThickness());
        content.setSize(width,std::max(getHeight(),static_cast<int>(buttons.size())*32));
        for(size_t i=0;i<buttons.size();++i) buttons[i]->setBounds(0,static_cast<int>(i)*32,width,30);
    }
private:
    class Rows final : public juce::Component {
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
            return std::make_unique<juce::AccessibilityHandler>(*this,juce::AccessibilityRole::ignored);
        }
    };
    class Scroller final : public juce::Viewport {
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
            return std::make_unique<juce::AccessibilityHandler>(*this,juce::AccessibilityRole::ignored);
        }
    };
    juce::AudioDeviceManager& devices;
    juce::Array<juce::MidiDeviceInfo> ports;
    Rows content;
    Scroller viewport;
    std::vector<std::unique_ptr<scbridge::AccessibleToggleButton>> buttons;
};

class AudioPreferences final : public juce::Component,private juce::ChangeListener,private juce::Timer {
public:
    explicit AudioPreferences(juce::AudioDeviceManager& manager,std::function<void()> test={},std::function<bool()> playing={})
        :devices(manager),midi(manager),playTest(std::move(test)),testPlaying(std::move(playing)) {
        setName("Audio settings");setTitle("Audio settings");
        setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);
#ifdef _WIN32
        driverLabel.setText("Audio driver",juce::dontSendNotification);driverLabel.setAccessible(false);
        driver.setName("Audio driver");driver.setTitle(driver.getName());driver.setExplicitFocusOrder(1);
        driver.setHelpText("Select ASIO for your manufacturer's low-latency driver, or Windows Audio for WASAPI. Changing drivers briefly interrupts audio without resetting MIDI sounds.");
        driver.setDescription(driver.getName());driver.setTooltip(driver.getHelpText());
        driver.onChange=[this] {
            const auto selected=driver.getText();
            if(selected.isNotEmpty() && selected!=devices.getCurrentAudioDeviceType()) devices.setCurrentAudioDeviceType(selected,true);
            update();
        };
        panel.setButtonText("Driver settings...");panel.setName("Driver settings");panel.setExplicitFocusOrder(6);
        panel.setHelpText("Open the selected ASIO driver's own control panel. Availability and accessibility depend on its manufacturer.");
        panel.setDescription(panel.getName());panel.setTooltip(panel.getHelpText());
        panel.onClick=[this] { if(auto* device=devices.getCurrentAudioDevice()) device->showControlPanel(); };
        addAndMakeVisible(driverLabel);addAndMakeVisible(driver);addAndMakeVisible(panel);
#endif
        const char* names[]={"Output device","Sample rate","Audio buffer"};
        const char* help[]={"Chooses the audio output used by this standalone module. No audio input is opened.",
            "Chooses the sample rate supported by the selected output device.",
            "Chooses the audio device buffer in samples. This is separate from the shared bridge latency."};
        for(unsigned i=0;i<3;++i) {
            labels[i].setText(names[i],juce::dontSendNotification);labels[i].setAccessible(false);
            auto& choice=choices[i];choice.setName(names[i]);choice.setTitle(names[i]);
            choice.setComponentID(juce::String("audioSetting")+juce::String(i));
            choice.setDescription(names[i]);choice.setHelpText(help[i]);choice.setTooltip(help[i]);
            choice.setWantsKeyboardFocus(true);choice.setExplicitFocusOrder(static_cast<int>(i+1)
#ifdef _WIN32
                +1
#endif
            );
            choice.onChange=[this,i] { apply(i); };
            addAndMakeVisible(labels[i]);addAndMakeVisible(choice);
        }
        refresh.setButtonText("Refresh devices");refresh.setName("Refresh devices");refresh.setExplicitFocusOrder(4);
        refresh.setTooltip("Refreshes the output device list without changing the selected device.");
        refresh.setDescription(refresh.getName());refresh.setHelpText(refresh.getTooltip());
        refresh.onClick=[this] { if(auto* type=devices.getCurrentDeviceTypeObject()) type->scanForDevices();midi.refresh();update(); };
        midiHeading.setText("MIDI inputs",juce::dontSendNotification);midiHeading.setAccessible(false);
        testButton.setName("Play test phrase");testButton.setComponentID("engineTestPhrase");
        testButton.setHelpText("Plays a short piano phrase through a separate Sound Canvas engine on this output. Does not change your current sounds. Press again to stop.");
        testButton.setTooltip(testButton.getHelpText());testButton.setDescription(testButton.getName());
        testButton.setExplicitFocusOrder(6);testButton.onClick=[this] { if(playTest) playTest();timerCallback(); };
        midi.setExplicitFocusOrder(5);
        status.setName("Audio status");status.setTitle("Audio status");status.setWantsKeyboardFocus(true);status.setExplicitFocusOrder(7);
        addAndMakeVisible(refresh);addAndMakeVisible(status);addAndMakeVisible(midiHeading);
        addAndMakeVisible(midi);addAndMakeVisible(testButton);
#ifdef _WIN32
        refresh.setExplicitFocusOrder(5);midi.setExplicitFocusOrder(7);testButton.setExplicitFocusOrder(8);
        status.setExplicitFocusOrder(9);
#endif
        devices.addChangeListener(this);setSize(650,630);update();startTimer(150);timerCallback();
    }
    ~AudioPreferences() override { devices.removeChangeListener(this); }
#ifdef SCBRIDGE_STANDALONE_TEST
    void useFocusFixture() {
        devices.removeChangeListener(this);
        for(auto& choice:choices) { choice.clear(juce::dontSendNotification);choice.addItem("Test option",1);
            choice.setSelectedId(1,juce::dontSendNotification);choice.setEnabled(true); }
    }
#endif
    void resized() override {
#ifdef _WIN32
        driverLabel.setBounds(12,12,150,32);driver.setBounds(164,12,getWidth()-176,32);
        for(unsigned i=0;i<3;++i) { labels[i].setBounds(12,56+44*i,150,32);choices[i].setBounds(164,56+44*i,getWidth()-176,32); }
        refresh.setBounds(164,190,160,32);panel.setBounds(340,190,180,32);
        midiHeading.setBounds(12,226,getWidth()-24,24);midi.setBounds(12,254,getWidth()-24,134);
#else
        for(unsigned i=0;i<3;++i) { labels[i].setBounds(12,12+44*i,150,32);choices[i].setBounds(164,12+44*i,getWidth()-176,32); }
        refresh.setBounds(164,148,160,32);midiHeading.setBounds(12,190,getWidth()-24,24);
        midi.setBounds(12,218,getWidth()-24,170);
#endif
        testButton.setBounds(164,400,200,32);
        status.setBounds(12,444,getWidth()-24,56);
    }
private:
    static void fill(juce::ComboBox& choice,const juce::StringArray& names,int selected) {
        juce::StringArray previous;
        for(int i=0;i<choice.getNumItems();++i) previous.add(choice.getItemText(i));
        if(previous!=names) { choice.clear(juce::dontSendNotification);choice.addItemList(names,1); }
        choice.setSelectedItemIndex(selected,juce::dontSendNotification);
        choice.setEnabled(!names.isEmpty());
    }
    void update() {
#ifdef _WIN32
        juce::StringArray types;
        for(auto* type:devices.getAvailableDeviceTypes()) types.add(type->getTypeName());
        fill(driver,types,types.indexOf(devices.getCurrentAudioDeviceType()));
        panel.setEnabled(devices.getCurrentAudioDevice() && devices.getCurrentAudioDevice()->hasControlPanel());
#endif
        const auto setup=devices.getAudioDeviceSetup();
        auto* type=devices.getCurrentDeviceTypeObject();
        auto outputs=type ? type->getDeviceNames(false) : juce::StringArray{};
        fill(choices[0],outputs,outputs.indexOf(setup.outputDeviceName));
        juce::StringArray rates,buffers;
        int selectedRate=-1,selectedBuffer=-1;
        if(auto* device=devices.getCurrentAudioDevice()) {
            for(double rate:device->getAvailableSampleRates()) {
                if(rate==device->getCurrentSampleRate()) selectedRate=rates.size();
                rates.add(juce::String(rate,0)+" Hz");
            }
            for(int size:device->getAvailableBufferSizes()) {
                if(size==device->getCurrentBufferSizeSamples()) selectedBuffer=buffers.size();
                buffers.add(juce::String(size)+" samples");
            }
        }
        fill(choices[1],rates,selectedRate);fill(choices[2],buffers,selectedBuffer);
        status.setText(error.isNotEmpty() ? error : devices.getCurrentAudioDevice() ?
#ifdef _WIN32
            "Audio output is active. Select MIDI inputs above. No audio input enabled."
#else
            "Virtual MIDI input: Sound Canvas Bridge. No audio input enabled."
#endif
            : "No audio output device is active.",juce::dontSendNotification);
    }
    void apply(unsigned field) {
        if(choices[field].getSelectedItemIndex()<0) return;
        auto setup=devices.getAudioDeviceSetup();
        setup.inputDeviceName.clear();setup.inputChannels.clear();setup.useDefaultInputChannels=false;
        setup.useDefaultOutputChannels=true;
        if(field==0) { setup.outputDeviceName=choices[0].getText();setup.sampleRate=0;setup.bufferSize=0; }
        if(field==1) setup.sampleRate=choices[1].getText().getDoubleValue();
        if(field==2) setup.bufferSize=choices[2].getText().getIntValue();
        error=devices.setAudioDeviceSetup(setup,true);update();
        if(error.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Audio output",error);
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { midi.refresh();update(); }
    void timerCallback() override {
        const bool playing=testPlaying && testPlaying();
        const juce::String text=playing ? "Stop test phrase" : "Play test phrase";
        if(testButton.getButtonText()!=text) { testButton.setButtonText(text);testButton.setName(text); }
        testButton.setEnabled(playTest && devices.getCurrentAudioDevice()!=nullptr);
    }
    juce::AudioDeviceManager& devices;
    MidiInputChoices midi;
    juce::Label labels[3],status,midiHeading;
    juce::ComboBox choices[3];
    juce::TextButton refresh,testButton;
    std::function<void()> playTest;
    std::function<bool()> testPlaying;
    scbridge::TransientTooltip tips{this,700};
#ifdef _WIN32
    juce::Label driverLabel;
    juce::ComboBox driver;
    juce::TextButton panel;
#endif
    juce::String error;
};

bool focusFirstControl(juce::Component& root) {
    // Follow either kind of focus container so hosted and standalone editors
    // can share their initial control without focusing a layout wrapper.
    juce::FocusTraverser traverser;
        for(auto* control:traverser.getAllComponents(&root))
            if(control->isShowing() && control->isEnabled()) {
                if(control->isFocusContainer() && control!=&root && focusFirstControl(*control)) return true;
                if(!control->getWantsKeyboardFocus()) continue;
                control->grabKeyboardFocus();
                if(auto* handler=control->getAccessibilityHandler()) handler->grabFocus();
                return control->hasKeyboardFocus(true);
            }
    return false;
}

void scheduleInitialFocus(juce::Component& root) {
    const juce::Identifier requestKey("initialFocusRequest");
    const int generation=static_cast<int>(root.getProperties().getWithDefault(requestKey,0))+1;
    root.getProperties().set(requestKey,generation);
    class Request final : private juce::Timer {
    public:
        Request(juce::Component& target,int value):safe(&target),generation(value) { startTimer(25); }
    private:
        void timerCallback() override {
            if(!safe || static_cast<int>(safe->getProperties()["initialFocusRequest"])!=generation || ++attempts>40) {
                delete this;return;
            }
            auto* peer=safe->getPeer();
            if(safe->isShowing() && peer &&
#ifdef _WIN32
               peer->isFocused()
#else
               [NSApp isActive] && [[(NSView*)peer->getNativeHandle() window] isKeyWindow]
#endif
               && focusFirstControl(*safe)) delete this;
        }
        juce::Component::SafePointer<juce::Component> safe;
        const int generation;
        int attempts=0;
    };
    // Native window activation may finish after the first message-loop turn.
    new Request(root,generation);
}

void restoreControlsWindow(juce::DocumentWindow& window) {
#ifndef _WIN32
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp unhide:nil];
#endif
    window.setMinimised(false);window.setVisible(true);
#ifndef _WIN32
    [NSApp activateIgnoringOtherApps:YES];
#endif
    window.toFront(true);
#ifndef _WIN32
    if(auto* peer=window.getPeer()) [[(NSView*)peer->getNativeHandle() window] makeKeyAndOrderFront:nil];
#endif
    window.repaint();
    if(auto* content=window.getContentComponent()) {
        if(auto* tabs=dynamic_cast<juce::TabbedComponent*>(content)) content=tabs->getCurrentContentComponent();
        if(content) scheduleInitialFocus(*content);
    }
}

class ControlHelpWindow final : public juce::DocumentWindow {
public:
    ControlHelpWindow(const juce::String& title,const juce::String& help,juce::Component* previous)
        :DocumentWindow("Help: "+title,juce::Colours::white,closeButton),restore(previous) {
        setUsingNativeTitleBar(true);
        text.setMultiLine(true);text.setReadOnly(true);text.setName(title+" help");text.setTitle(text.getName());
        text.setTabKeyUsedAsCharacter(false);
        text.setText(help.replace(". ",".\n\n"),false);text.setCaretPosition(0);text.setExplicitFocusOrder(1);
        text.onEscapeKey=[this] { closeButtonPressed(); };
        close.setButtonText("Close");close.setName("Close help");close.setExplicitFocusOrder(2);
        close.setHelpText("Close help and return to the previous control.");close.onClick=[this] { closeButtonPressed(); };
        content.addAndMakeVisible(text);content.addAndMakeVisible(close);
        content.setSize(620,320);text.setBounds(12,12,596,250);close.setBounds(488,274,120,32);
        setContentNonOwned(&content,true);centreWithSize(getWidth(),getHeight());setVisible(true);toFront(true);
        scheduleInitialFocus(content);
    }
    void closeButtonPressed() override {
        setVisible(false);
        if(restore && restore->isShowing()) {
            restore->getTopLevelComponent()->toFront(true);restore->grabKeyboardFocus();
            if(auto* handler=restore->getAccessibilityHandler()) handler->grabFocus();
        }
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if(key==juce::KeyPress::escapeKey || key==juce::KeyPress('w',juce::ModifierKeys::commandModifier,0)) {
            closeButtonPressed();return true;
        }
        return DocumentWindow::keyPressed(key);
    }
private:
    juce::Component content;
    scbridge::ReviewTextEditor text;
    juce::TextButton close;
    juce::Component::SafePointer<juce::Component> restore;
};

class PreferenceTabs final : public juce::TabbedComponent {
public:
    PreferenceTabs(juce::AudioDeviceManager& manager,scbridge::AppPreferences& preferences,
                   scbridge::StartupService& startup,scbridge::UpdateService& updates,
                   std::function<void()> test,std::function<bool()> playing,std::function<void()> changed)
        :TabbedComponent(juce::TabbedButtonBar::TabsAtTop) {
        setName("Preferences categories");
        auto* general=new scbridge::GeneralPreferences(preferences,startup,updates,std::move(changed));general->setComponentID("generalPreferences");
        const auto background=juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
        addTab("General",background,general,true);
        addTab("Audio and MIDI",background,new AudioPreferences(manager,std::move(test),std::move(playing)),true);
        // Preferences owns one Tab loop on both platforms. Nested keyboard
        // containers otherwise wrap within a page and make the tabs unreachable.
        setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
        auto& bar=getTabbedButtonBar();bar.setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
        bar.setExplicitFocusOrder(1);bar.setName("Preferences categories");bar.setTitle(bar.getName());
        for(int i=0;i<getNumTabs();++i) {
            auto* content=getTabContentComponent(i);
            content->setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
            content->setExplicitFocusOrder(2);
        }
        setSize(650,670);
    }
    void currentTabChanged(int,const juce::String&) override {
        if(!getTabbedButtonBar().hasKeyboardFocus(true))
            if(auto* content=getCurrentContentComponent()) scheduleInitialFocus(*content);
    }
protected:
    juce::TabBarButton* createTabButton(const juce::String& name,int index) override {
        class CategoryButton final : public juce::TabBarButton {
        public:
            CategoryButton(const juce::String& label,juce::TabbedButtonBar& owner):TabBarButton(label,owner) {
                setWantsKeyboardFocus(true);setTitle(label);
            }
            bool keyPressed(const juce::KeyPress& key) override {
                if(!key.getModifiers().isAnyModifierKeyDown()) {
                    const auto code=key.getKeyCode();
                    const int count=getTabbedButtonBar().getNumTabs();
                    int next=getIndex();
                    if(code==juce::KeyPress::leftKey || code==juce::KeyPress::upKey) next=(next+count-1)%count;
                    else if(code==juce::KeyPress::rightKey || code==juce::KeyPress::downKey) next=(next+1)%count;
                    else if(code==juce::KeyPress::homeKey) next=0;
                    else if(code==juce::KeyPress::endKey) next=count-1;
                    else return TabBarButton::keyPressed(key);
                    getTabbedButtonBar().setCurrentTabIndex(next);
                    if(auto* target=getTabbedButtonBar().getTabButton(next)) {
                        target->grabKeyboardFocus();
                        if(auto* handler=target->getAccessibilityHandler()) handler->grabFocus();
                    }
                    return true;
                }
                return TabBarButton::keyPressed(key);
            }
        };
        auto* button=new CategoryButton(name,getTabbedButtonBar());
        button->setExplicitFocusOrder(index+1);
        return button;
    }
};

#ifdef _WIN32
class SettingsPanel final : public juce::Component {
public:
    SettingsPanel(juce::AudioDeviceManager& manager,scbridge::AppPreferences& preferences,scbridge::StartupService& startup,
                  scbridge::UpdateService& updates,std::function<void()> test,std::function<bool()> playing,std::function<void()> engineChanged)
        :tabs(manager,preferences,startup,updates,std::move(test),std::move(playing),std::move(engineChanged)) {
        setName("Preferences");setTitle(getName());setComponentID("preferences");
        setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);
        heading.setText("Preferences",juce::dontSendNotification);heading.setAccessible(false);
        close.setButtonText("Close");close.setName("Close Preferences");close.setExplicitFocusOrder(2);
        close.onClick=[this] { dismiss(true); };
        tabs.setExplicitFocusOrder(1);
        for(auto* child:std::initializer_list<juce::Component*>{&heading,&tabs,&close}) addAndMakeVisible(child);
        setSize(650,714);
    }
    std::function<void()> onClose;
    void dismiss(bool restoreFocus) {
        setVisible(false);
        if(restoreFocus && onClose) onClose();
    }
    void selectAudioTab() { tabs.setCurrentTabIndex(1); }
    void selectGeneralTab() { tabs.setCurrentTabIndex(0); }
    void focusInitialControl() {
        if(auto* content=tabs.getCurrentContentComponent()) scheduleInitialFocus(*content);
    }
    void resized() override {
        heading.setBounds(12,8,getWidth()-152,28);close.setBounds(getWidth()-132,6,120,32);
        tabs.setBounds(0,44,getWidth(),std::max(0,getHeight()-44));
    }
    void paint(juce::Graphics& graphics) override {
        graphics.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if(key==juce::KeyPress::escapeKey) { dismiss(true);return true; }
        return false;
    }
private:
    PreferenceTabs tabs;
    juce::Label heading;
    juce::TextButton close;
};
#else
class SettingsWindow final : public juce::DocumentWindow {
public:
    SettingsWindow(juce::AudioDeviceManager& manager,scbridge::AppPreferences& preferences,scbridge::StartupService& startup,
                   scbridge::UpdateService& updates,std::function<void()> test,std::function<bool()> playing,std::function<void()> engineChanged,
                   juce::DocumentWindow* owner=nullptr)
        :DocumentWindow("Preferences",juce::Colours::darkgrey,closeButton,
#ifdef _WIN32
            false
#else
            true
#endif
        ) {
        setUsingNativeTitleBar(true);setContentOwned(new PreferenceTabs(manager,preferences,startup,updates,std::move(test),std::move(playing),std::move(engineChanged)),true);
#ifdef _WIN32
        addToDesktop(getDesktopWindowStyleFlags());
        if(owner && owner->getPeer())
            SetWindowLongPtrW(static_cast<HWND>(getPeer()->getNativeHandle()),GWLP_HWNDPARENT,
                reinterpret_cast<LONG_PTR>(owner->getPeer()->getNativeHandle()));
#endif
        centreWithSize(getWidth(),getHeight());restoreControlsWindow(*this);
    }
    std::function<void()> onClose;
    void dismiss(bool restoreFocus) {
#ifdef _WIN32
        if(isCurrentlyModal()) exitModalState(0);
#endif
        setVisible(false);
        if(restoreFocus && onClose) onClose();
    }
    void closeButtonPressed() override { dismiss(true); }
    bool keyPressed(const juce::KeyPress& key) override {
        if(key==juce::KeyPress::escapeKey) { closeButtonPressed();return true; }
        return DocumentWindow::keyPressed(key);
    }
};
#endif

class MainContent final : public juce::Component {
public:
    MainContent(juce::AudioProcessor& processor,scbridge::MidiActivity& activity,bool visible,std::function<void(int)> applyMap={})
#if JUCE_MAJOR_VERSION >= 8
        :editor(processor.createEditorAndMakeActive()),display(activity) {
#else
        :editor(processor.createEditorIfNeeded()),display(activity) {
#endif
        setName("Sound Canvas controls");
        if(applyMap) scbridge::configureStandaloneEditor(*editor,std::move(applyMap));
        setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);
        addAndMakeVisible(*editor);addChildComponent(display);showActivity(visible);
    }
    ~MainContent() override {
        if(editor) editor->getAudioProcessor()->editorBeingDeleted(editor.get());
    }
    void showActivity(bool visible) {
        display.setVisible(visible);setSize(editor->getWidth()+(visible ? 212 : 0),editor->getHeight());
    }
    void resized() override {
        editor->setTopLeftPosition(0,0);
        display.setBounds(editor->getWidth()+12,12,188,getHeight()-24);
    }
private:
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    scbridge::ActivityDisplay display;
};

#ifdef SCBRIDGE_WINDOWS7
static void fitLegacyStandaloneToDisplay() {
    auto& desktop=juce::Desktop::getInstance();
    if(const auto* display=desktop.getDisplays().getPrimaryDisplay()) {
        // Leave room for the title bar and fit both the editor and its activity display.
        constexpr float width=700.0f+212.0f+32.0f;
        constexpr float height=762.0f+96.0f;
        const auto scale=std::min({1.0f,display->userArea.getWidth()/width,display->userArea.getHeight()/height});
        if(scale>0.0f && scale<1.0f) desktop.setGlobalScaleFactor(desktop.getGlobalScaleFactor()*scale);
    }
}
#endif

class MainWindow final : public juce::DocumentWindow {
public:
    MainWindow(juce::AudioProcessor& processor,scbridge::MidiActivity& activity,bool showActivity,bool initiallyVisible=true,std::function<void(int)> mapAll={})
        :DocumentWindow("Sound Canvas Bridge",juce::Colours::black,allButtons),applyMap(std::move(mapAll)) {
        setUsingNativeTitleBar(true);setEditor(processor,activity,showActivity);
        centreWithSize(getWidth(),getHeight());setVisible(initiallyVisible);
    }
    void setEditor(juce::AudioProcessor& processor,scbridge::MidiActivity& activity,bool visible) {
        setContentOwned(new MainContent(processor,activity,visible,applyMap),true);
        scheduleInitialFocus(*getContentComponent());
    }
    void showActivity(bool visible) { if(auto* content=dynamic_cast<MainContent*>(getContentComponent())) content->showActivity(visible); }
#ifdef _WIN32
    void showPreferences(SettingsPanel& panel) {
        if(!preferences || !preferences->isVisible()) {
            auto* focused=juce::Component::getCurrentlyFocusedComponent();
            if(focused && getContentComponent()->isParentOf(focused)) previousFocus=focused;
        }
        preferences=&panel;
        getContentComponent()->setVisible(false);
        addAndMakeVisible(panel);panel.setBounds(getContentComponent()->getBounds());panel.toFront(false);
        toFront(true);panel.focusInitialControl();
    }
    void dismissPreferences() {
        if(preferences) preferences->setVisible(false);
        getContentComponent()->setVisible(true);
        if(previousFocus && previousFocus->isShowing() && previousFocus->isEnabled()) {
            previousFocus->grabKeyboardFocus();
            if(auto* handler=previousFocus->getAccessibilityHandler()) handler->grabFocus();
        } else scheduleInitialFocus(*getContentComponent());
        previousFocus=nullptr;
    }
    bool preferencesVisible() const { return preferences && preferences->isVisible(); }
    void resized() override {
        DocumentWindow::resized();
        if(preferences && getContentComponent()) preferences->setBounds(getContentComponent()->getBounds());
    }
#endif
    void activeWindowStatusChanged() override {
        DocumentWindow::activeWindowStatusChanged();
        if(isActiveWindow()) if(auto* content=focusRoot())
            if(!content->hasKeyboardFocus(true)) scheduleInitialFocus(*content);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if(key.getKeyCode()==juce::KeyPress::tabKey && !key.getModifiers().isCtrlDown() &&
           !key.getModifiers().isAltDown() && !key.getModifiers().isCommandDown()) {
            if(auto* content=focusRoot()) if(!content->hasKeyboardFocus(true)) {
                if(!focusFirstControl(*content)) scheduleInitialFocus(*content);
                return true;
            }
        }
        return DocumentWindow::keyPressed(key);
    }
    std::function<void()> onClose;
    void closeButtonPressed() override { if(onClose) onClose();else setVisible(false); }
private:
    std::function<void(int)> applyMap;
    juce::Component* focusRoot() {
#ifdef _WIN32
        if(preferencesVisible()) return preferences;
#endif
        return getContentComponent();
    }
#ifdef _WIN32
    juce::Component::SafePointer<SettingsPanel> preferences;
    juce::Component::SafePointer<juce::Component> previousFocus;
#endif
};

class Standalone final : public juce::JUCEApplication,private juce::MenuBarModel,private juce::ChangeListener {
public:
    const juce::String getApplicationName() override {
#ifdef SCBRIDGE_WINDOWS7
        return "Sound Canvas Bridge (Windows 7)";
#else
        return "Sound Canvas Bridge";
#endif
    }
    const juce::String getApplicationVersion() override { return appVersion(); }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String& commandLine) override {
        juce::LookAndFeel::getDefaultLookAndFeel().setUsingNativeAlertWindows(true);
#ifdef SCBRIDGE_WINDOWS7
        fitLegacyStandaloneToDisplay();
#endif
        auto directory=applicationData();
        if(!directory.createDirectory().wasOk()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Sound Canvas Bridge","Could not create the application settings folder.");
            quit();return;
        }
#ifdef _WIN32
        logger=std::make_unique<juce::FileLogger>(directory.getChildFile("Standalone.log"),"Standalone started",262144);
#else
        logger.reset(juce::FileLogger::createDefaultAppLogger("OnjMusic/SoundCanvasBridge","Standalone.log","Standalone started",262144));
#endif
        juce::Logger::setCurrentLogger(logger.get());
        if(resourceFile("DAW Presets.zip").existsAsFile()) {
            const auto result=scbridge::DawPresets::installBundled(resourceFile("DAW Presets.zip"));
            juce::Logger::writeToLog(result.wasOk() ? "Grouped DAW presets ready" : "DAW preset installation: "+result.getErrorMessage());
        }
        processor.reset(createPluginFilter());
        auto session=directory.getChildFile("Standalone.state");
        if(session.existsAsFile() && session.getSize()<=65536) {
            juce::MemoryBlock state;session.loadFileAsData(state);
            processor->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        }
        const auto audioFile=directory.getChildFile("Audio.xml");
        auto audio=juce::XmlDocument::parse(audioFile);
        const auto error=audioFile.existsAsFile() && (!audio || !audio->hasTagName("DEVICESETUP"))
            ? juce::String("The saved audio settings could not be read. Choose an output in Preferences; the saved file has not been overwritten.")
            : scbridge::initialiseStandaloneAudio(devices,audio.get());
        juce::Logger::writeToLog("Audio settings: "+audioFile.getFullPathName());
        if(audio) juce::Logger::writeToLog("Requested audio: "+audio->getStringAttribute("deviceType")+" / "+audio->getStringAttribute("audioOutputDeviceName")+
            "; rate "+audio->getStringAttribute("audioDeviceRate")+"; buffer "+audio->getStringAttribute("audioDeviceBufferSize"));
        if(error.isNotEmpty()) juce::Logger::writeToLog("Audio initialisation failed; saved output retained: "+error);
        else if(auto* device=devices.getCurrentAudioDevice()) juce::Logger::writeToLog("Active audio: "+devices.getCurrentAudioDeviceType()+" / "+device->getName()+
            "; rate "+juce::String(device->getCurrentSampleRate())+"; buffer "+juce::String(device->getCurrentBufferSizeSamples()));
        output=std::make_unique<scbridge::PersistentEngineOutput>(*processor,midiCollector);
        devices.addAudioCallback(output.get());
        devices.addMidiInputDeviceCallback({},&midiInput);
        const auto arguments=juce::StringArray::fromTokens(commandLine,true);
        const bool requestVirtualMidi=!arguments.contains("--no-virtual-midi")
#if defined(_WIN32) && JUCE_MAJOR_VERSION >= 8
            && arguments.contains("--experimental-native-midi")
#elif defined(_WIN32)
            && false
#endif
            ;
        // Current Windows builds can deadlock the MIDI service during virtual
        // device teardown (Microsoft/MIDI #1047). Never opt users in silently.
#if !defined(_WIN32) || JUCE_MAJOR_VERSION >= 8
        if(requestVirtualMidi)
            virtualInput=scbridge::VirtualMidiInput::createNewDevice(virtualPortName,&midiInput);
#endif
        if(virtualInput) { virtualInput->start();juce::Logger::writeToLog("Virtual MIDI input ready"); }
        else if(requestVirtualMidi)
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"MIDI input","Could not create the Sound Canvas Bridge virtual MIDI input. Physical MIDI inputs and audio remain available.");
#ifdef _WIN32
        else juce::Logger::writeToLog("Native virtual MIDI disabled; system MIDI inputs remain available.");
#endif
        if(auto ui=juce::XmlDocument::parse(directory.getChildFile("Interface.xml"))) showActivity=ui->getBoolAttribute("activity",true);
        const bool hiddenStart=appPreferences.startHidden() && !arguments.contains("--show-controls");
        juce::Logger::writeToLog("Start hidden: "+juce::String(appPreferences.startHidden() ? "enabled" : "disabled")+
            "; explicit Show Controls: "+juce::String(arguments.contains("--show-controls") ? "yes" : "no"));
#ifndef _WIN32
        if(hiddenStart) [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
#endif
        window=std::make_unique<MainWindow>(*processor,activity,showActivity,!hiddenStart,[this](int map) {
            std::array<scbridge::ControlMessage,32> messages;
            if(!scbridge::standaloneMapSelection(*processor,map,messages)) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Sound map","The engine is not ready. Wait for playback to become available, then select the map again.");
                return;
            }
            for(const auto& message:messages)
                if(message.size) sendMidi(juce::MidiMessage(message.bytes.data(),static_cast<int>(message.size)));
            juce::Logger::writeToLog("Sound map applied to all parts: "+juce::String(map));
        });
        window->onClose=[this] {
#ifdef _WIN32
            systemRequestedQuit();
#else
            hideControls();
#endif
        };
        if(!hiddenStart) { window->toFront(true);scheduleInitialFocus(*window->getContentComponent()); }
        commands.registerAllCommandsForTarget(this);commands.setFirstCommandTarget(this);
        window->addKeyListener(commands.getKeyMappings());
        statusItem=std::make_unique<scbridge::MenuBarStatus>([this](int command) { commands.invokeDirectly(command,true); },
            showControlsCommand,audioSettingsCommand,panicCommand,juce::StandardApplicationCommandIDs::quit,true);
        applicationExtras.addCommandItem(&commands,updatesCommand);applicationExtras.addCommandItem(&commands,donateCommand);
        applicationExtras.addCommandItem(&commands,projectCommand);
#ifdef _WIN32
        nativeMenu=std::make_unique<scbridge::WindowsMenuBar>(*window,commands,
            std::vector<scbridge::WindowsMenuBar::Section>{
                {"&File",{audioSettingsCommand,0,juce::StandardApplicationCommandIDs::quit},{"&Preferences...",{},"E&xit"}},
                windowsMidiMenu(),
                {"&View",{showControlsCommand,hideControlsCommand,0,activityDisplayCommand,readActivityCommand}},
                {"&Help",{historyCommand,contextHelpCommand,manualCommand,dawPresetsCommand,projectCommand,0,updatesCommand,contactCommand,donateCommand,aboutCommand}}});
#else
        juce::MenuBarModel::setMacMainMenu(this,&applicationExtras);
#endif
        devices.addChangeListener(this);
        if(error.isEmpty()) saveAudioSettings();
        const bool engineReady=scbridge::EngineLocation().available();
        const bool engineSetup=scbridge::AppPreferences::shouldShowEngineSetup(appPreferences.isFirstLaunch(),engineReady);
        if(!engineSetup && scbridge::AppPreferences::shouldHide(hiddenStart,statusItem->available(),engineReady,error.isEmpty() && devices.getCurrentAudioDevice()!=nullptr)) {
            hideControls();
        } else {
            if(hiddenStart) showControls();
            if(engineSetup) showSettings(false,error,true);
            else if(error.isNotEmpty() || devices.getCurrentAudioDevice()==nullptr) showSettings(true,error);
        }
        updates.start();
    }
    void shutdown() override {
        updates.stop();
        devices.removeChangeListener(this);
#ifdef _WIN32
        nativeMenu.reset();
#else
        juce::MenuBarModel::setMacMainMenu(nullptr);
#endif
        statusItem.reset();helpWindow.reset();settings.reset();window.reset();
        testPhrase.stop();devices.removeMidiInputDeviceCallback({},&midiInput);
        if(virtualInput) { virtualInput->stop();virtualInput.reset(); }
        devices.removeAudioCallback(output.get());
        if(processor) {
            juce::MemoryBlock state;processor->getStateInformation(state);
            if(!applicationData().getChildFile("Standalone.state").replaceWithData(state.getData(),state.getSize()))
                juce::Logger::writeToLog("Could not save standalone sound settings");
        }
        output.reset();devices.closeAudioDevice();processor.reset();
        juce::Logger::writeToLog("Standalone stopped; virtual MIDI input removed");
        juce::Logger::setCurrentLogger(nullptr);logger.reset();
    }
    void anotherInstanceStarted(const juce::String&) override { showControls(); }
    void getAllCommands(juce::Array<juce::CommandID>& result) override {
        juce::JUCEApplication::getAllCommands(result);
        for(int command=audioSettingsCommand;command<=aboutCommand;++command) result.add(command);
    }
    void getCommandInfo(juce::CommandID command,juce::ApplicationCommandInfo& result) override {
        if(!commandInfo(command,result)) juce::JUCEApplication::getCommandInfo(command,result);
        if(command==activityDisplayCommand) result.setTicked(showActivity);
    }
    bool perform(const InvocationInfo& info) override {
        switch(info.commandID) {
            case audioSettingsCommand: showSettings();return true;
            case contextHelpCommand: showControlHelp();return true;
            case dawPresetsCommand: {
                const auto folder=scbridge::DawPresets::defaultFolder();
                const auto result=scbridge::DawPresets::installBundled(resourceFile("DAW Presets.zip"));
                helpWindow=std::make_unique<ControlHelpWindow>("DAW Presets",result.wasOk() ?
                    "The grouped DAW presets are installed.\n\n"+folder.getFullPathName() : result.getErrorMessage(),juce::Component::getCurrentlyFocusedComponent());
                return true;
            }
            case manualCommand: openResource("Manual.html");return true;
            case historyCommand: openResource("VersionHistory.html");return true;
            case projectCommand: openLink(projectUrl);return true;
            case donateCommand: openLink(donateUrl);return true;
            case contactCommand: openLink(contactUrl);return true;
            case allNotesOffCommand: silence(false);return true;
            case panicCommand: silence(true);return true;
            case gsResetCommand: case gmResetCommand: case restartEngineCommand: confirmReset(info.commandID);return true;
            case activityDisplayCommand: {
                showActivity=!showActivity;window->showActivity(showActivity);commands.commandStatusChanged();
                juce::XmlElement ui("SoundCanvasInterface");ui.setAttribute("activity",showActivity);
                if(!applicationData().getChildFile("Interface.xml").replaceWithText(ui.toString()))
                    juce::Logger::writeToLog("Could not save activity display preference");
                return true;
            }
            case readActivityCommand: {
                const auto values=activity.levels(juce::Time::getMillisecondCounter());juce::StringArray active;
                for(unsigned i=0;i<values.size();++i) if(values[i]) active.add(juce::String(i+1));
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,"MIDI Activity",
                    active.isEmpty() ? "No recent note-on messages." : "Recent note activity on MIDI channels "+active.joinIntoString(", ")+".");
                return true;
            }
            case showControlsCommand: showControls();return true;
            case hideControlsCommand: hideControls();return true;
            case aboutCommand:
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,"Sound Canvas Bridge",
                    "Sound Canvas Bridge "+appVersion()+"\nOnjMusic");return true;
            case updatesCommand:
                updates.checkNow();return true;
            default: return juce::JUCEApplication::perform(info);
        }
    }
private:
    juce::StringArray getMenuBarNames() override { return {"Audio","MIDI","View","Help"}; }
    juce::PopupMenu getMenuForIndex(int index,const juce::String&) override {
        juce::PopupMenu menu;
        if(index==0) menu.addCommandItem(&commands,audioSettingsCommand);
        else if(index==1) {
            menu.addCommandItem(&commands,allNotesOffCommand);menu.addCommandItem(&commands,panicCommand);menu.addSeparator();
            for(int command:{gsResetCommand,gmResetCommand,restartEngineCommand}) menu.addCommandItem(&commands,command);
        } else if(index==2) {
            menu.addCommandItem(&commands,showControlsCommand);menu.addCommandItem(&commands,hideControlsCommand);menu.addSeparator();
            menu.addCommandItem(&commands,activityDisplayCommand);menu.addCommandItem(&commands,readActivityCommand);
        } else {
            for(int command:{historyCommand,contextHelpCommand,manualCommand,dawPresetsCommand,projectCommand}) menu.addCommandItem(&commands,command);
            menu.addSeparator();
            for(int command:{updatesCommand,contactCommand,donateCommand,aboutCommand}) menu.addCommandItem(&commands,command);
        }
        return menu;
    }
    void menuItemSelected(int,int) override {}
    void showControls() {
        if(statusItem) statusItem->setControlsHidden(false);
#ifdef _WIN32
        if(window && (window->preferencesVisible() || !window->getContentComponent()->isVisible())) {
            if(settings) settings->dismiss(false);
            window->dismissPreferences();
        }
#endif
        if(window) restoreControlsWindow(*window);
        juce::Logger::writeToLog("Controls restored");
    }
    void hideControls() {
        if(!statusItem || !statusItem->available()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Menu-bar status item","The menu-bar item is unavailable, so the controls will stay visible.");return;
        }
        if(helpWindow) helpWindow->setVisible(false);
        if(settings) settings->dismiss(false);
        statusItem->setControlsHidden(true);
        if(window) window->setVisible(false);
#ifndef _WIN32
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
#endif
        juce::Logger::writeToLog("Controls hidden to menu bar; audio continues");
    }
    void showSettings(bool audioRecovery=false,const juce::String& audioError={},bool engineSetup=false) {
        if(statusItem) statusItem->setControlsHidden(false);
#ifndef _WIN32
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
#endif
        if(!settings) {
#ifdef _WIN32
            settings=std::make_unique<SettingsPanel>(devices,appPreferences,startup,updates,[this] {
#else
            settings=std::make_unique<SettingsWindow>(devices,appPreferences,startup,updates,[this] {
#endif
                if(testPhrase.isPlaying()) testPhrase.stop();
                else if(!testPhrase.start()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Test phrase","Choose an active audio output first.");
            },[this] { return testPhrase.isPlaying(); },[this] { confirmReset(restartEngineCommand); }
#ifndef _WIN32
            ,window.get()
#endif
            );
#ifdef _WIN32
            settings->onClose=[this] { window->dismissPreferences(); };
#else
            settings->onClose=[this] { showControls(); };
#endif
            settings->addKeyListener(commands.getKeyMappings());
        }
#ifdef _WIN32
        if(engineSetup) settings->selectGeneralTab();
        else if(audioRecovery) settings->selectAudioTab();
        if(window) {
            if(statusItem) statusItem->setControlsHidden(false);
            restoreControlsWindow(*window);window->showPreferences(*settings);
        }
#else
        if(auto* tabs=dynamic_cast<PreferenceTabs*>(settings->getContentComponent())) {
            if(engineSetup) tabs->setCurrentTabIndex(0);
            else if(audioRecovery) tabs->setCurrentTabIndex(1);
        }
        restoreControlsWindow(*settings);
#endif
        if(audioError.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Audio output unavailable",
            audioError+"\n\nThe saved audio output has been retained; no replacement interface was selected. Choose an available output in Audio/MIDI Preferences.");
    }
    void sendMidi(juce::MidiMessage message) {
        message.setTimeStamp(juce::Time::getMillisecondCounterHiRes()*0.001);
        activity.observe(message,juce::Time::getMillisecondCounter());
        output->midiCollector().addMessageToQueue(message);
    }
    void silence(bool immediate) {
        testPhrase.stop();
        for(int channel=1;channel<=16;++channel)
            sendMidi(immediate ? juce::MidiMessage::allSoundOff(channel) : juce::MidiMessage::allNotesOff(channel));
        juce::Logger::writeToLog(immediate ? "Panic requested" : "All Notes Off requested");
    }
    void confirmReset(int command) {
        const auto action=command==restartEngineCommand ? "Restart engine with defaults" : command==gsResetCommand ? "Send GS Reset" : "Send GM Reset";
        juce::Component::SafePointer<MainWindow> safe(window.get());
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,action,
            "This stops notes and resets sound settings. Stop external MIDI playback first. Audio output, audio buffer and shared bridge latency will not change.",
            "Reset","Cancel",window.get(),juce::ModalCallbackFunction::create([this,safe,command](int result) {
                if(!result || !safe) return;
                if(command==restartEngineCommand) restartEngine();
                else {
                    const uint8_t gs[]={0xf0,0x41,0x10,0x42,0x12,0x40,0,0x7f,0,0x41,0xf7};
                    const uint8_t gm[]={0xf0,0x7e,0x7f,0x09,0x01,0xf7};
                    sendMidi(command==gsResetCommand ? juce::MidiMessage(gs,sizeof(gs)) : juce::MidiMessage(gm,sizeof(gm)));
                    juce::Logger::writeToLog(command==gsResetCommand ? "GS reset requested" : "GM reset requested");
                }
            }));
    }
    void restartEngine() {
        juce::Logger::writeToLog("Fresh engine restart requested");
        if(virtualInput) virtualInput->stop();
        testPhrase.stop();devices.removeMidiInputDeviceCallback({},&midiInput);
        devices.removeAudioCallback(output.get());
        helpWindow.reset();window->clearContentComponent();output.reset();midiCollector.clear();processor.reset(createPluginFilter());
        output=std::make_unique<scbridge::PersistentEngineOutput>(*processor,midiCollector);
        devices.addAudioCallback(output.get());
        activity.clear();window->setEditor(*processor,activity,showActivity);
        if(virtualInput) virtualInput->start();
        devices.addMidiInputDeviceCallback({},&midiInput);
        juce::MemoryBlock defaults;processor->getStateInformation(defaults);
        if(!applicationData().getChildFile("Standalone.state").replaceWithData(defaults.getData(),defaults.getSize()))
            juce::Logger::writeToLog("Could not save reset standalone settings");
        juce::Logger::writeToLog("Fresh engine restart completed; audio configuration retained");
#ifndef _WIN32
        juce::AccessibilityHandler::postAnnouncement("Engine restarted with default settings",juce::AccessibilityHandler::AnnouncementPriority::medium);
#endif
    }
    static void openLink(const juce::String& url) {
        if(!juce::URL(url).launchInDefaultBrowser())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Open link","The browser could not open this link: "+url);
    }
    static juce::File resourceFile(const juce::String& name) {
        return juce::File::getSpecialLocation(juce::File::currentApplicationFile)
#ifdef _WIN32
            .getParentDirectory()
#else
            .getChildFile("Contents/Resources")
#endif
            .getChildFile(name);
    }
    static void openResource(const juce::String& name) {
        const auto file=resourceFile(name);
        if(!file.existsAsFile() || !file.startAsProcess())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Help","The bundled document could not be opened.");
    }
    void showControlHelp() {
        auto* focused=juce::Component::getCurrentlyFocusedComponent();
        while(focused && scbridge::contextHelp(*focused).isEmpty()) focused=focused->getParentComponent();
        const auto title=focused ? focused->getName() : "Sound Canvas Bridge";
        const auto help=focused ? scbridge::contextHelp(*focused) :
#ifdef _WIN32
            "Open Preferences with Ctrl+comma. The Help menu contains the full manual.";
#else
            "Open Preferences with Command+comma. The Help menu contains the full manual.";
#endif
        helpWindow=std::make_unique<ControlHelpWindow>(title,help,focused);
    }
    void saveAudioSettings() {
        auto xml=devices.createStateXml();if(!xml) return;
        auto text=xml->toString();
        if(text==lastAudioXml) return;
        auto target=applicationData().getChildFile("Audio.xml");
        if(!target.replaceWithText(text)) juce::Logger::writeToLog("Could not save audio settings");
        else { lastAudioXml=text;juce::Logger::writeToLog("Audio configuration saved"); }
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { saveAudioSettings(); }
    scbridge::AppPreferences appPreferences;
    scbridge::StartupService startup;
    scbridge::UpdateService updates{appPreferences,appVersion()};
    juce::AudioDeviceManager devices;
    scbridge::EngineTestPhrase testPhrase{devices};
    scbridge::SessionMidiInput midiCollector;
    std::unique_ptr<scbridge::PersistentEngineOutput> output;
    scbridge::MidiActivity activity;
    scbridge::ActivityMidiInput midiInput{midiCollector,activity};
    std::unique_ptr<juce::AudioProcessor> processor;
    std::unique_ptr<scbridge::VirtualMidiInput> virtualInput;
    std::unique_ptr<MainWindow> window;
#ifdef _WIN32
    std::unique_ptr<SettingsPanel> settings;
#else
    std::unique_ptr<SettingsWindow> settings;
#endif
    std::unique_ptr<ControlHelpWindow> helpWindow;
    std::unique_ptr<scbridge::MenuBarStatus> statusItem;
    juce::ApplicationCommandManager commands;
    juce::PopupMenu applicationExtras;
#ifdef _WIN32
    std::unique_ptr<scbridge::WindowsMenuBar> nativeMenu;
#endif
    std::unique_ptr<juce::FileLogger> logger;
    juce::String lastAudioXml;
    bool showActivity=true;
};
}

#ifndef SCBRIDGE_STANDALONE_TEST
START_JUCE_APPLICATION(Standalone)
#endif
