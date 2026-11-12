#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "KeyboardAnnouncement.h"
#include "LegacyWindowsAccessibility.h"
#include "MusicalTyping.h"
#include "NumericSlider.h"
#include "ControlHelp.h"

namespace scbridge {
class MusicalKeyboardStrip final : public juce::Component, private juce::Timer {
public:
    explicit MusicalKeyboardStrip(std::function<void(juce::MidiMessage)> output): send(std::move(output)) {
        setComponentID("musicalKeyboardStrip");
        setName("Musical Keyboard settings");
        setExplicitFocusOrder(101);
        configureSetting(octaveSlider,octaveLabel,"typingOctave","Octave",lowestTypingOctave,highestTypingOctave,1,
                         "Sets the keyboard's octave. Z and X also change it.");
        configureSetting(velocitySlider,velocityLabel,"typingVelocity","Velocity",lowestTypingVelocity,highestTypingVelocity,2,
                         "Sets the velocity of new keyboard notes. C and V also change it.");
        octaveSlider.textFromValueFunction=[](double value) {
            const auto octave=static_cast<int>(value);
            return octave<0 ? "minus "+juce::String(-octave) : juce::String(octave);
        };
        octaveSlider.onValueChange=[this] { notes.setOctave(static_cast<int>(octaveSlider.getValue())); };
        velocitySlider.onValueChange=[this] { notes.setVelocity(static_cast<int>(velocitySlider.getValue())); };
        syncSettings();
    }
    ~MusicalKeyboardStrip() override { stopTimer(); releaseNotes(); }

