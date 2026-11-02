#include <juce_audio_utils/juce_audio_utils.h>
#include "Protocol.h"
#include "Pipeline.h"
#include "Performance.h"
#include "EngineLocation.h"
#include "TransientTooltip.h"
#include "NumericSlider.h"
#include "StandaloneEditor.h"
#include "ControlHelp.h"
#ifdef _WIN32
#include "WindowsIpc.h"
#else
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <spawn.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <dlfcn.h>
#include <cerrno>
#include <CoreFoundation/CoreFoundation.h>
extern char** environ;
#endif

namespace {
void moduleAnchor() {}
constexpr auto ids=[] { std::array<const char*,scbridge::parameterCount> value{};
    for(unsigned i=0;i<scbridge::parameterCount;++i) value[i]=scbridge::controls[i].id;return value; }();
constexpr auto names=[] { std::array<const char*,scbridge::parameterCount> value{};
    for(unsigned i=0;i<scbridge::parameterCount;++i) value[i]=scbridge::controls[i].name;return value; }();
constexpr auto defaults=[] { std::array<int,scbridge::parameterCount> value{};
    for(unsigned i=0;i<scbridge::parameterCount;++i) value[i]=scbridge::controls[i].initial;return value; }();
const char* descriptions[] = {
    "Selects the MIDI part, from 1 to 16, edited by the sound and mixer controls. Drum kits normally use part 10. Changing Part views that part's settings without selecting a new sound or resetting its edits. In Multitimbral routing, incoming notes still play on their original MIDI channels.",
    "Selects the SC-55, SC-88, SC-88 Pro or SC-8820 compatibility map.",
    "Selects the bank variation within the current sound map.",
    "Selects the program, displayed from 1 to 128.",
    "Sets the selected part's volume, from 0 to 127.",
    "Positions the selected part from left to right; 64 is centre.",
    "Sets expression for the selected part, from 0 to 127.",
    "Sets modulation for the selected part, from 0 to 127.",
    "Sets the selected part's reverb send; 0 removes its reverb send.",
    "Sets the selected part's chorus send; 0 removes its chorus send.",
    "Sets the selected part's delay send; 0 removes its delay send.",
    "Adjusts filter brightness for the selected melodic part; 64 is neutral.",
    "Adjusts filter resonance for the selected melodic part; 64 is neutral.",
    "Adjusts attack time for the selected melodic part; 64 is neutral.",
    "Adjusts release time for the selected melodic part; 64 is neutral.",
    "Multitimbral preserves incoming MIDI channels so up to 16 parts can play independently. Selected Part redirects incoming channel messages to the chosen Part, which is useful for playing an NKS sound from a keyboard. System Exclusive messages pass through unchanged in either mode."
};

using KeyboardSlider = scbridge::NumericSlider;
bool choiceControl(unsigned i) {
    const auto unit=scbridge::controls[i].unit;
    return i==1 || i==15 || unit==scbridge::ControlUnit::ReverbType || unit==scbridge::ControlUnit::ChorusType ||
           unit==scbridge::ControlUnit::DelayType || unit==scbridge::ControlUnit::Setup ||
           unit==scbridge::ControlUnit::InsertionType ||
           unit==scbridge::ControlUnit::LowFrequency || unit==scbridge::ControlUnit::HighFrequency;
}

juce::String controlHelp(unsigned index) {
    if(index<scbridge::legacyParameterCount) return descriptions[index];
    const auto& control=scbridge::controls[index];
    if(index==scbridge::drumNoteIndex)
        return "Selects the MIDI drum note edited by the drum controls. Selecting a note does not play it or change its settings. All 128 MIDI notes are available.";
    if(index==scbridge::drumSetupIndex)
        return "Selects GS Drum Setup 1 or 2. Part 10 normally uses Setup 1; a MIDI file can assign either setup to other parts. This selects an editor, not a new kit.";
    if(index==scbridge::insertionSelectorIndex)
        return "Selects insertion-effect parameter 1 to 20 for the Insertion Value control. Selection alone does not alter any effect setting. Parameter functions depend on the chosen algorithm; unused slots have no audible effect. The numbered EFX Parameter controls edit the same slots directly.";
    if(index==scbridge::insertionTypeIndex)
        return "Selects Thru or one of 64 insertion-effect algorithms shared by this instance. A type change recalls that algorithm's native defaults. Turn on Part MFX for each part you want to hear through the effect. Part MFX is routing, not a second effect instance.";
    if(index==23)
        return "Routes the selected MIDI part through the shared insertion effect. Select a non-Thru Insertion Type to hear processing. Other parts have independent routing switches but share this instance's insertion algorithm and settings.";
    juce::String help=juce::String("Adjusts ")+control.name+". ";
    if(control.scope==scbridge::ControlScope::Global || control.scope==scbridge::ControlScope::InsertionParameter)
        help+="This is shared by all MIDI parts in this instance. Effect types recall their associated settings; their readouts follow the engine. ";
    else if(control.scope==scbridge::ControlScope::Drum)
        help+="Edits the selected MIDI note within Drum Setup 1 or 2. Values follow the kit's actual settings. ";
    else help+="Applies to the selected MIDI part. ";
    if(control.unit==scbridge::ControlUnit::Raw)
        help+="Numbers use the GS parameter's native units, not seconds or hertz. ";
    return help;
}

class BridgeEditor final : public juce::AudioProcessorEditor, private juce::Timer, private juce::KeyListener {
public:
    BridgeEditor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& state,
                 std::function<juce::String()> statusText, std::function<int()> latencyValue,
                 std::function<void(int)> changeLatency, std::function<bool()> advancedValue)
        : AudioProcessorEditor(p), parameters(state), readStatus(std::move(statusText)), readLatency(std::move(latencyValue)),
          readAdvanced(std::move(advancedValue)) {
        setName("Sound Canvas controls"); setTitle("Sound Canvas controls");
        tooltips.setComponentID("controlHelp");
        setFocusContainerType(juce::Component::FocusContainerType::keyboardFocusContainer);
        for (unsigned i = 0; i < scbridge::parameterCount; ++i) {
            const bool isChoice=choiceControl(i), isSwitch=scbridge::controls[i].unit==scbridge::ControlUnit::Switch;
            auto& control=controlAt(i);
            control.setComponentID(ids[i]); control.setName(names[i]); control.setTitle(names[i]);
            auto help = controlHelp(i);
            if (!isChoice && !isSwitch) help += " Arrow keys change one step; Page Up and Page Down change ten steps; Home and End select the limits. Enter opens direct value entry; Enter confirms and Escape cancels.";
            control.setDescription(names[i]); control.setHelpText(help);
            control.setWantsKeyboardFocus(true); control.setExplicitFocusOrder(static_cast<int>(i + 1));
            labels[i].setText(names[i], juce::dontSendNotification); labels[i].setAccessible(false);
            addAndMakeVisible(labels[i]); addAndMakeVisible(control);
            if(isSwitch) {
                toggles[i].setTooltip(help);
                buttonAttachments[i]=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state,ids[i],toggles[i]);
            } else if (isChoice) {
                auto& choice = choices[i]; choice.setTooltip(help);
                auto* parameter=state.getParameter(ids[i]);
                juce::StringArray items;
                for(int value=scbridge::controls[i].minimum;value<=scbridge::controls[i].maximum;++value)
                    items.add(parameter->getText(parameter->convertTo0to1(static_cast<float>(value)),128));
                choice.addItemList(items,1);
                choiceAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, ids[i], choice);
            } else {
                auto& slider = sliders[i]; slider.setTooltip(help);
#ifdef _WIN32
                const auto unit=scbridge::controls[i].unit;
                slider.setAccessibilityValueOffset(i==0 || i==3 ? 1 : unit==scbridge::ControlUnit::Signed64 ? -64 :
                    unit==scbridge::ControlUnit::DrumPitch ? -60 : 0);
#endif
                slider.setSliderStyle(juce::Slider::LinearHorizontal);
                slider.configureValueField(i<scbridge::legacyParameterCount ? 116 : 200);
                auto* parameter = state.getParameter(ids[i]);
                slider.onStepStart = [parameter] { parameter->beginChangeGesture(); };
                slider.onStepEnd = [parameter] { parameter->endChangeGesture(); };
                attachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, ids[i], slider);
            }
        }
        page.setComponentID("controlPage");page.setName("Control page");page.setTitle("Control page");
        page.setTooltip("Selects Main, Tone, Master, Reverb, Chorus, Delay, EQ, Drums or Insertion controls. Insertion Parameters pages expose all 20 slots directly. This changes only the displayed controls, not the sound.");
        page.setDescription(page.getName());page.setHelpText(page.getTooltip());
        page.addItemList({"Main","Tone","Master","Reverb","Chorus","Delay","EQ","Drums","Insertion","Insertion Parameters 1-10","Insertion Parameters 11-20"},1);
        page.setSelectedId(1,juce::dontSendNotification);
        page.onChange=[this] { showPage(); };page.setExplicitFocusOrder(1);
        addAndMakeVisible(page);
        pageLabel.setText("Control page",juce::dontSendNotification);pageLabel.setAccessible(false);
        addAndMakeVisible(pageLabel);
        status.setComponentID("engineStatus"); status.setName("Engine status"); status.setTitle("Engine status");
        latency.setComponentID("bridgeLatency"); latency.setName("Bridge latency"); latency.setTitle("Bridge latency");
        auto help = "Default bridge latency shared by the standalone app and "
