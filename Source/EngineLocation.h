#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <CoreFoundation/CoreFoundation.h>
#include "Performance.h"

namespace scbridge {
class EngineLocation {
public:
    explicit EngineLocation(juce::File config=performanceFile().getSiblingFile("Engine.xml"))
        :file(std::move(config)),lock("OnjSoundCanvasEngine-"+juce::String(file.getFullPathName().hashCode64())),properties(file,options()) {}
    static juce::File systemBundle() { return juce::File("/Library/Audio/Plug-Ins/VST/SOUND Canvas VA.vst"); }
    juce::File bundle() const {
        const auto saved=properties.getValue("bundlePath");
        if(saved.isNotEmpty()) return juce::File(saved);
        juce::String error;
        if(validate(systemBundle(),error)) return systemBundle();
        const auto user=juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/VST/SOUND Canvas VA.vst");
        return validate(user,error) ? user : systemBundle();
    }
    bool available() const { juce::String error;return validate(bundle(),error); }
    juce::File core() const {
        const auto selected=bundle();juce::String error;
        return validate(selected,error) ? selected.getChildFile("Contents/Resources/SCCore00.dylib") : juce::File();
    }
    bool save(const juce::File& selected,juce::String& error) {
        if(!validate(selected,error)) return false;
        if(!lock.enter(200)) { error="The engine setting is busy. Try again.";return false; }
        properties.reload();properties.setValue("bundlePath",selected.getFullPathName());
        const bool saved=properties.saveIfNeeded();lock.exit();
        if(!saved) error="Could not save the Sound Canvas VA location.";
        return saved;
    }
    static bool validate(const juce::File& selected,juce::String& error) {
        error="Choose the original Sound Canvas VA VST bundle, including its companion resources.";
        if(!selected.isDirectory() || !selected.hasFileExtension("vst")) return false;
        const auto info=selected.getChildFile("Contents/Info.plist");
        if(info.getSize()<=0 || info.getSize()>1024*1024) return false;
        juce::MemoryBlock bytes;if(!info.loadFileAsData(bytes)) return false;
        const auto data=CFDataCreate(kCFAllocatorDefault,static_cast<const UInt8*>(bytes.getData()),static_cast<CFIndex>(bytes.getSize()));
        if(!data) return false;
        const auto plist=CFPropertyListCreateWithData(kCFAllocatorDefault,data,kCFPropertyListImmutable,nullptr,nullptr);CFRelease(data);
        bool identity=false;
        if(plist && CFGetTypeID(plist)==CFDictionaryGetTypeID()) {
            const auto identifier=CFDictionaryGetValue(static_cast<CFDictionaryRef>(plist),CFSTR("CFBundleIdentifier"));
            identity=identifier && CFEqual(identifier,CFSTR("com.roland.soundcanvas.vst"));
        }
        if(plist) CFRelease(plist);
        if(!identity) return false;
        for(const auto* name:{"SCCore00.dylib","SCCoreGby.dylib"}) {
            const auto resource=selected.getChildFile("Contents/Resources").getChildFile(name);
            if(!resource.existsAsFile() || resource.getSize()<=0) {
                error="The selected VST bundle is missing required Sound Canvas engine resources.";return false;
            }
        }
        error.clear();return true;
    }
private:
    static juce::PropertiesFile::Options options() {
        juce::PropertiesFile::Options value;value.millisecondsBeforeSaving=-1;value.storageFormat=juce::PropertiesFile::storeAsXML;return value;
    }
    juce::File file;
    juce::InterProcessLock lock;
    juce::PropertiesFile properties;
};
}
