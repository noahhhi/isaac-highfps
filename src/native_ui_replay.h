#pragma once
// Include at global scope after Addr/Expect/Poke/Log. This is a narrowly scoped
// replay of native UI clock updates, not a global ANM2 hook. Parent must run one
// canonical native render per fixed tick, pure presentations with *pureFlag=1.
#include <stdint.h>
#include <stddef.h>

namespace highfps { namespace native_ui_replay {
constexpr unsigned kMaxLayers=128;
constexpr unsigned kMaxRecords=64;
struct Memory {
    bool (*read)(uint32_t,void*,size_t);
    bool (*write)(uint32_t,const void*,size_t);
};
struct AnimationSnapshot {
    uint32_t address;
    uint32_t state[8]; // owner, animation, layer-array, null-array, frame, playing, events
    uint32_t layerCount,nullCount;
    uint32_t layers[kMaxLayers],nulls[kMaxLayers];
};
inline bool Capture(const Memory& m,uint32_t address,AnimationSnapshot& out) {
    out.address=address;
    if(address<0x10000 || !m.read(address,out.state,sizeof out.state)) return false;
    const uint32_t owner=out.state[0];
    if(owner<0x10000) return false;
    if(!m.read(owner+0x80,&out.layerCount,4) || !m.read(owner+0x88,&out.nullCount,4)) return false;
    if(out.layerCount>kMaxLayers || out.nullCount>kMaxLayers) return false;
    if(!out.state[2]) out.layerCount=0;
    if(!out.state[3]) out.nullCount=0;
    return (!out.layerCount || m.read(out.state[2],out.layers,out.layerCount*4)) &&
           (!out.nullCount || m.read(out.state[3],out.nulls,out.nullCount*4));
}
inline bool Restore(const Memory& m,const AnimationSnapshot& saved,bool matchAnimation) {
    uint32_t live[4];
    if(!m.read(saved.address,live,sizeof live) || live[0]!=saved.state[0] ||
       live[2]!=saved.state[2] || live[3]!=saved.state[3] ||
       (matchAnimation && live[1]!=saved.state[1])) return false;
    uint32_t layers=0,nulls=0;
    if(!m.read(live[0]+0x80,&layers,4) || !m.read(live[0]+0x88,&nulls,4)) return false;
    if(saved.layerCount>layers || saved.nullCount>nulls) return false;
    return (!saved.layerCount || m.write(live[2],saved.layers,saved.layerCount*4)) &&
           (!saved.nullCount || m.write(live[3],saved.nulls,saved.nullCount*4)) &&
           m.write(saved.address,saved.state,sizeof saved.state);
}
struct Record {
    uint32_t object,kind,inputType,header[4],inputAnimations[4];
    unsigned count;
    AnimationSnapshot animations[4];
};
struct ReplayScope {
    bool active,pure,valid;
    uint32_t owner;
    unsigned count,cursor;
    uint64_t captured,replayed,misses,restoreFailures;
    Record records[kMaxRecords];
};
inline void InvalidateScope(ReplayScope& scope) {
    scope.valid=false;scope.owner=0;scope.count=scope.cursor=0;
    // Cumulative diagnostic counters deliberately survive lifecycle changes.
}
inline void ResetCanonical(ReplayScope& scope,uint32_t owner) {
    scope.owner=owner;scope.count=scope.cursor=0;scope.valid=true;
}
inline Record* Append(ReplayScope& scope) {
    if(!scope.valid || scope.count>=kMaxRecords) {scope.valid=false;++scope.misses;return nullptr;}
    ++scope.captured;return &scope.records[scope.count++];
}
inline const Record* Next(ReplayScope& scope,uint32_t object,uint32_t kind) {
    if(!scope.valid || scope.cursor>=scope.count) {++scope.misses;return nullptr;}
    const Record* r=&scope.records[scope.cursor++];
    if(r->object!=object || r->kind!=kind) {++scope.misses;return nullptr;}
    ++scope.replayed;return r;
}
}} // namespace highfps::native_ui_replay

