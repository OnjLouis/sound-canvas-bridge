#include "EngineSetupPolicy.h"
#include <cassert>

int main() {
    const uint8_t note[]{0x90,60,100},off[]{0x90,60,0},controller[]{0xb0,1,127};
    assert(scbridge::requestEngineSetupNotice(true,false,true,note,3));
    assert(!scbridge::requestEngineSetupNotice(false,false,true,note,3));
    assert(!scbridge::requestEngineSetupNotice(true,true,true,note,3));
    assert(!scbridge::requestEngineSetupNotice(true,false,false,note,3));
    assert(!scbridge::requestEngineSetupNotice(true,false,true,off,3));
    assert(!scbridge::requestEngineSetupNotice(true,false,true,controller,3));
    assert(!scbridge::requestEngineSetupNotice(true,false,true,note,2));
    assert(!scbridge::requestEngineSetupNotice(true,false,true,nullptr,3));
}
