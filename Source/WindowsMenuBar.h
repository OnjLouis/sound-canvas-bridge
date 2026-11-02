#pragma once
#include <commctrl.h>
#include <map>
namespace scbridge {
class WindowsMenuBar {
public:
    struct Section { juce::String label;std::vector<juce::CommandID> commands;juce::StringArray labels; };
    WindowsMenuBar(juce::DocumentWindow& window,juce::ApplicationCommandManager& manager,const std::vector<Section>& sections)
        :commands(manager) {
        if(!window.getPeer()) return;
        handle=static_cast<HWND>(window.getPeer()->getNativeHandle());menu=CreateMenu();
        for(const auto& section:sections) {
            const auto popup=CreatePopupMenu();
            for(size_t index=0;index<section.commands.size();++index) {
                const auto command=section.commands[index];
                if(!command) { AppendMenuW(popup,MF_SEPARATOR,0,nullptr);continue; }
                const auto* info=commands.getCommandForID(command);
                if(!info) continue;
                juce::String label=index<static_cast<size_t>(section.labels.size()) && section.labels[static_cast<int>(index)].isNotEmpty()
                    ? section.labels[static_cast<int>(index)] : info->shortName.replace("&", "&&");
                auto keys=commands.getKeyMappings()->getKeyPressesAssignedToCommand(command);
                if(!keys.isEmpty()) label+="\t"+keys.getFirst().getTextDescription();
                const auto nativeId=static_cast<UINT>(lookup.size()+1);lookup[nativeId]=command;
                AppendMenuW(popup,MF_STRING,nativeId,label.toWideCharPointer());
            }
            AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(popup),section.label.toWideCharPointer());
        }
        if(!SetWindowSubclass(handle,procedure,reinterpret_cast<UINT_PTR>(this),reinterpret_cast<DWORD_PTR>(this))) return;
        installed=true;SetMenu(handle,menu);DrawMenuBar(handle);
        window.setSize(window.getWidth(),window.getHeight()+GetSystemMetrics(SM_CYMENU));
    }
    ~WindowsMenuBar() {
        if(installed && IsWindow(handle)) { SetMenu(handle,nullptr);RemoveWindowSubclass(handle,procedure,reinterpret_cast<UINT_PTR>(this));DrawMenuBar(handle); }
        if(menu) DestroyMenu(menu);
    }
private:
    static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR context) {
        auto& self=*reinterpret_cast<WindowsMenuBar*>(context);
        if(message==WM_COMMAND && lparam==0) {
            const auto it=self.lookup.find(LOWORD(wparam));
            if(it!=self.lookup.end()) { self.commands.invokeDirectly(it->second,true);return 0; }
        }
        if(message==WM_INITMENUPOPUP) {
            const auto popup=reinterpret_cast<HMENU>(wparam);
            for(const auto& [id,command]:self.lookup) if(const auto* info=self.commands.getCommandForID(command)) {
                CheckMenuItem(popup,id,MF_BYCOMMAND|((info->flags&juce::ApplicationCommandInfo::isTicked) ? MF_CHECKED : MF_UNCHECKED));
                EnableMenuItem(popup,id,MF_BYCOMMAND|((info->flags&juce::ApplicationCommandInfo::isDisabled) ? MF_GRAYED : MF_ENABLED));
            }
        }
        if(message==WM_SYSCHAR || (message==WM_SYSCOMMAND && (wparam&0xfff0)==SC_KEYMENU) ||
           message==WM_SYSKEYUP || message==WM_SYSKEYDOWN ||
           (message==WM_KEYDOWN && wparam==VK_F10)) return DefWindowProcW(window,message,wparam,lparam);
        return DefSubclassProc(window,message,wparam,lparam);
    }
    juce::ApplicationCommandManager& commands;
    HWND handle=nullptr;HMENU menu=nullptr;bool installed=false;
    std::map<UINT,juce::CommandID> lookup;
};
}
