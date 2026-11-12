#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "StandaloneControls.h"

namespace scbridge {
// The standalone view can broadcast a map without changing the VST parameter contract.
void configureStandaloneEditor(juce::AudioProcessorEditor&,std::function<void(int)> applyMapToAll,
                               std::function<void(juce::MidiMessage)> playNote={});
void toggleStandaloneMusicalKeyboard(juce::AudioProcessorEditor&);
bool standaloneMapSelection(juce::AudioProcessor&,int map,std::array<ControlMessage,32>& messages);
}
