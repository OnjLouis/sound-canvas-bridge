#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#ifndef _WIN32
#include <CoreFoundation/CoreFoundation.h>
#endif
#include "Performance.h"

namespace scbridge {
class EngineLocation {
public:
    explicit EngineLocation(juce::File config=performanceFile().getSiblingFile("Engine.xml"))
        :file(std::move(config)),lock("OnjSoundCanvasEngine-"+juce::String(file.getFullPathName().hashCode64())),properties(file,options()) {}
    static juce::File systemBundle() {
#ifdef _WIN32
        return juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
            .getChildFile("Roland/SOUND Canvas VA/SCCore.dll");
#else
        return juce::File("/Library/Audio/Plug-Ins/VST/SOUND Canvas VA.vst");
#endif
    }
    juce::File bundle() const {
        const auto saved=properties.getValue("bundlePath");
        if(saved.isNotEmpty()) return juce::File(saved);
        juce::String error;
        if(validate(systemBundle(),error)) return systemBundle();
#ifdef _WIN32
        const auto common=juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
        for(const auto& relative:{"VstPlugins/Roland/SOUND Canvas VA/SCCore.dll","Roland Cloud/SOUND Canvas VA/SCCore.dll"}) {
            const auto candidate=common.getChildFile(relative);
            if(validate(candidate,error)) return candidate;
        }
        return systemBundle();
#else
        const auto user=juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Plug-Ins/VST/SOUND Canvas VA.vst");
        return validate(user,error) ? user : systemBundle();
#endif
    }
    bool available() const { juce::String error;return validate(bundle(),error); }
    juce::File core() const {
        const auto selected=bundle();juce::String error;
        if(!validate(selected,error)) return {};
#ifdef _WIN32
        return selected;
#else
        return selected.getChildFile("Contents/Resources/SCCore00.dylib");
#endif
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
#ifdef _WIN32
        error="Choose the original 64-bit Sound Canvas VA SCCore.dll from your own installation.";
        if(!selected.existsAsFile() || selected.getFileName().compareIgnoreCase("SCCore.dll") ||
           selected.getSize()<4096 || selected.getSize()>64*1024*1024) return false;
        juce::FileInputStream stream(selected);
        if(!stream.openedOk() || stream.readShort()!=0x5a4d || !stream.setPosition(0x3c)) return false;
        const auto header=static_cast<uint32_t>(stream.readInt());
        if(header>static_cast<uint64_t>(selected.getSize()-26) || !stream.setPosition(header) ||
           stream.readInt()!=0x00004550 || static_cast<uint16_t>(stream.readShort())!=0x8664 ||
           !stream.setPosition(header+24) || static_cast<uint16_t>(stream.readShort())!=0x20b) return false;
        error.clear();return true;
#else
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
#endif
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