#ifdef _WIN32
            "VST3 plug-in. "
#else
            "AU/VST3 plug-ins. "
#endif
            "Lower values need more scheduling headroom. Changing tracks or host block sizes does not change this delay. Adjust while stopped to avoid a brief gap. Arrow keys change 32 samples; Page Up and Page Down change 320; Home and End select the limits. Enter opens direct value entry; Enter confirms and Escape cancels.";
        latency.setDescription(latency.getName()); latency.setHelpText(help); latency.setTooltip(help);
        latency.setWantsKeyboardFocus(true); latency.setExplicitFocusOrder(100);
        latency.setSliderStyle(juce::Slider::LinearHorizontal);
        latency.setRange(scbridge::minimumLatency, scbridge::maximumLatency, scbridge::latencyStep);
        latency.configureValueField(150);
        latency.setTextValueSuffix(" samples"); latency.setValue(readLatency(), juce::dontSendNotification);
        latency.onValueChange = [this, changeLatency] { changeLatency(static_cast<int>(latency.getValue())); };
        latencyLabel.setText("Bridge latency", juce::dontSendNotification); latencyLabel.setAccessible(false);
        addAndMakeVisible(latencyLabel); addAndMakeVisible(latency);
        status.setWantsKeyboardFocus(true); status.setExplicitFocusOrder(101);
        status.setDescription(status.getName());
        status.setHelpText("Reports engine readiness, scheduling interruptions and missing engine errors.");
        addAndMakeVisible(status);
        auto* display=juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        setSize(700,display ? juce::jlimit(700,762,juce::roundToInt(display->userBounds.getHeight())-50) : 762);
        showPage(); timerCallback(); startTimerHz(2);
    }
    ~BridgeEditor() override { stopTimer(); setVisible(false); }
    void configureStandalone(std::function<void(int)> applyMap) {
        if(mapAll) return;
        mapAll=std::move(applyMap);
        choices[1].addKeyListener(this);
        sliders[0].setComponentID("selectedPartParameter");
        partScope.setComponentID("part");partScope.setName("Part");partScope.setTitle("Part");
        partScope.setDescription("Part");
        partScope.getProperties().set(scbridge::explicitHelpProperty,"Select a part from 1 to 16, or All to apply the next Sound Map choice to every part. All does not broadcast other controls; part-specific controls are unavailable until you select a numbered part. Incoming MIDI keeps its own routing.");
        partScope.setWantsKeyboardFocus(true);
        partScope.addItem("All (Sound Map)",1);
        for(int part=1;part<=16;++part) partScope.addItem(juce::String(part),part+1);
        partScope.setSelectedId(static_cast<int>(sliders[0].getValue())+2,juce::dontSendNotification);
        partScope.onChange=[this] {
            const bool all=partScope.getSelectedId()==1;
            if(all==allParts && all) return;
            allParts=all;
            if(all) {
                lastRequestedMap=0;
                choiceAttachments[1].reset();
                choices[1].setTextWhenNothingSelected("Choose map for all parts");
                choices[1].setSelectedId(0,juce::dontSendNotification);
                choices[1].setText({},juce::dontSendNotification);
                choices[1].onChange=[this] {
                    const int map=choices[1].getSelectedId();
                    if(!allParts || map<=0) return;
                    lastRequestedMap=map;
                    const auto name=choices[1].getItemText(map-1);
                    mapAll(map);
                    // Leave the menu unselected so the same map can be requested again after external MIDI.
                    choices[1].setText("Last requested: "+name,juce::dontSendNotification);
                };
            } else {
                choices[1].onChange=nullptr;
                auto* part=parameters.getParameter(ids[0]);part->beginChangeGesture();
                part->setValueNotifyingHost(part->convertTo0to1(static_cast<float>(partScope.getSelectedId()-2)));part->endChangeGesture();
                choiceAttachments[1]=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(parameters,ids[1],choices[1]);
            }
            updateEnabledControls();
        };
        choices[1].setHelpText("With a numbered part selected, changes that part's map. With All selected, applies the chosen map to all 16 parts without changing their bank, program, mixer or effects. This is the last requested map; subsequent MIDI can override individual parts.");
        choices[1].setTooltip(choices[1].getHelpText());
        sliders[0].setVisible(false);addAndMakeVisible(partScope);showPage();
    }
    void resized() override {
        pageLabel.setBounds(12,10,200,30);page.setBounds(214,10,getWidth()-226,30);
        const int rowHeight=std::min(36,(getHeight()-180)/16);
        int row=0;
        for (unsigned i = 0; i < scbridge::parameterCount; ++i) {
            if(!labels[i].isVisible()) continue;
            int y = 48 + row++ * rowHeight;
            labels[i].setBounds(12, y, 200, 30);controlAt(i).setBounds(214,y,getWidth()-226,30);
        }
        latencyLabel.setBounds(12, getHeight()-134, 200, 30); latency.setBounds(214, getHeight()-134, getWidth() - 226, 30);
        status.setBounds(12, getHeight()-92, getWidth() - 24, 80);
    }
