#include "EngineSetupPolicy.h"
#include <cassert>
#include <cstring>

int main() {
    assert(std::strstr(scbridge::engineSetupGuidance,"standalone app"));
    assert(std::strstr(scbridge::engineSetupGuidance,"Preferences > General"));
    assert(std::strstr(scbridge::engineSetupGuidance,"reload this plug-in instance"));
}
