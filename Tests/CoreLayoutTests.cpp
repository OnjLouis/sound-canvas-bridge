#include "../Source/CoreLayouts.h"
#include <cassert>
#include <iostream>

int main() {
    for(const auto& entry:scbridge::windowsCoreLayouts) {
        assert(scbridge::windowsCoreLayout(entry.sha256,entry.bits)==&entry);
        assert(!scbridge::windowsCoreLayout(entry.sha256,entry.bits==32 ? 64 : 32));
        auto changed=std::string(entry.sha256);changed[0]=changed[0]=='0' ? '1' : '0';
        assert(!scbridge::windowsCoreLayout(changed.c_str(),entry.bits));
    }
    assert(!scbridge::windowsCoreLayout(nullptr,32));
    assert(!scbridge::windowsCoreLayout("",64));
    std::cout<<"PASS exact-hash layout dispatch, architecture separation and unknown/tampered identity refusal\n";
}
