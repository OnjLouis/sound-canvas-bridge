#include "UpdatePresentation.h"
#include <iostream>

int main() {
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    check(!scbridge::shouldShowUpdateResult(true, true, false, "prepared"),
          "Windows installer handoff must not leave a modal OK dialog");
    check(!scbridge::shouldShowUpdateResult(true, true, false, "installer-open"),
          "Mac installer handoff must not leave a modal OK dialog");
    check(scbridge::shouldShowUpdateResult(true, true, true, "prepared"),
          "A failed handoff must not be mistaken for success");
    check(scbridge::shouldShowUpdateResult(true, true, false, "error"),
          "Download/verification failures must remain visible");
    check(scbridge::shouldShowUpdateResult(false, true, false, ""),
          "Malformed download replies must remain visible");
    check(scbridge::shouldShowUpdateResult(true, false, false, "current"),
          "Manual no-update checks must still report their result");
    check(!scbridge::shouldShowUpdateResult(false, false, false, "current"),
          "Automatic no-update checks must remain silent");
    check(scbridge::shouldShowUpdateResult(true, false, false, "unexpected"),
          "Unexpected manual replies must not be silently discarded");
    if (failures) return 1;
    std::cout << "PASS: installer handoffs are non-modal; errors and manual results remain visible\n";
}
