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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace {
enum Command : int { audioSettingsCommand=0x20001,contextHelpCommand,manualCommand,historyCommand,
                    updatesCommand,projectCommand,contactCommand,donateCommand,allNotesOffCommand,
                    panicCommand,gsResetCommand,gmResetCommand,restartEngineCommand,activityDisplayCommand,
                    readActivityCommand,showControlsCommand,hideControlsCommand,aboutCommand };
constexpr const char* virtualPortName="Sound Canvas Bridge";
constexpr const char* donateUrl="https://onj.me/donate";
constexpr const char* contactUrl="https://onj.me/contact";
constexpr const char* projectUrl="https://github.com/OnjLouis/sound-canvas-bridge";

bool commandInfo(juce::CommandID command,juce::ApplicationCommandInfo& result) {
    switch(command) {
        case audioSettingsCommand:
            result.setInfo("Preferences...","Audio, MIDI, update and startup preferences","Audio",0);
            result.addDefaultKeypress(',',juce::ModifierKeys::commandModifier);
            result.addDefaultKeypress('u',juce::ModifierKeys::commandModifier|juce::ModifierKeys::shiftModifier);break;
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
        case allNotesOffCommand: result.setInfo("All Notes Off","Release notes on all 16 MIDI channels; sustain may continue","MIDI",0);break;
        case panicCommand:
            result.setInfo("Panic: All Sounds Off","Immediately stop voices without changing programs or effects","MIDI",0);
            result.addDefaultKeypress('p',juce::ModifierKeys::commandModifier|juce::ModifierKeys::shiftModifier);break;
        case gsResetCommand: result.setInfo("Send GS Reset...","Reset all parts and effects to native GS defaults","MIDI",0);break;
        case gmResetCommand: result.setInfo("Send GM Reset...","Reset the original engine to General MIDI mode","MIDI",0);break;
        case restartEngineCommand:
            result.setInfo("Restart Engine with Defaults...","Recover even when normal MIDI reset messages are disabled","MIDI",0);
            result.addDefaultKeypress('r',juce::ModifierKeys::commandModifier|juce::ModifierKeys::shiftModifier);break;
        case activityDisplayCommand:
            result.setInfo("Show MIDI Activity","Show or hide the coloured channel activity display","View",0);
            result.addDefaultKeypress('d',juce::ModifierKeys::commandModifier|juce::ModifierKeys::shiftModifier);break;
        case readActivityCommand:
            result.setInfo("Read MIDI Activity","Read a snapshot without continuous announcements","View",0);
            result.addDefaultKeypress('a',juce::ModifierKeys::commandModifier|juce::ModifierKeys::shiftModifier);break;
        case aboutCommand: result.setInfo("About Sound Canvas Bridge","Show application information","Help",0);break;
        case showControlsCommand: result.setInfo("Show Controls","Show the Sound Canvas controls window","View",0);break;
        case hideControlsCommand:
            result.setInfo("Hide to Menu Bar","Keep playback running with only the menu-bar status item visible","View",0);
            result.addDefaultKeypress('h',juce::ModifierKeys::commandModifier|juce::ModifierKeys::shiftModifier);break;
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
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support/OnjMusic/SoundCanvasBridge");
}

class MidiInputChoices final : public juce::Component {
public:
    explicit MidiInputChoices(juce::AudioDeviceManager& manager):devices(manager) {
        setName("System MIDI inputs");setComponentID("midiInputs");
        viewport.setViewedComponent(&content,false);viewport.setScrollBarsShown(true,false);
        viewport.setName("Available MIDI inputs");addAndMakeVisible(viewport);refresh();
    }
    void refresh() {
        const auto available=juce::MidiInput::getAvailableDevices();
        if(available!=ports) {
            ports=available;buttons.clear();
            for(const auto& port:ports) {
                auto button=std::make_unique<juce::ToggleButton>(port.name);
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
    juce::AudioDeviceManager& devices;
    juce::Array<juce::MidiDeviceInfo> ports;
    juce::Component content;
    juce::Viewport viewport;
    std::vector<std::unique_ptr<juce::ToggleButton>> buttons;
};

class AudioPreferences final : public juce::Component,private juce::ChangeListener,private juce::Timer {
public:
    explicit AudioPreferences(juce::AudioDeviceManager& manager,std::function<void()> test={},std::function<bool()> playing={},std::function<void()> engineChanged={})
        :devices(manager),midi(manager),playTest(std::move(test)),testPlaying(std::move(playing)),onEngineChanged(std::move(engineChanged)) {
        setName("Audio settings");setTitle("Audio settings");
        setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);
        const char* names[]={"Output device","Sample rate","Audio buffer"};
        const char* help[]={"Chooses the audio output used by this standalone module. No audio input is opened.",
            "Chooses the sample rate supported by the selected output device.",
            "Chooses the audio device buffer in samples. This is separate from the shared bridge latency."};
        for(unsigned i=0;i<3;++i) {
            labels[i].setText(names[i],juce::dontSendNotification);labels[i].setAccessible(false);
            auto& choice=choices[i];choice.setName(names[i]);choice.setTitle(names[i]);
            choice.setComponentID(juce::String("audioSetting")+juce::String(i));
            choice.setDescription(names[i]);choice.setHelpText(help[i]);choice.setTooltip(help[i]);
            choice.setWantsKeyboardFocus(true);choice.setExplicitFocusOrder(static_cast<int>(i+1));
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
        engineHeading.setText("Sound Canvas VA location",juce::dontSendNotification);engineHeading.setAccessible(false);
        enginePath.setName("Sound Canvas VA location");enginePath.setTitle(enginePath.getName());enginePath.setComponentID("engineLocation");
        enginePath.setReadOnly(true);enginePath.setMultiLine(false);enginePath.setTabKeyUsedAsCharacter(false);enginePath.setExplicitFocusOrder(8);
        enginePath.setDescription(enginePath.getName());enginePath.setHelpText("Location of the original Sound Canvas VA VST bundle. Select and copy this path, or use Browse to choose another installation.");
        enginePath.setTooltip(enginePath.getHelpText());
        browse.setButtonText("Browse...");browse.setName("Browse for Sound Canvas VA");browse.setExplicitFocusOrder(9);
        browse.setDescription(browse.getName());browse.setHelpText("Choose the original Sound Canvas VA VST bundle, not the bridge. A valid selection is saved and offers an engine restart.");
        browse.setTooltip(browse.getHelpText());browse.onClick=[this] { chooseEngine(); };
        engineStatus.setName("Engine location status");engineStatus.setTitle(engineStatus.getName());engineStatus.setWantsKeyboardFocus(true);engineStatus.setExplicitFocusOrder(10);
        for(auto* control:std::initializer_list<juce::Component*>{&engineHeading,&enginePath,&browse,&engineStatus}) addAndMakeVisible(control);
        updateEnginePath();devices.addChangeListener(this);setSize(650,630);update();startTimer(150);timerCallback();
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
        for(unsigned i=0;i<3;++i) { labels[i].setBounds(12,12+44*i,150,32);choices[i].setBounds(164,12+44*i,getWidth()-176,32); }
        refresh.setBounds(164,148,160,32);midiHeading.setBounds(12,190,getWidth()-24,24);
        midi.setBounds(12,218,getWidth()-24,170);testButton.setBounds(164,400,200,32);
        status.setBounds(12,444,getWidth()-24,56);
        engineHeading.setBounds(12,502,getWidth()-24,24);enginePath.setBounds(12,532,getWidth()-144,32);
        browse.setBounds(getWidth()-120,532,108,32);engineStatus.setBounds(12,576,getWidth()-24,44);
    }
private:
    void updateEnginePath() {
        scbridge::EngineLocation location;const auto path=location.bundle();juce::String error;
        enginePath.setText(path.getFullPathName(),false);
        engineStatus.setText(scbridge::EngineLocation::validate(path,error) ? "Sound Canvas VA installation found." : "Sound Canvas VA not found. Use Browse to choose its VST bundle.",juce::dontSendNotification);
    }
    void chooseEngine() {
        chooser=std::make_unique<juce::FileChooser>("Choose Sound Canvas VA VST bundle",scbridge::EngineLocation().bundle().getParentDirectory(),"*.vst");
        juce::Component::SafePointer<AudioPreferences> safe(this);
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
    static void fill(juce::ComboBox& choice,const juce::StringArray& names,int selected) {
        juce::StringArray previous;
        for(int i=0;i<choice.getNumItems();++i) previous.add(choice.getItemText(i));
        if(previous!=names) { choice.clear(juce::dontSendNotification);choice.addItemList(names,1); }
        choice.setSelectedItemIndex(selected,juce::dontSendNotification);
        choice.setEnabled(!names.isEmpty());
    }
    void update() {
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
            "Virtual MIDI input: Sound Canvas Bridge. No audio input enabled." : "No audio output device is active.",juce::dontSendNotification);
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
    std::function<void()> onEngineChanged;
    juce::Label engineHeading,engineStatus;
    juce::TextEditor enginePath;
    juce::TextButton browse;
    std::unique_ptr<juce::FileChooser> chooser;
    scbridge::TransientTooltip tips{this,700};
    juce::String error;
};

bool focusFirstControl(juce::Component& root) {
    // Keyboard traversers omit non-focusable containers, hiding the editor's
    // controls when called on the surrounding standalone layout.
    juce::FocusTraverser traverser;
        for(auto* control:traverser.getAllComponents(&root))
            if(control->isShowing() && control->isEnabled()) {
                if(control->isKeyboardFocusContainer() && control!=&root && focusFirstControl(*control)) return true;
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
            if(safe->isShowing() && [NSApp isActive] && peer &&
               [[(NSView*)peer->getNativeHandle() window] isKeyWindow] && focusFirstControl(*safe)) delete this;
        }
        juce::Component::SafePointer<juce::Component> safe;
        const int generation;
        int attempts=0;
    };
    // Native window activation may finish after the first message-loop turn.
    new Request(root,generation);
}

void restoreControlsWindow(juce::DocumentWindow& window) {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp unhide:nil];
    window.setMinimised(false);window.setVisible(true);
    [NSApp activateIgnoringOtherApps:YES];
    window.toFront(true);
    if(auto* peer=window.getPeer()) [[(NSView*)peer->getNativeHandle() window] makeKeyAndOrderFront:nil];
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
        auto* general=new scbridge::GeneralPreferences(preferences,startup,updates);general->setComponentID("generalPreferences");
        addTab("General",juce::Colours::lightgrey,general,true);
        addTab("Audio and MIDI",juce::Colours::lightgrey,new AudioPreferences(manager,std::move(test),std::move(playing),std::move(changed)),true);
        setSize(650,670);
    }
    void currentTabChanged(int,const juce::String&) override {
        if(!getTabbedButtonBar().hasKeyboardFocus(true))
            if(auto* content=getCurrentContentComponent()) scheduleInitialFocus(*content);
    }
};

class SettingsWindow final : public juce::DocumentWindow {
public:
    SettingsWindow(juce::AudioDeviceManager& manager,scbridge::AppPreferences& preferences,scbridge::StartupService& startup,
                   scbridge::UpdateService& updates,std::function<void()> test,std::function<bool()> playing,std::function<void()> engineChanged)
        :DocumentWindow("Preferences",juce::Colours::darkgrey,closeButton) {
        setUsingNativeTitleBar(true);setContentOwned(new PreferenceTabs(manager,preferences,startup,updates,std::move(test),std::move(playing),std::move(engineChanged)),true);
        centreWithSize(getWidth(),getHeight());restoreControlsWindow(*this);
    }
    void closeButtonPressed() override { setVisible(false); }
    bool keyPressed(const juce::KeyPress& key) override {
        if(key==juce::KeyPress::escapeKey) { setVisible(false);return true; }
        return DocumentWindow::keyPressed(key);
    }
};

class MainContent final : public juce::Component {
public:
    MainContent(juce::AudioProcessor& processor,scbridge::MidiActivity& activity,bool visible)
        :editor(processor.createEditorAndMakeActive()),display(activity) {
        setName("Sound Canvas controls");addAndMakeVisible(*editor);addChildComponent(display);showActivity(visible);
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

class MainWindow final : public juce::DocumentWindow {
public:
    MainWindow(juce::AudioProcessor& processor,scbridge::MidiActivity& activity,bool showActivity,bool initiallyVisible=true)
        :DocumentWindow("Sound Canvas Bridge",juce::Colours::black,allButtons) {
        setUsingNativeTitleBar(true);setEditor(processor,activity,showActivity);
        centreWithSize(getWidth(),getHeight());setVisible(initiallyVisible);
    }
    void setEditor(juce::AudioProcessor& processor,scbridge::MidiActivity& activity,bool visible) {
        setContentOwned(new MainContent(processor,activity,visible),true);
        scheduleInitialFocus(*getContentComponent());
    }
    void showActivity(bool visible) { if(auto* content=dynamic_cast<MainContent*>(getContentComponent())) content->showActivity(visible); }
    std::function<void()> onClose;
    void closeButtonPressed() override { if(onClose) onClose();else setVisible(false); }
};

class Standalone final : public juce::JUCEApplication,private juce::MenuBarModel,private juce::ChangeListener {
public:
    const juce::String getApplicationName() override { return "Sound Canvas Bridge"; }
    const juce::String getApplicationVersion() override { return appVersion(); }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override {
        juce::LookAndFeel::getDefaultLookAndFeel().setUsingNativeAlertWindows(true);
        auto directory=applicationData();
        if(!directory.createDirectory().wasOk()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Sound Canvas Bridge","Could not create the application settings folder.");
            quit();return;
        }
        logger.reset(juce::FileLogger::createDefaultAppLogger("OnjMusic/SoundCanvasBridge","Standalone.log","Standalone started",262144));
        juce::Logger::setCurrentLogger(logger.get());
        processor.reset(createPluginFilter());
        auto session=directory.getChildFile("Standalone.state");
        if(session.existsAsFile() && session.getSize()<=65536) {
            juce::MemoryBlock state;session.loadFileAsData(state);
            processor->setStateInformation(state.getData(),static_cast<int>(state.getSize()));
        }
        auto audio=juce::XmlDocument::parse(directory.getChildFile("Audio.xml"));
        juce::AudioDeviceManager::AudioDeviceSetup preferred;preferred.bufferSize=128;
        const auto error=devices.initialise(0,2,audio.get(),true,{},&preferred);
        if(error.isNotEmpty()) juce::Logger::writeToLog("Audio initialisation: "+error);
        output=std::make_unique<scbridge::PersistentEngineOutput>(*processor,midiCollector);
        devices.addAudioCallback(output.get());
        devices.addMidiInputDeviceCallback({},&midiInput);
        virtualInput=juce::MidiInput::createNewDevice(virtualPortName,&midiInput);
        if(virtualInput) { virtualInput->start();juce::Logger::writeToLog("Virtual MIDI input ready"); }
        else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"MIDI input","Could not create the Sound Canvas Bridge virtual MIDI input.");
        if(auto ui=juce::XmlDocument::parse(directory.getChildFile("Interface.xml"))) showActivity=ui->getBoolAttribute("activity",true);
        const bool hiddenStart=appPreferences.startHidden();
        if(hiddenStart) [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        window=std::make_unique<MainWindow>(*processor,activity,showActivity,!hiddenStart);
        window->onClose=[this] { hideControls(); };
        if(!hiddenStart) { window->toFront(true);scheduleInitialFocus(*window->getContentComponent()); }
        commands.registerAllCommandsForTarget(this);commands.setFirstCommandTarget(this);
        window->addKeyListener(commands.getKeyMappings());
        statusItem=std::make_unique<scbridge::MenuBarStatus>([this](int command) { commands.invokeDirectly(command,true); },
            showControlsCommand,audioSettingsCommand,panicCommand,juce::StandardApplicationCommandIDs::quit,true);
        applicationExtras.addCommandItem(&commands,updatesCommand);applicationExtras.addCommandItem(&commands,donateCommand);
        applicationExtras.addCommandItem(&commands,projectCommand);
        juce::MenuBarModel::setMacMainMenu(this,&applicationExtras);
        devices.addChangeListener(this);
        saveAudioSettings();
        const bool engineReady=scbridge::EngineLocation().available();
        if(scbridge::AppPreferences::shouldHide(hiddenStart,statusItem->available(),engineReady,error.isEmpty() && devices.getCurrentAudioDevice()!=nullptr)) {
            hideControls();
        } else {
            if(hiddenStart) showControls();
            if(error.isNotEmpty() || !engineReady || devices.getCurrentAudioDevice()==nullptr) showSettings(true);
        }
        updates.start();
    }
    void shutdown() override {
        updates.stop();
        devices.removeChangeListener(this);juce::MenuBarModel::setMacMainMenu(nullptr);
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
            for(int command:{historyCommand,contextHelpCommand,manualCommand,projectCommand}) menu.addCommandItem(&commands,command);
            menu.addSeparator();
            for(int command:{updatesCommand,contactCommand,donateCommand,aboutCommand}) menu.addCommandItem(&commands,command);
        }
        return menu;
    }
    void menuItemSelected(int,int) override {}
    void showControls() {
        if(statusItem) statusItem->setControlsHidden(false);
        if(window) restoreControlsWindow(*window);
        juce::Logger::writeToLog("Controls restored");
    }
    void hideControls() {
        if(!statusItem || !statusItem->available()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Menu-bar status item","The menu-bar item is unavailable, so the controls will stay visible.");return;
        }
        if(helpWindow) helpWindow->setVisible(false);
        if(settings) settings->setVisible(false);
        statusItem->setControlsHidden(true);
        if(window) window->setVisible(false);
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        juce::Logger::writeToLog("Controls hidden to menu bar; audio continues");
    }
    void showSettings(bool audioRecovery=false) {
        if(statusItem) statusItem->setControlsHidden(false);
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        if(!settings) {
            settings=std::make_unique<SettingsWindow>(devices,appPreferences,startup,updates,[this] {
                if(testPhrase.isPlaying()) testPhrase.stop();
                else if(!testPhrase.start()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Test phrase","Choose an active audio output first.");
            },[this] { return testPhrase.isPlaying(); },[this] { confirmReset(restartEngineCommand); });
            settings->addKeyListener(commands.getKeyMappings());
        }
        if(audioRecovery)
            if(auto* tabs=dynamic_cast<PreferenceTabs*>(settings->getContentComponent())) tabs->setCurrentTabIndex(1);
        restoreControlsWindow(*settings);
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
        juce::AccessibilityHandler::postAnnouncement("Engine restarted with default settings",juce::AccessibilityHandler::AnnouncementPriority::medium);
    }
    static void openLink(const juce::String& url) {
        if(!juce::URL(url).launchInDefaultBrowser())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Open link","The browser could not open this link: "+url);
    }
    static void openResource(const juce::String& name) {
        auto file=juce::File::getSpecialLocation(juce::File::currentApplicationFile).getChildFile("Contents/Resources").getChildFile(name);
        if(!file.existsAsFile() || !file.startAsProcess())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Help","The bundled document could not be opened.");
    }
    void showControlHelp() {
        auto* focused=juce::Component::getCurrentlyFocusedComponent();
        while(focused && focused->getHelpText().isEmpty()) focused=focused->getParentComponent();
        const auto title=focused ? focused->getName() : "Sound Canvas Bridge";
        const auto help=focused ? focused->getHelpText() : "Open Preferences with Command+comma or Command+Shift+U. The Help menu contains the full manual.";
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
    std::unique_ptr<juce::MidiInput> virtualInput;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<SettingsWindow> settings;
    std::unique_ptr<ControlHelpWindow> helpWindow;
    std::unique_ptr<scbridge::MenuBarStatus> statusItem;
    juce::ApplicationCommandManager commands;
    juce::PopupMenu applicationExtras;
    std::unique_ptr<juce::FileLogger> logger;
    juce::String lastAudioXml;
    bool showActivity=true;
};
}

#ifndef SCBRIDGE_STANDALONE_TEST
START_JUCE_APPLICATION(Standalone)
#endif
