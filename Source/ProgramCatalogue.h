#pragma once
#include "ProgramData.h"
#include <cstddef>
#include <string>
#include <string_view>

namespace scbridge {
inline constexpr std::size_t catalogueCount=sizeof(programCatalogue)/sizeof(programCatalogue[0]);

// Returned entries have static lifetime; an invalid index or exact address returns null.
constexpr const ProgramEntry* programAt(std::size_t index) noexcept {
    return index<catalogueCount ? &programCatalogue[index] : nullptr;
}
constexpr const ProgramEntry* findProgram(int map,int bank,int program,bool drums) noexcept {
    for(const auto& entry:programCatalogue)
        if(entry.map==map && entry.bank==bank && entry.program==program && entry.drums==drums)
            return &entry;
    return nullptr;
}
// Map 0 (PAGE0) is not a catalogue map and never aliases map 4.
constexpr std::string_view mapName(int map) noexcept {
    switch(map) {
        case 1:return "SC-55";
        case 2:return "SC-88";
        case 3:return "SC-88 Pro";
        case 4:return "SC-8820";
        default:return {};
    }
}

namespace catalogueDetail {
constexpr char lowerAscii(char value) noexcept {
    return value>='A' && value<='Z' ? static_cast<char>(value+('a'-'A')) : value;
}
constexpr bool contains(std::string_view text,std::string_view token) noexcept {
    if(token.size()>text.size()) return false;
    for(std::size_t start=0;start<=text.size()-token.size();++start) {
        std::size_t i=0;
        while(i<token.size() && lowerAscii(text[start+i])==lowerAscii(token[i])) ++i;
        if(i==token.size()) return true;
    }
    return false;
}
constexpr bool whitespace(char value) noexcept {
    return value==' ' || value=='\t' || value=='\n' || value=='\r' || value=='\f' || value=='\v';
}
}

// ASCII case-insensitive AND of whitespace-separated tokens across name, id,
// map name and type (Programs/Drums). Empty or whitespace-only queries match all.
constexpr bool programMatches(const ProgramEntry& entry,std::string_view query) noexcept {
    while(!query.empty()) {
        if(catalogueDetail::whitespace(query.front())) { query.remove_prefix(1);continue; }
        std::size_t length=0;
        while(length<query.size() && !catalogueDetail::whitespace(query[length])) ++length;
        const auto token=query.substr(0,length);
        if(!catalogueDetail::contains(entry.name,token) && !catalogueDetail::contains(entry.id,token)
            && !catalogueDetail::contains(mapName(entry.map),token)
            && !catalogueDetail::contains(entry.drums ? "Drums" : "Programs",token)) return false;
        query.remove_prefix(length);
    }
    return true;
}

// Use the editor's one-based program display while keeping bank numbers unchanged.
inline std::string programHostName(const ProgramEntry& entry) {
    return std::string(mapName(entry.map))+(entry.drums ? " / Drums / " : " / Programs / ")
        +entry.name+" (bank "+std::to_string(entry.bank)+", program "+std::to_string(entry.program+1)+")";
}
}
