#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace scbridge {
class AccessibleToggleButton : public juce::ToggleButton {
public:
    using juce::ToggleButton::ToggleButton;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
#ifdef _WIN32
        class Handler final : public juce::AccessibilityHandler {
        public:
            explicit Handler(AccessibleToggleButton& control)
                :AccessibilityHandler(control,juce::AccessibilityRole::toggleButton,actions(control)),button(control) {}
            juce::AccessibleState getCurrentState() const override {
                auto state=AccessibilityHandler::getCurrentState().withCheckable();
                return button.getToggleState() ? state.withChecked() : state;
            }
            juce::String getTitle() const override {
                const auto title=AccessibilityHandler::getTitle();
                return title.isNotEmpty() ? title : button.getButtonText();
            }
            juce::String getHelp() const override { return button.getTooltip(); }
        private:
            static juce::AccessibilityActions actions(AccessibleToggleButton& button) {
                return juce::AccessibilityActions()
                    .addAction(juce::AccessibilityActionType::press,[&button] {
                        if(button.isEnabled()) button.triggerClick();
                    })
                    .addAction(juce::AccessibilityActionType::toggle,[&button] {
                        if(button.isEnabled()) button.setToggleState(!button.getToggleState(),juce::sendNotification);
                    });
            }
            AccessibleToggleButton& button;
        };
        // TogglePattern supplies the state/action. A read-only text ValuePattern
        // incorrectly makes NVDA describe an otherwise operable checkbox as read-only.
        return std::make_unique<Handler>(*this);
#else
        return juce::ToggleButton::createAccessibilityHandler();
#endif
    }
};
}
