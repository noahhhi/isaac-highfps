#pragma once
// Optional J374 player-tear cosmetic poof admission before RNG/factory work.
// Denial omits the complete cosmetic entity, including its Lua init/update/render
// events. It never returns null to an unchecked factory consumer.
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace highfps::poof_spawn19715 {
class WorldQuota {
    std::uint64_t stamp_=0;unsigned remaining_=0;bool valid_=false;
public:
    void Reset(){valid_=false;}
    void Begin(std::uint64_t stamp,unsigned count){if(valid_&&stamp_==stamp)return;stamp_=stamp;remaining_=count;valid_=true;}
    bool Admit(){if(!valid_)return true;if(!remaining_)return false;--remaining_;return true;}
};
#if defined(_WIN32) && (defined(_M_IX86)||defined(__i386__))
}
#include <windows.h>
namespace highfps::poof_spawn19715 {
struct Host {
    std::uintptr_t(*address)(std::uintptr_t)=nullptr;
    bool(*poke)(std::uintptr_t,const std::uint8_t*,std::size_t)=nullptr;
    // MUST admit outside the budgeted actual world update/overload scope.
    // Must not call game code, mutate entities or consume gameplay RNG.
    bool(*admit)(void*tear,unsigned variant,unsigned subtype)=nullptr;
    DWORD gameThread=0;bool enabled=false;
    // Optional process-lifetime flag, read only on gameThread. Disabled UI
    // controls bypass entity/frame ownership reads without repatching code.
    const bool*activeFlag=nullptr;
};
struct Stats {std::uint64_t admitted[4]{},denied[4]{},unsupported=0,reentry=0,exceptions=0;};
class Adapter {
    friend struct TestAccess;
    inline static Adapter*active_=nullptr;
    static constexpr std::uintptr_t Site=0x669413,Continue=0x66941A,Deny=0x669AAD;
    Host host_;Stats stats_{};bool enabled_=false,inside_=false,patched_=false;
    std::uintptr_t tearVtable_=0;std::uint8_t saved_[7]{};void*stub_=nullptr;
    static unsigned VariantIndex(unsigned variant){switch(variant){case 11:return 0;case 12:return 1;case 13:return 2;case 79:return 3;default:return 4;}}
    bool PlayerOwned(void*self)const {
        auto*p=static_cast<const std::uint8_t*>(self);
        if(!p||*reinterpret_cast<const std::uintptr_t*>(p)!=tearVtable_||*reinterpret_cast<const unsigned*>(p+0x28)!=2)return false;
        for(unsigned i=0;i<16;++i){p=*reinterpret_cast<const std::uint8_t*const*>(p+0x3c8);if(!p)return false;if(*reinterpret_cast<const unsigned*>(p+0x28)==1)return true;}
        return false;
    }
    // A separate call keeps the native-frame read within the SEH region even
    // with clang-cl /O2's asynchronous-fault optimization of inline loads.
    static __declspec(noinline) unsigned FrameValue(const void*frame,std::ptrdiff_t offset){
        return *reinterpret_cast<const volatile unsigned*>(static_cast<const std::uint8_t*>(frame)+offset);
    }
    static unsigned __cdecl Decide(void*self,const void*frame){
        auto*r=active_;if(!r||!r->enabled_||GetCurrentThreadId()!=r->host_.gameThread)return 1;
        if(r->host_.activeFlag&&!*r->host_.activeFlag)return 1;
        if(r->inside_){++r->stats_.reentry;return 1;}
        r->inside_=true;bool allow=true;
        __try {
            const unsigned variant=FrameValue(frame,-0x89c),subtype=FrameValue(frame,-0x874),index=VariantIndex(variant);
            if(index==4||subtype||!r->PlayerOwned(self))++r->stats_.unsupported;
            else {allow=r->host_.admit(self,variant,subtype);if(allow)++r->stats_.admitted[index];else ++r->stats_.denied[index];}
        }__except(EXCEPTION_EXECUTE_HANDLER){++r->stats_.exceptions;allow=true;}
        r->inside_=false;return allow?1:0;
    }
    static void Jump(std::uint8_t*p,std::uintptr_t target,std::uintptr_t from){p[0]=0xe9;const auto delta=std::uint32_t(target-(from+5));std::memcpy(p+1,&delta,4);}
    bool Bytes(std::uintptr_t va,const std::uint8_t*p,std::size_t n)const {
        __try{return std::memcmp(reinterpret_cast<const void*>(host_.address(va)),p,n)==0;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Validate(){
        const std::uint8_t gate[]={0x83,0xbd,0x64,0xf7,0xff,0xff,0};
        const std::uint8_t branch[]={0x0f,0x86,0x8d,6,0,0};
        const std::uint8_t rng[]={0xe8,0x4b,0x6f,8,0};
        const std::uint8_t variant[]={0xff,0xb5,0x64,0xf7,0xff,0xff,0x68,0xe8,3,0,0,0xe8,0xf3,0xb1,0xdb,0xff};
        const std::uint8_t subtype[]={0xff,0xb5,0x8c,0xf7,0xff,0xff,0x8d,0x87,0x3c,3,0,0,0x57};
        const std::uint8_t cleanup[]={0x8d,0xb7,0x28,4,0,0,0x8b,0x47,0x2c,0x85,0xc0};
        const std::uint8_t after[]={0x8b,0x47,0x2c,0x85,0xc0,0x74,0x31,0x83,0xf8,0x18};
        return Bytes(Site,gate,sizeof gate)&&Bytes(Continue,branch,sizeof branch)&&Bytes(0x669420,rng,sizeof rng)&&
            Bytes(0x66944d,variant,sizeof variant)&&Bytes(0x66943a,subtype,sizeof subtype)&&Bytes(0x669aa7,cleanup,sizeof cleanup)&&Bytes(Deny,after,sizeof after);
    }
    void*MakeStub(){
        auto*p=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,192,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!p)return nullptr;
        unsigned n=0;auto byte=[&](std::uint8_t v){p[n++]=v;};auto word=[&](std::uint32_t v){std::memcpy(p+n,&v,4);n+=4;};
        byte(0x9c);byte(0x60);byte(0x8b);byte(0xdc); // pushfd/pushad; EBX=recovery ESP
        byte(0x81);byte(0xec);word(0x210);byte(0x83);byte(0xe4);byte(0xf0);
        byte(0x0f);byte(0xae);byte(0x04);byte(0x24); // FXSAVE, aligned512-byte area
        byte(0x55);byte(0x57); // original EBP(frame), EDI(tear)
        byte(0xb8);word(reinterpret_cast<std::uint32_t>(&Decide));byte(0xff);byte(0xd0);
        byte(0x83);byte(0xc4);byte(8);byte(0x85);byte(0xc0);byte(0x74);const unsigned conditional=n++;
        auto restore=[&](){byte(0x0f);byte(0xae);byte(0x0c);byte(0x24);byte(0x8b);byte(0xe3);byte(0x61);byte(0x9d);};
        restore();std::memcpy(p+n,saved_,7);n+=7;Jump(p+n,host_.address(Continue),reinterpret_cast<std::uintptr_t>(p+n));n+=5;
        p[conditional]=std::uint8_t(n-conditional-1);restore();Jump(p+n,host_.address(Deny),reinterpret_cast<std::uintptr_t>(p+n));n+=5;
        FlushInstructionCache(GetCurrentProcess(),p,n);return p;
    }
public:
    // Keep process-lifetime ownership, including a failed rollback. Poke false
    // means no write; generated code remains allocated while potentially reachable.
    explicit Adapter(Host host):host_(host){}
    bool Install(){
        if(active_||!host_.enabled||!host_.address||!host_.poke||!host_.admit||GetCurrentThreadId()!=host_.gameThread||!Validate())return false;
        tearVtable_=host_.address(0xB3C128);std::memcpy(saved_,reinterpret_cast<void*>(host_.address(Site)),7);stub_=MakeStub();if(!stub_)return false;
        active_=this;std::uint8_t patch[7]={0,0,0,0,0,0x90,0x90};Jump(patch,reinterpret_cast<std::uintptr_t>(stub_),host_.address(Site));
        if(!host_.poke(Site,patch,7))return false;patched_=enabled_=true;return true;
    }
    void Disable(){enabled_=false;}
    void Shutdown(){Disable();if(patched_&&host_.poke(Site,saved_,7))patched_=false;}
    Stats GetStats()const{return stats_;}
};
#endif
}
