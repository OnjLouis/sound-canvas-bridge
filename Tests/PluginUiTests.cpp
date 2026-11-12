#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <memory>
#include <set>
#include "../Source/ProgramBrowser.h"
#include "../Source/Controls.h"
#include "KeyboardChoiceFixture.h"
#ifdef _WIN32
#include "../Source/EngineLocation.h"
#include <windows.h>
#endif

extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
struct ProgramNotification final : juce::AudioProcessorListener {
    explicit ProgramNotification(juce::AudioProcessor& source):processor(source) { processor.addListener(this); }
    ~ProgramNotification() override { processor.removeListener(this); }
    void audioProcessorParameterChanged(juce::AudioProcessor*,int,float) override {}
    void audioProcessorChanged(juce::AudioProcessor*,const juce::AudioProcessor::ChangeDetails& changes) override {
        programChanged|=changes.programChanged;
    }
    juce::AudioProcessor& processor;
    bool programChanged=false;
};

#ifdef _WIN32
static bool testLivePartNames(const char* mockCore) {
    const auto path=juce::File(juce::String(mockCore));
    const auto core=scbridge::performanceFile().getSiblingFile("SCCore.dll");
    juce::String error;
    if(core.getParentDirectory().createDirectory().failed() || !path.copyFileTo(core) ||
       !scbridge::EngineLocation().save(core,error)) return false;
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorIfNeeded());
    auto* parts=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("part"));
    if(!parts) return false;
    processor->setNonRealtime(true);
    processor->prepareToPlay(48000,128);
    juce::AudioBuffer<float> audio(2,128);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(2,0,1),0);
    midi.addEvent(juce::MidiMessage::programChange(2,0),1);
    midi.addEvent(juce::MidiMessage::programChange(10,25),2);
    for(int block=0;block<12;++block) {
        audio.clear();processor->processBlock(audio,midi);midi.clear();
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(700);
    const bool passed=parts->getText()=="1: Piano 1" && parts->getItemText(1)=="2: UprightPiano" &&
        parts->getItemText(9)=="10: TR-808" && parts->getProperties()["lastKeyboardAnnouncement"].isVoid();
    processor->releaseResources();
    return passed;
}
#endif

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
#ifdef _WIN32
    if(argc==3 && juce::String(argv[1])=="--part-midi") {
        const bool passed=testLivePartNames(argv[2]);
        std::cout<<(passed ? "PASS" : "FAIL")<<": incoming MIDI updates the names of unselected parts without announcing them\n";
        return passed ? 0 : 1;
    }
#endif
    bool choicesPassed=true;
    keyboardChoiceFixture::run([&](bool value,const char* message) {
        if(!value) {std::cerr<<"FAIL: "<<message<<'\n';choicesPassed=false;}
    });
    if(!choicesPassed) return 1;
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    ProgramNotification notification(*processor);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorIfNeeded());
    juce::Component wrapper;
    wrapper.setSize(editor->getWidth(), editor->getHeight());
    wrapper.addAndMakeVisible(*editor);
    const auto snapshot=[&](const char* name) {
        if(argc!=3 || juce::String(argv[1])!="--snapshot-dir") return true;
        const juce::File directory{juce::String(argv[2])};
        if(directory.createDirectory().failed()) return false;
        juce::FileOutputStream stream(directory.getChildFile(name));
        return stream.openedOk() && juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),stream);
    };
    if(!snapshot("controls.png")) { std::cerr<<"FAIL: control snapshot\n";return 1; }
    juce::KeyboardFocusTraverser traversal;
    auto* first = traversal.getDefaultComponent(&wrapper);
    if (!first || first->getComponentID() != "controlPage") {
        std::cerr << "FAIL: native plug-in wrapper focus must reach Control page without standalone focus helpers\n";
        return 1;
    }
    const auto targets = traversal.getAllComponents(&wrapper);
    std::set<int> order;
    for (auto* target : targets) {
        if (target->getName().isEmpty()) {
            std::cerr << "FAIL: unnamed plug-in keyboard target\n";
            return 1;
        }
        const auto position = target->getExplicitFocusOrder();
        if (position > 0 && !order.insert(position).second) {
            std::cerr << "FAIL: duplicate plug-in keyboard position\n";
            return 1;
        }
    }
    if (processor->getParameters().size() != 93) {
        std::cerr << "FAIL: existing automation/NKS parameter count changed\n";
        return 1;
    }
    for(unsigned i=0;i<scbridge::parameterCount;++i) {
        auto* parameter=dynamic_cast<juce::RangedAudioParameter*>(processor->getParameters()[static_cast<int>(i)]);
        const auto& expected=scbridge::controls[i];
        if(!parameter || parameter->paramID!=expected.id || parameter->getName(128)!=expected.name ||
           parameter->getNormalisableRange().start!=expected.minimum || parameter->getNormalisableRange().end!=expected.maximum ||
           std::abs(parameter->getDefaultValue()-parameter->convertTo0to1(static_cast<float>(expected.initial)))>0.000001f) {
            std::cerr<<"FAIL: automation identity/range/default changed at "<<i<<'\n';return 1;
        }
    }
    wrapper.setTopLeftPosition(-10000,-10000);wrapper.addToDesktop(0);wrapper.setVisible(true);
