#pragma once
#include <juce_core/juce_core.h>
#include <sys/types.h>

namespace scbridge {
bool sandboxedHost();
int createSandboxEngineMemory();
// Returns the normal engine status code. The authenticated socket is the same
// audio transport used by the directly spawned helper in non-sandboxed hosts.
int launchSandboxEngine(const juce::File& launcher,int memory,int& socket,pid_t& process);
}