    std::function<void()> refresh;
    bool isEnteringSetting() const { return octaveSlider.isEnteringValue() || velocitySlider.isEnteringValue(); }
    void setArmed(bool armed,bool drums) {
        if(armed) {
            notes.start(drums);
            syncSettings();
            blockHeldKeys();
            startTimerHz(60);
        } else {
            stopTimer();
            setPlaying(false);
        }
    }
    void setPlaying(bool playing) {
        if(playing==isVisible()) return;
        if(!playing) { blockHeldKeys(); releaseNotes(); }
        setVisible(playing);
    }
    void setPart(int channel,const juce::String& name) {
        if(channel!=part) releaseNotes();
        if(channel==part && name==partName) return;
        part=channel;partName=name;repaint();
    }
    bool playKey(const juce::KeyPress& key) {
        if(!isVisible() || isEnteringSetting() || key.getModifiers().isAnyModifierKeyDown()) return false;
        const auto code=key.getKeyCode();
        const int index=MusicalTyping::keyIndex(code);
        if(index>=0) {
            if(!blocked[static_cast<size_t>(index)]) press(index,index);
            return true;
        }
        const bool octave=code=='Z' || code=='z' || code=='X' || code=='x';
        const bool velocity=code=='C' || code=='c' || code=='V' || code=='v';
        if(!octave && !velocity) return false;
        const int step=code=='Z' || code=='z' || code=='C' || code=='c' ? -1 : 1;
        if(octave ? notes.shiftOctave(step) : notes.shiftVelocity(step)) {
            syncSettings();
            auto& slider=octave ? octaveSlider : velocitySlider;
            const auto message=slider.getName()+" "+slider.getTextFromValue(slider.getValue());
#ifdef SCBRIDGE_WINDOWS7
            if(auto* focus=getCurrentlyFocusedComponent()) windowsAnnouncement.post(*focus,message);
#else
            if(auto* focus=getCurrentlyFocusedComponent()) announceKeyboardSetting(*focus,message);
#endif
        }
        return true;
    }
    void releaseNotes() {
        notes.releaseAll([this](int channel,int pitch,int velocity) { emit(channel,pitch,velocity); });
        pointerIndex=-1;
        repaint();
    }
    void releaseKeysUp() {
        for(int index=0;index<MusicalTyping::mouseKey;++index) {
            auto& waitForUp=blocked[static_cast<size_t>(index)];
            if((waitForUp || notes.held(index)) && !keyDown(index)) {
                waitForUp=false;
                release(index);
            }
        }
    }
    void resized() override {
        auto settings=getLocalBounds().removeFromRight(juce::jmin(270,getWidth()/2));
        settings.removeFromLeft(12);
        auto row=settings.removeFromTop(36);
        octaveLabel.setBounds(row.removeFromLeft(62));octaveSlider.setBounds(row);
        settings.removeFromTop(4);
        row=settings.removeFromTop(36);
        velocityLabel.setBounds(row.removeFromLeft(62));velocitySlider.setBounds(row);
    }
    void paint(juce::Graphics& g) override {
        if(isEnteringSetting()) return;
        g.setColour(juce::Colours::white);
        g.drawFittedText("Part "+partName,pianoBounds().removeFromTop(captionHeight),juce::Justification::centredLeft,1);
        for(bool sharp:{false,true}) for(int index=0;index<MusicalTyping::mouseKey;++index) {
            const auto& key=typingKeys[static_cast<size_t>(index)];
            if(key.sharp!=sharp) continue;
            const auto bounds=keyBounds(index);
            g.setColour(notes.pitchHeld(key.semitone) ? juce::Colour(0xff8bd9c9) :
                sharp ? juce::Colour(0xff20262b) : juce::Colour(0xfff6f7f7));
            g.fillRect(bounds);
            g.setColour(juce::Colour(0xff535b61));g.drawRect(bounds);
            g.setColour(sharp && !notes.pitchHeld(key.semitone) ? juce::Colours::white : juce::Colours::black);
            g.drawText(juce::String::charToString(juce::CharacterFunctions::toUpperCase(key.key)),
                bounds.toNearestInt().withTrimmedTop(sharp ? 0 : 28),juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override { playPointer(e.position); }
    void mouseDrag(const juce::MouseEvent& e) override { if(isVisible()) playPointer(e.position); }
    void mouseUp(const juce::MouseEvent&) override { release(MusicalTyping::mouseKey);pointerIndex=-1; }

private:
    void configureSetting(NumericSlider& slider,juce::Label& label,const char* id,const char* name,int low,int high,
                          int order,const char* help) {
        label.setText(name,juce::dontSendNotification);
        label.setAccessible(false);
        addAndMakeVisible(label);
        slider.setComponentID(id);slider.setName(name);slider.setTitle(name);
        slider.setWantsKeyboardFocus(true);slider.setExplicitFocusOrder(order);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setRange(low,high,1);
        slider.configureValueField(56);
        slider.getProperties().set(explicitHelpProperty,help);
        slider.onEntryChanged=[this] {
            if(isEnteringSetting()) { blockHeldKeys();releaseNotes(); }
            repaint();
        };
        addAndMakeVisible(slider);
    }
    void syncSettings() {
        octaveSlider.setValue(notes.octave(),juce::dontSendNotification);
        velocitySlider.setValue(notes.velocity(),juce::dontSendNotification);
        repaint();
    }
    juce::Rectangle<int> pianoBounds() const {
        return getLocalBounds().withTrimmedRight(juce::jmin(270,getWidth()/2));
    }
    void timerCallback() override { if(refresh) refresh(); releaseKeysUp(); }
    static bool keyDown(int index) { return juce::KeyPress::isKeyCurrentlyDown(typingKeys[static_cast<size_t>(index)].key); }
    void blockHeldKeys() { for(int i=0;i<MusicalTyping::mouseKey;++i) blocked[static_cast<size_t>(i)]=keyDown(i); }
    void emit(int channel,int pitch,int velocity) {
        send(velocity ? juce::MidiMessage::noteOn(channel,pitch,static_cast<juce::uint8>(velocity)) : juce::MidiMessage::noteOff(channel,pitch));
    }
    void press(int slot,int index) {
        if(notes.held(slot)) return;
        notes.press(slot,index,part,[this](int channel,int pitch,int velocity) { emit(channel,pitch,velocity); });
        repaint();
    }
    void release(int slot) {
        if(!notes.held(slot)) return;
        notes.release(slot,[this](int channel,int pitch,int velocity) { emit(channel,pitch,velocity); });
        repaint();
    }
    juce::Rectangle<float> keyBounds(int index) const {
        const auto width=static_cast<float>(pianoBounds().getWidth())/typingWhiteKeyCount;
        int white=0;
        for(int i=0;i<index;++i) if(!typingKeys[static_cast<size_t>(i)].sharp) ++white;
        return typingKeys[static_cast<size_t>(index)].sharp ? juce::Rectangle<float>(width*(white-0.29f),captionHeight,width*0.58f,28) :
            juce::Rectangle<float>(width*white,captionHeight,width,static_cast<float>(getHeight()-captionHeight));
    }
    void playPointer(juce::Point<float> position) {
        if(isEnteringSetting()) return;
        int found=-1;
        for(bool sharp:{true,false}) {
            for(int i=0;i<MusicalTyping::mouseKey;++i) if(typingKeys[static_cast<size_t>(i)].sharp==sharp && keyBounds(i).contains(position)) { found=i;break; }
            if(found>=0) break;
        }
        if(found==pointerIndex) return;
        release(MusicalTyping::mouseKey);pointerIndex=found;
        if(found>=0) press(MusicalTyping::mouseKey,found);
    }
    static constexpr int captionHeight=22;
    std::function<void(juce::MidiMessage)> send;
    MusicalTyping notes;
    std::array<bool,typingKeys.size()> blocked{};
    int part=1,pointerIndex=-1;
    juce::String partName="1";
    NumericSlider octaveSlider,velocitySlider;
    juce::Label octaveLabel,velocityLabel;
#ifdef SCBRIDGE_WINDOWS7
    LegacyWindowsAnnouncement windowsAnnouncement;
#endif
};
}
