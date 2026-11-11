#pragma once

// J374: fixed-tick menu scalars plus scoped portrait-fade presentation guard.
// No ANM2/Sprite::Update, SetFrame, Play, callback or world-animation gate.
// Include at GLOBAL scope after dllmain.cpp's Addr/Expect/Poke definitions. Install before
// entering the patched frame loop, then call NativeRenderFixedTick19715()
// immediately AFTER EACH actual Manager::Update (including catchup ticks).
// Never call it for an intermediate output frame or for a discarded deadline.

#include <stdint.h>
#include <stddef.h>

namespace highfps {
namespace native_render_clock {

inline float StepToward(float value, float target, float step) {
    if (value < target) {
        value += step;
        return value < target ? value : target;
    }
    if (value > target) {
        value -= step;
        return value > target ? value : target;
    }
    return target;
}

// Vanilla MenuManager::Render 9811CE..981200: target .5 in character states
// 1/3, otherwise 0; step .025. One call represents exactly ONE native tick.
inline float VignetteTick(float value, int characterState) {
    return StepToward(value, characterState == 1 || characterState == 3
                                ? 0.5f : 0.0f, 0.025f);
}

// Vanilla MenuCharacter::Render 9431C7..94321E: +.05 in states 2/3,
// otherwise -.05. Clamp the original [0,1] visual range so low-output-rate
// catchup cannot overshoot; vanilla's float addition can exceed by ~1e-7.
inline float CharacterTick(float value, int characterState) {
    return StepToward(value, characterState == 2 || characterState == 3
                                ? 1.0f : 0.0f, 0.05f);
}

constexpr uintptr_t kPortraitSite = 0x00943FDA;
constexpr uintptr_t kPortraitResume = 0x00944030;
constexpr uintptr_t kPortraitRenderCall = 0x00942A96;
constexpr uintptr_t kPortraitFunction = 0x00943E20;
constexpr uintptr_t kStaticImageBase = 0x00400000;
constexpr size_t kPortraitStubBytes = 123;

constexpr uintptr_t kVignetteSite = 0x009811CE;
constexpr uintptr_t kVignetteResume = 0x00981200;
constexpr uintptr_t kCharacterSite = 0x009431C7;
constexpr uintptr_t kCharacterResume = 0x0094321E;

// Complete, decoded instruction blocks from the vanilla mapped J374 image.
// All branches originating inside the blocks are replaced. Validation helper
// native_render_clock_verify.py checks boundaries, branch entries and buffers.
static const uint8_t kVignetteOriginal[] = {
    0xf3,0x0f,0x10,0x56,0x34,0xf3,0x0f,0x10,0x25,0xf4,0x68,0xb6,0x00,
    0x0f,0x28,0xc2,0xf3,0x0f,0x5c,0xc1,0x0f,0x54,0x05,0x80,0x92,0xb6,0x00,
    0x0f,0x2f,0xe0,0x73,0x12,0x0f,0x2f,0xca,0x0f,0x28,0xca,0x76,0x06,
    0xf3,0x0f,0x58,0xcc,0xeb,0x04,0xf3,0x0f,0x5c,0xcc
};
static const uint8_t kCharacterOriginal[] = {
    0xf3,0x0f,0x10,0x4f,0x34,0xf3,0x0f,0x10,0x05,0xa4,0x6c,0xb6,0x00,
    0x0f,0x2f,0xc1,0x76,0x1f,0x8b,0x47,0x08,0x83,0xf8,0x02,0x74,0x05,
    0x83,0xf8,0x03,0x75,0x12,0xf3,0x0f,0x58,0x0d,0x28,0x69,0xb6,0x00,
    0xf3,0x0f,0x11,0x4f,0x34,0x0f,0x28,0xc1,0xeb,0x26,0x0f,0x2f,0x0d,
    0x3c,0x68,0xb6,0x00,0x0f,0x28,0xc1,0x76,0x1a,0x8b,0x47,0x08,
    0x83,0xf8,0x02,0x74,0x12,0x83,0xf8,0x03,0x74,0x0d,0xf3,0x0f,0x5c,
    0x05,0x28,0x69,0xb6,0x00,0xf3,0x0f,0x11,0x47,0x34
};
static const uint8_t kPortraitOriginal[] = {
    0xf3,0x0f,0x10,0x47,0x30,0x0f,0x57,0xd2,0xf3,0x0f,0x10,0x0d,0xa4,
    0x6c,0xb6,0x00,0x0f,0x2f,0xc8,0x76,0x1f,0x8b,0x47,0x08,0x83,0xf8,
    0x01,0x74,0x05,0x83,0xf8,0x03,0x75,0x12,0xf3,0x0f,0x58,0x05,0x28,
    0x69,0xb6,0x00,0xf3,0x0f,0x11,0x47,0x30,0x0f,0x28,0xc8,0xeb,0x22,
    0x0f,0x2f,0xc2,0x0f,0x28,0xc8,0x76,0x1a,0x8b,0x47,0x08,0x83,0xf8,
    0x01,0x74,0x12,0x83,0xf8,0x03,0x74,0x0d,0xf3,0x0f,0x5c,0x0d,0x28,
    0x69,0xb6,0x00,0xf3,0x0f,0x11,0x4f,0x30,
};
static const uint8_t kPortraitCallOriginal[] = {0xe8,0x85,0x13,0x00,0x00};
static_assert(sizeof kPortraitOriginal == kPortraitResume-kPortraitSite,
              "portrait instruction span");
inline uint32_t Read32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
inline void Put32(uint8_t* p,uint32_t value) {
    for(unsigned n=0;n<4;++n) p[n]=uint8_t(value>>(8*n));
}
inline void Rel32(uint8_t* p,uintptr_t address,uintptr_t target,uint8_t op=0xe9) {
    p[0]=op; Put32(p+1,uint32_t(target)-uint32_t(address+5));
}
inline void RelocatedOriginal(uint8_t* out,const uint8_t* original,size_t n,
                              const unsigned* fields,size_t count,uint32_t delta) {
    for(size_t i=0;i<n;++i) out[i]=original[i];
    for(size_t i=0;i<count;++i) Put32(out+fields[i],Read32(out+fields[i])+delta);
}
static const unsigned kVignetteRelocations[]={9,23};
static const unsigned kCharacterRelocations[]={9,35,52,78};
static const unsigned kPortraitRelocations[]={12,38,77};

// No C++ call inside this scalar block. Preserve flags while selecting the
// original relocated instruction block vs pure-output read-only path.
// Original short branches all remain inside copied block or target its end,
// where the appended JMP resumes the original renderer.
inline void BuildPortraitStub(uint8_t* out,uintptr_t stubAddress,
                              const uint8_t* relocatedOriginal,
                              uintptr_t skipFlag,uintptr_t resumeAddress) {
    out[0]=0x9c;                       // pushfd
    out[1]=0x80; out[2]=0x3d;          // cmp byte ptr [skipFlag],0
    Put32(out+3,uint32_t(skipFlag)); out[7]=0;
    out[8]=0x0f; out[9]=0x85;          // jne pure path (offset106)
    Put32(out+10,106-14);
    out[14]=0x9d;                      // popfd
    for(size_t n=0;n<sizeof kPortraitOriginal;++n) out[15+n]=relocatedOriginal[n];
    Rel32(out+101,stubAddress+101,resumeAddress);
    out[106]=0x9d;                     // pure: popfd
    const uint8_t loads[]={0xf3,0x0f,0x10,0x4f,0x30, // movss xmm1,[edi+30]
                           0x0f,0x28,0xc1,          // movaps xmm0,xmm1
                           0x0f,0x57,0xd2};         // xorps xmm2,xmm2
    for(size_t n=0;n<sizeof loads;++n) out[107+n]=loads[n];
    Rel32(out+118,stubAddress+118,resumeAddress);
}

static_assert(sizeof kVignetteOriginal == kVignetteResume-kVignetteSite,
              "vignette instruction span");
static_assert(sizeof kCharacterOriginal == kCharacterResume-kCharacterSite,
              "character instruction span");

inline void BuildPatches(uint8_t* vignette, uint8_t* character) {
    for (size_t i=0; i<sizeof kVignetteOriginal; ++i) vignette[i]=0x90;
    for (size_t i=0; i<sizeof kCharacterOriginal; ++i) character[i]=0x90;
    // movss xmm1,[esi+34]; the existing981208 store becomes idempotent.
    const uint8_t loadV[] = {0xf3,0x0f,0x10,0x4e,0x34};
    // movss xmm0,[edi+34]; movaps xmm1,xmm0.
    // xmm0 is the following draw's scalar result. No call => no ABI/stack risk.
    const uint8_t loadC[] = {0xf3,0x0f,0x10,0x47,0x34,0x0f,0x28,0xc8};
    for (size_t i=0; i<sizeof loadV; ++i) vignette[i]=loadV[i];
    for (size_t i=0; i<sizeof loadC; ++i) character[i]=loadC[i];
}

} // namespace native_render_clock
} // namespace highfps

