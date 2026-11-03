#include "ProgramCatalogue.h"
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

static void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}

static void testCatalogue() {
    using namespace scbridge;
    static_assert(catalogueCount==2958);
    static_assert(programAt(0)==&programCatalogue[0]);
    static_assert(programAt(catalogueCount)==nullptr);
    static_assert(std::is_same_v<decltype(programAt(0)),const ProgramEntry*>);
    static_assert(findProgram(4,0,0,false)==programAt(0));
    static_assert(mapName(3)=="SC-88 Pro");
    static_assert(programMatches(programCatalogue[0],"piano SC-8820"));
    require(programAt(std::numeric_limits<std::size_t>::max())==nullptr,"Huge index must return null");
    require(programAt(catalogueCount+1)==nullptr,"Past-end index must return null");

    const std::size_t groupCounts[]={1262,774,418,418,37,25,14,10};
    std::set<std::tuple<int,int,int,bool>> identities;
    std::set<std::string> ids,hostNames;
    constexpr std::uint64_t fnvPrime=1099511628211ULL;
    std::uint64_t fingerprint=14695981039346656037ULL;
    auto hashByte=[&](unsigned char byte) { fingerprint=(fingerprint^byte)*fnvPrime; };
    auto hashString=[&](std::string_view text) {
        for(unsigned char byte:text) hashByte(byte);
        hashByte(0);
    };
    std::size_t index=0;
    for(std::size_t group=0;group<8;++group) {
        const int map=4-static_cast<int>(group%4);
        const bool drums=group>=4;
        for(std::size_t row=0;row<groupCounts[group];++row,++index) {
            const auto* entry=programAt(index);
            require(entry && entry==&programCatalogue[index],"Getter changed catalogue order");
            require(entry->map==map && entry->drums==drums,"Frozen map/type block order changed");
            require(entry->id && entry->name && entry->id[0] && entry->name[0],"Empty catalogue field");
            require(entry->bank>=0 && entry->bank<=127 && entry->program>=0 && entry->program<=127,
                "Address outside zero-based MIDI range");
            char expectedId[32]{};
            std::snprintf(expectedId,sizeof(expectedId),"%c-%d-%03d-%03d",drums ? 'd' : 'p',map,entry->bank,entry->program);
            require(std::string_view(entry->id)==expectedId,"ID differs from exact map/address/type");
            require(identities.emplace(map,entry->bank,entry->program,drums).second,"Duplicate address/type identity");
            require(ids.emplace(entry->id).second,"Duplicate catalogue ID");
            require(findProgram(map,entry->bank,entry->program,drums)==entry,"Exact lookup failed round trip");
            require(findProgram(0,entry->bank,entry->program,drums)==nullptr,"PAGE0 must not alias a real map");
            require(!programMatches(*entry,"PAGE0"),"Catalogue contains a PAGE0 alias");
            require(programMatches(*entry,entry->id),"Search cannot find an entry's own ID");
            require(programMatches(*entry,entry->name),"Search cannot find an entry's own name");
            const auto hostName=programHostName(*entry);
            require(hostNames.emplace(hostName).second,"Host names must disambiguate every entry");
            require(hostName.find(entry->name)!=std::string::npos,"Host name lost the authoritative name");
            hashString(entry->id);hashString(entry->name);
            hashByte(static_cast<unsigned char>(map));
            hashByte(static_cast<unsigned char>(entry->bank));
            hashByte(static_cast<unsigned char>(entry->program));
            hashByte(static_cast<unsigned char>(drums));
        }
    }
    require(index==catalogueCount && identities.size()==catalogueCount,"Not all catalogue rows were checked");
    // Frozen metadata fingerprint includes every field and row order, not pointers or struct padding.
    require(fingerprint==0xcdf64a643c5318ecULL,"Catalogue content/order differs from verified metadata");
    require(std::string_view(programAt(catalogueCount-1)->id)=="d-1-000-127","Last frozen entry changed");
}

