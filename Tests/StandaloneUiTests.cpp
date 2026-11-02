#define SCBRIDGE_STANDALONE_TEST 1
#include "../Source/Standalone.cpp"
#include <iostream>
#include <stdexcept>
#include <set>
#include "AudioStartupFixture.h"

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        auto require=[](bool ok,const char* text){if(!ok) throw std::runtime_error(text);};
        audioStartupFixture::run(require);
        {
            juce::AudioDeviceManager devices;
            scbridge::AppPreferences preferences;
            scbridge::StartupService startup;
            scbridge::UpdateService updates(preferences,"test");
            SettingsWindow settings(devices,preferences,startup,updates,{},{},{});
            auto* tabs=dynamic_cast<PreferenceTabs*>(settings.getContentComponent());
            require(tabs && !tabs->isKeyboardFocusContainer(),"Preferences tabs must share the window Tab loop");
            auto* general=tabs->getTabbedButtonBar().getTabButton(0);
            auto* audio=tabs->getTabbedButtonBar().getTabButton(1);
            require(general->getWantsKeyboardFocus() && audio->getWantsKeyboardFocus(),"Both Mac preference tabs must be keyboard reachable");
            for(int selected=0;selected<2;++selected) {
                tabs->setCurrentTabIndex(selected);
                require(!tabs->getCurrentContentComponent()->isKeyboardFocusContainer(),"A preference page must not trap Tab inside its controls");
                juce::KeyboardFocusTraverser traversal;
                const auto targets=traversal.getAllComponents(&settings);
                require(std::find(targets.begin(),targets.end(),general)!=targets.end() && std::find(targets.begin(),targets.end(),audio)!=targets.end(),"Both category tabs must be included in keyboard traversal from either page");
                for(bool forward:{false,true}) {
                    require(focusFirstControl(*tabs->getCurrentContentComponent()),"Each Mac preference page must have an initial keyboard target");
                    std::set<juce::Component*> visited;
                    for(int step=0;step<64;++step) {
                        auto* current=juce::Component::getCurrentlyFocusedComponent();
                        if(!current || !visited.insert(current).second) break;
                        require(current->isShowing(),"Tab must not enter a hidden preference page");
                        current->moveKeyboardFocusToSibling(forward);
                    }
                    require(visited.count(general) && visited.count(audio),"Tab and Shift+Tab must reach both categories from either preference page");
                }
            }
            general->grabKeyboardFocus();
            settings.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::rightKey));
            require(tabs->getCurrentTabIndex()==1 && audio->hasKeyboardFocus(false),"Right Arrow must select and focus Audio and MIDI");
            settings.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::leftKey));
            require(tabs->getCurrentTabIndex()==0 && general->hasKeyboardFocus(false),"Left Arrow must select and focus General");
        }
        juce::ApplicationCommandInfo preferences(audioSettingsCommand),notes(allNotesOffCommand);
        commandInfo(audioSettingsCommand,preferences);commandInfo(allNotesOffCommand,notes);
        require(preferences.defaultKeypresses.size()==1 && preferences.defaultKeypresses[0]==juce::KeyPress(',',juce::ModifierKeys::commandModifier,0),"Preferences shortcut must be Command+comma only");
        require(notes.defaultKeypresses.size()==1 && notes.defaultKeypresses[0]==juce::KeyPress(juce::KeyPress::F9Key),"All Notes Off must be F9");
        for(const auto command:{panicCommand,gsResetCommand,restartEngineCommand,activityDisplayCommand,readActivityCommand}) {
            juce::ApplicationCommandInfo info(command);commandInfo(command,info);
            require(info.defaultKeypresses.size()==1 && info.defaultKeypresses[0].getModifiers()==juce::ModifierKeys(juce::ModifierKeys::commandModifier),"Action shortcuts must use Command without Shift");
        }
        for(const auto command:{gmResetCommand,hideControlsCommand}) {
            juce::ApplicationCommandInfo info(command);commandInfo(command,info);
            require(info.defaultKeypresses[0].getModifiers()==juce::ModifierKeys(juce::ModifierKeys::commandModifier|juce::ModifierKeys::shiftModifier),"Minimise and standard Hide shortcuts must be preserved");
        }
        std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditorAndMakeActive());
        juce::DocumentWindow host("Map keyboard test",juce::Colours::black,juce::DocumentWindow::closeButton,true);
        host.setContentNonOwned(editor.get(),true);host.setVisible(true);host.toFront(true);
        require(dynamic_cast<juce::Slider*>(editor->findChildWithID("part"))!=nullptr,"Ordinary plug-in editor must retain numbered Part slider");
        std::vector<int> requests;
        scbridge::configureStandaloneEditor(*editor,[&](int map){requests.push_back(map);});
        auto* part=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("part"));
        auto* map=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("map"));
        require(part && map && part->getNumItems()==17,"Standalone must offer All and sixteen parts");
        require(part->getAccessibilityHandler()->getHelp().isEmpty(),"Part must not automatically announce instructions");
        require(part->getProperties()["scbridgeContextHelp"].toString().contains("All"),"Part instructions must remain available to F1");
        require(scbridge::contextHelp(*part).contains("All"),"F1 help lookup must resolve the quiet Part selector's full explanation");
        float saved=processor->getParameters()[0]->getValue();
        part->setSelectedId(1,juce::sendNotificationSync);
        require(requests.empty() && processor->getParameters()[0]->getValue()==saved,"Choosing All must not change a sound or parameter");
        require(!editor->findChildWithID("program")->isEnabled(),"All must block unintended per-part edits");
        map->setSelectedId(3,juce::sendNotificationSync);
        require(requests==std::vector<int>{3},"All must request the chosen map exactly once");
        map->setSelectedId(3,juce::sendNotificationSync);
        require(requests==std::vector<int>({3,3}),"The same map must be requestable again");
        map->grabKeyboardFocus();
        for(int key:{juce::KeyPress::upKey,juce::KeyPress::upKey,juce::KeyPress::downKey})
            host.getPeer()->handleKeyPress(juce::KeyPress(key));
        require(requests==std::vector<int>({3,3,2,1,2}),"All-map arrows must retain their last requested position");
        part->setSelectedId(2,juce::sendNotificationSync);
        require(editor->findChildWithID("program")->isEnabled(),"Returning to a numbered part must restore edits");
        require(processor->getParameters().size()==93,"NKS/automation contract must remain unchanged");
        processor->editorBeingDeleted(editor.get());
        std::cout<<"PASS Mac standalone All scope, unchanged plug-in contract and shortcut parity/native exceptions\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
