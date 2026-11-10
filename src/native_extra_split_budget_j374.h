#pragma once
// Optional J374 admission for Bone pairs and plain Type2 burst children.
// Weapon-specific Haemolacria branches and Lachryphagy absorption stay native.
#include <cstdint>
#include <cstddef>
#include <cstring>
#if defined(_WIN32) && (defined(_M_IX86)||defined(__i386__))
#include <windows.h>
namespace highfps::extra_split19715 {
enum class Kind:unsigned {BonePair=0,LachryChild=1,HaemoChild=2};
struct Host {
    std::uintptr_t(*address)(std::uintptr_t)=nullptr;
    bool(*poke)(std::uintptr_t,const std::uint8_t*,std::size_t)=nullptr;
    // Shared quota owner. Admit outside the authorized overloaded world Update.
    // Must not call game code, mutate entities or use gameplay RNG.
    bool(*admit)(Kind,void*tear,unsigned requestedChildren)=nullptr;
    DWORD gameThread=0;bool enabled=false;
};
struct Stats {std::uint64_t admitted[3]{},denied[3]{},unsupported=0,reentry=0,exceptions=0;};
class Adapter {
    friend struct TestAccess;
    inline static Adapter*active_=nullptr;
    inline static constexpr std::uintptr_t sites_[3]={0x676984,0x66E5FA,0x6780DC};
    inline static constexpr std::uintptr_t deny_[3]={0x676D0C,0x66E858,0x6783AC};
    inline static constexpr unsigned lengths_[3]={7,6,6},bits_[3]={51,59,62};
    Host host_;Stats stats_{};bool enabled_=false,inside_=false,patched_[3]{};
    std::uintptr_t tearVtable_=0,haemoReturn_=0;std::uint8_t saved_[3][8]{};void*stubs_[3]{};
    static __declspec(noinline) unsigned NativeWord(const void*p,std::ptrdiff_t offset){return *reinterpret_cast<const volatile unsigned*>(static_cast<const std::uint8_t*>(p)+offset);}
    bool PlayerOwned(void*self,unsigned kind)const {
        auto*p=static_cast<const std::uint8_t*>(self);
        if(!p||*reinterpret_cast<const std::uintptr_t*>(p)!=tearVtable_||*reinterpret_cast<const unsigned*>(p+0x28)!=2||
           !(*reinterpret_cast<const std::uint64_t*>(p+0x428)&(std::uint64_t(1)<<bits_[kind])))return false;
        for(unsigned i=0;i<16;++i){p=*reinterpret_cast<const std::uint8_t*const*>(p+0x3c8);if(!p)return false;if(*reinterpret_cast<const unsigned*>(p+0x28)==1)return true;}
        return false;
    }
    static unsigned __cdecl Decide(void*self,unsigned kind,const void*frame,const void*stack){
        auto*r=active_;if(!r||!r->enabled_||GetCurrentThreadId()!=r->host_.gameThread||kind>2)return 1;
        if(r->inside_){++r->stats_.reentry;return 1;}r->inside_=true;bool allow=true;
        __try {
            bool valid=r->PlayerOwned(self,kind);
            if(valid&&kind==1){const auto count=NativeWord(frame,-0x884),index=NativeWord(frame,-0x88c);valid=count>=3&&count<=18&&index<count;}
            if(valid&&kind==2){const auto left=NativeWord(stack,0x14);valid=NativeWord(frame,4)==r->haemoReturn_&&left>=1&&left<=11;}
            if(!valid)++r->stats_.unsupported;
            else {allow=r->host_.admit(static_cast<Kind>(kind),self,kind==0?2u:1u);if(allow)++r->stats_.admitted[kind];else ++r->stats_.denied[kind];}
        }__except(EXCEPTION_EXECUTE_HANDLER){++r->stats_.exceptions;allow=true;}
        r->inside_=false;return allow?1:0;
    }
    static void Jump(std::uint8_t*p,std::uintptr_t target,std::uintptr_t from){p[0]=0xe9;const auto d=std::uint32_t(target-(from+5));std::memcpy(p+1,&d,4);}
    bool Bytes(std::uintptr_t va,const std::uint8_t*p,std::size_t n)const {
        __try{return std::memcmp(reinterpret_cast<const void*>(host_.address(va)),p,n)==0;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    }
    bool Validate();
    void*MakeStub(unsigned k){
        auto*p=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,192,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!p)return nullptr;
        unsigned n=0;auto byte=[&](std::uint8_t v){p[n++]=v;};auto word=[&](std::uint32_t v){std::memcpy(p+n,&v,4);n+=4;};
        byte(0x9c);byte(0x60);byte(0x8b);byte(0xdc); // pushfd/pushad; EBX=recovery ESP
        byte(0x81);byte(0xec);word(0x210);byte(0x83);byte(0xe4);byte(0xf0);
        byte(0x0f);byte(0xae);byte(0x04);byte(0x24); // aligned FXSAVE
        byte(0x8d);byte(0x43);byte(0x24);byte(0x50); // original ESP = saved-register block+36
        byte(0x55);byte(0x6a);byte(std::uint8_t(k));byte(0x57); // frame,kind,EDI tear
        byte(0xb8);word(reinterpret_cast<std::uint32_t>(&Decide));byte(0xff);byte(0xd0);
        byte(0x83);byte(0xc4);byte(16);byte(0x85);byte(0xc0);byte(0x74);const unsigned conditional=n++;
        auto restore=[&](){byte(0x0f);byte(0xae);byte(0x0c);byte(0x24);byte(0x8b);byte(0xe3);byte(0x61);byte(0x9d);};
        restore();std::memcpy(p+n,saved_[k],lengths_[k]);n+=lengths_[k];Jump(p+n,host_.address(sites_[k])+lengths_[k],reinterpret_cast<std::uintptr_t>(p+n));n+=5;
        p[conditional]=std::uint8_t(n-conditional-1);restore();Jump(p+n,host_.address(deny_[k]),reinterpret_cast<std::uintptr_t>(p+n));n+=5;
        FlushInstructionCache(GetCurrentProcess(),p,n);return p;
    }
public:
    // Process lifetime, including failed rollback; never free reachable stubs.
    explicit Adapter(Host host):host_(host){}
    bool Install(){
        if(active_||!host_.enabled||!host_.address||!host_.poke||!host_.admit||GetCurrentThreadId()!=host_.gameThread||!Validate())return false;
        tearVtable_=host_.address(0xB3C128);haemoReturn_=host_.address(0x6693D0);
        for(unsigned i=0;i<3;++i){std::memcpy(saved_[i],reinterpret_cast<void*>(host_.address(sites_[i])),lengths_[i]);stubs_[i]=MakeStub(i);if(!stubs_[i])return false;}
        active_=this;
        for(unsigned i=0;i<3;++i){std::uint8_t patch[8]={0,0,0,0,0,0x90,0x90,0x90};Jump(patch,reinterpret_cast<std::uintptr_t>(stubs_[i]),host_.address(sites_[i]));
            if(!host_.poke(sites_[i],patch,lengths_[i])){Shutdown();return false;}patched_[i]=true;}
        enabled_=true;return true;
    }
    void Disable(){enabled_=false;}
    void Shutdown(){Disable();for(unsigned i=0;i<3;++i)if(patched_[i]&&host_.poke(sites_[i],saved_[i],lengths_[i]))patched_[i]=false;}
    Stats GetStats()const{return stats_;}
};

inline bool Adapter::Validate(){
    {const std::uint8_t expected[]={0xc7,0x45,0x9c,0x00,0x00,0x00,0x00};if(!Bytes(0x676984,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x0f,0x84,0x95,0x03,0x00,0x00};if(!Bytes(0x676971,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x56,0x6a,0x02,0xe8,0xbb,0xdb,0xda,0xff};if(!Bytes(0x676A8D,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0xff,0x50,0x0c};if(!Bytes(0x676CF3,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x83,0xf8,0x02,0x0f,0x8c,0x8a,0xfc,0xff,0xff,0x8d,0xb7,0x28,0x04,0x00,0x00};if(!Bytes(0x676CFD,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x8d,0x85,0x04,0xe3,0xff,0xff};if(!Bytes(0x66E5FA,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x56,0x6a,0x02,0xe8,0x3c,0x5f,0xdb,0xff};if(!Bytes(0x66E70C,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x8b,0x85,0x74,0xf7,0xff,0xff,0x40,0x89,0x85,0x74,0xf7,0xff,0xff,0x3b,0x85,0x7c,0xf7,0xff,0xff,0x0f,0x8c,0x83,0xfd,0xff,0xff};if(!Bytes(0x66E858,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x8b,0x07,0x8b,0xcf,0x8b,0x40,0x28,0xff,0xd0};if(!Bytes(0x66E871,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0xe8,0x9b,0x82,0x07,0x00,0x8b,0xf0,0xe8,0x94,0x82,0x07,0x00};if(!Bytes(0x6780D0,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0xf3,0x0f,0x10,0x4c,0x24,0x1c};if(!Bytes(0x6780DC,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x6a,0x02,0xf3,0x0f,0x58,0xd8,0xf3,0x0f,0x11,0x54,0x24,0x78,0xf3,0x0f,0x11,0x5c,0x24,0x7c,0xe8,0x14,0x74,0x08,0x00};if(!Bytes(0x678185,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x8b,0x74,0x24,0x0c,0x83,0x6c,0x24,0x14,0x01,0x0f,0x85,0x2a,0xf6,0xff,0xff,0x5f,0x5e,0x8b,0xe5,0x5d,0xc2,0x04,0x00};if(!Bytes(0x6783AC,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0xff,0xb5,0x94,0xf7,0xff,0xff,0x8b,0xcf,0xe8,0x00,0xe5,0x00,0x00};if(!Bytes(0x6693C3,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,0x68,0x56,0x57,0x8b,0xf9};if(!Bytes(0x6778D0,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};if(!Bytes(0xBD2340,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};if(!Bytes(0xBD2250,expected,sizeof expected))return false;}
    {const std::uint8_t expected[]={0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};if(!Bytes(0xBD23F0,expected,sizeof expected))return false;}
    return true;
}
}
#endif