#if defined(_WIN32) && defined(HIGHFPS_BUILD_19715)
namespace highfps { namespace native_ui_replay {
static const volatile LONG* pureFlag=nullptr;
static bool installed=false;
static ReplayScope dice={},lobby={};
static uint64_t uiEpoch=~uint64_t(0),epochInvalidations=0,stalePureScopes=0;
static bool passKnown=false,passCanonical=false;
inline void BeginNativeUiRender19715(uint64_t epoch,bool canonical) {
    if(uiEpoch!=epoch) {
        InvalidateScope(dice);InvalidateScope(lobby);
        uiEpoch=epoch;++epochInvalidations;
    }
    passKnown=true;passCanonical=canonical;
}
inline bool Read(uint32_t p,void* to,size_t n) {
    __try { memcpy(to,reinterpret_cast<const void*>(uintptr_t(p)),n);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
inline bool Write(uint32_t p,const void* from,size_t n) {
    __try { memcpy(reinterpret_cast<void*>(uintptr_t(p)),from,n);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
static const Memory memory={Read,Write};
using NoArgs=void(__thiscall*)(void*);
inline void Native(uintptr_t va,void* self) {reinterpret_cast<NoArgs>(Addr(va))(self);}
inline bool IsPure() {return passKnown ? !passCanonical : (pureFlag && *pureFlag!=0);}

inline void __fastcall DiceUpdate(void* self,void*) {
    const uint32_t object=uint32_t(reinterpret_cast<uintptr_t>(self));
    if(!dice.active) {Native(0x0042CBA0,self);return;}
    uint32_t type=0;Read(object+4,&type,4);
    const unsigned offsets[]={0x44,0x64,0x158,0x178};
    if(!dice.pure) {
        uint32_t inputAnimations[4]={};bool inputs=true;
        for(unsigned n=0;n<4;++n) inputs=Read(object+offsets[n]+4,&inputAnimations[n],4) && inputs;
        Native(0x0042CBA0,self);
        Record* r=Append(dice);if(!r) return;
        r->object=object;r->kind=0;r->inputType=type;r->count=4;
        if(!Read(object,r->header,sizeof r->header)) {dice.valid=false;++dice.misses;return;}
        if(!inputs) {dice.valid=false;++dice.misses;return;}
        for(unsigned n=0;n<4;++n) r->inputAnimations[n]=inputAnimations[n];
        for(unsigned n=0;n<4;++n) if(!Capture(memory,object+offsets[n],r->animations[n])) {
            dice.valid=false;++dice.misses;return;
        }
        return;
    }
    // Play/type selection still ran. No native time-update call on pure output.
    // Replaying each canonical visit also preserves duplicated/shared icons.
    const Record* r=Next(dice,object,0);
    if(!r || r->inputType!=type) {if(r) ++dice.misses;return;}
    for(unsigned n=0;n<r->count;++n) {
        uint32_t identity=0;
        if(!Read(object+offsets[n]+4,&identity,4) ||
           (identity!=r->inputAnimations[n] && identity!=r->animations[n].state[1]) ||
           !Restore(memory,r->animations[n],false)) {++dice.restoreFailures;return;}
    }
    if(!Write(object,r->header,sizeof r->header)) ++dice.restoreFailures;
}

inline void LobbyUpdate(void* self,uint32_t kind) {
    const uint32_t object=uint32_t(reinterpret_cast<uintptr_t>(self));
    if(!lobby.active) {Native(0x004044E0,self);return;}
    if(!lobby.pure) {
        uint32_t inputAnimation=0;const bool inputValid=Read(object+4,&inputAnimation,4);
        Native(0x004044E0,self);
        Record* r=Append(lobby);if(!r) return;
        r->object=object;r->kind=kind;r->count=1;r->inputAnimations[0]=inputAnimation;
        if(!inputValid || !Capture(memory,object,r->animations[0])) {lobby.valid=false;++lobby.misses;}
        return;
    }
    const Record* r=Next(lobby,object,kind);
    if(r) {
        uint32_t identity=0;
        if(!Read(object+4,&identity,4) ||
           (identity!=r->inputAnimations[0] && identity!=r->animations[0].state[1]) ||
           !Restore(memory,r->animations[0],false)) ++lobby.restoreFailures;
    }
}
inline void __fastcall Lobby0(void* s,void*) {LobbyUpdate(s,0);}
inline void __fastcall Lobby1(void* s,void*) {LobbyUpdate(s,1);}
inline void __fastcall Lobby2(void* s,void*) {LobbyUpdate(s,2);}
inline void __fastcall Lobby3(void* s,void*) {LobbyUpdate(s,3);}
inline void __fastcall Lobby4(void* s,void*) {LobbyUpdate(s,4);}
inline void __fastcall Lobby5(void* s,void*) {LobbyUpdate(s,5);}

inline void RenderScope(ReplayScope& scope,void* self,uintptr_t function,
                        const unsigned* offsets,unsigned count,bool diceHeader) {
    if(scope.active) {++scope.misses;Native(function,self);return;}
    const uint32_t owner=uint32_t(reinterpret_cast<uintptr_t>(self));
    const bool pure=IsPure();
    if(!pure) ResetCanonical(scope,owner);
    else {
        // A lifecycle may replace an object at the same address. Until a
        // canonical pass records this epoch, do not even enter the old UI
        // subtree: Play/type initialization itself can mutate its timeline.
        if(!scope.valid || scope.owner!=owner) {
            if(scope.valid && scope.owner!=owner) ++scope.misses;
            ++stalePureScopes;return;
        }
        scope.cursor=0;
    }
    const bool hasUpdates=scope.count!=0;
    AnimationSnapshot before[4];uint32_t header[4]={};bool saved=true;
    if(pure && hasUpdates) {
        for(unsigned n=0;n<count;++n) saved=Capture(memory,owner+offsets[n],before[n]) && saved;
        if(diceHeader) saved=Read(owner+8,header,sizeof header) && saved;
        if(!saved) {
            ++scope.restoreFailures;scope.valid=false;
            // Unsupported storage cannot safely be transacted. Keep native
            // canonical output and refuse this pure subtree; diagnostics make
            // this a failed validation, not silent successful compatibility.
            return;
        }
    }
    scope.active=true;scope.pure=pure;
    __try {Native(function,self);}
    __finally {
        scope.active=false;
        // These are the exact mutable timing fields + index arrays, not whole
        // objects or GPU resources. The visible pose was already canonical
        // before Draw; this restoration never hides an advanced pure frame.
        if(pure && hasUpdates && saved) {
            for(unsigned n=0;n<count;++n) if(!Restore(memory,before[n],false)) ++scope.restoreFailures;
            if(diceHeader && !Write(owner+8,header,sizeof header)) ++scope.restoreFailures;
        }
        if(pure && scope.valid && scope.cursor!=scope.count) ++scope.misses;
    }
}
inline void __fastcall DiceRender(void* self,void*) {
    // Parent8D0E00 owns its bubble object at+8.
    const unsigned offsets[]={8+0x44,8+0x64,8+0x158,8+0x178};
    RenderScope(dice,self,0x008D0E00,offsets,4,true);
}
inline void __fastcall LobbyRender(void* self,void*) {
    const unsigned offsets[]={0x490,0x4B0};
    RenderScope(lobby,self,0x008E6180,offsets,2,false);
}

inline bool InstallNativeUiReplay19715(const volatile LONG* flag) {
    if(installed) return pureFlag==flag;
    if(!flag) return false;
    const uintptr_t sites[]={0x006FD555,0x00981062,0x008D0F30,
                             0x008E7FBC,0x008E7FC7,0x008E80F0,0x008E80FB,0x008E8273,0x008E827E};
    const uintptr_t targets[]={0x008D0E00,0x008E6180,0x0042CBA0,
                               0x004044E0,0x004044E0,0x004044E0,0x004044E0,0x004044E0,0x004044E0};
    const void* hooks[]={reinterpret_cast<const void*>(&DiceRender),reinterpret_cast<const void*>(&LobbyRender),
                        reinterpret_cast<const void*>(&DiceUpdate),reinterpret_cast<const void*>(&Lobby0),
                        reinterpret_cast<const void*>(&Lobby1),reinterpret_cast<const void*>(&Lobby2),
                        reinterpret_cast<const void*>(&Lobby3),reinterpret_cast<const void*>(&Lobby4),
                        reinterpret_cast<const void*>(&Lobby5)};
    uint8_t originals[9][5],patches[9][5];
    for(unsigned n=0;n<9;++n) {
        originals[n][0]=patches[n][0]=0xe8;
        const int32_t oldRelative=int32_t(targets[n]-(sites[n]+5));
        const int32_t newRelative=int32_t(reinterpret_cast<uintptr_t>(hooks[n])-
                                         (reinterpret_cast<uintptr_t>(Addr(sites[n]))+5));
        memcpy(originals[n]+1,&oldRelative,4);memcpy(patches[n]+1,&newRelative,4);
        if(!Expect(sites[n],originals[n],5,"native UI replay callsite")) return false;
    }
    pureFlag=flag;unsigned completed=0;
    for(;completed<9;++completed) if(!Poke(sites[completed],patches[completed],5)) break;
    if(completed!=9) {
        bool restored=true;
        while(completed) {--completed;if(!Poke(sites[completed],originals[completed],5)) restored=false;}
        if(restored) pureFlag=nullptr;
        else Log("[FATAL] native UI replay rollback failed");
        return false;
    }
    installed=true;return true;
}
inline void LogNativeUiReplayDiagnostics19715() {
    Log("[native-ui-replay] dice canonical=%llu replay=%llu miss=%llu restore-fail=%llu | lobby canonical=%llu replay=%llu miss=%llu restore-fail=%llu",
        dice.captured,dice.replayed,dice.misses,dice.restoreFailures,
        lobby.captured,lobby.replayed,lobby.misses,lobby.restoreFailures);
    Log("[native-ui-epoch] epoch=%llu invalidations=%llu stale-pure-scopes=%llu",
        uiEpoch,epochInvalidations,stalePureScopes);
}
}} // namespace highfps::native_ui_replay
#endif
