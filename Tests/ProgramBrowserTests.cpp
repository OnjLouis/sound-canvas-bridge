#include "ProgramBrowser.h"
#include <iostream>
#include <stdexcept>

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        const auto require=[](bool value,const char* message) { if(!value) throw std::runtime_error(message); };
        scbridge::ProgramBrowser browser;
        juce::Component window;
        window.setSize(700,762);window.addAndMakeVisible(browser);
        browser.setBounds(window.getLocalBounds());window.addToDesktop(0);window.setVisible(true);
        auto* search=dynamic_cast<juce::TextEditor*>(browser.findChildWithID("presetSearch"));
        auto* list=dynamic_cast<juce::ListBox*>(browser.findChildWithID("presetList"));
        require(search && list,"Named browser controls must exist");
        for(int map=1;map<=4;++map) for(bool drums:{false,true}) {
            browser.openAt(-1,map,drums);
            require(browser.matchingCount()>0 && browser.selectedProgram()>=0,"Every map/type must open with a selected sound");
            const auto* table=list->getAccessibilityHandler()->getTableInterface();
            const auto* row=table ? table->getRowHandler(list->getSelectedRow()) : nullptr;
            require(table && table->getNumRows()==browser.matchingCount() && row && row->getTitle().isNotEmpty(),
                    "Browser must expose actual named rows, not just a count");
        }
        browser.openAt(0,4,false);
        search->setText("no-such-preset-7e3c",false);search->onTextChange();
        require(browser.matchingCount()==0,"A failed search must be empty");
        browser.openAt(0,4,false);
        require(search->getText().isEmpty() && browser.matchingCount()==1262,"Reopening must clear an empty search result");
        search->setText("UprightPiano",false);search->onTextChange();
        require(browser.matchingCount()==1 && browser.selectedProgram()==1,"Search must find its named sound");
        const auto selected=list->getSelectedRow();
        list->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
        require(list->getSelectedRow()==selected,"Single-result arrows must stay in range");
        std::cout<<"PASS: eight map/type lists expose named rows; search and reopen remain populated\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
