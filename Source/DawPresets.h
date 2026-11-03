#pragma once
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <set>
#include <filesystem>
#include <array>
#ifdef __APPLE__
#include <sys/stat.h>
#endif

namespace scbridge {
class DawPresets {
    static constexpr int maximumEntries=7000;
    static constexpr juce::int64 maximumBytes=64*1024*1024;
    static constexpr const char* receiptName=".scbridge-presets.json";
    static void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
    static void plain(const juce::File& file) {
        for(auto item=file;item!=item.getParentDirectory();item=item.getParentDirectory())
            require(!item.isSymbolicLink(),"A preset installation path is redirected.");
    }
    static bool safeName(const juce::String& path) {
        const auto parts=juce::StringArray::fromTokens(path,"/",{});
        return path.length()<220 && !path.containsAnyOf("\\:") && !path.startsWithChar('/') &&
            !parts.contains("..") && !parts.contains(".") && !parts.contains("") &&
            (path.endsWith(".aupreset") || path.endsWith(".vstpreset"));
    }
    static juce::String hash(const juce::File& file) {
        auto input=file.createInputStream();require(input!=nullptr,"Cannot read a preset file.");
        return juce::SHA256(*input).toHexString();
    }
    static juce::var readReceipt(const juce::File& folder) {
        const auto file=folder.getChildFile(receiptName);
        if(!file.existsAsFile() || file.getSize()>2*1024*1024) return {};
        return juce::JSON::parse(file);
    }
    static bool finderMetadata(const juce::File& file) {
#ifdef __APPLE__
        return file.getFileName()==".DS_Store" && file.getSize()<=1024*1024;
#else
        juce::ignoreUnused(file);return false;
#endif
    }
    static void preserveFinderMetadata(const juce::File& old,const juce::File& stage) {
        int count=0;juce::int64 bytes=0;
        for(const auto& file:old.findChildFiles(juce::File::findFiles,true,".DS_Store")) {
            plain(file);require(finderMetadata(file) && ++count<=128 && (bytes+=file.getSize())<=2*1024*1024,"Finder metadata is unexpectedly large.");
            const auto destination=stage.getChildFile(file.getRelativePathFrom(old));
            require(destination.isAChildOf(stage) && destination.getParentDirectory().createDirectory().wasOk() &&
                    file.copyFileTo(destination) && hash(file)==hash(destination),"Cannot preserve Finder folder settings.");
        }
    }
    static void verifyTree(const juce::File& root,const juce::NamedValueSet& files,bool receipt) {
        std::set<juce::String> expected;
        for(const auto& entry:files) {
            const auto path=entry.name.toString();require(safeName(path),"Invalid preset inventory.");
            const auto file=root.getChildFile(path);plain(file);
            require(file.isAChildOf(root) && file.existsAsFile() && file.getSize()<=262144 && hash(file)==entry.value.toString(),
                    "A preset file differs from its inventory. Existing files were retained.");
            expected.insert(file.getFullPathName());
        }
        if(receipt) expected.insert(root.getChildFile(receiptName).getFullPathName());
        for(const auto& file:root.findChildFiles(juce::File::findFilesAndDirectories,true)) {
            plain(file);
            if(file.existsAsFile() && !finderMetadata(file)) require(expected.erase(file.getFullPathName())==1,"An additional preset file was retained.");
        }
        require(expected.empty(),"A preset file is missing.");
    }
    static void showGeneratedTree(const juce::File& root,const juce::NamedValueSet& files) {
#ifdef __APPLE__
        std::set<juce::String> owned{root.getFullPathName()};
        for(const auto& entry:files) {
            auto file=root.getChildFile(entry.name.toString());
            for(;file==root || file.isAChildOf(root);file=file.getParentDirectory()) owned.insert(file.getFullPathName());
        }
        for(const auto& path:owned) {
            struct stat info{};
            require(lstat(path.toRawUTF8(),&info)==0 && !S_ISLNK(info.st_mode),"Cannot inspect generated preset visibility.");
            if(info.st_flags&UF_HIDDEN)
                require(chflags(path.toRawUTF8(),info.st_flags&~UF_HIDDEN)==0,"Cannot make generated presets visible.");
        }
#else
        juce::ignoreUnused(root,files);
#endif
    }
public:
    static juce::Result installFlat(const juce::File& archive,const juce::File& root,int failAfter=0,const juce::String& extension={}) {
        const std::array<juce::String,2> groups{{"Programs","Drums"}};
        std::array<bool,2> oldMoved{},newMoved{};
        juce::File stage;
        const auto legacy=root.getChildFile("Factory");
        bool legacyMoved=false;
        try {
            plain(root);plain(archive);
            require(archive.existsAsFile() && archive.getSize()<maximumBytes,"The bundled DAW preset archive is missing or too large.");
            const auto digest=hash(archive);
            bool current=!legacy.exists();
            for(const auto& group:groups) {
                const auto target=root.getChildFile(group);
                if(!target.exists()) { current=false;continue; }
                const auto receipt=readReceipt(target);
                const auto* files=receipt["files"].getDynamicObject();
                require(receipt["product"].toString()=="Sound Canvas Bridge" && int(receipt["formatVersion"])==1 &&
                        receipt["group"].toString()==group && files,"A Programs or Drums folder is not owned by this installer. It was retained.");
                current=current && receipt["archive"].toString()==digest && receipt["extension"].toString()==extension && int(receipt["visibilityVersion"])==2;
            }
            if(current && !failAfter) {
                for(const auto& group:groups) {
                    const auto receipt=readReceipt(root.getChildFile(group));
                    showGeneratedTree(root.getChildFile(group),receipt["files"].getDynamicObject()->getProperties());
                }
                showGeneratedTree(root,{});
                return juce::Result::ok();
            }
            for(const auto& group:groups) {
                const auto target=root.getChildFile(group);
                if(target.exists()) verifyTree(target,readReceipt(target)["files"].getDynamicObject()->getProperties(),true);
            }
            require(root.getParentDirectory().createDirectory().wasOk(),"Cannot create DAW preset location.");
            stage=root.getSiblingFile(root.getFileName()+"-incoming-"+juce::Uuid().toString());
            require(stage.createDirectory().wasOk(),"Cannot stage native preset groups.");
            for(const auto& group:groups) {
                const auto result=install(archive,stage.getChildFile(group),0,extension,group);
                require(result.wasOk(),result.getErrorMessage().toRawUTF8());
                if(root.getChildFile(group).exists()) preserveFinderMetadata(root.getChildFile(group),stage.getChildFile(group));
            }
            if(legacy.exists()) verifyLegacy(legacy,stage);
            require(root.createDirectory().wasOk(),"Cannot create native preset folder.");
            for(size_t i=0;i<groups.size();++i) {
                const auto target=root.getChildFile(groups[i]);
                if(target.exists()) {
                    require(target.moveFileTo(stage.getChildFile("Previous"+groups[i])),"Cannot retain previous native preset group.");oldMoved[i]=true;
                }
                require(stage.getChildFile(groups[i]).moveFileTo(target),"Cannot install native preset group.");newMoved[i]=true;
                const auto receipt=readReceipt(target);
                verifyTree(target,receipt["files"].getDynamicObject()->getProperties(),true);
                showGeneratedTree(target,receipt["files"].getDynamicObject()->getProperties());
                require(failAfter!=static_cast<int>(i+1),"Injected flat preset installation failure.");
            }
            if(legacy.exists()) {
                require(legacy.moveFileTo(stage.getChildFile("PreviousFactory")),"Cannot retire the verified Factory folder.");legacyMoved=true;
            }
            require(failAfter!=3,"Injected flat preset migration failure.");
            showGeneratedTree(root,{});
            if(!stage.deleteRecursively()) juce::Logger::writeToLog("Native presets installed; verified recovery folder retained: "+stage.getFullPathName());
            return juce::Result::ok();
        } catch(const std::exception& error) {
            bool restored=true;
            if(legacyMoved) restored=stage.getChildFile("PreviousFactory").moveFileTo(legacy) && restored;
            for(size_t n=groups.size();n>0;--n) {
                const auto i=n-1;const auto target=root.getChildFile(groups[i]);
                if(newMoved[i]) restored=target.moveFileTo(stage.getChildFile(groups[i])) && restored;
                if(oldMoved[i]) restored=stage.getChildFile("Previous"+groups[i]).moveFileTo(target) && restored;
            }
            if(!restored) return juce::Result::fail("Native preset migration failed; recovery files remain at "+stage.getFullPathName());
            if(stage.exists() && !stage.deleteRecursively()) juce::Logger::writeToLog("Native preset staging retained: "+stage.getFullPathName());
            return juce::Result::fail(error.what());
        }
    }
private:
    static void verifyLegacy(const juce::File& legacy,const juce::File& canonical) {
        plain(legacy);plain(canonical);
        const auto receipt=readReceipt(legacy);
        const auto* old=receipt["files"].getDynamicObject();
        require(old && receipt["product"].toString()=="Sound Canvas Bridge" && int(receipt["formatVersion"])==1,
                "The old Factory folder is not owned by this installer. It was retained.");
        juce::NamedValueSet combined;
        for(const auto* group:{"Programs","Drums"}) {
            const auto current=readReceipt(canonical.getChildFile(group));
            const auto* files=current["files"].getDynamicObject();
            require(files && current["group"].toString()==group && current["product"].toString()=="Sound Canvas Bridge","Native preset group inventory is missing.");
            verifyTree(canonical.getChildFile(group),files->getProperties(),true);
            for(const auto& file:files->getProperties()) combined.set(juce::Identifier(juce::String(group)+"/"+file.name.toString()),file.value);
        }
        require(combined.size()==old->getProperties().size(),"The old Factory bank contains different files. It was retained.");
        for(const auto& file:old->getProperties()) require(combined[file.name]==file.value,"The old Factory bank contains different sounds. It was retained.");
        verifyTree(legacy,old->getProperties(),true);
    }
public:
    static juce::Result retireLegacy(const juce::File& legacy,const juce::File& canonical) {
        if(!legacy.exists()) return juce::Result::ok();
        try {
            require(legacy!=canonical && !canonical.isAChildOf(legacy),"Preset banks overlap; no legacy bank removed.");
            verifyLegacy(legacy,canonical);
            require(legacy.deleteRecursively(),"The verified old Factory folder could not be removed.");
            return juce::Result::ok();
        } catch(const std::exception& error) { return juce::Result::fail(error.what()); }
    }
    static juce::File resolveKnownFolder(const juce::File& folder) {
        std::error_code error;
        const auto path=std::filesystem::weakly_canonical(std::filesystem::u8path(folder.getFullPathName().toStdString()),error);
        return error ? folder : juce::File(juce::String::fromUTF8(path.u8string().c_str()));
    }
    static juce::File defaultFolder() {
#ifdef _WIN32
        const auto folder=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("VST3 Presets/OnjMusic/Sound Canvas Bridge");
#else
        const auto folder=juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Library/Audio/Presets/OnjMusic/Sound Canvas Bridge");
#endif
        // Resolve only the OS-known preset location; payload and owned-tree
        // validation still reject links within the destination or archive.
        return resolveKnownFolder(folder);
    }
    static juce::Result installBundled(const juce::File& archive) {
#ifdef _WIN32
        const auto result=installFlat(archive,defaultFolder());
#else
        const auto result=installFlat(archive,defaultFolder(),0,".aupreset");
#endif
        if(result.failed()) return result;
#ifndef _WIN32
        const auto logic=juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Music/Audio Music Apps/Plug-In Settings");
        return retireLegacy(resolveKnownFolder(logic.getChildFile("Sound Canvas Bridge/Factory")),defaultFolder());
#endif
        return juce::Result::ok();
    }
    static juce::Result retireDuplicate(const juce::File& duplicate,const juce::File& canonical) {
        if(!duplicate.exists()) return juce::Result::ok();
        try {
            plain(duplicate);plain(canonical);
            require(duplicate!=canonical && !duplicate.isAChildOf(canonical) && !canonical.isAChildOf(duplicate),"Preset banks overlap; no duplicate removed.");
            const auto old=readReceipt(duplicate),current=readReceipt(canonical);
            const auto* oldFiles=old["files"].getDynamicObject();
            const auto* currentFiles=current["files"].getDynamicObject();
            require(old["product"].toString()=="Sound Canvas Bridge" && current["product"].toString()=="Sound Canvas Bridge" && oldFiles && currentFiles &&
                    oldFiles->getProperties().size()==currentFiles->getProperties().size(),"The other preset bank differs or is not owned by this installer. It was retained.");
            for(const auto& file:oldFiles->getProperties())
                require(currentFiles->getProperty(file.name)==file.value,"The other preset bank contains different sounds. It was retained.");
            verifyTree(canonical,currentFiles->getProperties(),true);
            verifyTree(duplicate,oldFiles->getProperties(),true);
            require(duplicate.deleteRecursively(),"Some duplicate presets could not be removed; the complete canonical bank remains intact.");
            return juce::Result::ok();
        } catch(const std::exception& error) { return juce::Result::fail(error.what()); }
    }
    static juce::Result install(const juce::File& archive,const juce::File& target,int failAfter=0,const juce::String& extension={},const juce::String& group={}) {
        juce::File stage,backup;
        bool oldMoved=false,newMoved=false;
        try {
            plain(target);plain(archive);
            require(archive.existsAsFile() && archive.getSize()<maximumBytes,"The bundled DAW preset archive is missing or too large.");
            const auto digest=hash(archive);
            const auto previousReceipt=readReceipt(target);
            if(auto previous=previousReceipt.getDynamicObject()) {
                if(previous->getProperty("archive").toString()==digest && previous->getProperty("extension").toString()==extension &&
                   previous->getProperty("group").toString()==group && int(previous->getProperty("visibilityVersion"))==2) {
                    if(target.isHidden()) {
                        const auto* files=previous->getProperty("files").getDynamicObject();
                        require(files!=nullptr,"Preset visibility inventory is missing.");
                        verifyTree(target,files->getProperties(),true);showGeneratedTree(target,files->getProperties());
                        previous->setProperty("visibilityVersion",2);
                        require(target.getChildFile(receiptName).replaceWithText(juce::JSON::toString(previousReceipt)),"Cannot save preset visibility receipt.");
                    }
                    return juce::Result::ok();
                }
            }
            juce::ZipFile zip(archive);
            require(zip.getNumEntries()>1 && zip.getNumEntries()<=maximumEntries,"Invalid DAW preset archive.");
            const int manifestIndex=zip.getIndexOfFileName("manifest.json");
            require(manifestIndex>=0 && zip.getEntry(manifestIndex)->uncompressedSize<1024*1024,"Preset inventory is missing or too large.");
            auto manifestStream=std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(manifestIndex));
            require(manifestStream!=nullptr,"Cannot read preset inventory.");
            const auto manifest=juce::JSON::parse(manifestStream->readEntireStreamAsString());
            require(manifest["product"].toString()=="Sound Canvas Bridge" && int(manifest["formatVersion"])==1, "Unexpected preset product or format.");
            const auto* entries=manifest["files"].getDynamicObject();
            require(entries && entries->getProperties().size()==zip.getNumEntries()-1,"Preset inventory count differs.");
            juce::DynamicObject::Ptr selected=new juce::DynamicObject;
            require(group.isEmpty() || group=="Programs" || group=="Drums","Invalid native preset group.");
            const auto prefix=group.isEmpty() ? juce::String{} : group+"/";
            for(const auto& entry:entries->getProperties()) {
                const auto path=entry.name.toString();
                if(path.startsWith(prefix) && (extension.isEmpty() || path.endsWith(extension))) selected->setProperty(path.substring(prefix.length()),entry.value);
            }
            require(!selected->getProperties().isEmpty(),"No matching native presets in the archive.");
            if(target.exists()) {
                const auto previous=readReceipt(target);
                const auto* old=previous["files"].getDynamicObject();
                require(old && previous["product"].toString()=="Sound Canvas Bridge" && int(previous["formatVersion"])==1,
                        "The preset folder already exists without an ownership receipt. It was retained.");
                verifyTree(target,old->getProperties(),true);
            }
            require(target.getParentDirectory().createDirectory().wasOk(),"Cannot create DAW preset location.");
            stage=target.getSiblingFile(target.getFileName()+"-incoming-"+juce::Uuid().toString());
            backup=target.getSiblingFile(target.getFileName()+"-previous-"+juce::Uuid().toString());
            require(stage.createDirectory().wasOk(),"Cannot stage DAW presets.");
            juce::int64 total=0;std::set<juce::String> seen;
            for(int i=0;i<zip.getNumEntries();++i) {
                if(i==manifestIndex) continue;
                const auto* entry=zip.getEntry(i);const auto path=entry->filename;
                total+=entry->uncompressedSize;
                require(safeName(path) && !entry->isSymbolicLink && entry->uncompressedSize>0 && entry->uncompressedSize<=262144 && total<=maximumBytes &&
                        seen.insert(path.toLowerCase()).second && entries->hasProperty(path),"Unsafe or unexpected DAW preset entry.");
                if(!path.startsWith(prefix) || !selected->hasProperty(path.substring(prefix.length()))) continue;
                const auto destination=stage.getChildFile(path.substring(prefix.length()));
                require(destination.isAChildOf(stage) && destination.getParentDirectory().createDirectory().wasOk(),"Cannot create preset staging path.");
                auto input=std::unique_ptr<juce::InputStream>(zip.createStreamForEntry(i));
                juce::FileOutputStream output(destination);
                require(input && output.openedOk() && output.writeFromInputStream(*input,entry->uncompressedSize)==entry->uncompressedSize,"Could not stage DAW presets.");
                output.flush();require(output.getStatus().wasOk(),"Could not flush staged preset.");
            }
            if(target.exists()) preserveFinderMetadata(target,stage);
            verifyTree(stage,selected->getProperties(),false);
            showGeneratedTree(stage,selected->getProperties());
            auto receipt=manifest.clone();
            receipt.getDynamicObject()->setProperty("files",juce::var(selected.get()));
            receipt.getDynamicObject()->setProperty("extension",extension);
            receipt.getDynamicObject()->setProperty("group",group);
            receipt.getDynamicObject()->setProperty("archive",digest);
            receipt.getDynamicObject()->setProperty("visibilityVersion",2);
            require(stage.getChildFile(receiptName).replaceWithText(juce::JSON::toString(receipt)),"Cannot write preset receipt.");
            if(target.exists()) { require(target.moveFileTo(backup),"Cannot retain the previous preset folder.");oldMoved=true; }
            require(failAfter!=1,"Injected preset installation failure.");
            require(stage.moveFileTo(target),"Cannot install staged DAW presets.");newMoved=true;
            verifyTree(target,selected->getProperties(),true);
            showGeneratedTree(target,selected->getProperties());
            require(failAfter!=2,"Injected preset verification failure.");
            if(oldMoved && !backup.deleteRecursively())
                juce::Logger::writeToLog("DAW presets installed; previous recovery folder retained: "+backup.getFullPathName());
            return juce::Result::ok();
        } catch(const std::exception& error) {
            if(newMoved && target.exists() && !target.deleteRecursively()) return juce::Result::fail("Preset installation failed; the recovery folder was retained.");
            if(oldMoved && !backup.moveFileTo(target)) return juce::Result::fail("Preset installation failed; the previous folder remains at "+backup.getFullPathName());
            if(stage.exists()) stage.deleteRecursively();
            return juce::Result::fail(error.what());
        }
    }
};
}
