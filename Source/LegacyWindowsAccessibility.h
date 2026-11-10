#pragma once
#ifdef SCBRIDGE_WINDOWS7
#include <juce_gui_basics/juce_gui_basics.h>
#include <windows.h>
#pragma push_macro("DEFINE_GUID")
#include <initguid.h>
#include <oleacc.h>
#pragma pop_macro("DEFINE_GUID")

namespace scbridge {
// Win7 predates UIA notification events. An MSAA alert lets the screen reader
// announce a user-initiated change without moving focus or synthesising speech.
class LegacyWindowsAnnouncement {
public:
    ~LegacyWindowsAnnouncement() { reset(); }

    bool post(juce::Component& component,const juce::String& text) {
        auto* peer=component.getPeer();
        const auto parent=peer ? static_cast<HWND>(peer->getNativeHandle()) : nullptr;
        if(!parent || !component.hasKeyboardFocus(true) || text.isEmpty()) return false;
        if(!anchor || GetParent(anchor)!=parent) {
            reset();
            if(FAILED(CoCreateInstance(CLSID_AccPropServices,nullptr,CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&properties)))) return false;
            anchor=CreateWindowExW(WS_EX_NOPARENTNOTIFY,L"STATIC",L"",WS_CHILD|WS_VISIBLE,
                0,0,0,0,parent,nullptr,nullptr,nullptr);
            VARIANT role{};
            role.vt=VT_I4;
            role.lVal=ROLE_SYSTEM_ALERT;
            if(!anchor || FAILED(properties->SetHwndProp(anchor,OBJID_CLIENT,CHILDID_SELF,PROPID_ACC_ROLE,role))) {
                reset();
                return false;
            }
        }
        if(FAILED(properties->SetHwndPropStr(anchor,OBJID_CLIENT,CHILDID_SELF,PROPID_ACC_NAME,text.toWideCharPointer())))
            return false;
        NotifyWinEvent(EVENT_SYSTEM_ALERT,anchor,OBJID_CLIENT,CHILDID_SELF);
        return true;
    }
private:
    void reset() {
        if(anchor && IsWindow(anchor)) {
            const MSAAPROPID props[]{PROPID_ACC_ROLE,PROPID_ACC_NAME};
            properties->ClearHwndProps(anchor,OBJID_CLIENT,CHILDID_SELF,props,2);
            DestroyWindow(anchor);
        }
        anchor=nullptr;
        if(properties) properties->Release();
        properties=nullptr;
    }
    HWND anchor=nullptr;
    IAccPropServices* properties=nullptr;
};
}
#endif
