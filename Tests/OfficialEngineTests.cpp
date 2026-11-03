#include "../Source/OfficialEngine.h"
#include <iostream>
#include <stdexcept>

static void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    const juce::File root(argv[1]);if(root.exists()) return 3;
    try {
        require(root.createDirectory().wasOk(),"Create isolated fixture");
        const auto source=root.getChildFile("source.dll"),target=root.getChildFile("engines/hash/SCCore.dll");
        require(source.replaceWithText("engine fixture"),"Create source");
        const auto hash=juce::SHA256(source).toHexString();
        require(scbridge::importVerifiedEngine(source,target,scbridge::officialCoreHash).failed(),"Reject unverified engine");
        require(!target.exists(),"Failure must not install anything");
        require(scbridge::importVerifiedEngine(source,target,hash).wasOk(),"Install verified fixture");
        require(scbridge::engineHashMatches(target,hash),"Verify committed fixture");
        require(scbridge::importVerifiedEngine(source,target,hash).wasOk(),"Repeat is idempotent");
        require(target.replaceWithText("retain unrelated file"),"Create occupied destination");
        require(scbridge::importVerifiedEngine(source,target,hash).failed(),"Do not replace another file");
        require(target.loadFileAsString()=="retain unrelated file","Occupied destination retained");
        const auto outside=root.getChildFile("outside"),redirect=root.getChildFile("redirect");outside.createDirectory();
#ifdef _WIN32
        juce::ChildProcess junction;
        require(junction.start({juce::File::getSpecialLocation(juce::File::windowsSystemDirectory).getChildFile("cmd.exe").getFullPathName(),
                "/c","mklink","/J",redirect.getFullPathName(),outside.getFullPathName()}) &&
                junction.waitForProcessToFinish(5000) && junction.getExitCode()==0,"Create isolated redirected fixture");
#else
        require(::symlink(outside.getFullPathName().toRawUTF8(),redirect.getFullPathName().toRawUTF8())==0,"Create redirected fixture");
#endif
        require(scbridge::importVerifiedEngine(source,redirect.getChildFile("SCCore.dll"),hash).failed(),"Reject redirected parent");
        require(!outside.getChildFile("SCCore.dll").exists(),"Redirect target untouched");
        require(redirect.deleteFile(),"Remove fixture link without traversing it");
        require(root.deleteRecursively(),"Clean owned fixture");
        std::cout<<"PASS verified import, wrong-hash refusal, no-replace commit, idempotency and redirected-path safety\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
