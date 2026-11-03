#pragma once
#include "../Source/EngineDownloadService.h"
#include <iostream>
#include <stdexcept>

namespace engineDownloadFixture {
inline int run() {
    auto require=[](bool ok,const char* text){if(!ok) throw std::runtime_error(text);};
    try {
        const auto isolated=juce::SystemStats::getEnvironmentVariable("SCBRIDGE_PROFILE_DIR",{});
        require(juce::File::isAbsolutePath(isolated),"Expected an isolated download profile");
        const auto root=juce::File(isolated);
        require(root.getFileName()=="engine-download-test-profile" && !root.exists(),"Expected a fresh download test profile");
        require(root.createDirectory().wasOk(),"Create isolated download profile");
        const auto setting=root.getChildFile("Engine.xml");
        require(setting.replaceWithText("<PROPERTIES><VALUE name=\"unchanged\" val=\"yes\"/></PROPERTIES>"),"Create sentinel selection");
        const auto original=setting.loadFileAsString();
        scbridge::EngineDownloadService download;
        for(bool cancel:{true,false}) {
            bool done=false,success=false;juce::String message;juce::File installed;
            download.start([&](juce::Result result,juce::File file){success=result.wasOk();message=result.getErrorMessage();installed=file;done=true;});
            if(cancel) download.cancel();
            const auto deadline=juce::Time::getMillisecondCounterHiRes()+190000;
            while(!done && juce::Time::getMillisecondCounterHiRes()<deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            require(done,"Download completion must be bounded");
            while(download.busy() && juce::Time::getMillisecondCounterHiRes()<deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
            require(!download.busy(),"Download worker must exit before retry");
            require(setting.loadFileAsString()==original,"Service must not replace the active selection");
            if(cancel) require(!success && message.contains("cancelled"),"Cancellation must preserve the active engine");
            else {
                if(!success) std::cerr<<message<<'\n';
                require(success && scbridge::engineHashMatches(installed,scbridge::officialCoreHash),"Native download must produce the verified platform DLL");
#ifdef _WIN32
                require(scbridge::engineHashMatches(installed.getSiblingFile("MSVCR100.dll"),scbridge::officialRuntimeHash),"Native Windows download must include its verified app-local runtime");
#else
                require(scbridge::engineHashMatches(installed.getSiblingFile("MSVCR100.dll"),"60c06e0fa4449314da3a0a87c1a9d9577df99226f943637e06f61188e5862efa"),"Native Mac download must include its verified external engine runtime");
#endif
            }
        }
        const auto cache=root.getChildFile("Download Cache");
        require(cache.findChildFiles(juce::File::findFilesAndDirectories,false).isEmpty(),"Extraction jobs must be cleaned");
        require(root.deleteRecursively(),"Remove only the completed isolated fixture");
        std::cout<<"PASS native service download, cancellation, retry, runtime verification, unchanged selection and cleanup\n";return 0;
    } catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
}
