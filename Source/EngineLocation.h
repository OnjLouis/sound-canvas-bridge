#pragma once
#include "Backend.h"
#include <juce_audio_utils/juce_audio_utils.h>
#ifndef _WIN32
#include <CoreFoundation/CoreFoundation.h>
#ifdef SCBRIDGE_TRANSLATED_ENGINE
#include <CommonCrypto/CommonDigest.h>
#endif
#endif
#include "Performance.h"
#include "CoreLayouts.h"

namespace scbridge {
class EngineLocation {
public:
    static const char* preferenceKey() {
#if defined(SCBRIDGE_TRANSLATED_ENGINE) && !defined(_WIN32)
        return "windows32CorePath";
#else
        return "bundlePath";
#endif
    }
    explicit EngineLocation(juce::File config=performanceFile().getSiblingFile("Engine.xml"))
        :file(std::move(config)),lock("OnjSoundCanvasEngine-"+juce::String(file.getFullPathName().hashCode64())),properties(file,options()) {}
    static juce::File systemBundle() {
#ifdef _WIN32
        return juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
            .getChildFile("Roland/SOUND Canvas VA/SCCore.dll");
#elif defined(SCBRIDGE_TRANSLATED_ENGINE)
        return juce::File("/Library/Application Support/OnjMusic/SoundCanvasBridge/Engine/SCCore.dll");
#else
        return juce::File("/Library/Audio/Plug-Ins/VST/SOUND Canvas VA.vst");
#endif
    }
    juce::File bundle() const {
        const auto saved=properties.getValue(preferenceKey());
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
#elif defined(SCBRIDGE_TRANSLATED_ENGINE)
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
#if defined(_WIN32) || defined(SCBRIDGE_TRANSLATED_ENGINE)
        return selected;
#else
        return selected.getChildFile("Contents/Resources/SCCore00.dylib");
#endif
    }
    bool save(const juce::File& selected,juce::String& error) {
        if(!validate(selected,error)) return false;
        if(!lock.enter(200)) { error="The engine setting is busy. Try again.";return false; }
        properties.reload();properties.setValue(preferenceKey(),selected.getFullPathName());
        const bool saved=properties.saveIfNeeded();lock.exit();
        if(!saved) error="Could not save the Sound Canvas VA location.";
        return saved;
    }
    static bool validate(const juce::File& selected,juce::String& error) {
#if defined(_WIN32) || defined(SCBRIDGE_TRANSLATED_ENGINE)
#ifdef _WIN64
        error="Choose the original 64-bit Sound Canvas VA SCCore.dll from your own installation.";
        constexpr uint16_t machine=0x8664,optionalMagic=0x20b;
#else
        error="Choose the 32-bit Sound Canvas VA SCCore.dll from your own Windows installation, not the 64-bit DLL or Mac VST.";
        constexpr uint16_t machine=0x014c,optionalMagic=0x10b;
#endif
        if(!selected.existsAsFile() || selected.getFileName().compareIgnoreCase("SCCore.dll") ||
           selected.getSize()<4096 || selected.getSize()>64*1024*1024) return false;
        juce::FileInputStream stream(selected);
        if(!stream.openedOk() || stream.readShort()!=0x5a4d || !stream.setPosition(0x3c)) return false;
        const auto header=static_cast<uint32_t>(stream.readInt());
        if(header>static_cast<uint64_t>(selected.getSize()-26) || !stream.setPosition(header) ||
           stream.readInt()!=0x00004550 || static_cast<uint16_t>(stream.readShort())!=machine ||
           !stream.setPosition(header+24) || static_cast<uint16_t>(stream.readShort())!=optionalMagic) return false;
#ifndef _WIN32
        juce::MemoryBlock bytes;
        if(!selected.loadFileAsData(bytes)) return false;
        unsigned char digest[CC_SHA256_DIGEST_LENGTH]{};
        CC_SHA256(bytes.getData(),static_cast<CC_LONG>(bytes.getSize()),digest);
        const auto actual=juce::String::toHexString(digest,sizeof(digest),0);
        if(!windowsCoreLayout(actual.toRawUTF8(),32)) {
            error="This 32-bit Sound Canvas engine version is not supported by the experimental backend.";return false;
        }
#endif
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
