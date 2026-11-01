#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>

namespace scbridge {
// A note-event display, not an estimate of DSP voices or audio amplitude.
// MIDI callbacks publish fixed-size pulses; only the UI computes their decay.
class MidiActivity {
public:
    static constexpr uint32_t pulseMilliseconds=400;
    void clear() { for(auto& channel:channels) channel.store(0,std::memory_order_relaxed); }
    void observe(const juce::MidiMessage& message,uint32_t now) {
        const int channel=message.getChannel()-1;
        if(message.isNoteOn() && channel>=0 && channel<16)
            channels[channel].store((uint64_t(now)<<8)|message.getVelocity(),std::memory_order_relaxed);
        else if(message.isController() && channel>=0 && channel<16 &&
                (message.getControllerNumber()==120 || message.getControllerNumber()==123))
            channels[channel].store(0,std::memory_order_relaxed);
        else if(message.isSysEx()) {
            const auto* data=message.getRawData();const auto size=message.getRawDataSize();
            const bool gm=size==6 && data[1]==0x7e && data[3]==9 && (data[4]==1 || data[4]==3);
            const bool gs=size==11 && data[1]==0x41 && data[3]==0x42 && data[4]==0x12 &&
                data[5]==0x40 && data[6]==0 && data[7]==0x7f && data[8]==0;
            if(gm || gs) clear();
        }
    }
    std::array<int,16> levels(uint32_t now) const {
        std::array<int,16> result{};
        for(unsigned channel=0;channel<16;++channel) {
            const auto pulse=channels[channel].load(std::memory_order_relaxed);
            const auto age=now-static_cast<uint32_t>(pulse>>8);
            if(!pulse || age>=pulseMilliseconds) continue;
            const auto velocity=static_cast<int>(pulse&127);
            result[channel]=std::max(1,velocity*static_cast<int>(pulseMilliseconds-age)/static_cast<int>(pulseMilliseconds));
        }
        return result;
    }
private:
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::array<std::atomic<uint64_t>,16> channels{};
};

class ActivityMidiInput final : public juce::MidiInputCallback {
public:
    template<class Receiver>
    ActivityMidiInput(Receiver& destination,MidiActivity& state)
        :deliver([&destination](juce::MidiInput* input,const juce::MidiMessage& message) {
            destination.handleIncomingMidiMessage(input,message);
        }),activity(state) {}
    void handleIncomingMidiMessage(juce::MidiInput* input,const juce::MidiMessage& message) override {
        activity.observe(message,juce::Time::getMillisecondCounter());
        deliver(input,message);
    }
private:
    std::function<void(juce::MidiInput*,const juce::MidiMessage&)> deliver;
    MidiActivity& activity;
};

class ActivityDisplay final : public juce::Component,private juce::Timer {
public:
    explicit ActivityDisplay(MidiActivity& source,std::function<uint32_t()> clock=[] { return juce::Time::getMillisecondCounter(); })
        :activity(source),now(std::move(clock)) {
        setName("Recent MIDI channel activity");setAccessible(false);setWantsKeyboardFocus(false);
        startTimerHz(30);
    }
    ~ActivityDisplay() override { stopTimer(); }
    void visibilityChanged() override { if(isVisible()) startTimerHz(30);else stopTimer(); }
    void paint(juce::Graphics& graphics) override {
        graphics.fillAll(juce::Colour(0xff101b12));
        graphics.setColour(juce::Colour(0xffd5e6ce));graphics.setFont(13.0f);
        graphics.drawText("MIDI CHANNELS",8,6,getWidth()-16,24,juce::Justification::centred);
        const auto values=activity.levels(now());
        const int row=(getHeight()-42)/16;
        const uint32_t colours[]={0xff49c6c7,0xff5bd1b0,0xff72d67a,0xffa2d35a,0xffd9c94b,0xffe7aa46,
            0xffeb874a,0xffea6c65,0xffe880b0,0xffcc8ddd,0xffad98e5,0xff8eb4e8,0xff72c7e0,0xff72d5c8,0xff9cdaa2,0xffd5d986};
        const int segmentWidth=(getWidth()-42)/12;
        for(int channel=0;channel<16;++channel) {
            const int y=36+channel*row;
            graphics.setColour(juce::Colour(0xffd5e6ce));
            graphics.drawText(juce::String(channel+1),4,y,25,row,juce::Justification::centredRight);
            const int lit=values[channel] ? std::max(1,(values[channel]*12+126)/127) : 0;
            for(int segment=0;segment<12;++segment) {
                graphics.setColour(segment<lit ? juce::Colour(colours[channel]) : juce::Colour(0xff243929));
                graphics.fillRect(36+segment*segmentWidth,y+row/4,segmentWidth-2,std::max(3,row/2));
            }
        }
    }
private:
    void timerCallback() override {
        if(!isShowing()) return;
        const auto current=activity.levels(now());
        if(current!=previous) { previous=current;repaint(); }
    }
    MidiActivity& activity;
    std::function<uint32_t()> now;
    std::array<int,16> previous{};
};
}