private:
    bool keyPressed(const juce::KeyPress& key,juce::Component* origin) override {
        if(!allParts || origin!=&choices[1] || !choices[1].isEnabled() || choices[1].isPopupActive() ||
           key.getModifiers().isAnyModifierKeyDown()) return false;
        const int count=choices[1].getNumItems(),code=key.getKeyCode();
        if(!count) return false;
        int next=lastRequestedMap;
        if(code==juce::KeyPress::downKey || code==juce::KeyPress::rightKey) next=next ? std::min(count,next+1) : 1;
        else if(code==juce::KeyPress::upKey || code==juce::KeyPress::leftKey) next=next ? std::max(1,next-1) : count;
        else if(code==juce::KeyPress::homeKey) next=1;
        else if(code==juce::KeyPress::endKey) next=count;
        else return false;
        // The request-only display deliberately has no selected menu item, so
        // remember its position separately instead of nudging ComboBox's zero ID.
        if(next!=lastRequestedMap) choices[1].setSelectedId(next,juce::sendNotificationSync);
        return true;
    }
    juce::Component& controlAt(unsigned i) {
        if(i==0 && mapAll) return partScope;
        if(scbridge::controls[i].unit==scbridge::ControlUnit::Switch) return toggles[i];
        if(choiceControl(i)) return choices[i];
        return sliders[i];
    }
    void showPage() {
        const int selected=page.getSelectedId();
        for(unsigned i=0;i<scbridge::parameterCount;++i) {
            const bool visible=selected==1 ? i<16 : selected==2 ? i>=16 && i<24 :
                selected==3 ? i>=24 && i<27 : selected==4 ? i>=27 && i<34 :
                selected==5 ? i>=34 && i<43 : selected==6 ? i>=43 && i<54 :
                selected==7 ? i>=54 && i<58 : selected==8 ? i>=58 && i<66 :
                selected==9 ? i==23 || (i>=scbridge::insertionTypeIndex && i<scbridge::insertionFirstDirectIndex) :
                selected==10 ? i>=scbridge::insertionFirstDirectIndex && i<scbridge::insertionFirstDirectIndex+10 :
                i>=scbridge::insertionFirstDirectIndex+10;
            labels[i].setVisible(visible);
            auto& control=controlAt(i);control.setVisible(visible);
            control.setExplicitFocusOrder(static_cast<int>(i+2));
        }
        resized();
    }
    void timerCallback() override {
        updateEnabledControls();
        if(mapAll && !allParts) partScope.setSelectedId(static_cast<int>(sliders[0].getValue())+2,juce::dontSendNotification);
        auto* focus = juce::Component::getCurrentlyFocusedComponent();
        if (!latency.isMouseButtonDown() && !(focus && latency.isParentOf(focus)))
            latency.setValue(readLatency(), juce::dontSendNotification);
        auto text = readStatus();
        if (text == status.getText()) return;
        status.setText(text, juce::dontSendNotification);
        if (auto* handler = status.getAccessibilityHandler())
            handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    }
    void updateEnabledControls() {
        for(unsigned i=0;i<scbridge::parameterCount;++i) {
            const auto scope=scbridge::controls[i].scope;
            const bool perPart=(i>=2 && i<scbridge::legacyParameterCount) || scope==scbridge::ControlScope::Part;
            controlAt(i).setEnabled((i<scbridge::legacyParameterCount || readAdvanced()) && !(allParts && perPart));
        }
    }
    juce::AudioProcessorValueTreeState& parameters;
    std::function<void(int)> mapAll;
    juce::ComboBox partScope;
    bool allParts=false;
    int lastRequestedMap=0;
    std::function<juce::String()> readStatus;
    std::function<int()> readLatency;
    std::function<bool()> readAdvanced;
    juce::ComboBox page;
    juce::Label pageLabel;
    scbridge::TransientTooltip tooltips{this, 700};
    KeyboardSlider latency;
    juce::Label latencyLabel;
    std::array<KeyboardSlider, scbridge::parameterCount> sliders;
    std::array<juce::Label, scbridge::parameterCount> labels;
    std::array<juce::ComboBox, scbridge::parameterCount> choices;
    std::array<juce::ToggleButton, scbridge::parameterCount> toggles;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>, scbridge::parameterCount> choiceAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, scbridge::parameterCount> buttonAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, scbridge::parameterCount> attachments;
    juce::Label status;
};

