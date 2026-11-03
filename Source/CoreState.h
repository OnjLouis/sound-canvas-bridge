#pragma once
#include "Controls.h"
#include "Scene.h"
#include "CoreLayouts.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <string>
#else
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <CommonCrypto/CommonDigest.h>
#endif

namespace scbridge {
// Read-only view of a separately verified owner-installed engine. No engine
// storage is modified here; changes continue through ordinary MIDI/SysEx.
class CoreState {
public:
    bool attach(const char* path, void* library) {
        data=nullptr; parts=nullptr; drums=nullptr;
#ifdef _WIN32
        auto module=static_cast<HMODULE>(library);
        if(!GetProcAddress(module,"TG_initialize")) return false;
        if(verifiedLibrary!=library) {
            if(!recognisedFile(path)) return false;
            verifiedLibrary=library;
        }
        const auto* base=reinterpret_cast<const uint8_t*>(module);
        // Independently verified Windows layout; never apply Mach-O addresses.
        data=base+layout->dataRva;
        std::memcpy(&parts,base+layout->partsRva,sizeof(parts));
        std::memcpy(&drums,base+layout->drumsRva,sizeof(drums));
#else
        Dl_info image{};
        if(!dladdr(dlsym(library,"TG_initialize"),&image) || !image.dli_fname ||
           std::strcmp(image.dli_fname,path)) return false;
        if(verifiedLibrary!=library) {
            if(!recognisedFile(path)) return false;
            verifiedLibrary=library;
        }
        for(uint32_t i=0;i<_dyld_image_count();++i) {
            if(std::strcmp(_dyld_get_image_name(i),path)) continue;
            const auto* header=reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
            if(header->magic!=MH_MAGIC_64) return false;
            auto* command=reinterpret_cast<const load_command*>(header+1);
            const auto* end=reinterpret_cast<const uint8_t*>(command)+header->sizeofcmds;
            for(uint32_t n=0;n<header->ncmds;++n) {
                if(reinterpret_cast<const uint8_t*>(command)+sizeof(load_command)>end ||
                   command->cmdsize<sizeof(load_command) ||
                   reinterpret_cast<const uint8_t*>(command)+command->cmdsize>end) return false;
                if(command->cmd==LC_SEGMENT_64 && command->cmdsize>=sizeof(segment_command_64)) {
                    const auto* segment=reinterpret_cast<const segment_command_64*>(command);
                    if(!std::strcmp(segment->segname,"__DATA") && segment->vmsize==479232)
                        data=reinterpret_cast<const uint8_t*>(segment->vmaddr+_dyld_get_image_vmaddr_slide(i));
                }
                command=reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(command)+command->cmdsize);
            }
        }
        if(!data) return false;
        std::memcpy(&parts,data+0xb9b0,sizeof(parts));
        std::memcpy(&drums,data+0xb998,sizeof(drums));
#endif
        if(!readable(parts,0x4c00) || !readable(drums,0xa18)) {
            data=nullptr;parts=nullptr;drums=nullptr;return false;
        }
        return true;
    }
    int read(unsigned index, int part, int note, int setup, int insertionParameter=1) const {
        if(!data || index<legacyParameterCount || index>=parameterCount ||
           part<0 || part>15 || note<0 || note>127 || setup<0 || setup>1) return -1;
        const auto& definition=controls[index];
        int value=-1;
        switch(definition.scope) {
            case ControlScope::Global:
#ifdef _WIN32
                if(index>=24 && index<=26) {
                    constexpr ptrdiff_t masterOffsets[]{0,4,3};
                    value=data[layout->masterVolume+masterOffsets[index-24]];
                } else
#endif
                value=dataAt(definition.offset);break;
            case ControlScope::Part:
#ifdef _WIN32
                value=parts[layout->firstPart+gsPart(part)*layout->partStride+definition.offset];break;
#else
                value=parts[0x3dc+gsPart(part)*0x488+definition.offset];break;
#endif
            case ControlScope::Drum: value=drums[setup*0x50c+definition.offset+note];break;
            case ControlScope::Selector: return index==drumNoteIndex ? note : index==drumSetupIndex ? setup : insertionParameter;
            case ControlScope::InsertionType:
                value=insertionTypeIndexForCode(dataAt(insertionTypeOffset+1),dataAt(insertionTypeOffset));break;
            case ControlScope::InsertionParameter:
                if(insertionParameter<1 || insertionParameter>static_cast<int>(insertionParameterCount)) return -1;
                value=dataAt(insertionFirstParameterOffset+insertionParameter-1);break;
            default: return -1;
        }
        return value>=definition.minimum && value<=definition.maximum ? value : -1;
    }
    bool capture(EngineScene& scene) const {
        for(int part=0;part<16;++part) for(unsigned i=16;i<24;++i) {
            const int value=read(i,part,36,0);if(value<0) return false;
            scene.parts[part][i-16]=static_cast<uint8_t>(value);
        }
        for(unsigned i=24;i<58;++i) {
            const int value=read(i,0,36,0);if(value<0) return false;
            scene.globals[i-24]=static_cast<uint8_t>(value);
        }
        for(int setup=0;setup<2;++setup) for(int field=0;field<6;++field) for(int note=0;note<128;++note) {
            const int value=read(field+60,0,note,setup);if(value<0) return false;
            scene.drums[setup][field][note]=static_cast<uint8_t>(value);
        }
        auto& insertion=scene.insertion;
        const int type=read(insertionTypeIndex,0,36,0);
        if(type<0) return false;
        insertion.type=static_cast<uint8_t>(type);
        for(unsigned i=0;i<insertionParameterCount;++i) insertion.parameters[i]=dataAt(insertionFirstParameterOffset+i);
        insertion.toReverb=dataAt(0xb940);insertion.toChorus=dataAt(0xb941);insertion.toDelay=dataAt(0xb942);insertion.eq=dataAt(0xb946);
        insertion.controlSource1=dataAt(0xb948);insertion.controlDepth1=dataAt(0xb949);
        insertion.controlSource2=dataAt(0xb94a);insertion.controlDepth2=dataAt(0xb94b);
        insertion.valid=1;
        return validInsertionState(insertion);
    }
private:
    uint8_t dataAt(ptrdiff_t offset) const {
#ifdef _WIN32
        if(offset>=insertionTypeOffset && offset<=0xb94b) offset+=layout->insertionDelta;
#endif
        return data[offset];
    }
    static bool readable(const void* pointer,size_t required) {
        if(!pointer) return false;
#ifdef _WIN32
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(pointer,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) return false;
        const auto begin=reinterpret_cast<uintptr_t>(info.BaseAddress),address=reinterpret_cast<uintptr_t>(pointer);
        const auto protection=info.Protect&0xff;
        return address>=begin && address-begin<=info.RegionSize && required<=info.RegionSize-(address-begin) &&
            (protection==PAGE_READONLY || protection==PAGE_READWRITE || protection==PAGE_WRITECOPY ||
             protection==PAGE_EXECUTE_READ || protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY);
#else
        mach_vm_address_t start=reinterpret_cast<uintptr_t>(pointer);
        const auto address=start;
        mach_vm_size_t length=0; vm_region_basic_info_data_64_t info{};
        mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=MACH_PORT_NULL;
        const auto result=mach_vm_region(mach_task_self(),&start,&length,VM_REGION_BASIC_INFO_64,
                                         reinterpret_cast<vm_region_info_t>(&info),&count,&object);
        if(object!=MACH_PORT_NULL) mach_port_deallocate(mach_task_self(),object);
        return result==KERN_SUCCESS && address>=start && address-start<=length &&
               required<=length-(address-start) && (info.protection&VM_PROT_READ);
#endif
    }
    bool recognisedFile(const char* path) {
        // Internal layouts are never guessed for a different vendor build.
        // File I/O and validation happen only during helper initialisation.
#ifdef _WIN32
#ifdef SCBRIDGE_VERIFIED_TRANSLATED_CORE
        // Only the private translated guest uses this path. Its native parent
        // hashes the actual DLL before starting it and supplies the identity.
        char verified[65]{};
        if(GetEnvironmentVariableA("SCBRIDGE_VERIFIED_CORE_SHA256",verified,sizeof(verified))!=64) return false;
        layout=windowsCoreLayout(verified,32);
        return layout!=nullptr;
#else
        const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,nullptr,0);
        if(count<=0) return false;
        std::wstring wide(static_cast<size_t>(count),L'\0');
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide.data(),count);
        auto* file=_wfopen(wide.c_str(),L"rb");if(!file) return false;
        BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) { std::fclose(file);return false; }
        if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0) { BCryptCloseAlgorithmProvider(algorithm,0);std::fclose(file);return false; }
        unsigned char buffer[65536],digest[32];size_t total=0,countRead=0;bool ok=true;
        while((countRead=std::fread(buffer,1,sizeof(buffer),file))) {
            total+=countRead;
            if(total>32*1024*1024 || BCryptHashData(hash,buffer,static_cast<ULONG>(countRead),0)<0) { ok=false;break; }
        }
        ok=ok && !std::ferror(file);std::fclose(file);
        if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0) ok=false;
        BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
        if(!ok) return false;
        char hex[65]{};for(size_t i=0;i<sizeof(digest);++i) std::snprintf(hex+i*2,3,"%02x",digest[i]);
        layout=windowsCoreLayout(hex,sizeof(void*)*8);
        return layout!=nullptr;
#endif
#else
        auto* file=std::fopen(path,"rb");if(!file) return false;
        CC_SHA256_CTX context;CC_SHA256_Init(&context);
        unsigned char buffer[65536],digest[CC_SHA256_DIGEST_LENGTH];
        size_t total=0,count;
        while((count=std::fread(buffer,1,sizeof(buffer),file))) {
            total+=count;
            if(total>32*1024*1024) { std::fclose(file);return false; }
            CC_SHA256_Update(&context,buffer,static_cast<CC_LONG>(count));
        }
        const bool ok=!std::ferror(file);std::fclose(file);if(!ok) return false;
        CC_SHA256_Final(digest,&context);
        char hex[65]{};for(size_t i=0;i<sizeof(digest);++i) std::snprintf(hex+i*2,3,"%02x",digest[i]);
        return !std::strcmp(hex,"bcdc48bfafb3777f5eb3da7077f9ee791e0e2c0e5dbf9b03aa607b2d5e4c2701");
#endif
    }
    const uint8_t* data=nullptr;
    const uint8_t* parts=nullptr;
    const uint8_t* drums=nullptr;
    void* verifiedLibrary=nullptr;
#ifdef _WIN32
    const WindowsCoreLayout* layout=nullptr;
#endif
};
}
