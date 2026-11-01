#pragma once
#include "Controls.h"
#include "Scene.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <CommonCrypto/CommonDigest.h>

namespace scbridge {
// Read-only view of a separately verified owner-installed engine. No engine
// storage is modified here; changes continue through ordinary MIDI/SysEx.
class CoreState {
public:
    bool attach(const char* path, void* library) {
        data=nullptr; parts=nullptr; drums=nullptr;
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
        if(!readable(parts,0x4c00) || !readable(drums,0xa18)) {
            data=nullptr;parts=nullptr;drums=nullptr;return false;
        }
        return true;
    }
    int read(unsigned index, int part, int note, int setup) const {
        if(!data || index<legacyParameterCount || index>=parameterCount ||
           part<0 || part>15 || note<0 || note>127 || setup<0 || setup>1) return -1;
        const auto& definition=controls[index];
        int value=-1;
        switch(definition.scope) {
            case ControlScope::Global: value=data[definition.offset];break;
            case ControlScope::Part: value=parts[0x3dc+gsPart(part)*0x488+definition.offset];break;
            case ControlScope::Drum: value=drums[setup*0x50c+definition.offset+note];break;
            case ControlScope::Selector: return index==drumNoteIndex ? note : setup;
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
        return true;
    }
private:
    static bool readable(const void* pointer,size_t required) {
        if(!pointer) return false;
        mach_vm_address_t start=reinterpret_cast<uintptr_t>(pointer);
        const auto address=start;
        mach_vm_size_t length=0; vm_region_basic_info_data_64_t info{};
        mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=MACH_PORT_NULL;
        const auto result=mach_vm_region(mach_task_self(),&start,&length,VM_REGION_BASIC_INFO_64,
                                         reinterpret_cast<vm_region_info_t>(&info),&count,&object);
        if(object!=MACH_PORT_NULL) mach_port_deallocate(mach_task_self(),object);
        return result==KERN_SUCCESS && address>=start && address-start<=length &&
               required<=length-(address-start) && (info.protection&VM_PROT_READ);
    }
    static bool recognisedFile(const char* path) {
        // Internal layouts are never guessed for a different vendor build.
        // File I/O and validation happen only during helper initialisation.
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
    }
    const uint8_t* data=nullptr;
    const uint8_t* parts=nullptr;
    const uint8_t* drums=nullptr;
    void* verifiedLibrary=nullptr;
};
}
