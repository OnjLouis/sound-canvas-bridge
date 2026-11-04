#pragma once
#include "ProgramCatalogue.h"
#include "ControlHelp.h"
#include "ReviewTextEditor.h"
#include "KeyboardChoice.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <functional>
#include <vector>

namespace scbridge {
class PresetListBox final : public juce::ListBox {
public:
    using juce::ListBox::ListBox;
    void focusSelectedRow() {
#ifdef _WIN32
        if(auto* handler=getAccessibilityHandler()) if(auto* table=handler->getTableInterface())
            if(const auto* row=table->getRowHandler(getSelectedRow()))
                const_cast<juce::AccessibilityHandler*>(row)->grabFocus();
#endif
    }
    void focusGained(FocusChangeType cause) override {
        juce::ListBox::focusGained(cause);
#ifdef _WIN32
        // JUCE focuses the list container. Expose its selected row after that
        // handoff, so Windows screen readers receive a named item on entry.
        juce::Component::SafePointer<PresetListBox> safe(this);
        juce::MessageManager::callAsync([safe] {
            if(safe && safe->hasKeyboardFocus(true)) safe->focusSelectedRow();
        });
#endif
    }
};
class PresetSelector final : public KeyboardChoice {
public:
    std::function<void()> onBrowse,onDetails;
    bool keyPressed(const juce::KeyPress& key) override {
        if(key==juce::KeyPress::returnKey && onBrowse) { onBrowse();return true; }
        if(key==juce::KeyPress::F1Key && onDetails) { onDetails();return true; }
        return KeyboardChoice::keyPressed(key);
    }
};
class ProgramBrowser final : public juce::Component, private juce::ListBoxModel {
public:
    ProgramBrowser() : list("Presets", this) {
        setName("Preset browser");
        setFocusContainerType(FocusContainerType::focusContainer);
        configure(search, "presetSearch", "Search", 1);
        search.setMultiLine(false);
        search.onTextChange = [this] { rebuild(); };
        search.onReturnKey = [this] { loadSelected(); };
        search.onEscapeKey = [this] { close(); };
        configure(map, "presetMap", "Sound map", 2);
        map.addItem("All maps", 1);
        for (int i = 1; i <= 4; ++i) map.addItem(juce::String(mapName(i).data()), i + 1);
        map.setSelectedId(5, juce::dontSendNotification);
        map.onChange = [this] { rebuild(); };
        configure(type, "presetType", "Sound type", 3);
        type.addItemList({"All sounds", "Programs", "Drums"}, 1);
        type.setSelectedId(2, juce::dontSendNotification);
        type.onChange = [this] { rebuild(); };
        configure(list, "presetList", "Presets", 4);
        list.setMultipleSelectionEnabled(false);
        list.setRowHeight(28);
        configure(load, "loadPreset", "Load preset", 5);
        load.setButtonText("Load");
        load.onClick = [this] { loadSelected(); };
        configure(cancel, "closePresetBrowser", "Close browser", 6);
        cancel.setButtonText("Close");
        cancel.onClick = [this] { close(); };
        details.setComponentID("presetDetails");details.setName("Preset details");details.setTitle(details.getName());
        details.setExplicitFocusOrder(1);addChildComponent(details);
        detailsBack.setComponentID("closePresetDetails");detailsBack.setName("Back to presets");
        detailsBack.setButtonText("Back");detailsBack.setExplicitFocusOrder(2);
        detailsBack.onClick=[this] { showDetails(false); };addChildComponent(detailsBack);
        searchLabel.setText("Search", juce::dontSendNotification);
        mapLabel.setText("Sound map", juce::dontSendNotification);
        typeLabel.setText("Sound type", juce::dontSendNotification);
        for (auto* label : {&searchLabel, &mapLabel, &typeLabel}) {
            label->setAccessible(false);
            addAndMakeVisible(*label);
        }
        count.setName("Matching presets");
        addAndMakeVisible(count);
        search.getProperties().set(explicitHelpProperty, "Filters the preset names. Enter loads the selected sound; Escape returns to the controls.");
        list.getProperties().set(explicitHelpProperty, "Arrow keys select a preset without changing the sound. Enter or double-click loads it. F1 opens the selected sound's map, bank and program details.");
        rebuild();
    }

    void openAt(int index, int currentMap, bool drums) {
        showDetails(false,false);
        search.setText({}, false);
        map.setSelectedId(juce::jlimit(1, 4, currentMap) + 1, juce::dontSendNotification);
        type.setSelectedId(drums ? 3 : 2, juce::dontSendNotification);
        rebuild(index);
    }
    void focusSearch() { search.grabKeyboardFocus(); }
    int selectedProgram() const {
        const int row = list.getSelectedRow();
        return juce::isPositiveAndBelow(row, matches.size()) ? matches[static_cast<size_t>(row)] : -1;
    }
    int matchingCount() const { return static_cast<int>(matches.size()); }
    std::function<void(int)> onLoad;
    std::function<void()> onClose;

