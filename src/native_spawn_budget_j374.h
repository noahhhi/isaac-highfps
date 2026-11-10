#pragma once
// J374 optional attack work admission, BEFORE allocation/neighbor search.
// Denial intentionally reduces optional player attack damage under overload.
// No factory result is replaced; parent Update/collision and native link
// maintenance run. A denied re-link may let native cleanup remove an orphan beam.
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace highfps::spawn_budget19715 {
enum class Kind : unsigned { ParasitePair=0, TechZeroSearch=1, QuadSplit=2, TrisagionCreate=3 };
// Tick identity must be reset by the host on a new game/restore epoch. Sampling
// rotates with world tick and InitSeed without consuming the game's RNG.
class WorldQuota {
    std::uint64_t stamp_=0;bool valid_=false;
    unsigned remaining_[3]{},divisor_=1;
public:
    void Reset(){valid_=false;}
    void Begin(std::uint64_t stamp,unsigned children,unsigned searches,unsigned divisor=1,unsigned trisagionCreates=0xFFFFFFFFu){
        // Fifth argument is a separate creation bucket. Legacy callers retain
        // unlimited Trisagion tokens until explicitly supplying this limit.
        // The common rotating divisor still applies to Trisagion attempts.
        if(valid_&&stamp_==stamp)return;
        valid_=true;stamp_=stamp;remaining_[0]=children;remaining_[1]=searches;remaining_[2]=trisagionCreates;divisor_=divisor?divisor:1;
    }
    bool Admit(Kind kind,unsigned seed,unsigned cost){
        const unsigned raw=static_cast<unsigned>(kind);if(!valid_||raw>3||!cost)return true;
        const unsigned k=kind==Kind::TechZeroSearch?1:(kind==Kind::TrisagionCreate?2:0);
        // A deterministic rotating eligibility window prevents the same early
        // entities from monopolizing all admissions on every world update.
        unsigned h=seed*0x9e3779b9u;h^=h>>16;
        if(((h+static_cast<unsigned>(stamp_))%divisor_)!=0||cost>remaining_[k])return false;
        remaining_[k]-=cost;return true;
    }
};
#if defined(_WIN32) && (defined(_M_IX86)||defined(__i386__))
}
#include <windows.h>
namespace highfps::spawn_budget19715 {
struct Host {
    std::uintptr_t(*address)(std::uintptr_t)=nullptr;
    bool(*poke)(std::uintptr_t,const std::uint8_t*,std::size_t)=nullptr;
    bool(*admit)(Kind,void*tear,unsigned cost)=nullptr;
    DWORD gameThread=0;bool enabled=false;
};
struct Stats {std::uint64_t admitted[4]{},denied[4]{},unsupported=0,reentry=0,exceptions=0;};
// Process-lifetime allocation, INCLUDING failed Install/rollback: a hook whose
// restore was rejected may remain reachable, in transparent disabled mode.
class Adapter {
    friend struct TestAccess;
    inline static Adapter*active_=nullptr;
    Host host_;Stats stats_{};bool enabled_=false,inside_=false,patched_[4]{};
    std::uint8_t saved_[4][8]{};void*stubs_[4]{};
    inline static constexpr std::uintptr_t sites_[4]={0x6765CD,0x667648,0x668F97,0x667BED};
    inline static constexpr std::uintptr_t deny_[4]={0x676947,0x66797D,0x6693A3,0x667EE4};
    inline static constexpr unsigned lengths_[4]={8,8,6,6};
    bool PlayerOwned(void*self,Kind kind)const{
        const auto*p=static_cast<const std::uint8_t*>(self);
        if(!p||*reinterpret_cast<const std::uintptr_t*>(p)!=host_.address(0xB3C128)||
           *reinterpret_cast<const unsigned*>(p+0x28)!=2)return false;
        const auto flags=*reinterpret_cast<const std::uint64_t*>(p+0x428);
        if(!(flags&(std::uint64_t(1)<<(kind==Kind::ParasitePair?6:(kind==Kind::QuadSplit?18:(kind==Kind::TrisagionCreate?60:57))))))return false;
        for(unsigned n=0;n<16;++n){
            p=*reinterpret_cast<const std::uint8_t*const*>(p+0x3C8);
            if(!p)return false;
            if(*reinterpret_cast<const unsigned*>(p+0x28)==1)return true;
        }
        return false; // Unknown/cyclic chains preserve native work.
    }
    static unsigned __cdecl Decide(void*self,unsigned which){
        auto*r=active_;if(!r||!r->enabled_||GetCurrentThreadId()!=r->host_.gameThread||which>3)return 1;
        if(r->inside_){++r->stats_.reentry;return 1;}
        r->inside_=true;bool allow=true;
        __try {
            if(!r->PlayerOwned(self,static_cast<Kind>(which)))++r->stats_.unsupported;
            else {allow=r->host_.admit(static_cast<Kind>(which),self,which==0?2:(which==2?4:1));
                if(allow)++r->stats_.admitted[which];else ++r->stats_.denied[which];}
        } __except(EXCEPTION_EXECUTE_HANDLER){++r->stats_.exceptions;allow=true;}
        r->inside_=false;return allow?1:0;
    }
    static void Jump(std::uint8_t*p,std::uintptr_t to,std::uintptr_t from){p[0]=0xE9;const auto d=std::uint32_t(to-(from+5));std::memcpy(p+1,&d,4);}
    bool Bytes(std::uintptr_t at,const std::uint8_t*p,std::size_t n)const{
        __try{return std::memcmp(reinterpret_cast<const void*>(host_.address(at)),p,n)==0;}
        __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool LoadGuard(std::uintptr_t at,unsigned xmm,std::uintptr_t constant)const{
        std::uint8_t bytes[]={0xF3,0x0F,0x10,std::uint8_t(5+xmm*8),0,0,0,0};
        const auto v=std::uint32_t(host_.address(constant));std::memcpy(bytes+4,&v,4);return Bytes(at,bytes,8);
    }
    void*MakeStub(unsigned k){
        auto*p=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,256,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!p)return nullptr;
        unsigned n=0;auto put=[&](std::uint8_t v){p[n++]=v;};auto word=[&](std::uint32_t v){std::memcpy(p+n,&v,4);n+=4;};
        put(0x9C);put(0x60); // pushfd; pushad
        put(0x8B);put(0xDC); // mov ebx,esp (callee-saved recovery pointer)
        put(0x81);put(0xEC);word(0x210);put(0x83);put(0xE4);put(0xF0); // aligned FXSAVE scratch
        put(0x0F);put(0xAE);put(0x04);put(0x24); // fxsave [esp]
        put(0x6A);put(static_cast<std::uint8_t>(k));put(0x57); // push kind,self(EDI)
        put(0xB8);word(reinterpret_cast<std::uint32_t>(&Decide));put(0xFF);put(0xD0);
        put(0x83);put(0xC4);put(8);put(0x85);put(0xC0);put(0x74);const unsigned conditional=n++; // jz denied
        auto restore=[&](){put(0x0F);put(0xAE);put(0x0C);put(0x24);put(0x8B);put(0xE3);put(0x61);put(0x9D);};
        restore();std::memcpy(p+n,saved_[k],lengths_[k]);n+=lengths_[k];Jump(p+n,host_.address(sites_[k])+lengths_[k],reinterpret_cast<std::uintptr_t>(p+n));n+=5;
        p[conditional]=static_cast<std::uint8_t>(n-conditional-1);
        restore();Jump(p+n,host_.address(deny_[k]),reinterpret_cast<std::uintptr_t>(p+n));n+=5;
        FlushInstructionCache(GetCurrentProcess(),p,n);return p;
    }
public:
    explicit Adapter(Host h):host_(h){}
    bool Install(){
        if(!host_.enabled||!host_.address||!host_.poke||!host_.admit||GetCurrentThreadId()!=host_.gameThread||active_)return false;
        // Exact native decision, continuation, allocation and child-update sites.
        // The existing +490 laser branch bypasses this gate; do not use the
        // factory-failure landing667DDD, which permanently clears tear flags.
        const std::uint8_t trisGate[]={0x8B,0x85,0x58,0xF7,0xFF,0xFF};
        const std::uint8_t trisExisting[]={0x8B,0x87,0x90,4,0,0,0x85,0xC0,0x0F,0x85,0x25,2,0,0};
        const std::uint8_t trisLanding[]={0x8D,0xB7,0x28,4,0,0,0xF3,0x0F,0x10,0x87,0x14,4,0,0};
        const std::uint8_t trisSpawn[]={0x56,0x6A,3,0xE8,0x83,0xEC,0x2F,0};
        const std::uint8_t quadGate[]={0x8D,0x85,0x80,0xF6,0xFF,0xFF};
        const std::uint8_t quadAbsent[]={0x84,0xC0,0x0F,0x84,0x0C,4,0,0};
        const std::uint8_t quadSpawn[]={0xFF,0x77,0x28,0xE8,0x68,0xB5,0xDB,0xFF};
        const std::uint8_t quadTail[]={0x83,0xF8,4,0x0F,0x8C,0x73,0xFC,0xFF,0xFF,0x8D,0xB7,0x28,4,0,0};
        std::uint8_t quadMask[]={0x68,0,0,0,0,0x8B,0xD6};
        std::uint8_t quadNext[]={0x68,0,0,0,0,0x8B,0xD6};
        auto maskAddress=std::uint32_t(host_.address(0xBD2310)),nextAddress=std::uint32_t(host_.address(0xBD23F0));
        std::memcpy(quadMask+1,&maskAddress,4);std::memcpy(quadNext+1,&nextAddress,4);
        const std::uint8_t parasiteFlagsPointer[]={0x8D,0xB7,0x28,4,0,0};
        const std::uint8_t techLocals[]={0x83,0xBF,0x8C,4,0,0,0,0x8D,0xB7,0x8C,4,0,0,0xC6,0x85,0x6F,0xF7,0xFF,0xFF,0,0x89,0xB5,0x78,0xF7,0xFF,0xFF};
        const std::uint8_t parasiteCheck[]={0x0F,0x84,0xA6,0x03,0,0};
        const std::uint8_t parasiteSpawn[]={0x6A,0,0x6A,2,0xE8,0xB9,0xDF,0xDA,0xFF};
        const std::uint8_t childUpdate[]={0x8B,0x06,0x8B,0xCE,0xFF,0x50,0x0C};
        const std::uint8_t techBranch[]={0x0F,0x85,0x35,0x03,0,0};
        const std::uint8_t techContinue[]={0x83,0xBF,0x88,4,0,0,0,0x8D,0x87,0x88,4,0,0};
        const std::uint8_t techSpawn[]={0x6A,0x0A,0xE8,0x86,0xF0,0x2F,0};
        if(!Bytes(0x667BED,trisGate,sizeof trisGate)||!Bytes(0x667BDF,trisExisting,sizeof trisExisting)||
           !Bytes(0x667EE4,trisLanding,sizeof trisLanding)||!Bytes(0x667C55,trisSpawn,sizeof trisSpawn)||
           !Bytes(0x668F97,quadGate,sizeof quadGate)||!Bytes(0x668F8F,quadAbsent,sizeof quadAbsent)||
           !Bytes(0x6690E0,quadSpawn,sizeof quadSpawn)||!Bytes(0x669394,quadTail,sizeof quadTail)||
           !Bytes(0x668F73,quadMask,sizeof quadMask)||!Bytes(0x6693A3,quadNext,sizeof quadNext)||
           !LoadGuard(0x6765CD,2,0xB692B0)||!LoadGuard(0x676947,0,0xB67578)||!LoadGuard(0x667648,0,0xB6745C)||
           !Bytes(0x676574,parasiteFlagsPointer,sizeof parasiteFlagsPointer)||!Bytes(0x667628,techLocals,sizeof techLocals)||
           !Bytes(0x67659B,parasiteCheck,sizeof parasiteCheck)||!Bytes(0x67668E,parasiteSpawn,sizeof parasiteSpawn)||
           !Bytes(0x676922,childUpdate,sizeof childUpdate)||!Bytes(0x667642,techBranch,sizeof techBranch)||
           !Bytes(0x66797D,techContinue,sizeof techContinue)||!Bytes(0x667853,techSpawn,sizeof techSpawn))return false;
        for(unsigned k=0;k<4;++k){std::memcpy(saved_[k],reinterpret_cast<void*>(host_.address(sites_[k])),lengths_[k]);stubs_[k]=MakeStub(k);if(!stubs_[k])return false;}
        active_=this;
        for(unsigned k=0;k<4;++k){std::uint8_t patch[8]={0,0,0,0,0,0x90,0x90,0x90};Jump(patch,reinterpret_cast<std::uintptr_t>(stubs_[k]),host_.address(sites_[k]));
            // Poke contract: false means no bytes changed. Keep reachable code
            // alive if restoration subsequently fails.
            if(!host_.poke(sites_[k],patch,lengths_[k])){Shutdown();return false;}patched_[k]=true;
        }
        enabled_=true;return true;
    }
    void Disable(){enabled_=false;}
    void Shutdown(){Disable();for(unsigned k=0;k<4;++k)if(patched_[k]&&host_.poke(sites_[k],saved_[k],lengths_[k]))patched_[k]=false;}
    Stats GetStats()const{return stats_;}
};
#endif
}
