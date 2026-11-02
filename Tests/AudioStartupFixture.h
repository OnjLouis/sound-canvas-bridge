#pragma once
#include "../Source/StandaloneAudio.h"
#include <vector>

namespace audioStartupFixture {
class DeviceType final : public juce::AudioIODeviceType {
public:
    DeviceType():AudioIODeviceType("Fixture") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool input) const override { return input ? juce::StringArray{} : juce::StringArray{"Chosen output","System default"}; }
    int getDefaultDeviceIndex(bool) const override { return 1; }
    int getIndexOfDevice(juce::AudioIODevice*,bool) const override { return -1; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String& output,const juce::String&) override {
        attempts.push_back(output);return nullptr;
    }
    std::vector<juce::String> attempts;
};
class Manager final : public juce::AudioDeviceManager {
public:
    DeviceType* fixture=nullptr;
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override {
        fixture=new DeviceType();types.add(fixture);
    }
};
template<class Check> void run(Check check) {
    Manager saved;
    juce::XmlElement xml("DEVICESETUP");xml.setAttribute("deviceType","Fixture");
    xml.setAttribute("audioOutputDeviceName","Chosen output");xml.setAttribute("audioInputDeviceName","");
    xml.setAttribute("audioDeviceRate",48000);xml.setAttribute("audioDeviceBufferSize",32);
    xml.setAttribute("audioDeviceInChans","0");
    const auto before=xml.toString();
    const auto error=scbridge::initialiseStandaloneAudio(saved,&xml);
    check(error.isNotEmpty(),"A saved device failure must be reported");
    check(saved.fixture->attempts==std::vector<juce::String>{"Chosen output"},"A failed saved output must never open the system default");
    check(xml.toString()==before,"Audio recovery must not rewrite the saved preferences");
    Manager fresh;scbridge::initialiseStandaloneAudio(fresh,nullptr);
    check(fresh.fixture->attempts==std::vector<juce::String>{"System default"},"First-run setup must still try the default device");
}
}
