#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace scbridge {
// JUCE's read-only editor exposes static text and rejects cursor keys. Help is
// immutable, but still needs a text-area role, selection and caret navigation.
class ReviewTextEditor final : public juce::TextEditor {
    class TextAccess final : public juce::AccessibilityTextInterface {
    public:
        explicit TextAccess(ReviewTextEditor& view):editor(view) {}
        bool isDisplayingProtectedText() const override { return false; }
        bool isReadOnly() const override { return true; }
        int getTotalNumCharacters() const override { return editor.getText().length(); }
        juce::Range<int> getSelection() const override { return editor.getHighlightedRegion(); }
        void setSelection(juce::Range<int> range) override { editor.setHighlightedRegion(range); }
        juce::String getText(juce::Range<int> range) const override { return editor.getTextInRange(range); }
        void setText(const juce::String&) override {}
        int getTextInsertionOffset() const override { return editor.getCaretPosition(); }
        juce::RectangleList<int> getTextBounds(juce::Range<int> range) const override {
            juce::RectangleList<int> result;
            for(const auto& rectangle:editor.getTextBounds(range)) result.add(editor.localAreaToGlobal(rectangle));
            return result;
        }
        int getOffsetAtPoint(juce::Point<int> point) const override { return editor.getTextIndexAt(editor.getLocalPoint(nullptr,point)); }
    private:
        ReviewTextEditor& editor;
    };
public:
    ReviewTextEditor() {
        setMultiLine(true);setReadOnly(true);setCaretVisible(true);setTabKeyUsedAsCharacter(false);
        setHelpText("Read-only help text. Use arrow keys to review, Shift with arrows to select, and Command+C to copy. Escape closes help.");
        setTooltip(getHelpText());
    }
    bool keyPressed(const juce::KeyPress& key) override {
        const auto modifiers=key.getModifiers();const bool selecting=modifiers.isShiftDown();
        if(modifiers.isCtrlDown()) return TextEditor::keyPressed(key);
        const auto code=key.getKeyCode();bool handled=false;
        if(code==juce::KeyPress::upKey) handled=modifiers.isCommandDown() ? moveCaretToTop(selecting) : moveCaretUp(selecting);
        else if(code==juce::KeyPress::downKey) handled=modifiers.isCommandDown() ? moveCaretToEnd(selecting) : moveCaretDown(selecting);
        else if(code==juce::KeyPress::leftKey) handled=modifiers.isCommandDown() ? moveCaretToStartOfLine(selecting) : moveCaretLeft(modifiers.isAltDown(),selecting);
        else if(code==juce::KeyPress::rightKey) handled=modifiers.isCommandDown() ? moveCaretToEndOfLine(selecting) : moveCaretRight(modifiers.isAltDown(),selecting);
        else if(code==juce::KeyPress::homeKey) handled=moveCaretToTop(selecting);
        else if(code==juce::KeyPress::endKey) handled=moveCaretToEnd(selecting);
        else if(code==juce::KeyPress::pageUpKey) handled=pageUp(selecting);
        else if(code==juce::KeyPress::pageDownKey) handled=pageDown(selecting);
        else return TextEditor::keyPressed(key);
        return handled;
    }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(*this,juce::AccessibilityRole::editableText,
            juce::AccessibilityActions{},juce::AccessibilityHandler::Interfaces{std::make_unique<TextAccess>(*this)});
    }
};
}
