#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace scbridge {
class TransientTooltip final : public juce::TooltipWindow,private juce::KeyListener {
    class Expiry final : public juce::Timer {
    public:
        explicit Expiry(TransientTooltip& tooltip):owner(tooltip) {}
        void timerCallback() override { owner.poll(); }
    private:
        TransientTooltip& owner;
    };
public:
    explicit TransientTooltip(juce::Component* parent,int delay=700,
                              std::function<uint32_t()> clock=[] { return juce::Time::getMillisecondCounter(); })
        :TooltipWindow(parent,delay),host(parent),now(std::move(clock)),expiry(*this) {
        if(host) host->addKeyListener(this);
    }
    ~TransientTooltip() override {
        expiry.stopTimer();if(observedFocus && observedFocus!=host) observedFocus->removeKeyListener(this);
        if(host) host->removeKeyListener(this);
    }
    static juce::String brief(const juce::String& text) {
        const int end=text.indexOf(". ");return end<0 ? text : text.substring(0,end+1);
    }
    juce::String getTipFor(juce::Component& component) override {
        if(candidate!=&component) { candidate=&component;blocked=nullptr;if(isVisible()) shownAt=now(); }
        if(blocked==&component) return {};
        return brief(TooltipWindow::getTipFor(component));
    }
    void visibilityChanged() override {
        if(isVisible()) {
            if(observedFocus && observedFocus!=host) observedFocus->removeKeyListener(this);
            observedFocus=juce::Component::getCurrentlyFocusedComponent();
            if(observedFocus && observedFocus!=host) observedFocus->addKeyListener(this);
            shownAt=now();expiry.startTimer(50);
        }
    }
    bool keyPressed(const juce::KeyPress&,juce::Component*) override { dismiss();return false; }
private:
    void dismiss() {
        if(isVisible()) { blocked=candidate;hideTip();expiry.startTimer(50); }
    }
    void poll() {
        if(isVisible() && (now()-shownAt>=3000 || juce::Component::getCurrentlyFocusedComponent()!=observedFocus)) dismiss();
        if(!isVisible() && blocked && juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse()!=blocked)
            blocked=nullptr;
        if(!isVisible() && !blocked) expiry.stopTimer();
    }
    juce::Component::SafePointer<juce::Component> host,candidate,blocked,observedFocus;
    std::function<uint32_t()> now;
    Expiry expiry;
    uint32_t shownAt=0;
};
}
