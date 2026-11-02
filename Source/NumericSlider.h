#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <charconv>

namespace scbridge {
class NumericSlider final : public juce::Slider {
    class Entry final : public juce::TextEditor {
    public:
        std::function<bool(const juce::KeyPress&)> onStep;
        std::function<void(bool)> onNavigate;
        bool keyPressed(const juce::KeyPress& key) override {
            const auto code = key.getKeyCode();
            const auto modifiers = key.getModifiers();
            if (code == juce::KeyPress::tabKey && !modifiers.isCtrlDown() &&
                !modifiers.isAltDown() && !modifiers.isCommandDown() && onNavigate) {
                onNavigate(!modifiers.isShiftDown()); return true;
            }
            if (!key.getModifiers().isAnyModifierKeyDown() &&
                (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey ||
                 code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey) && onStep)
                return onStep(key);
            return TextEditor::keyPressed(key);
        }
    };
public:
#ifdef _WIN32
    void setAccessibilityValueOffset(double value) { accessibilityValueOffset=value; }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        class Value final : public juce::AccessibilityValueInterface {
        public:
            explicit Value(NumericSlider& value):slider(value) {}
            bool isReadOnly() const override { return false; }
            double getCurrentValue() const override { return slider.getValue()+slider.accessibilityValueOffset; }
            juce::String getCurrentValueAsString() const override { return slider.getTextFromValue(slider.getValue()); }
            void setValue(double value) override {
                juce::Slider::ScopedDragNotification drag(slider);
                slider.setValue(value-slider.accessibilityValueOffset,juce::sendNotificationSync);
            }
            void setValueAsString(const juce::String& text) override {
                setValue(slider.getValueFromText(text)+slider.accessibilityValueOffset);
            }
            AccessibleValueRange getRange() const override {
                return {{slider.getMinimum()+slider.accessibilityValueOffset,slider.getMaximum()+slider.accessibilityValueOffset},slider.getInterval()};
            }
        private:
            NumericSlider& slider;
        };
        return std::make_unique<juce::AccessibilityHandler>(*this,juce::AccessibilityRole::slider,
            juce::AccessibilityActions{},juce::AccessibilityHandler::Interfaces{std::make_unique<Value>(*this)});
    }
#endif
    NumericSlider() {
        addChildComponent(entry);
        entry.setMultiLine(false); entry.setReturnKeyStartsNewLine(false);
        entry.setSelectAllWhenFocused(true); entry.setTabKeyUsedAsCharacter(false);
        entry.onReturnKey = [this] { finishEntry(true,true); };
        entry.onEscapeKey = [this] { finishEntry(false,true); };
        entry.onFocusLost = [this] { finishEntry(true,false); };
        entry.onNavigate = [this](bool forward) {
            finishEntry(true,false); moveKeyboardFocusToSibling(forward);
        };
        entry.onStep = [this](const juce::KeyPress& key) {
            if (!commitTypedValue()) return true;
            stepValue(key);
            initialText = getTextFromValue(getValue());
            entry.setText(initialText,false); entry.selectAll();
            if (auto* handler = entry.getAccessibilityHandler())
                handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
            return true;
        };
    }
    ~NumericSlider() override {
        closing = true; editing = false; entry.onFocusLost = nullptr;
        entry.setAccessible(false); entry.setVisible(false); setAccessible(false);
    }
    std::function<void()> onStepStart, onStepEnd;
    void configureValueField(int width) {
        setTextBoxStyle(juce::Slider::TextBoxRight,true,width,28);
        hideReadOnlyLabels();
    }
    void resized() override {
        Slider::resized();
        entry.setBounds(getLocalBounds().removeFromRight(getTextBoxWidth()).withSizeKeepingCentre(getTextBoxWidth(),28));
        entry.toFront(false);
    }
    void lookAndFeelChanged() override {
        Slider::lookAndFeelChanged(); hideReadOnlyLabels();
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key.getModifiers().isAnyModifierKeyDown()) return Slider::keyPressed(key);
        if (key.getKeyCode() == juce::KeyPress::returnKey) {
            initialText = getTextFromValue(getValue());
            entry.setName(getName()+" value"); entry.setTitle(entry.getName());
            entry.setHelpText("Up and Down change one step. Page Up and Page Down change ten. Enter confirms; Escape cancels.");
            entry.setDescription(entry.getName());
            entry.setText(initialText,false); editing = true;
            entry.setVisible(true); entry.toFront(true); entry.grabKeyboardFocus(); entry.selectAll();
            if (auto* handler = entry.getAccessibilityHandler()) handler->grabFocus();
            return true;
        }
        return stepValue(key) || Slider::keyPressed(key);
    }
