#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace scbridge {
inline juce::String initialiseStandaloneAudio(juce::AudioDeviceManager& devices,const juce::XmlElement* saved) {
    juce::AudioDeviceManager::AudioDeviceSetup preferred;preferred.bufferSize=128;
    // A disconnected or busy saved output must not silently select a different
    // physical interface. Only first-run setup may select the system default.
    return devices.initialise(0,2,saved,saved==nullptr,{},&preferred);
}
}
