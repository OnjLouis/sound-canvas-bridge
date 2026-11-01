#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace scbridge {
struct PhraseEvent { int milliseconds,note;bool on; };
inline constexpr std::array<PhraseEvent,8> testPhraseEvents{{
    {100,60,true},{280,60,false},{320,64,true},{500,64,false},
    {540,67,true},{720,67,false},{760,72,true},{1150,72,false}
}};
inline constexpr int testPhraseDurationMs=1650;

// An isolated default engine shares the selected output, never the user's parts.
class EngineTestPhrase final : private juce::Timer {
public:
    explicit EngineTestPhrase(juce::AudioDeviceManager& output):devices(output) {}
    ~EngineTestPhrase() override { stop(); }
    bool isPlaying() const { return processor!=nullptr; }
    bool start() {
        if(isPlaying()) return true;
        if(!devices.getCurrentAudioDevice()) return false;
        processor.reset(createPluginFilter());
        player.setProcessor(processor.get());devices.addAudioCallback(&player);
        started=juce::Time::getMillisecondCounterHiRes();next=0;startTimer(5);
        return true;
    }
    void stop() {
        stopTimer();
        if(!processor) return;
        devices.removeAudioCallback(&player);player.setProcessor(nullptr);processor.reset();
    }
private:
    void timerCallback() override {
        const auto elapsed=juce::Time::getMillisecondCounterHiRes()-started;
        while(next<testPhraseEvents.size() && elapsed>=testPhraseEvents[next].milliseconds) {
            const auto event=testPhraseEvents[next++];
            auto message=event.on ? juce::MidiMessage::noteOn(1,event.note,juce::uint8(88))
                                  : juce::MidiMessage::noteOff(1,event.note);
            message.setTimeStamp(juce::Time::getMillisecondCounterHiRes()*0.001);
            player.getMidiMessageCollector().addMessageToQueue(message);
        }
        if(elapsed>=testPhraseDurationMs) stop();
    }
    juce::AudioDeviceManager& devices;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<juce::AudioProcessor> processor;
    double started=0;
    size_t next=0;
};
}
