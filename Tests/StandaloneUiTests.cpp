#define SCBRIDGE_STANDALONE_TEST 1
#include "../Source/Standalone.cpp"
#include <iostream>
#include <stdexcept>
#include <set>
#include "AudioStartupFixture.h"
#include "PreferencesStartupFixture.h"

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        auto require=[](bool ok,const char* text){if(!ok) throw std::runtime_error(text);};
#if defined(SCBRIDGE_TRANSLATED_ENGINE) && !defined(_WIN32)
        if(argc==2 && std::string(argv[1])=="--download-flow") {
            const auto root=scbridge::performanceFile().getParentDirectory();
            require(root.getFileName()=="engine-download-test-profile" && !root.exists(),"Expected fresh isolated download profile");
            require(root.createDirectory().wasOk(),"Create download profile");
            const auto setting=root.getChildFile("Engine.xml");setting.replaceWithText("<PROPERTIES><VALUE name=\"unchanged\" val=\"yes\"/></PROPERTIES>");
            const auto original=setting.loadFileAsString();
            scbridge::EngineDownloadService download;
            for(bool cancel:{true,false}) {
                bool done=false,success=false;juce::String message;juce::File installed;
                download.start([&](juce::Result result,juce::File file) {success=result.wasOk();message=result.getErrorMessage();installed=file;done=true;});
                if(cancel) download.cancel();
                const auto deadline=juce::Time::getMillisecondCounterHiRes()+190000;
                while(!done && juce::Time::getMillisecondCounterHiRes()<deadline)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
                require(done,"Download completion must be bounded");
                while(download.busy() && juce::Time::getMillisecondCounterHiRes()<deadline)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
                require(!download.busy(),"Download worker must exit before retry");
                require(setting.loadFileAsString()==original,"Download service must not replace the active selection");
                if(cancel) require(!success && message.contains("cancelled"),"Cancellation preserves the current engine");
                else {
                    require(success && scbridge::engineHashMatches(installed,scbridge::officialCoreHash),"Native Mac download/extract/import must produce the verified DLL");
                    require(scbridge::engineHashMatches(installed.getSiblingFile("MSVCR100.dll"),"60c06e0fa4449314da3a0a87c1a9d9577df99226f943637e06f61188e5862efa"),"Native Mac download must include its verified external engine runtime");
                }
            }
            require(root.deleteRecursively(),"Remove completed isolated download profile");
            std::cout<<"PASS native download service cancellation, retry, import and unchanged active selection\n";return 0;
        }
#endif
        audioStartupFixture::run(require);
        preferencesStartupFixture::run(require);
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
#if defined(SCBRIDGE_TRANSLATED_ENGINE) && !defined(_WIN32)
            tabs->setCurrentTabIndex(0);
            auto* page=tabs->getCurrentContentComponent();
            auto* engine=page->findChildWithID("enginePreferences");
            require(engine!=nullptr,"Engine setup must live in General on Mac");
            require(!tabs->getTabContentComponent(1)->findChildWithID("engineLocation"),"Audio and MIDI must not duplicate engine setup");
            auto* download=engine->findChildWithID("downloadEngine");
            require(download && download->isShowing() && download->getWantsKeyboardFocus(),"Official download must be visible and keyboard reachable even with an existing engine");
            require(download->getAccessibilityHandler()->getTitle()=="Download from Roland...","Download has a concise accessible name");
            require(download->getAccessibilityHandler()->getHelp().contains("Roland"),"Download has VoiceOver help");
            juce::KeyboardFocusTraverser traversal;
            const auto controls=traversal.getAllComponents(&settings);
            require(std::find(controls.begin(),controls.end(),download)!=controls.end(),"Download belongs to the Preferences Tab loop");
            require(engine->getLocalBounds().contains(download->getBounds()) && page->getLocalBounds().contains(engine->getBounds()),"Engine controls fit inside General");
#endif
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
        for(int key:{juce::KeyPress::homeKey,juce::KeyPress::endKey,juce::KeyPress::pageUpKey,juce::KeyPress::pageDownKey})
            host.getPeer()->handleKeyPress(juce::KeyPress(key));
        require(requests==std::vector<int>({3,3,2,1,2,1,4,1,4}),"All-map Home/End and page keys must retain their request position and clamp");
        part->setSelectedId(2,juce::sendNotificationSync);
        require(editor->findChildWithID("program")->isEnabled(),"Returning to a numbered part must restore edits");
        require(processor->getParameters().size()==93,"NKS/automation contract must remain unchanged");
        processor->editorBeingDeleted(editor.get());
        std::cout<<"PASS Mac standalone All scope, unchanged plug-in contract and shortcut parity/native exceptions\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
