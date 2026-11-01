#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace scbridge {
// JUCE deliberately ages out collector events during long callback gaps. During
// device changes retain ordered setup messages separately, without that expiry.
class SessionMidiInput {
public:
    SessionMidiInput() { collector.reset(44100);pending.ensureSize(16384); }
    void handleIncomingMidiMessage(juce::MidiInput*,const juce::MidiMessage& message) { addMessageToQueue(message); }
    void addMessageToQueue(const juce::MidiMessage& message) {
        const juce::ScopedLock lock(mutex);
        if(!paused) { collector.addMessageToQueue(message);return; }
        const int bytes=message.getRawDataSize()+16;
        if(bytes>maximumPausedBytes-pendingBytes) {
            if(!warned) {
                warned=true;
                juce::MessageManager::callAsync([] {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"MIDI input backlog",
                        "MIDI continued for too long without an audio output. Stop and restart MIDI playback after selecting an output.");
                });
            }
            return;
        }
        pending.addEvent(message,0);pendingBytes+=bytes;
    }
    void pause() {
        const juce::ScopedLock lock(mutex);
        if(paused) return;
        juce::MidiBuffer waiting;collector.removeNextBlockOfMessages(waiting,1<<20);
        for(const auto event:waiting) { pending.addEvent(event.data,event.numBytes,0);pendingBytes+=event.numBytes+16; }
        paused=true;
    }
    void resume(double rate) {
        const juce::ScopedLock lock(mutex);
        collector.reset(rate);paused=false;
    }
    void removeNextBlockOfMessages(juce::MidiBuffer& output,int frames) {
        const juce::ScopedLock lock(mutex);
        output.clear();output.addEvents(pending,0,-1,0);pending.clear();pendingBytes=0;warned=false;
        collector.removeNextBlockOfMessages(output,frames);
    }
    void clear() {
        const juce::ScopedLock lock(mutex);
        collector.reset(44100);pending.clear();pendingBytes=0;paused=true;warned=false;
    }
private:
    static constexpr int maximumPausedBytes=2*1024*1024;
    juce::CriticalSection mutex;
    juce::MidiMessageCollector collector;
    juce::MidiBuffer pending;
    bool paused=true,warned=false;
    int pendingBytes=0;
};
}