class Processor final : public juce::AudioProcessor, private juce::Thread,
                        private juce::Timer, private juce::AudioProcessorValueTreeState::Listener {
public:
    Processor() : AudioProcessor(BusesProperties().withOutput("Stereo", juce::AudioChannelSet::stereo(), true)),
        juce::Thread("Sound Canvas audio bridge"), state(*this, nullptr, "OnjSoundCanvasPilot", layout()) {
        for (unsigned i = 0; i < scbridge::parameterCount; ++i) {
            values[i] = state.getRawParameterValue(ids[i]);
            commands[i].store(scbridge::packControl(i<scbridge::legacyParameterCount ? 1 : 0,
                                                   static_cast<int32_t>(std::lround(values[i]->load()))));
            state.addParameterListener(ids[i],this);
        }
        requestedLatency.store(preferences.getLatency());
        actualLatency.store(requestedLatency.load()); activeLatency = static_cast<uint32_t>(requestedLatency.load());
        setLatencySamples(activeLatency);
#ifndef _WIN32
        auto notification = preferences.notification();
        preferenceNotification = CFStringCreateWithCString(nullptr, notification.toRawUTF8(), kCFStringEncodingUTF8);
        CFNotificationCenterAddObserver(CFNotificationCenterGetDistributedCenter(), this, preferencesChanged,
            preferenceNotification, nullptr, CFNotificationSuspensionBehaviorDeliverImmediately);
#endif
        startTimerHz(10);
    }
    ~Processor() override {
        stopTimer();
        for (auto* id : ids) state.removeParameterListener(id,this);
#ifndef _WIN32
        CFNotificationCenterRemoveObserver(CFNotificationCenterGetDistributedCenter(), this, preferenceNotification, nullptr);
        CFRelease(preferenceNotification);
#endif
        releaseResources();
    }
    const juce::String getName() const override { return "Sound Canvas Bridge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10; }
    bool isBusesLayoutSupported(const BusesLayout& value) const override {
        return value.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Pilot"; }
    void changeProgramName(int, const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override {
        return new BridgeEditor(*this, state, [this] {
            switch (engineStatus.load()) {
                case 1: return juce::String("Ready. Requested latency ") + juce::String(requestedLatency.load()) +
                    " samples; effective " + juce::String(actualLatency.load()) + " samples (" +
                    juce::String(actualLatency.load() * 1000.0 / currentRate.load(), 2) + " ms). Scheduling gaps: " +
                    juce::String(gaps.load()) + "; missing samples: " + juce::String(missingSamples.load()) +
                    "; host block: " + juce::String(lastBlock.load()) +
                    "; offline: " + (lastOffline.load() ? "yes" : "no") +
                    "; realtime scheduling worker/helper: " + (workerRealtime.load() ? "yes" : "no") +
                    "/" + (helperRealtime.load() ? "yes" : "no") +
                    (advancedReady.load() ? ". Advanced controls ready." : ". Advanced controls unavailable for this engine build.") +
                    (preferenceError.load() ? ". Could not save the default latency." : "");
                case 2: return juce::String("Engine stopped responding. Unload and reload the instrument.");
                case 3: return juce::String("Sound Canvas engine or bridge helper is missing.");
                case 4: return juce::String("Could not start the Sound Canvas bridge.");
                default: return juce::String("Waiting for the host to start audio.");
            }
        }, [this] { return requestedLatency.load(); }, [this](int value) {
            value = scbridge::normaliseLatency(value);
            bool saved = preferences.setLatency(value);
            preferenceError.store(!saved); requestedLatency.store(value);
#ifndef _WIN32
            if (saved) CFNotificationCenterPostNotification(CFNotificationCenterGetDistributedCenter(), preferenceNotification,
                                                            nullptr, nullptr, true);
#endif
        }, [this] { return advancedReady.load(); });
    }
    void prepareToPlay(double rate, int maximum) override {
        releaseResources();
        scbridge::StateFrame previousFrame;
        if(capturedState.read(previousFrame) && previousFrame.sceneValid && previousFrame.epoch==stateEpoch.load())
            restoredState.publish(previousFrame);
        capturedState.clear();
        publishControls(true);
        for (auto& item : feedback) item.store(0);
        feedbackAvailable.store(false);advancedReady.store(false);
        engineStatus.store(4);
        if (rate < 22050 || rate > 192000 || maximum > static_cast<int>(scbridge::maxFrames) ||
            !inputReady.valid() || !outputReady.valid()) return;
        currentRate.store(rate); fadeRemaining = 0;
        activeLatency = static_cast<uint32_t>(requestedLatency.load()); actualLatency.store(activeLatency);
        setLatencySamples(static_cast<int>(activeLatency));
#ifdef SOUNDCANVAS_TEST_CORE
        auto core = juce::File(SOUNDCANVAS_TEST_CORE);
#else
        auto core = scbridge::EngineLocation().core();
#endif
#ifdef _WIN32
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&moduleAnchor),&module)) return;
        wchar_t path[32768]{};
        const auto count=GetModuleFileNameW(module,path,32768);
        if(!count || count>=32768) return;
        const auto directory=juce::File(juce::String(path)).getParentDirectory();
        auto helper=directory.getParentDirectory().getChildFile("Resources/SoundCanvasHelper.exe");
        if(!helper.existsAsFile()) helper=directory.getChildFile("SoundCanvasHelper.exe");
        if(!core.existsAsFile() || !helper.existsAsFile()) { engineStatus.store(3);return; }
        if(!windowsIpc.start(helper.getFullPathName().toWideCharPointer(),core.getFullPathName().toWideCharPointer())) return;
        shared=windowsIpc.shared();
#else
        Dl_info module{};
        if (!dladdr(reinterpret_cast<void*>(&moduleAnchor), &module) || !module.dli_fname) return;
        auto helper = juce::File(juce::String::fromUTF8(module.dli_fname)).getParentDirectory()
                          .getParentDirectory().getChildFile("Resources/SoundCanvasHelper");
        if (!core.existsAsFile() || !helper.existsAsFile()) { engineStatus.store(3); return; }
        // Darwin limits POSIX shared-memory names to 31 bytes including terminator.
        auto memoryName = "/onjsc-" + juce::Uuid().toString().removeCharacters("-").substring(0, 20);
        memory = shm_open(memoryName.toRawUTF8(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (memory < 0) return;
        shm_unlink(memoryName.toRawUTF8());
        if (ftruncate(memory, sizeof(scbridge::Shared))) { releaseResources(); return; }
        shared = static_cast<scbridge::Shared*>(mmap(nullptr, sizeof(scbridge::Shared), PROT_READ | PROT_WRITE, MAP_SHARED, memory, 0));
        if (shared == MAP_FAILED) { shared = nullptr; releaseResources(); return; }
#endif
        std::memset(shared, 0, sizeof(*shared));
        shared->version = scbridge::protocolVersion; shared->sampleRate = rate;
        shared->stateEpoch=stateEpoch.load();
        copyParameters(); shared->reset = 1;
        scbridge::StateFrame restoration;
        shared->restoreSceneValid=restoredState.read(restoration) ? restoration.sceneValid : 0;
        if(shared->restoreSceneValid) shared->restoredScene=restoration.scene;
#ifndef _WIN32
        int sockets[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) { releaseResources(); return; }
        socket = sockets[0];
        int suppress = 1; setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &suppress, sizeof(suppress));
        int childSocket = fcntl(sockets[1], F_DUPFD_CLOEXEC, 10);
        int childMemory = fcntl(memory, F_DUPFD_CLOEXEC, 10);
        close(sockets[1]);
        if (childSocket < 0 || childMemory < 0) {
            if (childSocket >= 0) close(childSocket);
            if (childMemory >= 0) close(childMemory);
            releaseResources(); return;
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, childSocket, 3);
        posix_spawn_file_actions_adddup2(&actions, childMemory, 4);
        posix_spawn_file_actions_addclose(&actions, childSocket);
        posix_spawn_file_actions_addclose(&actions, childMemory);
        // Only the two explicitly inherited descriptors survive the spawn.
        posix_spawnattr_t attributes; posix_spawnattr_init(&attributes);
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
        auto helperPath = helper.getFullPathName().toStdString(), corePath = core.getFullPathName().toStdString();
        char* arguments[] = {helperPath.data(), corePath.data(), nullptr};
        int result = posix_spawn(&child, helperPath.c_str(), &actions, &attributes, arguments, environ);
        posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes);
        close(childSocket); close(childMemory);
        if (result || !receive('R', 5000) || !exchange('S', 5000)) { releaseResources(); return; }
#else
        if(!exchange('S',5000)) { releaseResources();return; }
#endif
        inputs.reset(); outputs.reset(); inputReady.reset(); outputReady.reset(); pending = {}; timeline = 0; gaps.store(0);
        missingSamples.store(0); lastBlock.store(0); lastOffline.store(false);
        healthy.store(true); resetRequested.store(false); engineStatus.store(1);
        if (!startThread(juce::Thread::Priority::highest)) {
            releaseResources(); engineStatus.store(4);
        }
    }
    void releaseResources() override {
        healthy.store(false);
        signalThreadShouldExit();
        inputReady.notify(); outputReady.notify();
        // Only the worker waits for the Intel process; never terminate a worker
        // while it owns shared memory. Its IPC wait is bounded to 250 ms.
        if (isThreadRunning()) waitForThreadToExit(-1);
#ifdef _WIN32
        windowsIpc.close();shared=nullptr;
#else
        if (socket >= 0) { char quit = 'Q'; ::send(socket, &quit, 1, MSG_DONTWAIT); close(socket); socket = -1; }
        if (child > 0) {
            for (int i = 0; i < 50; ++i) {
                if (waitpid(child, nullptr, WNOHANG) == child) { child = -1; break; }
                juce::Thread::sleep(10);
            }
            if (child > 0) {
                kill(child, SIGTERM);
                for (int i = 0; i < 50; ++i) {
                    if (waitpid(child, nullptr, WNOHANG) == child) { child = -1; break; }
                    juce::Thread::sleep(10);
                }
                if (child > 0) { kill(child, SIGKILL); waitpid(child, nullptr, 0); child = -1; }
            }
        }
        if (shared) { munmap(shared, sizeof(*shared)); shared = nullptr; }
        if (memory >= 0) { close(memory); memory = -1; }
#endif
    }
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override {
        juce::ScopedNoDenormals guard;
        auto frames = static_cast<uint32_t>(audio.getNumSamples());
        audio.clear();
        if (!healthy.load() || frames > scbridge::maxFrames || !frames) { midi.clear(); return; }
        const bool offline = isNonRealtime();
        lastBlock.store(static_cast<int>(frames)); lastOffline.store(offline);
        uint32_t callbackMissing = 0;
        // Buffered host tracks may arrive in bursts even in realtime mode.
        // A missing quantum gets a bounded handoff wait, not silent corruption.
        // The realtime allowance is shared by the ENTIRE callback, not renewed
        // per packet; the socket itself is still used only by the worker.
        const auto callbackStart = std::chrono::steady_clock::now();
        const auto waitBudget = offline ? scbridge::offlineWaitSeconds :
            std::min(scbridge::maximumRealtimeWaitSeconds, frames / currentRate.load() * scbridge::realtimeWaitFraction);
        const auto deadline = callbackStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(waitBudget));
        auto nextLatency = static_cast<uint32_t>(scbridge::effectiveLatency(requestedLatency.load(), static_cast<int>(frames)));
        if (timeline == 0) { activeLatency = nextLatency; actualLatency.store(nextLatency); }
        const bool changingLatency = nextLatency != activeLatency;
        auto event = midi.cbegin();
        const auto callbackTimeline = timeline;
        uint32_t submitted = 0;
        auto submitSample = [&](uint32_t frame, bool allowDrop) {
            const auto sourceTime = callbackTimeline + frame;
            auto offset = static_cast<uint32_t>(sourceTime % scbridge::renderQuantum);
            auto* input = offset == scbridge::renderQuantum - 1 ? inputs.beginWrite() : nullptr;
            if (offset == scbridge::renderQuantum - 1 && !input && !allowDrop) return false;
            if (offset == 0) {
                pending.start = sourceTime; pending.count = pending.bytes = 0;
                pending.reset = resetRequested.exchange(false) ? 1 : 0;
                pending.stateEpoch=stateEpoch.load();
                pending.restoreSceneValid=0;
                if(pending.reset) {
                    scbridge::StateFrame restored;
                    if(restoredState.read(restored) && restored.sceneValid) {
                        pending.restoredScene=restored.scene;pending.restoreSceneValid=1;
                    }
                }
                for (unsigned i = 0; i < scbridge::parameterCount; ++i)
                {
                    const auto command=commands[i].load();
                    pending.parameters[i]=scbridge::controlValue(command);
                    pending.generations[i]=scbridge::controlGeneration(command);
                }
            }
            while (event != midi.cend() && (*event).samplePosition <= static_cast<int>(frame)) {
                auto message = *event++;
                if (message.numBytes <= 0 || pending.count == scbridge::maxEvents ||
                    pending.bytes + message.numBytes > scbridge::maxMidiBytes) {
                    resetRequested.store(true); gaps.fetch_add(1); continue;
                }
                pending.events[pending.count++] = {offset, static_cast<uint32_t>(message.numBytes), pending.bytes};
                std::memcpy(pending.midi + pending.bytes, message.data, message.numBytes);
                pending.bytes += message.numBytes;
            }
            if (offset == scbridge::renderQuantum - 1) {
                if (input) {
                    input->start = pending.start; input->count = pending.count; input->bytes = pending.bytes; input->reset = pending.reset;
                    input->restoreSceneValid=pending.restoreSceneValid;
                    input->stateEpoch=pending.stateEpoch;
                    if(pending.restoreSceneValid) input->restoredScene=pending.restoredScene;
                    std::copy(std::begin(pending.parameters), std::end(pending.parameters), std::begin(input->parameters));
                    std::copy(std::begin(pending.generations), std::end(pending.generations), std::begin(input->generations));
                    std::copy_n(pending.events, pending.count, input->events);
                    std::memcpy(input->midi, pending.midi, pending.bytes); inputs.endWrite();
                } else { gaps.fetch_add(1); resetRequested.store(true); }
            }
            return true;
        };
        auto submitAvailable = [&] {
            const auto before = submitted;
            while (submitted < frames && submitSample(submitted, false)) ++submitted;
            if (submitted != before) inputReady.notify();
        };
        // Publish available source audio before consuming its delayed output.
        // Otherwise a buffered callback serialises helper work with each wait.
        submitAvailable();
        for (uint32_t frame = 0; frame < frames; ++frame, ++timeline) {
            if (submitted <= frame || frame % scbridge::renderQuantum == 0) submitAvailable();
            if (submitted <= frame) {
                submitSample(submitted++, true); inputReady.notify();
            }
            if (timeline < activeLatency) continue;
            auto wanted = timeline - activeLatency;
            auto* output = outputs.beginRead();
            for (;;) {
                if (output && output->start + scbridge::renderQuantum <= wanted) {
                    outputs.endRead(); inputReady.notify(); output = outputs.beginRead(); continue;
                }
                if (output || !healthy.load() || std::chrono::steady_clock::now() >= deadline) break;
                outputReady.waitUntil(deadline); output = outputs.beginRead();
            }
            if (output && output->start <= wanted) {
                auto sample = static_cast<uint32_t>(wanted - output->start);
                float gain = fadeRemaining > 0 ? (65.0f - static_cast<float>(fadeRemaining--)) / 64.0f : 1.0f;
                audio.setSample(0, frame, output->left[sample] * gain); audio.setSample(1, frame, output->right[sample] * gain);
                if (sample == scbridge::renderQuantum - 1) { outputs.endRead(); inputReady.notify(); }
            } else {
                ++callbackMissing;
                if (wanted % scbridge::renderQuantum == 0 && fadeRemaining == 0) gaps.fetch_add(1);
            }
        }
        if (changingLatency) {
            const int fade = std::min(64, static_cast<int>(frames));
            for (int i = 0; i < fade; ++i)
                for (int channel = 0; channel < 2; ++channel)
                    audio.setSample(channel, frames - fade + i,
                        audio.getSample(channel, frames - fade + i) * (fade - i - 1.0f) / fade);
            activeLatency = nextLatency; actualLatency.store(nextLatency); fadeRemaining = 64;
        }
        missingSamples.fetch_add(callbackMissing);
        midi.clear();
    }
    bool standaloneMapSelection(int map,std::array<scbridge::ControlMessage,32>& messages) const {
        scbridge::StateFrame snapshot;
        if(map<1 || map>4 || !healthy.load() || !capturedState.read(snapshot) || snapshot.epoch!=stateEpoch.load()) return false;
        // The mirror always captures legacy selections, even if advanced engine
        // inspection is unavailable. Do not substitute default banks or patches.
        messages=scbridge::allPartMapSelection(map,snapshot.scene);
        return true;
    }
    void getStateInformation(juce::MemoryBlock& destination) override {
        auto xml = state.copyState().createXml();
        scbridge::StateFrame snapshot;
        const bool haveSnapshot=capturedState.read(snapshot) && snapshot.epoch==stateEpoch.load();
        for(auto* child=xml->getFirstChildElement();child;) {
            auto* next=child->getNextElement();
            const auto id=child->getStringAttribute("id");
            for(unsigned i=0;i<scbridge::parameterCount;++i) if(id==ids[i]) {
                if(i>=scbridge::legacyParameterCount && !haveSnapshot && !advancedPersistent[i].load()) {
                    xml->removeChildElement(child,true);break;
                }
                const auto command=commands[i].load(),report=haveSnapshot ?
                    scbridge::packControl(snapshot.generations[i],snapshot.values[i]) : feedback[i].load();
                const auto value=currentReport(i,report,command)
                    ? scbridge::controlValue(report) : scbridge::controlValue(command);
                child->setAttribute("value",value);break;
            }
            child=next;
        }
        scbridge::StateFrame savedScene;
        if(haveSnapshot) savedScene=snapshot;
        else restoredState.read(savedScene);
        if(savedScene.sceneValid) {
            auto* scene=xml->createNewChildElement("BridgeScene");scene->setAttribute("version",static_cast<int>(scbridge::sceneVersion));
            scene->addTextElement(juce::MemoryBlock(&savedScene.scene,sizeof(savedScene.scene)).toBase64Encoding());
        }
        copyXmlToBinary(*xml, destination);
    }
    void setStateInformation(const void* data, int size) override {
        if (size <= 0 || size > 65536) return;
        auto xml = getXmlFromBinary(data, size);
        if (xml && xml->hasTagName(state.state.getType())) {
            scbridge::StateFrame restored;
            if(auto* saved=xml->getChildByName("BridgeScene")) {
                juce::MemoryBlock bytes;
                if(!bytes.fromBase64Encoding(saved->getAllSubText()) ||
                   !scbridge::decodeScene(saved->getIntAttribute("version"),bytes.getData(),bytes.getSize(),restored.scene)) return;
                restored.sceneValid=1;
                xml->removeChildElement(saved,true);
            }
            restored.epoch=stateEpoch.fetch_add(1)+1;
            restoredState.publish(restored);capturedState.clear();
            auto next = juce::ValueTree::fromXml(*xml);
            const auto prior=reflecting;reflecting=this;
            state.replaceState(next);
            for(unsigned i=scbridge::legacyParameterCount;i<scbridge::parameterCount;++i) {
                auto* saved=xml->getChildByAttribute("id",ids[i]);
                advancedPersistent[i].store(saved!=nullptr);
                if(saved) publishControl(i,static_cast<int32_t>(std::lround(values[i]->load())));
                else {
                    auto* parameter=state.getParameter(ids[i]);
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(defaults[i])));
                    commands[i].store(scbridge::packControl(0,defaults[i]));
                }
            }
            reflecting=prior;
            publishControls();feedbackAvailable.store(false);resetRequested.store(true);
        }
    }
