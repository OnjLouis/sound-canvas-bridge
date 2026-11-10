#include "../Source/DawPresets.h"
#include <iostream>

static void require(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
static juce::File package(const juce::File& root,const char* name,const juce::String& content,
                          const juce::String& path="Programs/SC-8820/Piano/Piano 1.vstpreset",bool badHash=false,bool withDrums=false) {
    auto* files=new juce::DynamicObject;
    files->setProperty(path,badHash ? juce::String::repeatedString("0",64) : juce::SHA256(content.toRawUTF8(),content.getNumBytesAsUTF8()).toHexString());
    auto* manifest=new juce::DynamicObject;
    manifest->setProperty("product","Sound Canvas Bridge");manifest->setProperty("formatVersion",1);manifest->setProperty("files",juce::var(files));
    juce::ZipFile::Builder builder;
    auto addEntry=[&](const juce::String& value,const juce::String& entryPath) {
        auto input=std::make_unique<juce::MemoryInputStream>(value.toRawUTF8(),value.getNumBytesAsUTF8(),true);
#if JUCE_MAJOR_VERSION >= 8
        builder.addEntry(std::move(input),6,entryPath,juce::Time{});
#else
        builder.addEntry(input.release(),6,entryPath,juce::Time{});
#endif
    };
    addEntry(content,path);
    if(withDrums) {
        const juce::String drumPath="Drums/SC-8820/Standard 1.vstpreset",drumContent=content+" drums";
        files->setProperty(drumPath,juce::SHA256(drumContent.toRawUTF8(),drumContent.getNumBytesAsUTF8()).toHexString());
        addEntry(drumContent,drumPath);
    }
    const auto json=juce::JSON::toString(juce::var(manifest));
    addEntry(json,"manifest.json");
    const auto file=root.getChildFile(name);juce::FileOutputStream output(file);
    require(output.openedOk() && builder.writeToStream(output,nullptr),"Cannot write fixture archive");return file;
}
int main(int argc,char** argv) {
    if((argc!=2 && argc!=3) || !juce::File::isAbsolutePath(argv[1])) return 2;
    const juce::File root{juce::String(argv[1])};
    if(root.exists()) { std::cerr<<"FAIL test folder must be new\n";return 1; }
    try {
        require(root.createDirectory().wasOk(),"Cannot create isolated fixture");
        const auto first=package(root,"first.zip","first"),second=package(root,"second.zip","second");
        const auto target=root.getChildFile("installed/Factory"),preset=target.getChildFile("Programs/SC-8820/Piano/Piano 1.vstpreset");
        require(scbridge::DawPresets::install(first,target).wasOk() && preset.loadFileAsString()=="first","Initial installation failed");
#ifdef __APPLE__
        target.getChildFile(".DS_Store").replaceWithText("Finder view settings");
        require(chflags(target.getFullPathName().toRawUTF8(),UF_HIDDEN)==0 &&
                chflags(preset.getFullPathName().toRawUTF8(),UF_HIDDEN|UF_NODUMP)==0,"Cannot create hidden-flag regression fixture");
#endif
        const auto before=preset.getLastModificationTime();
        require(scbridge::DawPresets::install(first,target).wasOk() && preset.getLastModificationTime()==before,"Repeat installation rewrote a preset");
#ifdef __APPLE__
        struct stat flags{};
        require(!target.isHidden() && !preset.isHidden() && lstat(preset.getFullPathName().toRawUTF8(),&flags)==0 && (flags.st_flags&UF_NODUMP),
                "Generated visibility repair must clear only UF_HIDDEN without changing other flags");
#endif
        for(int point:{1,2}) {
            require(scbridge::DawPresets::install(second,target,point).failed(),"Injected failure did not fail");
            require(preset.loadFileAsString()=="first","Rollback did not restore exact previous preset");
        }
        require(scbridge::DawPresets::install(second,target).wasOk() && preset.loadFileAsString()=="second","Update failed");
#ifdef __APPLE__
        require(target.getChildFile(".DS_Store").loadFileAsString()=="Finder view settings","Finder view settings lost during update");
#endif
        preset.replaceWithText("user edit");
        require(scbridge::DawPresets::install(first,target).failed() && preset.loadFileAsString()=="user edit","User-edited preset overwritten");
        const auto traversal=package(root,"traversal.zip","bad","../outside.vstpreset");
        require(scbridge::DawPresets::install(traversal,root.getChildFile("unsafe/Factory")).failed() && !root.getChildFile("unsafe/outside.vstpreset").exists(),"Traversal accepted");
        const auto bad=package(root,"bad.zip","bad","Programs/Piano.vstpreset",true);
        require(scbridge::DawPresets::install(bad,root.getChildFile("bad/Factory")).failed() && !root.getChildFile("bad/Factory").exists(),"Bad hash accepted");
        const auto unknown=root.getChildFile("unknown/Factory");unknown.createDirectory();unknown.getChildFile("Keep.txt").replaceWithText("keep");
        require(scbridge::DawPresets::install(first,unknown).failed() && unknown.getChildFile("Keep.txt").loadFileAsString()=="keep","Unknown preset folder overwritten");
        const auto canonical=root.getChildFile("canonical/Factory"),duplicate=root.getChildFile("duplicate/Factory");
        require(scbridge::DawPresets::install(first,canonical).wasOk() && scbridge::DawPresets::install(first,duplicate).wasOk(),"Cannot prepare duplicate-bank fixture");
        duplicate.getChildFile("Programs/SC-8820/Piano/Piano 1.vstpreset").replaceWithText("unique edit");
        require(scbridge::DawPresets::retireDuplicate(duplicate,canonical).failed() && duplicate.exists(),"Edited duplicate bank deleted");
        duplicate.getChildFile("Programs/SC-8820/Piano/Piano 1.vstpreset").replaceWithText("first");
        require(scbridge::DawPresets::retireDuplicate(duplicate,canonical).wasOk() && !duplicate.exists() &&
                canonical.getChildFile("Programs/SC-8820/Piano/Piano 1.vstpreset").loadFileAsString()=="first","Exact duplicate-bank retirement failed");
        require(scbridge::DawPresets::retireDuplicate(canonical,canonical).failed() && canonical.exists(),"Canonical bank could be retired as its own duplicate");
        const auto flatPackage=package(root,"flat.zip","flat","Programs/SC-8820/Piano/Piano 1.vstpreset",false,true);
        const auto flatUpdated=package(root,"flat-update.zip","updated","Programs/SC-8820/Piano/Piano 1.vstpreset",false,true);
        const auto flat=root.getChildFile("flat-root"),legacy=flat.getChildFile("Factory");
        require(scbridge::DawPresets::install(flatPackage,legacy).wasOk(),"Cannot prepare legacy Factory fixture");
        const auto user=flat.getChildFile("My Sounds/Keep.vstpreset");user.getParentDirectory().createDirectory();user.replaceWithText("user sound");
        for(int point:{1,2,3}) {
            require(scbridge::DawPresets::installFlat(flatPackage,flat,point).failed(),"Flat migration injected failure did not fail");
            require(legacy.getChildFile("Programs/SC-8820/Piano/Piano 1.vstpreset").loadFileAsString()=="flat" &&
                    !flat.getChildFile("Programs").exists() && !flat.getChildFile("Drums").exists(),"Flat migration rollback lost old bank or left partial groups");
        }
        require(scbridge::DawPresets::installFlat(flatPackage,flat).wasOk() && !legacy.exists() &&
                flat.getChildFile("Programs/SC-8820/Piano/Piano 1.vstpreset").loadFileAsString()=="flat" &&
                flat.getChildFile("Drums/SC-8820/Standard 1.vstpreset").loadFileAsString()=="flat drums", "Factory nesting was not removed");
        require(scbridge::DawPresets::installFlat(flatPackage,flat).wasOk() && user.loadFileAsString()=="user sound","Repeat flat installation changed user presets");
        for(int point:{1,2}) {
            require(scbridge::DawPresets::installFlat(flatUpdated,flat,point).failed() &&
                    flat.getChildFile("Programs/SC-8820/Piano/Piano 1.vstpreset").loadFileAsString()=="flat" &&
                    flat.getChildFile("Drums/SC-8820/Standard 1.vstpreset").loadFileAsString()=="flat drums","Flat update rollback did not restore both groups");
        }
        require(scbridge::DawPresets::installFlat(flatUpdated,flat).wasOk() && user.loadFileAsString()=="user sound","Flat update lost user presets");
        const auto collision=root.getChildFile("flat-collision");collision.getChildFile("Drums").createDirectory();
        collision.getChildFile("Drums/Custom.vstpreset").replaceWithText("custom");
        require(scbridge::DawPresets::installFlat(flatPackage,collision).failed() && !collision.getChildFile("Programs").exists() &&
                collision.getChildFile("Drums/Custom.vstpreset").loadFileAsString()=="custom","Custom group collision was not protected");
        const auto edited=root.getChildFile("flat-edited-legacy");
        require(scbridge::DawPresets::install(flatPackage,edited.getChildFile("Factory")).wasOk(),"Cannot prepare edited legacy bank");
        edited.getChildFile("Factory/Drums/SC-8820/Standard 1.vstpreset").replaceWithText("edited kit");
        require(scbridge::DawPresets::installFlat(flatPackage,edited).failed() && !edited.getChildFile("Programs").exists() &&
                edited.getChildFile("Factory/Drums/SC-8820/Standard 1.vstpreset").loadFileAsString()=="edited kit","Edited legacy bank was not preserved");
        const auto real=root.getChildFile("real-presets"),link=root.getChildFile("known-presets-link");
        real.createDirectory();std::error_code linkError;
        std::filesystem::create_directory_symlink(std::filesystem::u8path(real.getFullPathName().toStdString()),
            std::filesystem::u8path(link.getFullPathName().toStdString()),linkError);
        if(!linkError) {
            const auto resolved=scbridge::DawPresets::resolveKnownFolder(link.getChildFile("Factory"));
            require(resolved==real.getChildFile("Factory"),"Known native-folder link was not resolved");
            require(scbridge::DawPresets::install(first,resolved).wasOk(),"Resolved known native-folder install failed");
            require(scbridge::DawPresets::install(second,link.getChildFile("Factory")).failed(),"Unresolved arbitrary link bypassed the path guard");
            require(std::filesystem::remove(std::filesystem::u8path(link.getFullPathName().toStdString()),linkError),"Cannot remove fixture link");
        } else std::cout<<"SKIP symbolic-link fixture: OS did not permit creating a directory link\n";
        require(root.findChildFiles(juce::File::findDirectories,true,"Factory-*").isEmpty(),"Staging/recovery directories left behind");
        if(argc==3) {
            const juce::File archive{juce::String(argv[2])};
            const auto full=root.getChildFile("real-package/Factory");
            const auto result=scbridge::DawPresets::install(archive,full);
            if(result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString());
            for(const auto& file:full.findChildFiles(juce::File::findFilesAndDirectories,true))
                if(!file.getFileName().startsWithChar('.')) require(!file.isHidden(),"Generated native preset is hidden");
            require(scbridge::DawPresets::install(archive,full).wasOk(),"Real package repeat installation failed");
            const auto migrated=scbridge::DawPresets::installFlat(archive,full.getParentDirectory());
            if(migrated.failed()) throw std::runtime_error(migrated.getErrorMessage().toStdString());
            require(!full.exists() && full.getParentDirectory().getChildFile("Programs").isDirectory() &&
                    full.getParentDirectory().getChildFile("Drums").isDirectory(),"Real bank retained Factory nesting");
            require(scbridge::DawPresets::installFlat(archive,full.getParentDirectory()).wasOk(),"Real flat package repeat failed");
            juce::ZipFile zip(archive);
            if(zip.getIndexOfFileName("Programs/SC-8820/Piano/Piano 1.aupreset")>=0) {
                const auto logic=root.getChildFile("logic/Factory");
                require(scbridge::DawPresets::install(archive,logic,0,".aupreset").wasOk(),"AU-only group installation failed");
                require(logic.findChildFiles(juce::File::findFiles,true,"*.aupreset").size()==2958 &&
                        logic.findChildFiles(juce::File::findFiles,true,"*.vstpreset").isEmpty(),"Logic received the wrong native formats");
                require(scbridge::DawPresets::install(archive,logic,0,".aupreset").wasOk(),"AU-only repeat install failed");
            }
        }
        require(root.deleteRecursively(),"Cannot remove completed fixtures");
        std::cout<<"PASS native preset install/repeat/update/rollback, edited and unknown data protection, traversal/hash refusal and cleanup\n";
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