#ifdef _WIN32
    SetFocus(static_cast<HWND>(wrapper.getPeer()->getNativeHandle()));
    auto* focused=juce::Component::getCurrentlyFocusedComponent();
    if(!focused || focused->getComponentID()!="controlPage") {
        std::cerr<<"FAIL: native HWND focus handoff must reach the first plug-in control\n";return 1;
    }
    wrapper.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::tabKey));
    focused=juce::Component::getCurrentlyFocusedComponent();
    if(!focused || focused->getComponentID()!="browsePresets") {
        std::cerr<<"FAIL: Tab must continue from the native focus handoff\n";return 1;
    }
#endif
    if(processor->getNumPrograms()!=static_cast<int>(scbridge::catalogueCount) ||
       !processor->getProgramName(0).contains("SC-8820") || !processor->getProgramName(0).contains("Piano 1")) {
        std::cerr<<"FAIL: native program catalogue names/count\n";return 1;
    }
    auto* parts=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("part"));
    if(!parts || parts->getNumItems()!=16 || parts->getItemText(0)!="1: Piano 1" ||
       parts->getItemText(9)!="10: STANDARD 1") {
        std::cerr<<"FAIL: plug-in Part selector must expose the sixteen named sounds\n";return 1;
    }
    auto* partValue=parts->getAccessibilityHandler()->getValueInterface();
    parts->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    if(!partValue || partValue->getCurrentValueAsString()!="2: Piano 1" ||
       parts->getProperties()["lastKeyboardAnnouncement"].toString()!="Part, 2: Piano 1" ||
       std::abs(processor->getParameters()[0]->getValue()-1.0f/15.0f)>0.000001f) {
        std::cerr<<"FAIL: selecting a named part must announce it and preserve automation numbering\n";return 1;
    }
    parts->keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
    std::vector<float> untouched;
    for(int i=4;i<93;++i) untouched.push_back(processor->getParameters()[i]->getValue());
    processor->setCurrentProgram(1);
    if(processor->getCurrentProgram()!=1) { std::cerr<<"FAIL: factory program recall before audio starts\n";return 1; }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(650);
    if(parts->getText()!="1: UprightPiano" || parts->getItemText(1)!="2: Piano 1" ||
       parts->getProperties()["lastKeyboardAnnouncement"].toString()!="Part, 1: Piano 1") {
        std::cerr<<"FAIL: a sound change must refresh only its part without requesting an announcement\n";return 1;
    }
    for(int i=4;i<93;++i) if(processor->getParameters()[i]->getValue()!=untouched[static_cast<size_t>(i-4)]) {
        std::cerr<<"FAIL: sound selection changed mixer/effects/routing\n";return 1;
    }
    processor->setCurrentProgram(2872);
    if(processor->getCurrentProgram()!=2872 || processor->getParameters()[0]->getValue()!=9.0f/15.0f) {
        std::cerr<<"FAIL: drum catalogue selection must use an existing rhythm part\n";return 1;
    }
    juce::MemoryBlock saved;
    processor->getStateInformation(saved);
    std::unique_ptr<juce::AudioProcessor> restored(createPluginFilter());
    restored->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
    if(restored->getCurrentProgram()!=2872) { std::cerr<<"FAIL: pending selection save/restore\n";return 1; }
    processor->setCurrentProgram(0);
    auto* open=dynamic_cast<juce::TextButton*>(editor->findChildWithID("browsePresets"));
    if(!open || !open->onClick) { std::cerr<<"FAIL: named preset browser command missing\n";return 1; }
    const auto* browseAccessibility=open->getAccessibilityHandler();
    if(!browseAccessibility || !scbridge::contextHelp(*open).contains("compatibility map")) {
        std::cerr<<"FAIL: Browse presets must expose native accessibility help\n";return 1;
    }
