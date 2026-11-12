#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <optional>

namespace scbridge {
inline constexpr auto portableReleaseApi="https://api.github.com/repos/OnjLouis/sound-canvas-bridge/releases?per_page=100";
inline constexpr auto portableReleasePage="https://github.com/OnjLouis/sound-canvas-bridge/releases/tag/windows7-v";
using PortableVersion=std::array<int,3>;

inline std::optional<PortableVersion> portableVersion(const juce::String& text) {
    const auto fields=juce::StringArray::fromTokens(text,".",{});
    if(fields.size()!=3) return {};
    PortableVersion result{};
    for(int i=0;i<3;++i) {
        const auto& field=fields[i];
        if(field.isEmpty() || field.length()>6 || !field.containsOnly("0123456789") ||
           (field.length()>1 && field.startsWithChar('0'))) return {};
        result[static_cast<size_t>(i)]=field.getIntValue();
    }
    return result;
}

inline juce::String newerPortableVersion(const juce::var& data,const juce::String& installed,
                                         const juce::String& architecture) {
    const auto* releases=data.getArray();
    const auto current=portableVersion(installed);
    if(!releases || !current || (architecture!="x86" && architecture!="x64")) return {};
    auto newest=*current;
    juce::String selected;
    constexpr auto prefix="windows7-v";
    for(const auto& release:*releases) {
        const auto draft=release.getProperty("draft",{}),preview=release.getProperty("prerelease",{});
        if(!draft.isBool() || bool(draft) || !preview.isBool() || bool(preview)) continue;
        const auto tag=release.getProperty("tag_name",{}).toString();
        if(!tag.startsWith(prefix)) continue;
        const auto version=tag.fromFirstOccurrenceOf(prefix,false,false);
        const auto parsed=portableVersion(version);
        if(!parsed || *parsed<=newest) continue;
        const auto assetList=release.getProperty("assets",{});
        const auto* assets=assetList.getArray();
        if(!assets) continue;
        const auto expected="SoundCanvasBridge-"+version+"-windows7-"+architecture+".zip";
        bool matching=false,checksum=false;
        for(const auto& asset:*assets) {
            const auto name=asset.getProperty("name",{}).toString();
            matching|=name==expected;
            checksum|=name=="SHA256SUMS.txt";
        }
        if(matching && checksum) { newest=*parsed;selected=version; }
    }
    return selected;
}
}
