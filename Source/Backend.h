#pragma once

// Native Apple Silicon uses the compatibility worker; Intel hosts retain the
// original Mac engine backend, including hosts deliberately running in Rosetta.
#if defined(__APPLE__) && defined(__arm64__) && defined(SCBRIDGE_MAC_TRANSLATION_AVAILABLE)
#define SCBRIDGE_TRANSLATED_ENGINE 1
#endif