#ifdef _WIN32
    if(browseAccessibility->getHelp().isNotEmpty()) {
        std::cerr<<"FAIL: Windows browser command must reserve instructions for F1\n";return 1;
    }
#else
    if(!browseAccessibility->getHelp().contains("compatibility map")) return 1;
#endif
    open->onClick();
    scbridge::ProgramBrowser* browser=nullptr;
    for(auto* child:editor->getChildren()) if(auto* candidate=dynamic_cast<scbridge::ProgramBrowser*>(child)) browser=candidate;
    if(!browser || !browser->isVisible() || browser->matchingCount()!=1262) {
        std::cerr<<"FAIL: browser initial map/type filters\n";return 1;
    }
    if(!snapshot("browser.png")) { std::cerr<<"FAIL: browser snapshot\n";return 1; }
    auto* search=dynamic_cast<juce::TextEditor*>(browser->findChildWithID("presetSearch"));
    auto* list=dynamic_cast<juce::ListBox*>(browser->findChildWithID("presetList"));
    if(!search || !list || list->getListBoxModel()->getNameForRow(0)!="Piano 1") {
        std::cerr<<"FAIL: browser rows must expose their actual preset names\n";return 1;
    }
    list->selectRow(2);
    if(list->getListBoxModel()->getNameForRow(2)!="Mild Piano") {
        std::cerr<<"FAIL: browser row repeats map/type/address context\n";return 1;
    }
    if(!browser->keyPressed(juce::KeyPress(juce::KeyPress::F1Key))) {
        std::cerr<<"FAIL: F1 must expose the selected address on request\n";return 1;
    }
    auto* details=dynamic_cast<juce::TextEditor*>(browser->findChildWithID("presetDetails"));
    if(!details || !details->isVisible() || !details->getText().contains("Mild Piano") ||
       !details->getText().contains("SC-8820") || !details->getText().contains("Bank: 2") || !details->getText().contains("Program: 1")) {
        std::cerr<<"FAIL: requested details must retain exact sound address\n";return 1;
    }
    if(!snapshot("details.png")) { std::cerr<<"FAIL: details snapshot\n";return 1; }
    browser->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    if(!browser->isVisible() || !list->isVisible() || list->getSelectedRow()!=2) {
        std::cerr<<"FAIL: closing details must return to the same browser row\n";return 1;
    }
    search->setText("UprightPiano",false);search->onTextChange();
    if(browser->matchingCount()!=1 || browser->selectedProgram()!=1) { std::cerr<<"FAIL: searchable names\n";return 1; }
    if(processor->getCurrentProgram()!=0) { std::cerr<<"FAIL: filtering must not load sounds\n";return 1; }
    search->onReturnKey();
    if(processor->getCurrentProgram()!=1 || browser->isVisible()) { std::cerr<<"FAIL: Enter loads and closes browser\n";return 1; }
    const auto name=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("currentSound"));
    if(!name || name->getText()!="UprightPiano") { std::cerr<<"FAIL: current program name display\n";return 1; }
    name->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    if(processor->getCurrentProgram()!=0 || name->getText()!="Piano 1") {
        std::cerr<<"FAIL: main preset selector Up must load and name the previous sound\n";return 1;
    }
    name->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    const auto* selectorAccessibility=name->getAccessibilityHandler();
    const auto* value=selectorAccessibility ? selectorAccessibility->getValueInterface() : nullptr;
    if(processor->getCurrentProgram()!=1 || name->getText()!="UprightPiano" || !value || value->getCurrentValueAsString()!="UprightPiano") {
        std::cerr<<"FAIL: main preset selector Down must expose the new sound name to accessibility\n";return 1;
    }
    if(!scbridge::contextHelp(*name).contains("Up and Down")) {
        std::cerr<<"FAIL: preset selector must expose native accessibility help\n";return 1;
    }
