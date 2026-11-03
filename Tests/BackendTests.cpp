#include "../Source/Backend.h"

int main() {
#if defined(__APPLE__) && defined(__arm64__) && defined(SCBRIDGE_MAC_TRANSLATION_AVAILABLE)
#ifndef SCBRIDGE_TRANSLATED_ENGINE
#error Apple Silicon must select the native compatibility worker
#endif
#else
#ifdef SCBRIDGE_TRANSLATED_ENGINE
#error Intel and Windows must retain their native engine paths
#endif
#endif
    return 0;
}