private:
    void publishControl(unsigned index,int32_t value) {
        auto previous=commands[index].load();
        while (!commands[index].compare_exchange_weak(previous,
            scbridge::packControl(scbridge::controlGeneration(previous)+1,value))) {}
    }
    void publishControls(bool includeAdvanced=false) {
        std::array<int32_t,scbridge::parameterCount> next{};
        std::array<bool,scbridge::parameterCount> publish{};
        // Resolve the whole snapshot before advancing selector generations;
        // otherwise later fields would reject their still-current readback.
        for (unsigned i=0;i<scbridge::parameterCount;++i) {
            if(i>=scbridge::legacyParameterCount && (!includeAdvanced || !advancedPersistent[i].load())) continue;
            const auto command=commands[i].load(),report=feedback[i].load();
            next[i]=includeAdvanced && currentReport(i,report,command)
                ? scbridge::controlValue(report) : static_cast<int32_t>(std::lround(values[i]->load()));
            publish[i]=true;
        }
        for(unsigned i=0;i<scbridge::parameterCount;++i) if(publish[i]) publishControl(i,next[i]);
    }
    void parameterChanged(const juce::String& id,float value) override {
        if (reflecting==this) return;
        for (unsigned i=0;i<scbridge::parameterCount;++i) if (id==ids[i]) {
            if (i>=1 && i<=3) publishControls();
            else {
                if(i>=scbridge::legacyParameterCount) advancedPersistent[i].store(true);
                publishControl(i,static_cast<int32_t>(std::lround(value)));
            }
            return;
        }
    }
    void timerCallback() override {
#ifdef _WIN32
        if(++preferenceTicks>=10) {
            preferenceTicks=0;preferences.reload();requestedLatency.store(preferences.getLatency());
        }
#endif
        auto latency = actualLatency.load();
        if (getLatencySamples() != latency) setLatencySamples(latency);
        for (unsigned i=1;i<scbridge::parameterCount;++i) {
            if(i==15) continue;
            const auto report=feedback[i].load();
            if (!currentReport(i,report,commands[i].load())) continue;
            auto* parameter=state.getParameter(ids[i]);
            const auto value=static_cast<float>(scbridge::controlValue(report));
            if (values[i]->load()==value) continue;
            // Readbacks notify the editor/host, but must not become new MIDI
            // commands. The guard is thread-local so DAW edits remain commands.
            const auto previous=reflecting; reflecting=this;
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
            reflecting=previous;
        }
    }
    bool currentReport(unsigned index,uint64_t report,uint64_t command) const {
        if(!feedbackAvailable.load() || feedbackEpoch.load()!=stateEpoch.load() ||
           scbridge::controlGeneration(report)!=scbridge::controlGeneration(command)) return false;
        const auto sameContext=[&](unsigned key) {
            return scbridge::controlGeneration(feedback[key].load())==scbridge::controlGeneration(commands[key].load());
        };
        const auto scope=scbridge::controls[index].scope;
        if((scope==scbridge::ControlScope::Part || (index>0 && index<15)) && !sameContext(0)) return false;
        if(scope==scbridge::ControlScope::Drum &&
           (!sameContext(scbridge::drumNoteIndex) || !sameContext(scbridge::drumSetupIndex))) return false;
        if((scope==scbridge::ControlScope::InsertionParameter || index>=scbridge::insertionFirstDirectIndex) &&
           !sameContext(scbridge::insertionTypeIndex)) return false;
        if(scope==scbridge::ControlScope::InsertionParameter && !sameContext(scbridge::insertionSelectorIndex)) return false;
        return true;
    }
