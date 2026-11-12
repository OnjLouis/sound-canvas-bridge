#include "PortableUpdate.h"
#include <iostream>

int main() {
    const auto fixture=juce::JSON::parse(R"json([
        {"tag_name":"v9.0.0","draft":false,"prerelease":false},
        {"tag_name":"windows-v9.0.0","draft":false,"prerelease":false},
        {"tag_name":"windows7-v2.0.0","draft":true,"prerelease":false},
        {"tag_name":"windows7-v1.10.0","draft":false,"prerelease":false,"assets":[{"name":"SoundCanvasBridge-1.10.0-windows7-x86.zip"},{"name":"SHA256SUMS.txt"}]},
        {"tag_name":"windows7-v1.8.0","draft":false,"prerelease":false,"assets":[{"name":"SoundCanvasBridge-1.8.0-windows7-x86.zip"},{"name":"SHA256SUMS.txt"}]},
        {"tag_name":"windows7-v3.0.0","draft":false,"prerelease":true,"assets":[{"name":"SoundCanvasBridge-3.0.0-windows7-x86.zip"},{"name":"SHA256SUMS.txt"}]},
        {"tag_name":"windows7-v4.0.0","draft":false,"prerelease":false,"assets":[{"name":"SoundCanvasBridge-4.0.0-windows7-x86.zip"}]},
        {"tag_name":"windows7-v1.12.0","draft":false,"prerelease":false,"assets":[{"name":"SoundCanvasBridge-1.12.0-windows7-x64.zip"},{"name":"SHA256SUMS.txt"}]}
    ])json");
    using scbridge::newerPortableVersion;
    if(newerPortableVersion(fixture,"1.4.3","x86")!="1.10.0" ||
       newerPortableVersion(fixture,"1.4.3","x64")!="1.12.0" ||
       newerPortableVersion(fixture,"1.10.0","x86").isNotEmpty() ||
       newerPortableVersion(fixture,"1.4.3","arm64").isNotEmpty() ||
       newerPortableVersion(fixture,"1.04.3","x86").isNotEmpty() ||
       scbridge::portableVersion("1.4.3/other") || scbridge::portableVersion("1..3")) {
        std::cerr<<"FAIL: portable release selection must require the matching stable ZIP and checksum\n";return 1;
    }
    std::cout<<"PASS: portable version, family, architecture and asset filtering\n";
}