static void testLookup() {
    using namespace scbridge;
    const std::string_view mapNames[]={"SC-55","SC-88","SC-88 Pro","SC-8820"};
    std::set<const ProgramEntry*> ambiguous;
    for(int map=1;map<=4;++map) {
        require(mapName(map)==mapNames[map-1],"Incorrect map name");
        const auto* piano=findProgram(map,0,0,false);
        const auto* kit=findProgram(map,0,0,true);
        require(piano && std::string_view(piano->name)=="Piano 1","Known Piano 1 is missing");
        require(kit && std::string_view(kit->name)==(map==1 ? "STANDARD" : "STANDARD 1"),"Known standard kit is missing");
        require(ambiguous.emplace(piano).second && ambiguous.emplace(kit).second,"Ambiguous maps/types were collapsed");
    }
    require(ambiguous.size()==8,"Same address must retain four maps and two types");
    const auto* upright=findProgram(4,1,0,false);
    require(upright && std::string_view(upright->name)=="UprightPiano","Known bank variation is missing");
    require(findProgram(4,1,0,true)==nullptr,"Missing drum bank must not fall back to bank zero or a melodic tone");
    const auto* tr808=findProgram(4,0,25,true);
    require(tr808 && std::string_view(tr808->name)=="TR-808","Known TR-808 kit is missing");
    const auto* cm64=findProgram(1,0,127,true);
    require(cm64 && std::string_view(cm64->name)=="CM-64/32L","Program 127 kit is missing");
    require(findProgram(4,127,127,true)==nullptr,"Absent in-range address must return null");

    const int invalidMaps[]={std::numeric_limits<int>::min(),-1,0,5,128,256,std::numeric_limits<int>::max()};
    for(int map:invalidMaps) {
        require(mapName(map).empty(),"Invalid map must have no display name");
        for(bool drums:{false,true}) require(findProgram(map,0,0,drums)==nullptr,"Invalid map lookup succeeded");
    }
    const int invalidMidi[]={std::numeric_limits<int>::min(),-1,128,256,std::numeric_limits<int>::max()};
    for(int map=1;map<=4;++map) for(bool drums:{false,true}) for(int value:invalidMidi) {
        require(findProgram(map,value,0,drums)==nullptr,"Invalid bank wrapped or fell back");
        require(findProgram(map,0,value,drums)==nullptr,"Invalid program wrapped or fell back");
    }
}

static void testSearchAndHostNames() {
    using namespace scbridge;
    const auto& piano=*findProgram(4,0,0,false);
    const auto& kit=*findProgram(1,0,0,true);
    require(programMatches(piano,{}) && programMatches(piano," \t\r\n\f\v"),"Empty/whitespace query must match");
    require(programMatches(piano,"  pIaNo\tSC-8820\nPROGRAMS 1 "),"Case-insensitive multi-field token search failed");
    require(programMatches(piano,"p-4-000-000"),"ID search failed");
    require(programMatches(kit,"standard SC-55 DRUMS"),"Kit/map/type search failed");
    require(!programMatches(piano,"drums") && !programMatches(kit,"programs"),"Search ignored type");
    require(!programMatches(piano,"SC-55") && !programMatches(piano,"Piano missing"),"Search ignored a required token");
    require(!programMatches(piano,"Piano1"),"Search must not invent name aliases");
    require(!programMatches(piano,std::string(4096,'x')),"Long unmatched token must fail");
    require(!programMatches(piano,std::string_view("Piano\0",6)),"Embedded NUL must not truncate the query");
    const auto* cm64=findProgram(1,0,127,true);
    require(programMatches(*cm64,"CM-64/32L"),"Punctuation in kit name must remain searchable");
    require(programHostName(piano)=="SC-8820 / Programs / Piano 1 (bank 0, program 1)","Host name must use displayed program numbering");
    require(programHostName(kit)=="SC-55 / Drums / STANDARD (bank 0, program 1)","Kit host name is ambiguous");
}

int main() {
    try {
        testCatalogue();testLookup();testSearchAndHostNames();
        std::cout<<"PASS 2958 frozen entries, unique identities/host names, all round trips, map/type ambiguity, "
            "bounds, invalid/missing addresses, no PAGE0 aliases, known tones/kits and search\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL "<<error.what()<<'\n';
        return 1;
    }
}