#ifndef _WIN32
    static void preferencesChanged(CFNotificationCenterRef, void* observer, CFStringRef, const void*, CFDictionaryRef) {
        auto& self = *static_cast<Processor*>(observer);
        self.preferences.reload(); self.requestedLatency.store(self.preferences.getLatency()); self.preferenceError.store(false);
    }
#endif
    void run() override {
        workerRealtime.store(scbridge::configureAudioWorker(currentRate.load()));
        helperRealtime.store(shared->helperRealtime != 0);
        while (!threadShouldExit()) {
            auto* input = inputs.beginRead();
            auto* output = outputs.beginWrite();
            if (!input || !output) {
                inputReady.waitUntil(std::chrono::steady_clock::now() +
                                     std::chrono::milliseconds(scbridge::workerIdleWaitMilliseconds));
                continue;
            }
            shared->eventCount = 0; shared->midiBytes = 0; shared->reset = input->reset;
            shared->stateEpoch=input->stateEpoch;
            shared->restoreSceneValid=input->restoreSceneValid;
            if(input->restoreSceneValid) shared->restoredScene=input->restoredScene;
            std::copy(std::begin(input->parameters), std::end(input->parameters), std::begin(shared->parameters));
            std::copy(std::begin(input->generations),std::end(input->generations),std::begin(shared->generations));
            const auto start = input->start;
            const auto latencyQuanta = std::max(1u, static_cast<uint32_t>(actualLatency.load()) / scbridge::renderQuantum);
            const auto limit = std::min({scbridge::maximumBatchQuanta, outputs.writable(), latencyQuanta});
            uint32_t packets = 0;
            // Use packets already queued; never wait to fill a batch. Preserve
            // parameter/reset boundaries and every original MIDI sample offset.
            while (packets < limit) {
                auto* next = inputs.beginRead(packets);
                if (!next || next->start != start + packets * scbridge::renderQuantum ||
                    (packets && next->reset) ||
                    next->stateEpoch!=shared->stateEpoch ||
                    !std::equal(std::begin(next->parameters), std::end(next->parameters), shared->parameters) ||
                    !std::equal(std::begin(next->generations),std::end(next->generations),shared->generations) ||
                    shared->eventCount + next->count > scbridge::maxEvents ||
                    shared->midiBytes + next->bytes > scbridge::maxMidiBytes) break;
                for (uint32_t i = 0; i < next->count; ++i) {
                    const auto& event = next->events[i];
                    shared->events[shared->eventCount++] = {
                        packets * scbridge::renderQuantum + event.offset, event.size,
                        shared->midiBytes + event.position};
                }
                std::memcpy(shared->midi + shared->midiBytes, next->midi, next->bytes);
                shared->midiBytes += next->bytes;
                ++packets;
            }
            shared->frames = packets * scbridge::renderQuantum;
            if (!exchange('P', 250)) { healthy.store(false); engineStatus.store(2); return; }
            advancedReady.store(shared->advancedReady!=0);
            for (unsigned i=0;i<scbridge::parameterCount;++i)
                feedback[i].store(scbridge::packControl(shared->generations[i],shared->reported[i]));
            feedbackAvailable.store(true);
            feedbackEpoch.store(shared->stateEpoch);
            scbridge::StateFrame frame;
            frame.epoch=shared->stateEpoch;
            frame.sceneValid=shared->capturedSceneValid;frame.scene=shared->capturedScene;
            std::copy(std::begin(shared->generations),std::end(shared->generations),std::begin(frame.generations));
            std::copy(std::begin(shared->reported),std::end(shared->reported),std::begin(frame.values));
            if(frame.epoch==stateEpoch.load()) capturedState.publish(frame);
            for (uint32_t i = 0; i < packets; ++i) {
                output = outputs.beginWrite();
                output->start = start + i * scbridge::renderQuantum;
                std::copy_n(shared->left + i * scbridge::renderQuantum, scbridge::renderQuantum, output->left);
                std::copy_n(shared->right + i * scbridge::renderQuantum, scbridge::renderQuantum, output->right);
                outputs.endWrite(); inputs.endRead();
            }
            outputReady.notify();
        }
    }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
        juce::AudioProcessorValueTreeState::ParameterLayout result;
        for (unsigned i = 0; i < scbridge::parameterCount; ++i) {
            int maximum = scbridge::controls[i].maximum;
            int minimum = scbridge::controls[i].minimum;
            auto format = [i](int value, int) -> juce::String {
                if (i == 0 || i == 3) return juce::String(value + 1);
                if (i == 1) return juce::StringArray{"", "SC-55", "SC-88", "SC-88 Pro", "SC-8820"}[value];
                if (i == 15) return value ? "Selected Part" : "Multitimbral";
                if (i == 5) return value == 64 ? "Centre" : juce::String(value);
                const auto unit=scbridge::controls[i].unit;
                if(unit==scbridge::ControlUnit::InsertionType) return scbridge::insertionTypes[value].name;
                if(unit==scbridge::ControlUnit::Switch) return value ? "On" : "Off";
                if(unit==scbridge::ControlUnit::Setup) return juce::String(value+1);
                if(unit==scbridge::ControlUnit::Signed64 || unit==scbridge::ControlUnit::DrumPitch) {
                    const int difference=value-(unit==scbridge::ControlUnit::DrumPitch ? 60 : 64);
                    return difference<0 ? "minus "+juce::String(-difference) : difference>0 ? "plus "+juce::String(difference) : "0";
                }
                if(unit==scbridge::ControlUnit::Pan) return value==0 ? "Random" : value==64 ? "Centre" :
                    value<64 ? "Left "+juce::String(64-value) : "Right "+juce::String(value-64);
                if(unit==scbridge::ControlUnit::ReverbType) return juce::StringArray{"Room 1","Room 2","Room 3","Hall 1","Hall 2","Plate","Delay","Panning Delay"}[value];
                if(unit==scbridge::ControlUnit::ChorusType) return juce::StringArray{"Chorus 1","Chorus 2","Chorus 3","Chorus 4","Feedback Chorus","Flanger","Short Delay","Short Delay Feedback"}[value];
                if(unit==scbridge::ControlUnit::DelayType) return juce::StringArray{"Delay 1","Delay 2","Delay 3","Delay 4","Pan Delay 1","Pan Delay 2","Pan Delay 3","Pan Delay 4","Delay To Reverb","Pan Repeat"}[value];
                if(unit==scbridge::ControlUnit::LowFrequency) return value ? "400 Hz" : "200 Hz";
                if(unit==scbridge::ControlUnit::HighFrequency) return value ? "6 kHz" : "3 kHz";
                return juce::String(value);
            };
            auto parse = [i,format](const juce::String& text) {
                if(i>=scbridge::legacyParameterCount) {
                    const auto& definition=scbridge::controls[i];
                    for(int candidate=definition.minimum;candidate<=definition.maximum;++candidate)
                        if(text.trim().equalsIgnoreCase(format(candidate,128))) return candidate;
                }
                if (i == 0 || i == 3) return text.getIntValue() - 1;
                if (i == 1) {
                    const juce::StringArray maps{"SC-55","SC-88","SC-88 Pro","SC-8820"};
                    const int index = maps.indexOf(text.trim(),true);
                    if (index >= 0) return index + 1;
                }
                if (i == 15 && text.trim().equalsIgnoreCase("Selected Part")) return 1;
                if (i == 5 && text.trim().equalsIgnoreCase("Centre")) return 64;
                const auto unit=scbridge::controls[i].unit;
                auto input=text.trim();
                if(unit==scbridge::ControlUnit::Signed64 || unit==scbridge::ControlUnit::DrumPitch) {
                    if(input.startsWithIgnoreCase("minus ")) input="-"+input.substring(6);
                    if(input.startsWithIgnoreCase("plus ")) input=input.substring(5);
                    return input.getIntValue()+(unit==scbridge::ControlUnit::DrumPitch ? 60 : 64);
                }
                if(unit==scbridge::ControlUnit::Switch) return input.equalsIgnoreCase("On") || input=="1" ? 1 : 0;
                if(unit==scbridge::ControlUnit::Setup) return input.getIntValue()-1;
                if(unit==scbridge::ControlUnit::Pan) {
                    if(input.equalsIgnoreCase("Centre")) return 64;
                    if(input.equalsIgnoreCase("Random")) return 0;
                    if(input.startsWithIgnoreCase("Left ")) return 64-input.substring(5).getIntValue();
                    if(input.startsWithIgnoreCase("Right ")) return 64+input.substring(6).getIntValue();
                }
                return text.getIntValue();
            };
            result.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{ids[i], 1}, names[i],
                minimum, maximum, defaults[i], juce::AudioParameterIntAttributes().withStringFromValueFunction(format)
                    .withValueFromStringFunction(parse)));
        }
        return result;
    }
    void copyParameters() {
        for (unsigned i = 0; i < scbridge::parameterCount; ++i) {
            const auto command=commands[i].load();
            shared->parameters[i]=scbridge::controlValue(command);
            shared->generations[i]=scbridge::controlGeneration(command);
        }
    }