private:
    void hideReadOnlyLabels() {
        for (auto* child : getChildren()) if (auto* label = dynamic_cast<juce::Label*>(child)) {
            label->setAccessible(false); label->setWantsKeyboardFocus(false);
        }
    }
    void announceValue() {
        const auto text = getName()+", "+getTextFromValue(getValue());
#ifdef SOUNDCANVAS_TEST_PROFILE
        getProperties().set("lastKeyboardAnnouncement",text);
#endif
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
#ifndef _WIN32
        juce::AccessibilityHandler::postAnnouncement(text,juce::AccessibilityHandler::AnnouncementPriority::medium);
#endif
    }
    void applyValue(double next) {
        next = juce::jlimit(getMinimum(),getMaximum(),next);
        if (next == getValue()) return;
        if (onStepStart) onStepStart();
        setValue(next,juce::sendNotificationSync);
        if (onStepEnd) onStepEnd();
        announceValue();
    }
    bool stepValue(const juce::KeyPress& key) {
        const auto code = key.getKeyCode();
        const double step = getInterval() > 0 ? getInterval() : 1.0;
        double next = getValue();
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) next += step;
        else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) next -= step;
        else if (code == juce::KeyPress::pageUpKey) next += step*10;
        else if (code == juce::KeyPress::pageDownKey) next -= step*10;
        else if (code == juce::KeyPress::homeKey) next = getMinimum();
        else if (code == juce::KeyPress::endKey) next = getMaximum();
        else return false;
        applyValue(next); return true;
    }
    bool commitTypedValue() {
        const auto typed = entry.getText().trim();
        // Untouched editor text can be stale after keyboard/host changes. It
        // must never overwrite the current parameter when focus moves away.
        if (typed == initialText.trim()) return true;
        auto numeric = typed;
        const auto suffix = getTextValueSuffix().trim();
        if (suffix.isNotEmpty() && numeric.endsWithIgnoreCase(suffix))
            numeric = numeric.dropLastCharacters(suffix.length()).trimEnd();
        if (numeric.startsWithChar('+')) numeric = numeric.substring(1);
        const auto bytes = numeric.toStdString();
        int integer = 0;
        const auto result = std::from_chars(bytes.data(),bytes.data()+bytes.size(),integer);
        if (bytes.empty() || result.ec != std::errc{} || result.ptr != bytes.data()+bytes.size()) {
#ifdef _WIN32
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Enter a value",
                getName()+" is unchanged. Enter a whole number.");
#else
            juce::AccessibilityHandler::postAnnouncement(getName()+" unchanged. Enter a whole number.",
                juce::AccessibilityHandler::AnnouncementPriority::medium);
#endif
            return false;
        }
        applyValue(getValueFromText(juce::String(integer)));
        initialText = getTextFromValue(getValue());
        return true;
    }
    void finishEntry(bool commit, bool returnFocus) {
        if (!editing || closing) return;
        closing = true;
        if (commit) commitTypedValue();
        editing = false; entry.setVisible(false); closing = false;
        if (returnFocus) {
            grabKeyboardFocus();
            if (auto* handler = getAccessibilityHandler()) handler->grabFocus();
        }
    }
    Entry entry;
#ifdef _WIN32
    double accessibilityValueOffset=0;
#endif
    juce::String initialText;
    bool editing = false, closing = false;
};
}