    void resized() override {
        const int width = getWidth() - 24;
        searchLabel.setBounds(12, 8, 90, 28);
        search.setBounds(106, 8, width - 94, 28);
        const int column = (width - 12) / 2;
        mapLabel.setBounds(12, 46, 90, 28);
        map.setBounds(106, 46, column - 94, 28);
        typeLabel.setBounds(24 + column, 46, 90, 28);
        type.setBounds(118 + column, 46, width - column - 106, 28);
        list.setBounds(12, 88, width, std::max(28, getHeight() - 180));
        count.setBounds(12, getHeight() - 82, width, 28);
        load.setBounds(getWidth() - 212, getHeight() - 44, 92, 30);
        cancel.setBounds(getWidth() - 108, getHeight() - 44, 96, 30);
        details.setBounds(12,12,width,getHeight()-68);
        detailsBack.setBounds(getWidth()-108,getHeight()-44,96,30);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) {
            if(details.isVisible()) showDetails(false);else close();
            return true;
        }
        if(key==juce::KeyPress::F1Key && selectedProgram()>=0) { showDetails(true);return true; }
        if (key.getModifiers().isCommandDown() && !key.getModifiers().isAltDown() && !key.getModifiers().isShiftDown() &&
            juce::CharacterFunctions::toLowerCase(key.getKeyCode()) == 'f') {
            if(details.isVisible()) showDetails(false,false);
            focusSearch(); return true;
        }
        return false;
    }

private:
    template <typename T> void configure(T& control, const char* id, const char* name, int position) {
        control.setComponentID(id); control.setName(name); control.setTitle(name);
        control.setDescription(name); control.setWantsKeyboardFocus(true);
        control.setExplicitFocusOrder(position);
        addAndMakeVisible(control);
    }
    void rebuild(int requested = -1) {
        if (requested < 0) requested = selectedProgram();
        const auto query = search.getText().toStdString();
        std::vector<int> next;
        next.reserve(catalogueCount);
        for (size_t index = 0; index < catalogueCount; ++index) {
            const auto& entry = programCatalogue[index];
            if (map.getSelectedId() > 1 && entry.map != map.getSelectedId() - 1) continue;
            if (type.getSelectedId() == 2 && entry.drums) continue;
            if (type.getSelectedId() == 3 && !entry.drums) continue;
            if (programMatches(entry, query)) next.push_back(static_cast<int>(index));
        }
        const auto selected = std::find(next.begin(), next.end(), requested);
        const int row = selected != next.end() ? static_cast<int>(selected - next.begin()) : next.empty() ? -1 : 0;
        matches = std::move(next);
        list.updateContent();
        list.selectRow(row, false, true);
        if (row >= 0) list.scrollToEnsureRowIsOnscreen(row);
        count.setText(matches.empty() ? "No matching presets" : juce::String(matches.size()) + " presets", juce::dontSendNotification);
        load.setEnabled(row >= 0);
    }
    int getNumRows() override { return static_cast<int>(matches.size()); }
    juce::String getNameForRow(int row) override {
        if (!juce::isPositiveAndBelow(row, matches.size())) return {};
        return juce::String(programCatalogue[static_cast<size_t>(matches[static_cast<size_t>(row)])].name);
    }
    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override {
        const auto highlight = list.findColour(juce::TextEditor::highlightColourId);
        if (selected) graphics.fillAll(highlight);
        graphics.setColour(selected ? highlight.contrasting(1.0f) : list.findColour(juce::ListBox::textColourId));
        graphics.setFont(15.0f);
        graphics.drawText(getNameForRow(row), 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged(int) override {
        load.setEnabled(selectedProgram() >= 0);
        if(list.hasKeyboardFocus(true)) list.focusSelectedRow();
    }
    void returnKeyPressed(int) override { loadSelected(); }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { loadSelected(); }
    void loadSelected() {
        const int index = selectedProgram();
        if (index >= 0 && onLoad) onLoad(index);
    }
    void close() { if (onClose) onClose(); }
    void showDetails(bool visible,bool focus=true) {
        const auto* entry=programAt(static_cast<size_t>(selectedProgram()));
        if(visible && !entry) return;
        if(visible) details.setText(juce::String(entry->name)+"\nSound map: "+juce::String(mapName(entry->map).data())+
            "\nType: "+(entry->drums ? "Drum kit" : "Program")+"\nBank: "+juce::String(entry->bank)+
            "\nProgram: "+juce::String(entry->program+1),false);
        for(auto* control:std::initializer_list<juce::Component*>{&search,&map,&type,&list,&load,&cancel,
                                                               &searchLabel,&mapLabel,&typeLabel,&count})
            control->setVisible(!visible);
        details.setVisible(visible);detailsBack.setVisible(visible);
        if(focus) {
            auto& target=visible ? static_cast<juce::Component&>(details) : static_cast<juce::Component&>(list);
            target.grabKeyboardFocus();
            if(auto* handler=target.getAccessibilityHandler()) handler->grabFocus();
        }
    }

    juce::TextEditor search;
    juce::ComboBox map, type;
    PresetListBox list;
    juce::TextButton load, cancel;
    juce::Label searchLabel, mapLabel, typeLabel, count;
    ReviewTextEditor details;
    juce::TextButton detailsBack;
    std::vector<int> matches;
};
}