#ifndef _WIN32
    bool receive(char expected, int milliseconds) {
        pollfd descriptor{socket, POLLIN, 0}; char reply = 0;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        for (;;) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (remaining < 0) return false;
            int result = poll(&descriptor, 1, static_cast<int>(remaining));
            if (result < 0 && errno == EINTR) continue;
            if (result != 1 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) return false;
            ssize_t bytes;
            do { bytes = read(socket, &reply, 1); } while (bytes < 0 && errno == EINTR);
            return bytes == 1 && reply == expected;
        }
    }
#endif
    bool exchange(char command, int milliseconds) {
#ifdef _WIN32
        return windowsIpc.exchange(command,milliseconds);
#else
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (write(socket, &command, 1) != 1 || !receive('D', milliseconds)) return false;
        std::atomic_thread_fence(std::memory_order_seq_cst); return shared->result == 0;
#endif
    }
    juce::AudioProcessorValueTreeState state;
    scbridge::PerformancePreferences preferences{scbridge::performanceFile()};
#ifndef _WIN32
    CFStringRef preferenceNotification = nullptr;
#endif
    std::atomic<int> requestedLatency{scbridge::minimumLatency}, actualLatency{scbridge::minimumLatency};
    std::atomic<bool> preferenceError{false};
    std::atomic<double> currentRate{44100.0};
    uint32_t activeLatency = scbridge::minimumLatency;
    int fadeRemaining = 0;
    std::atomic<float>* values[scbridge::parameterCount]{};
    std::array<std::atomic<uint64_t>,scbridge::parameterCount> commands{},feedback{};
    std::array<std::atomic<bool>,scbridge::parameterCount> advancedPersistent{};
    std::atomic<bool> feedbackAvailable{false},advancedReady{false};
    std::atomic<uint32_t> stateEpoch{1},feedbackEpoch{0};
    scbridge::StateMailbox<scbridge::StateFrame> capturedState,restoredState;
    inline static thread_local Processor* reflecting=nullptr;
    scbridge::Shared* shared = nullptr;