#if defined(_WIN32) && defined(HIGHFPS_BUILD_19715)
namespace highfps {
namespace native_render_clock {
static bool installed = false;
static const volatile LONG* pureFrame = nullptr;
static volatile uint8_t skipPortraitAdvance = 0;
static uint8_t* portraitStub = nullptr;

// Only the main character renderer call942A96 is redirected here.
// Update-driven offscreen captures call the unmodified943E20 entry normally;
// they never set skipPortraitAdvance and retain their native initialization.
inline void __fastcall PortraitRenderProxy(void* self,void*,const void* position,bool flag) {
    const uint8_t previous=skipPortraitAdvance;
    skipPortraitAdvance=(pureFrame && *pureFrame) ? 1 : 0;
    using Fn=void(__thiscall*)(void*,const void*,bool);
    __try {
        reinterpret_cast<Fn>(Addr(kPortraitFunction))(self,position,flag);
    } __finally {
        skipPortraitAdvance=previous;
    }
}

inline bool InstallNativeRenderClock19715(const volatile LONG* pureFrameFlag) {
    if (installed) return pureFrame==pureFrameFlag;
    if (!pureFrameFlag) return false; // required: parent canonicalR=0/pureR=1
    const uint32_t delta=uint32_t(reinterpret_cast<uintptr_t>(Addr(kStaticImageBase)))-
                         uint32_t(kStaticImageBase);
    uint8_t oldV[sizeof kVignetteOriginal],oldC[sizeof kCharacterOriginal];
    uint8_t oldP[sizeof kPortraitOriginal];
    RelocatedOriginal(oldV,kVignetteOriginal,sizeof oldV,kVignetteRelocations,2,delta);
    RelocatedOriginal(oldC,kCharacterOriginal,sizeof oldC,kCharacterRelocations,4,delta);
    RelocatedOriginal(oldP,kPortraitOriginal,sizeof oldP,kPortraitRelocations,3,delta);
    // Check ALL bytes before allocating/writing anything. Absolute image-data
    // operands are relocated by the module delta; CALL rel32 is ASLR invariant.
    if (!Expect(kVignetteSite,oldV,sizeof oldV,"MenuManager vignette fixed clock") ||
        !Expect(kCharacterSite,oldC,sizeof oldC,"MenuCharacter fade fixed clock") ||
        !Expect(kPortraitSite,oldP,sizeof oldP,"MenuCharacter portrait scope") ||
        !Expect(kPortraitRenderCall,kPortraitCallOriginal,sizeof kPortraitCallOriginal,
                "MenuCharacter main portrait render call")) return false;
    uint8_t* stub=reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr,kPortraitStubBytes,
                                      MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if (!stub) return false;
    BuildPortraitStub(stub,reinterpret_cast<uintptr_t>(stub),oldP,
                       reinterpret_cast<uintptr_t>(&skipPortraitAdvance),
                       reinterpret_cast<uintptr_t>(Addr(kPortraitResume)));
    DWORD oldProtection=0;
    if (!VirtualProtect(stub,kPortraitStubBytes,PAGE_EXECUTE_READ,&oldProtection)) {
        VirtualFree(stub,0,MEM_RELEASE); return false;
    }
    FlushInstructionCache(GetCurrentProcess(),stub,kPortraitStubBytes);
    uint8_t v[sizeof oldV],c[sizeof oldC],p[sizeof oldP],call[5];
    BuildPatches(v,c);
    for(size_t n=0;n<sizeof p;++n) p[n]=0x90;
    Rel32(p,reinterpret_cast<uintptr_t>(Addr(kPortraitSite)),reinterpret_cast<uintptr_t>(stub));
    Rel32(call,reinterpret_cast<uintptr_t>(Addr(kPortraitRenderCall)),
          reinterpret_cast<uintptr_t>(&PortraitRenderProxy),0xe8);
    pureFrame=pureFrameFlag;
    const uintptr_t sites[]={kVignetteSite,kCharacterSite,kPortraitSite,kPortraitRenderCall};
    const uint8_t* replacements[]={v,c,p,call};
    const uint8_t* originals[]={oldV,oldC,oldP,kPortraitCallOriginal};
    const size_t sizes[]={sizeof v,sizeof c,sizeof p,sizeof call};
    unsigned completed=0;
    for(;completed<4;++completed) if (!Poke(sites[completed],replacements[completed],sizes[completed])) break;
    if(completed!=4) {
        bool restored=true;
        while(completed) { --completed; if(!Poke(sites[completed],originals[completed],sizes[completed])) restored=false; }
        if(restored) { VirtualFree(stub,0,MEM_RELEASE); pureFrame=nullptr; }
        else Log("[FATAL] native menu clock rollback failed; keep trampoline allocated");
        return false;
    }
    portraitStub=stub;
    installed=true;
    return true;
}

inline void NativeRenderFixedTick19715() {
    if (!installed) return;
    const uintptr_t manager=*reinterpret_cast<uintptr_t*>(Addr(0x00C0C09C));
    // 94D508 jump table: Manager state1->MenuManager::Render; state2->Game.
    if (manager<0x10000 || *reinterpret_cast<int*>(manager+8)!=1) return;
    const uintptr_t menu=*reinterpret_cast<uintptr_t*>(Addr(0x00C0D018));
    if (menu<0x10000) return;
    const uintptr_t character=menu+0x1530;
    const int state=*reinterpret_cast<int*>(character+8);
    float& characterValue=*reinterpret_cast<float*>(character+0x34);
    float& vignetteValue=*reinterpret_cast<float*>(menu+0x34);
    // Original Render order: character first (980FF4), vignette second.
    characterValue=CharacterTick(characterValue,state);
    vignetteValue=VignetteTick(vignetteValue,state);
}

} // namespace native_render_clock
} // namespace highfps
#endif
