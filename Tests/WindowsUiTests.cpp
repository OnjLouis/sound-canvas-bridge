#define SCBRIDGE_STANDALONE_TEST 1
#include "../Source/Standalone.cpp"
#include "../Source/ProgramBrowser.h"
#include <iostream>
#include <set>
#include "AudioStartupFixture.h"
#include "PreferencesStartupFixture.h"
#include "EngineDownloadFixture.h"

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if(argc==2 && std::string(argv[1])=="--download-flow") return engineDownloadFixture::run();
    int failures=0;
    auto check=[&](bool success,const char* message) {
        if(!success) { std::cerr<<"FAIL: "<<message<<'\n';++failures; }
    };
    {
        scbridge::AccessibleToggleButton checkbox("Test checkbox");
        juce::Component host;
        host.setSize(240,40);host.addAndMakeVisible(checkbox);checkbox.setBounds(host.getLocalBounds());
        host.addToDesktop(0);host.setVisible(true);
        checkbox.setToggleState(true,juce::dontSendNotification);
        auto* handler=checkbox.getAccessibilityHandler();
        check(handler && handler->getCurrentState().isCheckable() && handler->getCurrentState().isChecked(),
              "Checkboxes must expose their checked state");
        check(handler && !handler->getValueInterface(),"Windows checkboxes must not expose a read-only text value");
        int clicks=0;checkbox.onClick=[&] {++clicks;};
        if(handler) {
            check(handler->getActions().invoke(juce::AccessibilityActionType::toggle),"Checkbox must expose a native toggle action");
            check(!checkbox.getToggleState() && !handler->getCurrentState().isChecked() && clicks==1,"Checkbox toggle must update its state and callback");
            checkbox.setEnabled(false);handler->getActions().invoke(juce::AccessibilityActionType::toggle);
            check(!checkbox.getToggleState() && clicks==1,"Disabled checkboxes must not toggle");
            checkbox.setEnabled(true);checkbox.grabKeyboardFocus();
            host.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::returnKey));
            juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
            check(checkbox.getToggleState() && clicks==2,"Enter must retain normal checkbox activation");
        }
    }
    audioStartupFixture::run(check);
    preferencesStartupFixture::run(check);
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    scbridge::MidiActivity activity;
    {
        MainWindow hidden(*processor,activity,false,false);
        check(!hidden.isVisible(),"Hidden startup must not display the JUCE window");
        check(hidden.getPeer() && !IsWindowVisible(static_cast<HWND>(hidden.getPeer()->getNativeHandle())),
            "Hidden startup must not leave a visible native window in Alt+Tab");
        restoreControlsWindow(hidden);
        check(hidden.isVisible() && IsWindowVisible(static_cast<HWND>(hidden.getPeer()->getNativeHandle())),
            "Explicit Show Controls must restore the hidden native window");
        hidden.setVisible(false);
        check(!IsWindowVisible(static_cast<HWND>(hidden.getPeer()->getNativeHandle())),
            "Hide to tray must remove the native window, not merely minimise it");
    }
    MainWindow main(*processor,activity,false,false);
    check(main.keyPressed(juce::KeyPress(juce::KeyPress::tabKey)),
        "Tab must recover a missing main-window focus target");
    main.setVisible(true);main.toFront(true);
    check(focusFirstControl(*main.getContentComponent()),"A visible main window must actually focus a control");
    auto* focused=juce::Component::getCurrentlyFocusedComponent();
    check(focused && focused->getName()=="Control page","Initial focus must land on Control page, not a layout wrapper");
    if(focused) std::cout<<"Initial focused component: "<<focused->getName()<<'\n';
    auto* editor=processor->getActiveEditor();
    auto* preset=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("currentSound"));
    check(preset!=nullptr,"Standalone must expose its current preset selector");
    if(preset) {
        preset->grabKeyboardFocus();
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::homeKey));
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::pageDownKey));
        check(preset->getSelectedItemIndex()==10 && processor->getCurrentProgram()==preset->getItemId(10)-1,"Standalone must route Page Down from the focused current-sound selector");
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::endKey));
        check(preset->getSelectedItemIndex()==preset->getNumItems()-1,"Standalone must route End to the last sound");
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::homeKey));
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::pageUpKey));
        check(preset->getSelectedItemIndex()==0,"Standalone Home and Page Up must stop at the first sound");
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::returnKey));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        scbridge::ProgramBrowser* browser=nullptr;
        for(auto* child:editor->getChildren()) if(auto* candidate=dynamic_cast<scbridge::ProgramBrowser*>(child)) browser=candidate;
        auto* list=browser ? dynamic_cast<juce::ListBox*>(browser->findChildWithID("presetList")) : nullptr;
        check(browser && browser->isShowing() && browser->matchingCount()>0,"Enter must open a populated standalone browser");
        auto* table=list ? list->getAccessibilityHandler()->getTableInterface() : nullptr;
        auto* row=table ? table->getRowHandler(0) : nullptr;
        check(table && table->getNumRows()>0 && row && row->getTitle().isNotEmpty(),"Standalone browser must expose named rows to accessibility");
        if(browser) {
            std::cout<<"Standalone browser model="<<browser->matchingCount()<<" accessible="<<(table ? table->getNumRows() : -1)<<" bounds="<<browser->getBounds().toString()<<'\n';
            for(int step=0;step<3;++step) main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::tabKey));
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
            check(list && list->hasKeyboardFocus(true),"Three Tabs from Search must reach the preset list");
            if(list) {
                auto* accessibleFocus=list->getAccessibilityHandler()->getChildFocus();
                const auto* selectedRow=table ? table->getRowHandler(list->getSelectedRow()) : nullptr;
                check(accessibleFocus && accessibleFocus==selectedRow,"Tab into Presets must expose the selected row as accessibility focus");
                const auto selected=list->getSelectedRow();
                main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::downKey));
                check(list->getSelectedRow()==selected+1,"Preset list Down must select the next sound");
                accessibleFocus=list->getAccessibilityHandler()->getChildFocus();
                selectedRow=table ? table->getRowHandler(list->getSelectedRow()) : nullptr;
                check(accessibleFocus && accessibleFocus==selectedRow,"Preset arrows must expose the new selected row as accessibility focus");
            }
            if(argc==3 && std::string(argv[2])=="--inspect-browser") {
                std::cout<<"Browser inspection ready\n"<<std::flush;
                juce::MessageManager::getInstance()->runDispatchLoopUntil(90000);
            }
            browser->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            focusFirstControl(*main.getContentComponent());
        }
    }
    auto* controlPage=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("controlPage"));
    check(controlPage!=nullptr,"Standalone must expose the control-page selector");
    if(controlPage) {
        const auto sound=processor->getCurrentProgram();
        controlPage->grabKeyboardFocus();
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::endKey));
        check(controlPage->getSelectedItemIndex()==controlPage->getNumItems()-1,"Standalone must route End to the last control page");
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::pageUpKey));
        check(controlPage->getSelectedItemIndex()==0,"Standalone control-page Page Up must move ten entries");
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::pageDownKey));
        check(controlPage->getSelectedItemIndex()==10,"Standalone control-page Page Down must move ten entries");
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::homeKey));
        check(controlPage->getSelectedItemIndex()==0 && processor->getCurrentProgram()==sound,"Standalone page navigation must preserve the sound");
    }
    for(auto* child:editor->getChildren()) if(child->getComponentID()=="part" || child->getComponentID()=="program") {
        auto* value=child->getAccessibilityHandler()->getValueInterface();
        check(value && value->getCurrentValue()==1,"Accessible Part and Program must use the displayed one-based value");
        if(value) {
            value->setValue(2);
            check(value->getCurrentValue()==2,"Accessible numeric setters must round-trip the displayed value");
            check(dynamic_cast<juce::Slider*>(child)->getValue()==1,"Accessible setters must preserve zero-based stored values");
            value->setValueAsString("1");
            check(value->getCurrentValue()==1,"Accessible text setters must preserve display numbering");
        }
    }

    juce::AudioDeviceManager devices;
    AudioPreferences audio(devices);
    std::set<int> orders;
    for(auto* child:audio.getChildren()) {
        const int order=child->getExplicitFocusOrder();
        if(order>0) check(orders.insert(order).second,"Audio controls must have unique explicit tab positions");
    }
    check(orders.size()==9,"Audio and MIDI must contain device controls only");
    check(!audio.findChildWithID("engineLocation"),"Engine location must not remain in Audio and MIDI");
    juce::KeyboardFocusTraverser audioTraversal;
    for(auto* child:audioTraversal.getAllComponents(&audio)) {
        check(child->getName().isNotEmpty(),"Audio preferences must not contain unnamed keyboard stops");
        check(child->getName()!="Available MIDI inputs","The MIDI viewport must not add an empty keyboard stop before its checkboxes");
        std::cout<<"Audio keyboard target: "<<child->getName()<<'\n';
    }
    const auto file=juce::File::getCurrentWorkingDirectory().getChildFile("ui-test-unused-settings.xml");
    scbridge::AppPreferences preferences(file);
    scbridge::StartupService startup;
    scbridge::UpdateService updates(preferences,"test");
    SettingsPanel settings(devices,preferences,startup,updates,{},{},{});
    bool returned=false;
    settings.onClose=[&] {returned=true;main.dismissPreferences();};
    auto* originalContent=main.getContentComponent();
    main.showPreferences(settings);
    check(!settings.isOnDesktop() && settings.getPeer()==main.getPeer(),"Preferences must share the main window, not create another native window");
    check(settings.getTopLevelComponent()==&main && main.preferencesVisible(),"Preferences must be embedded in the main application window");
    check(!originalContent->isVisible(),"Hidden sound controls must not remain in the Preferences tab order");
    juce::TabbedComponent* categories=nullptr;
    for(auto* child:settings.getChildren()) if(auto* candidate=dynamic_cast<juce::TabbedComponent*>(child)) categories=candidate;
    check(categories!=nullptr,"Preferences must expose its category tabs");
    if(categories) {
        auto* engine=categories->getTabContentComponent(0)->findChildWithID("enginePreferences");
        check(engine!=nullptr,"Engine setup must live in General on Windows");
        if(engine) {
            auto* download=engine->findChildWithID("downloadEngine");
            auto* path=engine->findChildWithID("engineLocation");
            check(download && path,"General must expose engine location and the native Windows download");
            check(download && download->getWantsKeyboardFocus(),"Engine download must be keyboard reachable");
            check(download && download->getAccessibilityHandler()->getHelp().contains("Roland"),"Download needs screen-reader help");
            check(!engine->isKeyboardFocusContainer(),"Engine setup must not trap the Preferences tab loop");
            check(categories->getTabContentComponent(0)->getLocalBounds().contains(engine->getBounds()),"Engine setup must fit within General");
        }
        auto* general=categories->getTabbedButtonBar().getTabButton(0);
        auto* audioTab=categories->getTabbedButtonBar().getTabButton(1);
        check(general->getHelpText().isEmpty() && audioTab->getHelpText().isEmpty(),"Category buttons must not announce instructional hints");
        check(general->getWantsKeyboardFocus() && audioTab->getWantsKeyboardFocus(),"Both Preferences tabs must accept keyboard focus");
        auto* frequency=categories->getTabContentComponent(0)->findChildWithID("updateFrequency");
        frequency->grabKeyboardFocus();
        std::set<juce::Component*> backwards;
        for(int step=0;step<24;++step) {
            auto* current=juce::Component::getCurrentlyFocusedComponent();
            if(!current || !backwards.insert(current).second) break;
            current->moveKeyboardFocusToSibling(false);
        }
        check(backwards.count(general) && backwards.count(audioTab),"Shift+Tab from a Preferences field must reach both category tabs");
        general->grabKeyboardFocus();
        check(static_cast<juce::Component*>(general)->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)),"Right Arrow must switch Preferences category");
        check(categories->getCurrentTabIndex()==1 && audioTab->hasKeyboardFocus(false),"Category switching must select and focus Audio and MIDI");
        check(static_cast<juce::Component*>(audioTab)->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)),"Left Arrow must switch back to General");
        check(categories->getCurrentTabIndex()==0 && general->hasKeyboardFocus(false),"Category switching must retain focus on the selected tab");
        juce::Component* close=nullptr;
        for(auto* child:settings.getChildren()) if(child->getName()=="Close Preferences") close=child;
        check(close && close->getHelpText().isEmpty(),"Close Preferences must not announce instructional hints");
        for(int selected=0;selected<2;++selected) {
            categories->setCurrentTabIndex(selected);
            check(focusFirstControl(*categories->getCurrentContentComponent()),"Each Preferences page must have an initial keyboard target");
            std::set<juce::Component*> forwards;
            for(int step=0;step<64;++step) {
                auto* current=juce::Component::getCurrentlyFocusedComponent();
                if(!current || !forwards.insert(current).second) break;
                check(current->isShowing(),"Tab must not enter the hidden Preferences page");
                current->moveKeyboardFocusToSibling(true);
            }
            check(forwards.count(general) && forwards.count(audioTab) && forwards.count(close),
                "Tab from either Preferences page must reach both categories and Close");
        }
    }
    check(settings.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),"Escape must close the embedded Preferences view");
    check(!settings.isVisible() && returned && originalContent->isVisible(),"Closing Preferences must restore the sound controls");
    check(main.getContentComponent()==originalContent,"Preferences must not recreate the sound editor");
    focused=juce::Component::getCurrentlyFocusedComponent();
    check(focused && focused->getName()=="Control page","Closing Preferences must restore a usable focused control");
    juce::ApplicationCommandInfo gs(gsResetCommand),gm(gmResetCommand);
    commandInfo(gsResetCommand,gs);commandInfo(gmResetCommand,gm);
    check(gs.defaultKeypresses.size()==1 && gm.defaultKeypresses.size()==1 && gs.defaultKeypresses[0]!=gm.defaultKeypresses[0],
        "GS and GM resets need distinct keyboard shortcuts");
    juce::ApplicationCommandInfo prefs(audioSettingsCommand);commandInfo(audioSettingsCommand,prefs);
    check(prefs.defaultKeypresses.size()==1 && prefs.defaultKeypresses[0]==juce::KeyPress(',',juce::ModifierKeys::ctrlModifier,0),
        "Ctrl+comma must be the sole Windows Preferences shortcut");
    juce::ApplicationCommandInfo notes(allNotesOffCommand);commandInfo(allNotesOffCommand,notes);
    check(notes.defaultKeypresses.size()==1 && notes.defaultKeypresses[0]==juce::KeyPress(juce::KeyPress::F9Key),
        "F9 must send All Notes Off without additional modifiers");
    juce::ApplicationCommandInfo exit(juce::StandardApplicationCommandIDs::quit);
    check(commandInfo(exit.commandID,exit) && exit.shortName=="Exit" &&
        exit.defaultKeypresses.contains(juce::KeyPress('q',juce::ModifierKeys::ctrlModifier,0)),
        "Windows must expose an explicit Exit command with Ctrl+Q");
    std::set<int> actionKeys;
    for(const auto command:{audioSettingsCommand,panicCommand,gsResetCommand,gmResetCommand,restartEngineCommand,activityDisplayCommand,readActivityCommand,hideControlsCommand}) {
        juce::ApplicationCommandInfo info(command);commandInfo(command,info);
        const auto key=info.defaultKeypresses.getLast();
        check(key.getModifiers()==juce::ModifierKeys(juce::ModifierKeys::ctrlModifier),"Windows action shortcuts must use Ctrl without Shift");
        check(key.getKeyCode()!='A' && key.getKeyCode()!='a',"Select All must remain available in text controls");
        check(actionKeys.insert(juce::CharacterFunctions::toLowerCase(key.getKeyCode())).second,"Windows action shortcuts must not conflict");
    }
    auto midiMenu=windowsMidiMenu();std::set<juce::juce_wchar> mnemonics;
    for(const auto& label:midiMenu.labels) if(label.isNotEmpty()) {
        const int marker=label.indexOfChar('&');
        check(marker>=0 && marker+1<label.length(),"Every MIDI command needs a menu mnemonic");
        if(marker>=0 && marker+1<label.length()) check(mnemonics.insert(juce::CharacterFunctions::toLowerCase(label[marker+1])).second,
            "MIDI command mnemonics must be unique within their menu");
    }
    std::vector<int> broadcasts;
    scbridge::configureStandaloneEditor(*editor,[&](int map){broadcasts.push_back(map);});
    auto* partScope=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("part"));
    auto* soundMap=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("map"));
    check(partScope && partScope->getNumItems()==17 && soundMap,"Standalone Part must offer All and all sixteen numbered parts");
    if(partScope && soundMap) {
        check(partScope->getAccessibilityHandler()->getHelp().isEmpty(),"Part must not automatically announce instructions");
        check(partScope->getProperties()["scbridgeContextHelp"].toString().contains("All"),"Part instructions must remain available to F1");
        check(scbridge::contextHelp(*partScope).contains("All"),"F1 help lookup must resolve the quiet Part selector's full explanation");
        const auto savedPart=processor->getParameters()[0]->getValue();
        partScope->setSelectedId(1,juce::sendNotificationSync);
        check(broadcasts.empty() && processor->getParameters()[0]->getValue()==savedPart,"Selecting All must not edit parameters or send MIDI");
        check(!editor->findChildWithID("program")->isEnabled(),"All must disable per-part program edits");
        soundMap->setSelectedId(2,juce::sendNotificationSync);
        check(broadcasts==std::vector<int>{2},"All map must produce one explicit broadcast request");
        soundMap->setSelectedId(2,juce::sendNotificationSync);
        check(broadcasts==std::vector<int>({2,2}),"The same map must be re-applicable after external MIDI changes");
        soundMap->grabKeyboardFocus();
        for(int key:{juce::KeyPress::downKey,juce::KeyPress::downKey,juce::KeyPress::upKey})
            main.getPeer()->handleKeyPress(juce::KeyPress(key));
        check(broadcasts==std::vector<int>({2,2,3,4,3}),"All-map arrows must continue from the last request without opening the menu");
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::homeKey));
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::endKey));
        check(broadcasts==std::vector<int>({2,2,3,4,3,1,4}),"All-map Home and End must select the map limits");
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::downKey));
        check(broadcasts.size()==7,"Arrow keys at a map limit must not repeatedly reselect all tones");
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::pageUpKey));
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::pageDownKey));
        check(broadcasts==std::vector<int>({2,2,3,4,3,1,4,1,4}),"All-map page keys must move from the last requested map and clamp to its limits");
        main.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::pageDownKey));
        check(broadcasts.size()==9,"A page key at the map limit must not send a duplicate broadcast");
        partScope->setSelectedId(2,juce::sendNotificationSync);
        check(editor->findChildWithID("program")->isEnabled(),"A numbered part must restore ordinary edits");
        check(processor->getParameters().size()==93,"Standalone All must not change the VST/NKS parameter contract");
    }
    if((argc!=2 && argc!=3) || !juce::File::isAbsolutePath(argv[1])) {check(false,"An isolated test-files folder is required");}
    else {
        const auto root=juce::File(argv[1]).getNonexistentChildFile("shortcut-fixture",{});
        const auto source=root.getChildFile("portable"),local=root.getChildFile("installed"),desktop=root.getChildFile("desktop"),login=root.getChildFile("startup");
        check(source.createDirectory().wasOk() && desktop.createDirectory().wasOk() && login.createDirectory().wasOk(),"Create isolated shortcut fixture");
        const juce::StringArray files{"Sound Canvas Bridge.exe","SoundCanvasHelper.exe","Manual.html","VersionHistory.html","SCCore.dll","Settings.xml"};
        for(const auto& name:files) check(source.getChildFile(name).replaceWithText("test fixture "+name),"Create application-copy fixture");
        scbridge::WindowsInstallation installation(local);
        check(installation.existingExecutable()==juce::File{},
            "A missing local installation must not be selected for shortcuts");
        const auto result=installation.install(source.getChildFile("Sound Canvas Bridge.exe"));
        check(result.wasOk(),"Local application installation must succeed");
        check(installation.existingExecutable()==installation.executable(),
            "Portable shortcut actions must reuse an existing complete local installation");
        check(!local.getChildFile("SCCore.dll").exists() && !local.getChildFile("Settings.xml").exists(),"Local installation must not copy Roland files or user settings");
        check(installation.install(source.getChildFile("Sound Canvas Bridge.exe")).wasOk(),"Identical local installation must be reusable");
        juce::String error;
        check(scbridge::StartupService::addDesktopShortcut(installation.executable(),error,desktop),"Create isolated desktop shortcut");
        const auto shortcut=desktop.getChildFile("Sound Canvas Bridge.lnk");
        juce::MemoryBlock before;shortcut.loadFileAsData(before);
        check(scbridge::StartupService::addDesktopShortcut(installation.executable(),error,desktop),"Desktop shortcut creation must be idempotent");
        juce::MemoryBlock after;shortcut.loadFileAsData(after);check(before==after,"Existing correct desktop shortcut must be unchanged");
        IShellLinkW* link=nullptr;
        check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_IShellLinkW,reinterpret_cast<void**>(&link))),"Read back the generated shortcut");
        if(link) {
            IPersistFile* persistence=nullptr;
            if(SUCCEEDED(link->QueryInterface(IID_IPersistFile,reinterpret_cast<void**>(&persistence)))) {
                wchar_t target[32768]{},arguments[32768]{};
                check(SUCCEEDED(persistence->Load(shortcut.getFullPathName().toWideCharPointer(),STGM_READ)),"Load saved desktop shortcut");
                check(SUCCEEDED(link->GetPath(target,32768,nullptr,SLGP_RAWPATH)) && juce::File(juce::String(target))==installation.executable(),"Desktop shortcut must target the local installation");
                check(SUCCEEDED(link->GetArguments(arguments,32768)) && juce::String(arguments)=="--show-controls","Desktop launch must explicitly restore controls");
                persistence->Release();
            } else check(false,"Read shortcut persistence interface");
            link->Release();
        }
        scbridge::StartupService startupFixture(login);
        check(startupFixture.setEnabled(true,installation.executable(),error),"Startup must target the local installation");
        check(startupFixture.setEnabled(false,installation.executable(),error) && !startupFixture.enabled(),"Disabling startup must restore absence of its shortcut");
        shortcut.replaceWithText("unrelated shortcut");
        check(!scbridge::StartupService::addDesktopShortcut(installation.executable(),error,desktop) && shortcut.loadFileAsString()=="unrelated shortcut","An unrelated desktop file must not be overwritten");
        source.getChildFile("SoundCanvasHelper.exe").replaceWithText("changed source");
        check(installation.existingExecutable()==installation.executable(),
            "Shortcut actions must reuse a complete local copy even if the portable build differs");
        check(installation.install(source.getChildFile("Sound Canvas Bridge.exe")).failed() && local.getChildFile("SoundCanvasHelper.exe").loadFileAsString()=="test fixture SoundCanvasHelper.exe",
            "A differing local installation must not be replaced or damaged");
        local.getChildFile("SoundCanvasHelper.exe").deleteFile();
        check(installation.existingExecutable()==juce::File{},
            "An incomplete local installation must not be used for shortcuts");
        for(const auto& name:files) {source.getChildFile(name).deleteFile();local.getChildFile(name).deleteFile();}
        shortcut.deleteFile();source.deleteFile();local.deleteFile();desktop.deleteFile();login.deleteFile();root.deleteFile();
        check(!root.exists(),"Owned installation-test files must be cleaned up");
    }
    std::cout<<"Windows UI failures: "<<failures<<'\n';
    return failures ? 1 : 0;
}
