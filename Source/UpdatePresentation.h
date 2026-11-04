#pragma once
#include <string_view>

namespace scbridge {
inline bool shouldShowUpdateResult(bool manual, bool preparing, bool failed, std::string_view status) {
    const bool installerOpened = preparing && !failed &&
        (status == "prepared" || status == "installer-open");
    return (manual || preparing) && !installerOpened;
}
}
