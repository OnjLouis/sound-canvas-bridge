#pragma once
#include "../Source/KeyboardChoice.h"

namespace keyboardChoiceFixture {
template<typename Check> void run(Check check) {
    scbridge::KeyboardChoice choice;
    choice.setName("Test choice");
    juce::Component wrapper;
    wrapper.setBounds(-10000,-10000,200,40);
    choice.setBounds(0,0,200,40);
    wrapper.addAndMakeVisible(choice);
    wrapper.addToDesktop(0);wrapper.setVisible(true);
    for(int i=0;i<25;++i) choice.addItem("Item "+juce::String(i+1),100+i*3);
    choice.setSelectedItemIndex(4,juce::dontSendNotification);
    int changes=0;choice.onChange=[&] {++changes;};
    auto press=[&](int key) {return choice.keyPressed(juce::KeyPress(key));};
    check(press(juce::KeyPress::homeKey) && choice.getSelectedItemIndex()==0,"Choice Home must select the first item");
    check(press(juce::KeyPress::pageDownKey) && choice.getSelectedItemIndex()==10,"Choice Page Down must advance ten item positions");
    check(choice.getSelectedId()==130,"Choice navigation must preserve sparse item IDs");
    check(press(juce::KeyPress::pageUpKey) && choice.getSelectedItemIndex()==0,"Choice Page Up must go back ten positions");
    check(press(juce::KeyPress::endKey) && choice.getSelectedItemIndex()==24,"Choice End must select the last item");
    const auto atEnd=changes;
    check(press(juce::KeyPress::pageDownKey) && choice.getSelectedItemIndex()==24 && changes==atEnd,"Choice end boundary must not wrap or send duplicate changes");
    choice.setSelectedItemIndex(22,juce::dontSendNotification);
    check(press(juce::KeyPress::pageDownKey) && choice.getSelectedItemIndex()==24,"Choice page jumps must clamp to the last item");
    const auto* access=choice.getAccessibilityHandler();
    const auto* value=access ? access->getValueInterface() : nullptr;
    check(value && value->getCurrentValueAsString()=="Item 25","Choice jumps must expose the selected name to accessibility");
    choice.setSelectedItemIndex(12,juce::dontSendNotification);
    choice.keyPressed(juce::KeyPress(juce::KeyPress::homeKey,juce::ModifierKeys::ctrlModifier,0));
    check(choice.getSelectedItemIndex()==12,"Modified Home must retain host shortcut handling");
    choice.setEnabled(false);
    check(!press(juce::KeyPress::homeKey) && choice.getSelectedItemIndex()==12,"A disabled choice must not change its selection");
    scbridge::KeyboardChoice empty;
    check(empty.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)) && empty.getSelectedId()==0,"An empty choice must handle Home without selecting an invalid item");
}
}
