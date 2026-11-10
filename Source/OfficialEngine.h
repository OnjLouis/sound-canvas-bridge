#pragma once
#include <juce_cryptography/juce_cryptography.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace scbridge {
inline constexpr const char* officialEngineUrl="https://cdn.roland.com/assets/media/zip/scva_win_trial.zip";
inline constexpr const char* officialEngineLicence="https://in.roland.com/support/by_product/sound_canvas_va/updates_drivers/351454";
inline constexpr const char* officialArchiveHash="2753e1a5699a577aaf468e1fa9b6a293687fa29292bca9258c0fc83fb258680d";
inline constexpr const char* officialInstallerHash="6dcf35d208903eab9e50f8b492d5899e66dd0b7510e7c2e31b419531d7c9f67d";
inline constexpr int64_t officialArchiveBytes=96055960,officialInstallerBytes=97012512;
#ifdef _WIN64
inline constexpr const char* officialCoreHash="e95e7f8f97667c247cccb8b15735cba949a07e92673a6a5411116d18e2268c47";
inline constexpr int64_t officialCoreBytes=27440128;
inline constexpr const char* officialCoreGroup="SoundCanvas64";
inline constexpr const char* officialRuntimeHash="1d1a1ae540ba132f998d60d3622f0297b6e86ae399332c3b47462d7c0f560a36";
inline constexpr int64_t officialRuntimeBytes=829264;
#else
inline constexpr const char* officialCoreHash="8ddb29d5e1c20ba5a262084e96fbc4f4de8c70db988873b80a1a6bd3ee244f29";
inline constexpr int64_t officialCoreBytes=27472384;
inline constexpr const char* officialCoreGroup="SoundCanvas32";
inline constexpr const char* officialRuntimeHash="60c06e0fa4449314da3a0a87c1a9d9577df99226f943637e06f61188e5862efa";
inline constexpr int64_t officialRuntimeBytes=773968;
#endif
struct OfficialPayload { const char* name; int64_t offset,size; const char* sha256; };
#ifdef _WIN64
inline constexpr const char* officialRedistributableHash="c6cd2d3f0b11dc2a604ffdc4dd97861a83b77e21709ba71b962a47759c93f4c8";
inline constexpr int64_t officialRedistributableBytes=5673816;
inline constexpr OfficialPayload officialRuntimeCab{"runtime-outer.cab",41984,5625404,"07ba4fd3a4cc15108584bf2dc2fe1fafabf26196ce4128b6399c3729d1f60b10"};
inline constexpr const char* officialRuntimeInnerHash="387e94a26165e4e5f035d89f9c6589a8a9d223978abbcc728b4c45c0115267a6";
inline constexpr int64_t officialRuntimeInnerBytes=4877975;
#else
inline constexpr const char* officialRedistributableHash="66b797b3b4f99488f53c2b676610dfe9868984c779536891a8d8f73ee214bc4b";
inline constexpr int64_t officialRedistributableBytes=4995416;
inline constexpr OfficialPayload officialRuntimeCab{"runtime-outer.cab",41984,4947186,"15caac43e5c45314987f148cce7249befdebbc5fe2fbb4d744da61aab7539b7a"};
inline constexpr const char* officialRuntimeInnerHash="71c0acc75eecdf39051819dc7c26503583f6be6c43ab2c320853de15bece9978";
inline constexpr int64_t officialRuntimeInnerBytes=4224705;
#endif
// Byte ranges belong only to the exact signed installer identified above.
// A changed archive is rejected before any range is read or cabinet is opened.
inline constexpr OfficialPayload officialPayloads[]{
    {"data1.cab",844030,779369,"2c8e250b4b62d9cb3a2a091ee4121b1d92f11519ce79b69283476c1d8b8b2786"},
    {"data1.hdr",1623479,23645,"fd430b785f501129d79a63a4eb1760432ddbc3eeaad67d793f35baaa66b8cbda"},
    {"data2.cab",1647210,93727672,"ff83ead8ca413e1a29ee447cbd9a003df67608dc42c2be206e5036701720c96d"}};
inline bool engineHashMatches(const juce::File& file,const juce::String& expected) {
    return file.existsAsFile() && !file.isSymbolicLink() && juce::SHA256(file).toHexString()==expected;
}
inline bool enginePathIsRedirected(juce::File file) {
    for(;;) {
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(file.getFullPathName().toWideCharPointer());
        if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
        if(file.isSymbolicLink()) return true;
        const auto parent=file.getParentDirectory();if(parent==file) return false;file=parent;
    }
}
inline juce::Result importVerifiedEngine(const juce::File& source,const juce::File& target,const juce::String& expected) {
    if(!engineHashMatches(source,expected)) return juce::Result::fail("The extracted engine failed verification.");
    if(enginePathIsRedirected(target)) return juce::Result::fail("The engine destination contains a symbolic link. Use Browse instead.");
    juce::InterProcessLock lock("SoundCanvasEngineImport-"+juce::String(target.getFullPathName().hashCode64()));
    if(!lock.enter(1000)) return juce::Result::fail("Another engine import is running. Try again shortly.");
    struct Unlock { juce::InterProcessLock& lock; ~Unlock(){lock.exit();} } unlock{lock};
    if(target.exists()) return engineHashMatches(target,expected) ? juce::Result::ok() :
        juce::Result::fail("A different file already occupies the engine destination. It has not been replaced.");
    auto result=target.getParentDirectory().createDirectory();if(result.failed()) return result;
    juce::TemporaryFile temporary(target);
    if(!source.copyFileTo(temporary.getFile()) || !engineHashMatches(temporary.getFile(),expected))
        return juce::Result::fail("Could not copy and verify the downloaded engine.");
    // Commit without replacing a concurrently created destination.
#ifdef _WIN32
    if(!MoveFileExW(temporary.getFile().getFullPathName().toWideCharPointer(),target.getFullPathName().toWideCharPointer(),MOVEFILE_WRITE_THROUGH))
#else
    if(::link(temporary.getFile().getFullPathName().toRawUTF8(),target.getFullPathName().toRawUTF8())!=0)
#endif
        return juce::Result::fail("Could not install the engine without replacing an existing file.");
    return engineHashMatches(target,expected) ? juce::Result::ok() : juce::Result::fail("The installed engine failed verification.");
}
}