#ifdef _WIN32
    scbridge::WindowsIpc windowsIpc;
    int preferenceTicks=0;
#else
    int socket = -1, memory = -1; pid_t child = -1;
#endif
    std::atomic<bool> healthy{false};
    std::atomic<bool> workerRealtime{false}, helperRealtime{false};
    std::atomic<int> engineStatus{0};
    std::atomic<uint32_t> gaps{0};
    std::atomic<uint64_t> missingSamples{0};
    std::atomic<int> lastBlock{0};
    std::atomic<bool> lastOffline{false};
    scbridge::PacketQueue<scbridge::InputPacket> inputs;
    scbridge::PacketQueue<scbridge::OutputPacket> outputs;
    scbridge::WakeSignal inputReady, outputReady;
    scbridge::InputPacket pending;
    uint64_t timeline = 0;
    std::atomic<bool> resetRequested{false};
};
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
void scbridge::configureStandaloneEditor(juce::AudioProcessorEditor& editor,std::function<void(int)> applyMapToAll) {
    if(auto* bridge=dynamic_cast<BridgeEditor*>(&editor)) bridge->configureStandalone(std::move(applyMapToAll));
}
bool scbridge::standaloneMapSelection(juce::AudioProcessor& processor,int map,std::array<ControlMessage,32>& messages) {
    if(auto* bridge=dynamic_cast<Processor*>(&processor)) return bridge->standaloneMapSelection(map,messages);
    return false;
}
