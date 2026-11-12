#pragma once
#include "../Source/NumericSlider.h"

namespace musicalKeyboardFixture {
template<class Check> void run(Check check,std::function<bool(const juce::String&)> wasAnnounced={}) {
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter());
    scbridge::MidiActivity activity;
    std::vector<juce::MidiMessage> events;
    MainWindow window(*processor,activity,false,true,[](int) {},[&](juce::MidiMessage note) { events.push_back(note); });
    window.toFront(true);
    auto* editor=processor->getActiveEditor();
    auto* button=editor ? dynamic_cast<juce::TextButton*>(editor->findChildWithID("musicalKeyboard")) : nullptr;
    auto* strip=editor ? editor->findChildWithID("musicalKeyboardStrip") : nullptr;
    auto* program=editor ? dynamic_cast<scbridge::NumericSlider*>(editor->findChildWithID("program")) : nullptr;
    auto* part=editor ? dynamic_cast<juce::ComboBox*>(editor->findChildWithID("part")) : nullptr;
    auto* sound=editor ? editor->findChildWithID("currentSound") : nullptr;
    auto* octave=strip ? dynamic_cast<scbridge::NumericSlider*>(strip->findChildWithID("typingOctave")) : nullptr;
    auto* velocity=strip ? dynamic_cast<scbridge::NumericSlider*>(strip->findChildWithID("typingVelocity")) : nullptr;
    check(button && strip && program && part && sound && octave && velocity,"Standalone keyboard controls must exist");
    if(!button || !strip || !program || !part || !sound || !octave || !velocity) return;
    auto press=[&](int code,juce::juce_wchar text=0) {
        return window.getPeer()->handleKeyPress(juce::KeyPress(code,{},text));
    };
    auto waitUntil=[](auto ready) {
        const auto deadline=juce::Time::getMillisecondCounterHiRes()+1000;
        while(!ready() && juce::Time::getMillisecondCounterHiRes()<deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        return ready();
    };
    auto on=[&](size_t index,int channel,int note) {
        return events.size()>index && events[index].isNoteOn() && events[index].getChannel()==channel && events[index].getNoteNumber()==note;
    };
    // The fixture explicitly places focus; test the normal startup focus separately.
    window.getContentComponent()->getProperties().set("initialFocusRequest",0);
    check(!strip->isVisible(),"Musical keyboard is opt-in");
    check(!octave->isShowing() && !velocity->isShowing(),"Keyboard sliders are hidden until the keyboard is armed");
    button->grabKeyboardFocus();button->onClick();
    check(button->getToggleState() && strip->isShowing(),"The main button arms and shows the keyboard");
    check(octave->isShowing() && velocity->isShowing() && octave->getValue()==4 && velocity->getValue()==88,
          "Keyboard sliders are visible with the initial octave and velocity");
    part->setSelectedId(3,juce::sendNotificationSync);
    program->grabKeyboardFocus();
    check(program->hasKeyboardFocus(false),"Program slider must have focus before auditioning");
    check(press('A','a') && on(0,2,60),"White note plays on the selected part with a slider focused");
    check(press('W','w') && on(1,2,61),"Sharp note plays while another note is held");
    check(press(juce::KeyPress::returnKey) && !strip->isVisible() && events.size()==4 && events[2].isNoteOff() && events[3].isNoteOff(),
          "Enter-to-type hides the strip and immediately releases held notes");
    press('A','a');
    check(events.size()==4,"Typing in a value editor must not play notes");
    press(juce::KeyPress::escapeKey);
    check(waitUntil([&] { return strip->isShowing(); }),"Cancelling direct entry restores the keyboard");
    check(program->hasKeyboardFocus(false),"Closing direct entry restores focus to the slider");
    press(juce::KeyPress::returnKey);press('5','5');press('7','7');press(juce::KeyPress::returnKey);
    check(waitUntil([&] { return strip->isShowing(); }) && !program->isEnteringValue() && program->getValue()==56,
          "Confirming a typed Program value applies it and restores the keyboard");
    sound->grabKeyboardFocus();
    press(juce::KeyPress::returnKey);
    press('A','a');
    check(!strip->isVisible() && events.size()==4,"Preset search pauses the keyboard without playing search characters");
    press(juce::KeyPress::escapeKey);
    check(waitUntil([&] { return strip->isShowing(); }),"Closing preset search restores musical typing");
    part->setSelectedId(1,juce::sendNotificationSync);
    part->grabKeyboardFocus();
    check(press('X','x'),"Musical keyboard handles octave changes");
    if(wasAnnounced)
        check(waitUntil([&] { return wasAnnounced("Octave 5"); }),"Octave changes send a native accessibility notification");
    check(press('V','v'),"Musical keyboard handles velocity changes");
    if(wasAnnounced)
        check(waitUntil([&] { return wasAnnounced("Velocity 96"); }),"Velocity changes send a native accessibility notification");
    check(octave->getValue()==5 && velocity->getValue()==96,"Shortcuts keep both slider readouts in sync");
    check(part->hasKeyboardFocus(false),"Musical keyboard announcements do not move focus");
    check(press('A','a') && on(4,2,72) && events[4].getVelocity()==96,
          "All auditions the last numbered part at the selected octave and velocity");
    window.setVisible(false);
    check(waitUntil([&] { return !strip->isVisible(); }) && button->getToggleState() && events.size()==6 && events.back().isNoteOff(),
          "Hiding the window releases notes without forgetting the keyboard toggle");
    window.setVisible(true);window.toFront(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    window.getContentComponent()->getProperties().set("initialFocusRequest",0);
    part->grabKeyboardFocus();
    check(press('S','s') && strip->isShowing() && on(6,2,74),"Returning to the window restores the keyboard");
    press(juce::KeyPress::escapeKey);
    check(!button->getToggleState() && !strip->isVisible() && events.size()==8 && events.back().isNoteOff(),
          "Escape closes the keyboard and releases its notes");
    part->setSelectedId(3,juce::sendNotificationSync);
    button->grabKeyboardFocus();button->onClick();
    octave->setValue(3,juce::sendNotificationSync);
    velocity->setValue(73,juce::sendNotificationSync);
    octave->grabKeyboardFocus();
    check(octave->hasKeyboardFocus(false) && press('A','a') && on(8,2,48) && events[8].getVelocity()==73,
          "Slider changes set the pitch and velocity of the next note");
    check(press(juce::KeyPress::returnKey) && octave->isEnteringValue() &&
          events.size()==10 && events.back().isNoteOff(),"Opening a keyboard slider value releases held notes");
    press('A','a');
    check(events.size()==10,"Entering a keyboard slider value cannot trigger notes");
    press(juce::KeyPress::escapeKey);
    check(waitUntil([&] { return !octave->isEnteringValue() && octave->hasKeyboardFocus(false); }),
          "Closing keyboard slider entry restores its focus");
    press(juce::KeyPress::tabKey);
    check(velocity->hasKeyboardFocus(false),"Tab moves from Octave to Velocity");
    check(press('P','p') && press(';',';') && press('\'','\'') &&
          on(10,2,63) && on(11,2,64) && on(12,2,65),"Extended keys play D sharp through F in the standalone");
    press(juce::KeyPress::escapeKey);
    check(!octave->isShowing() && !velocity->isShowing() && button->hasKeyboardFocus(false),
          "Closing the keyboard hides both sliders and restores focus to its button");
    check(events.size()==16 && events[13].isNoteOff() && events[14].isNoteOff() && events[15].isNoteOff(),
          "Closing the keyboard releases the extended keys");
    juce::ApplicationCommandInfo command(musicalKeyboardCommand);
    check(commandInfo(musicalKeyboardCommand,command) && command.defaultKeypresses.size()==1 &&
          command.defaultKeypresses[0]==juce::KeyPress('k',juce::ModifierKeys::commandModifier,0),
          "The standalone shortcut uses Command on Mac and Control on Windows");
}
}
