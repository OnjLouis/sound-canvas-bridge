#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "SessionMidiInput.h"

namespace scbridge {
// Audio-device lifetimes are shorter than a MIDI module session. Keep the synth
// and its collector alive; only the output conversion follows the device rate.
class PersistentEngineOutput final : public juce::AudioIODeviceCallback {
    class EngineSource final : public juce::AudioSource {
    public:
        EngineSource(juce::AudioProcessor& engine,SessionMidiInput& input):collector(input),processor(engine) {
            events.ensureSize(16384);
        }
        ~EngineSource() override { if(prepared) processor.releaseResources(); }
        void prepareToPlay(int,double rate) override {
            if(prepared) return;
            processor.setPlayConfigDetails(0,2,rate,renderChunk);
            processor.prepareToPlay(rate,renderChunk);prepared=true;
        }
        void releaseResources() override {}
        void setResampled(bool enabled) { resampled=enabled;cachedRead=cachedCount=0; }
        void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
            for(int offset=0;offset<info.numSamples;) {
                if(!resampled) {
                    const int frames=std::min(renderChunk,info.numSamples-offset);
                    juce::AudioBuffer<float> buffer(info.buffer->getArrayOfWritePointers(),2,info.startSample+offset,frames);
                    collector.removeNextBlockOfMessages(events,frames);processor.processBlock(buffer,events);offset+=frames;
                    continue;
                }
                if(cachedCount==0) {
                    collector.removeNextBlockOfMessages(events,conversionQuantum);processor.processBlock(cache,events);
                    cachedRead=0;cachedCount=conversionQuantum;
                }
                const int frames=std::min(cachedCount,info.numSamples-offset);
                for(int channel=0;channel<2;++channel)
                    info.buffer->copyFrom(channel,info.startSample+offset,cache,channel,cachedRead,frames);
                cachedRead+=frames;cachedCount-=frames;offset+=frames;
            }
        }
        SessionMidiInput& collector;
    private:
        static constexpr int renderChunk=4096;
        static constexpr int conversionQuantum=32;
        juce::AudioProcessor& processor;
        juce::MidiBuffer events;
        bool prepared=false;
        bool resampled=false;
        juce::AudioBuffer<float> cache{2,conversionQuantum};
        int cachedRead=0,cachedCount=0;
    };
public:
    PersistentEngineOutput(juce::AudioProcessor& processor,SessionMidiInput& input):source(processor,input),resampler(&source,false,2) {
        output.setSource(&source);
    }
    ~PersistentEngineOutput() override { output.setSource(nullptr); }
    SessionMidiInput& midiCollector() { return source.collector; }
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        if(nativeRate==0) nativeRate=device->getCurrentSampleRate();
        const bool converting=nativeRate!=device->getCurrentSampleRate();
        source.setResampled(converting);
        resampler.setResamplingRatio(nativeRate/device->getCurrentSampleRate());
        output.setSource(converting ? static_cast<juce::AudioSource*>(&resampler) : &source);
        output.audioDeviceAboutToStart(device);
        source.collector.resume(nativeRate);
    }
    void audioDeviceStopped() override { source.collector.pause();output.audioDeviceStopped(); }
    void audioDeviceIOCallbackWithContext(const float* const* in,int inputs,float* const* out,int outputs,int frames,
                                         const juce::AudioIODeviceCallbackContext& context) override {
        output.audioDeviceIOCallbackWithContext(in,inputs,out,outputs,frames,context);
    }
private:
    EngineSource source;
    juce::ResamplingAudioSource resampler;
    juce::AudioSourcePlayer output;
    double nativeRate=0;
};
}
