#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Pipeline.h"
#include <numeric>

namespace scbridge {
constexpr int minimumLatency = 32;
constexpr int defaultLatency = 128;
constexpr int maximumLatency = 2048;
constexpr int latencyStep = 32;

inline int normaliseLatency(int value) {
    return juce::jlimit(minimumLatency, maximumLatency,
                        ((value + latencyStep / 2) / latencyStep) * latencyStep);
}

inline int effectiveLatency(int requested, int) {
    // The bounded handoff can complete a quantum within the current callback.
    // Host block changes therefore must not change musical delay or insert
    // timeline padding when Logic moves a track into buffered processing.
    return normaliseLatency(requested);
}

inline juce::File performanceFile() {
#ifdef SOUNDCANVAS_TEST_PROFILE
    return juce::File(SOUNDCANVAS_TEST_PROFILE);
#else
    const auto profile=juce::SystemStats::getEnvironmentVariable("SCBRIDGE_PROFILE_DIR",{});
    if(profile.isNotEmpty() && juce::File::isAbsolutePath(profile))
        return juce::File(profile).getChildFile("Settings.xml");
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if defined(SCBRIDGE_WINDOWS7)
        .getChildFile("OnjMusic/SoundCanvasBridge-Windows7/Settings.xml");
#elif defined(_WIN32)
        .getChildFile("OnjMusic/SoundCanvasBridge/Settings.xml");
#else
        .getChildFile("Application Support/OnjMusic/SoundCanvasBridge/Settings.xml");
#endif
#endif
}

class PerformancePreferences {
public:
    explicit PerformancePreferences(juce::File file) : location(std::move(file)),
        lock("OnjSoundCanvasPrefs-" + juce::String(location.getFullPathName().hashCode64())),
        properties(location, options()) {}
    int getLatency() const { return normaliseLatency(properties.getIntValue("latencySamples", defaultLatency)); }
    void reload() { properties.reload(); }
    bool setLatency(int samples) {
        if (!lock.enter(200)) return false;
        struct Unlock { juce::InterProcessLock& value; ~Unlock() { value.exit(); } } guard{lock};
        properties.reload();
        properties.setValue("latencySamples", normaliseLatency(samples));
        return properties.saveIfNeeded();
    }
    juce::String notification() const {
        return "me.onj.soundcanvas.performance." + juce::String(location.getFullPathName().hashCode64());
    }
private:
    static juce::PropertiesFile::Options options() {
        juce::PropertiesFile::Options result;
        result.millisecondsBeforeSaving = -1;
        result.storageFormat = juce::PropertiesFile::storeAsXML;
        return result;
    }
    juce::File location;
    juce::InterProcessLock lock;
    juce::PropertiesFile properties;
};
}
