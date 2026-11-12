#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include "KeyboardAnnouncement.h"

bool scbridge::announceKeyboardSetting(juce::Component& focus,const juce::String& text) {
    auto* peer=focus.getPeer();
    if(!peer) return false;
    // Keep compatibility with systems that predate UIA notification events.
    static const auto notify=[] {
        auto module=GetModuleHandleW(L"UIAutomationCore.dll");
        return module ? reinterpret_cast<decltype(&UiaRaiseNotificationEvent)>(
            GetProcAddress(module,"UiaRaiseNotificationEvent")) : nullptr;
    }();
    if(!notify || !UiaClientsAreListening()) return false;
    auto* handler=peer->getComponent().getAccessibilityHandler();
    if(!handler || !handler->getNativeImplementation()) return false;
    // JUCE's pinned Win32 native handle has IRawElementProviderSimple as its
    // first COM base. The default HWND provider lacks JUCE's window identity.
    auto* native=reinterpret_cast<IRawElementProviderSimple*>(handler->getNativeImplementation());
    IRawElementProviderSimple* provider=nullptr;
    if(FAILED(native->QueryInterface(IID_PPV_ARGS(&provider))) || !provider) return false;
    const auto message=SysAllocString(text.toWideCharPointer());
    const auto activity=SysAllocString(L"SoundCanvas.MusicalKeyboard");
    const bool sent=message && activity && SUCCEEDED(notify(provider,NotificationKind_Other,
        NotificationProcessing_MostRecent,message,activity));
    SysFreeString(message);
    SysFreeString(activity);
    provider->Release();
    return sent;
}
