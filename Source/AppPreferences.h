#pragma once
#include "Performance.h"

namespace scbridge {
enum class CheckFrequency { never=1,startup,hourly,daily };
constexpr int64_t hourMilliseconds=60LL*60*1000;
constexpr int64_t dayMilliseconds=24*hourMilliseconds;

inline bool updateCheckDue(CheckFrequency frequency,int64_t last,int64_t now,bool launching) {
    if(frequency==CheckFrequency::never) return false;
    if(frequency==CheckFrequency::startup) return launching;
    const auto interval=frequency==CheckFrequency::hourly ? hourMilliseconds : dayMilliseconds;
    return last<=0 || now<last || now-last>=interval;
}

class AppPreferences {
public:
    explicit AppPreferences(juce::File path=performanceFile().getSiblingFile("Application.xml"))
        :file(std::move(path)),properties(file,options()) {}
    CheckFrequency frequency() const {
        const auto value=properties.getIntValue("updateFrequency",4);
        return value>=1 && value<=4 ? static_cast<CheckFrequency>(value) : CheckFrequency::daily;
    }
    bool startHidden() const { return properties.getBoolValue("startHidden",false); }
    int64_t lastCheck() const { return properties.getValue("lastUpdateCheck","0").getLargeIntValue(); }
    bool setFrequency(CheckFrequency frequency) { return save("updateFrequency",static_cast<int>(frequency)); }
    bool setStartHidden(bool hidden) { return save("startHidden",hidden); }
    bool recordCheck(int64_t time) { return save("lastUpdateCheck",juce::String(time)); }
    static bool shouldHide(bool requested,bool statusAvailable,bool engineReady,bool audioReady) {
        return requested && statusAvailable && engineReady && audioReady;
    }
private:
    bool save(const juce::String& key,const juce::var& value) {
        const bool existed=properties.containsKey(key);const auto previous=properties.getValue(key);
        properties.setValue(key,value);
        if(properties.saveIfNeeded()) return true;
        if(existed) properties.setValue(key,previous);else properties.removeValue(key);
        return false;
    }
    static juce::PropertiesFile::Options options() {
        juce::PropertiesFile::Options result;result.millisecondsBeforeSaving=-1;
        result.storageFormat=juce::PropertiesFile::storeAsXML;return result;
    }
    juce::File file;
    juce::PropertiesFile properties;
};
}