#ifdef _WIN32
    if(selectorAccessibility->getHelp().isNotEmpty()) {
        std::cerr<<"FAIL: Windows preset selector must reserve instructions for F1\n";return 1;
    }
#else
    if(!selectorAccessibility->getHelp().contains("Up and Down")) return 1;
#endif
    if(name->getProperties()["lastKeyboardAnnouncement"].toString()!="Current sound, UprightPiano") {
        std::cerr<<"FAIL: preset arrows must request the native spoken-value announcement\n";return 1;
    }
    auto* mapChoice=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("map"));
    if(!mapChoice) { std::cerr<<"FAIL: Sound Map choice missing\n";return 1; }
    mapChoice->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    if(mapChoice->getProperties()["lastKeyboardAnnouncement"].toString()!="Sound Map, SC-88 Pro") {
        std::cerr<<"FAIL: map arrows must request the native spoken-value announcement\n";return 1;
    }
    mapChoice->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(180);
    if(!notification.programChanged) {
        std::cerr<<"FAIL: internal sound changes must notify the host's current-program display\n";return 1;
    }
    for(int i=0;i<20;++i) {
        name->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
        if(processor->getCurrentProgram()!=i+2) {
            std::cerr<<"FAIL: rapid preset arrows must not wait for the display timer\n";return 1;
        }
    }
    name->keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
    if(name->getSelectedItemIndex()!=0 || processor->getCurrentProgram()!=name->getItemId(0)-1) {
        std::cerr<<"FAIL: current sound Home must load the first filtered sound\n";return 1;
    }
    name->keyPressed(juce::KeyPress(juce::KeyPress::pageDownKey));
    if(name->getSelectedItemIndex()!=10 || processor->getCurrentProgram()!=name->getItemId(10)-1) {
        std::cerr<<"FAIL: current sound Page Down must load the tenth following entry\n";return 1;
    }
    name->keyPressed(juce::KeyPress(juce::KeyPress::pageUpKey));
    if(name->getSelectedItemIndex()!=0) {std::cerr<<"FAIL: current sound Page Up\n";return 1;}
    name->keyPressed(juce::KeyPress(juce::KeyPress::endKey));
    if(name->getSelectedItemIndex()!=name->getNumItems()-1 ||
       processor->getCurrentProgram()!=name->getItemId(name->getNumItems()-1)-1 ||
       name->getProperties()["lastKeyboardAnnouncement"].toString()!="Current sound, "+name->getText()) {
        std::cerr<<"FAIL: current sound End must load and announce the last filtered sound\n";return 1;
    }
    auto* page=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("controlPage"));
    if(!page) {std::cerr<<"FAIL: control-page choice missing\n";return 1;}
    const auto soundBeforePageChange=processor->getCurrentProgram();
    page->keyPressed(juce::KeyPress(juce::KeyPress::endKey));
    if(page->getSelectedItemIndex()!=page->getNumItems()-1) {std::cerr<<"FAIL: control-page End\n";return 1;}
    page->keyPressed(juce::KeyPress(juce::KeyPress::pageUpKey));
    if(page->getSelectedItemIndex()!=0) {std::cerr<<"FAIL: control-page Page Up\n";return 1;}
    page->keyPressed(juce::KeyPress(juce::KeyPress::pageDownKey));
    if(page->getSelectedItemIndex()!=10) {std::cerr<<"FAIL: control-page Page Down\n";return 1;}
    page->keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
    if(page->getSelectedItemIndex()!=0 || processor->getCurrentProgram()!=soundBeforePageChange) {
        std::cerr<<"FAIL: control-page Home must not change the loaded sound\n";return 1;
    }
    name->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    if(!browser->isVisible()) { std::cerr<<"FAIL: Enter on the main preset name must open the searchable browser\n";return 1; }
    std::cout << "PASS: hosted editor entry, named keyboard traversal and unchanged parameter contract\n";
}
