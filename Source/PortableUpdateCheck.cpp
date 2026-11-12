#include "PortableUpdate.h"
#include <iostream>

namespace {
int reply(const char* status,const char* message,juce::String version={}) {
    auto result=std::make_unique<juce::DynamicObject>();
    result->setProperty("status",status);result->setProperty("message",message);
    if(version.isNotEmpty()) {
        auto release=std::make_unique<juce::DynamicObject>();
        release->setProperty("version",version);
        result->setProperty("release",juce::var(release.release()));
    }
    std::cout<<juce::JSON::toString(juce::var(result.release()),true).toRawUTF8();
    return 0;
}
}

int main(int argc,char** argv) {
    if(argc!=2 || !scbridge::portableVersion(argv[1])) return reply("error","Invalid installed version.");
    int status=0;
    auto input=juce::URL(scbridge::portableReleaseApi).createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(10000).withNumRedirectsToFollow(0).withStatusCode(&status)
            .withExtraHeaders("User-Agent: Sound-Canvas-Bridge\r\nAccept: application/vnd.github+json\r\n"));
    if(!input || status!=200)
        return reply("error","Could not check GitHub over HTTPS. You can check the releases page in a supported browser.");
    constexpr size_t maximumBytes=1024*1024;
    juce::MemoryOutputStream data;
    data.writeFromInputStream(*input,maximumBytes+1);
    juce::var releases;
    if(data.getDataSize()>maximumBytes || juce::JSON::parse(data.toUTF8(),releases).failed() || !releases.isArray())
        return reply("error","GitHub returned an invalid release list.");
#ifdef _WIN64
    const auto version=scbridge::newerPortableVersion(releases,argv[1],"x64");
#else
    const auto version=scbridge::newerPortableVersion(releases,argv[1],"x86");
#endif
    return version.isEmpty() ? reply("current","No newer Windows 7 portable release is available.")
                             : reply("available","",version);
}
