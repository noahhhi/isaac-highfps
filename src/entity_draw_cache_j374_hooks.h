#pragma once
#include "entity_draw_cache_probe.h"
#include "entity_render_subscribers_j374.h"
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>
namespace highfps_entity_probe {
// Host installs checked detours and supplies original trampolines. Bind only on
// the game thread; no private injector/thread/game launch is part of this probe.
struct J374Hooks {
    using EntityFn=void(__stdcall*)(void*,const float*);
    using SubmitFn=std::uint8_t*(__thiscall*)(void*,const Source*,const Destination*,
                                            const Color*,const Color*,const Color*,const Color*);
    inline static Cache* cache=nullptr;
    inline static bool canonical=true;
    inline static bool (*InsideRender)()=nullptr;
    inline static subscribers19715::State (*Subscribers)(unsigned)=nullptr;
    inline static void (*OnNoSubscribers)(unsigned,bool)=nullptr;
    inline static void (*OnOutsideRender)(unsigned)=nullptr;
    inline static DWORD gameThreadId=0;
    inline static void (*OnEvent)(unsigned,bool,bool)=nullptr;
    inline static void (*OnMiss)(const Key&,const Cache::ReplayInfo&)=nullptr;
    inline static bool (*MakeKey)(void*,unsigned,Key&)=nullptr;
    inline static EntityFn player=nullptr,tear=nullptr,effect=nullptr,npc=nullptr,projectile=nullptr,laser=nullptr,bomb=nullptr,pickup=nullptr,familiar=nullptr,knife=nullptr;
    inline static unsigned submitDepth=0;
    inline static SubmitFn submit=nullptr;
    using FormatFn=void(__thiscall*)(void*,const void*);
    inline static FormatFn setFormat=nullptr;
    inline static std::uint64_t formatNoop=0,formatInit=0,formatChange=0;
    static void __fastcall SetFormat(void* image,void*,const void* descriptor) {
        if(GetCurrentThreadId()!=gameThreadId || !cache || !cache->Capturing() || cache->Replaying()) {
            setFormat(image,descriptor);return;
        }
        // The native setter may free the old layout and subsequent native calls
        // may resize its batches. Finish caller-written vertex data first.
        cache->SealPending();
        const auto* p=static_cast<const std::uint8_t*>(image);
        const auto oldEnum=*reinterpret_cast<const std::uint32_t*>(p+0x20);
        const auto oldAttrs=*reinterpret_cast<const std::uintptr_t*>(p+0x34);
        const auto oldSize=*reinterpret_cast<const std::uint32_t*>(p+0x38);
        setFormat(image,descriptor);
        if(oldEnum==*reinterpret_cast<const std::uint32_t*>(p+0x20) &&
           oldAttrs==*reinterpret_cast<const std::uintptr_t*>(p+0x34) &&
           oldSize==*reinterpret_cast<const std::uint32_t*>(p+0x38))++formatNoop;
        else if(!oldAttrs || !(oldSize&0xff))++formatInit;
        else ++formatChange;
        // No early Retain here: LoadGraphics can call this before a new image is
        // registered. The first quad snapshots its format and takes a native ref;
        // Cache::End captures the final state of every image actually drawn.
    }
    static void Entity(unsigned callback,EntityFn original,void* entity,const float* offset) {
        if(GetCurrentThreadId()!=gameThreadId){original(entity,offset);return;}
        if(InsideRender && !InsideRender()){original(entity,offset);if(OnOutsideRender)OnOutsideRender(callback);return;}
        // The original wrapper itself returns before Lua/drawing when this live
        // bit is clear. Avoid key building, GL state queries and empty captures.
        // Preserve its authoritative invocation; pure presentation never calls Lua.
        if(Subscribers && Subscribers(callback)==subscribers19715::State::Absent) {
            if(canonical)original(entity,offset);
            if(OnNoSubscribers)OnNoSubscribers(callback,canonical);
            return;
        }
        Key key;key.entity=reinterpret_cast<std::uintptr_t>(entity);key.callback=callback;
        if(!cache || !MakeKey || !MakeKey(entity,callback,key)) {
            // A missing generation/pass key must never execute Lua at high rate.
            if(canonical) original(entity,offset);
            if(OnEvent)OnEvent(callback,canonical,false);
            if(!canonical && OnMiss){Cache::ReplayInfo info;info.reason="key-unavailable";OnMiss(key,info);}
            return;
        }
        if(canonical) {
            const bool owns=cache->Begin(key);
            original(entity,offset);
            if(owns) cache->End();
            if(OnEvent)OnEvent(callback,true,owns);
        } else {
            const bool hit=cache->Replay(key);
            if(!hit && OnMiss)OnMiss(key,cache->LastReplay());
            if(OnEvent)OnEvent(callback,false,hit); // NEVER run original here
        }
    }
    static void __stdcall Player(void* entity,const float* offset) {Entity(32,player,entity,offset);}
    static void __stdcall Tear(void* entity,const float* offset) {Entity(41,tear,entity,offset);}
    static void __stdcall Effect(void* entity,const float* offset) {Entity(56,effect,entity,offset);}
    static void __stdcall Npc(void* entity,const float* offset) {Entity(28,npc,entity,offset);}
    static void __stdcall Projectile(void* entity,const float* offset) {Entity(45,projectile,entity,offset);}
    static void __stdcall Laser(void* entity,const float* offset) {Entity(49,laser,entity,offset);}
    static void __stdcall Bomb(void* entity,const float* offset) {Entity(59,bomb,entity,offset);}
    static void __stdcall Pickup(void* entity,const float* offset) {Entity(36,pickup,entity,offset);}
    static void __stdcall Familiar(void* entity,const float* offset) {Entity(25,familiar,entity,offset);}
    static void __stdcall Knife(void* entity,const float* offset) {Entity(52,knife,entity,offset);}
    static std::uint8_t* ForwardSubmit(void* image,const Source* src,const Destination* dst,
                        const Color* c0,const Color* c1,const Color* c2,const Color* c3) {
        // Only the game thread owns submitDepth. Even the no-capture path must
        // bracket a native quad, so its internal allocator is not mistaken for
        // an untracked allocation if a parent scope later becomes active.
        struct DepthScope { DepthScope(){++submitDepth;} ~DepthScope(){--submitDepth;} } depth;
        return submit(image,src,dst,c0,c1,c2,c3);
    }
    static std::uint8_t* __fastcall Submit(void* image,void*,const Source* src,const Destination* dst,
                        const Color* c0,const Color* c1,const Color* c2,const Color* c3) {
        if(GetCurrentThreadId()!=gameThreadId)return submit(image,src,dst,c0,c1,c2,c3);
        if(!cache || !cache->Capturing() || cache->Replaying())
            return ForwardSubmit(image,src,dst,c0,c1,c2,c3);
        // Some native layouts do not read every color argument. Do not make a
        // legal native call fault by dereferencing an unused null parameter.
        if(!image || !src || !dst || !c0 || !c1 || !c2 || !c3) {
            cache->Unsafe(0xffff);
            return ForwardSubmit(image,src,dst,c0,c1,c2,c3);
        }
        Quad q; q.image=image;q.source=*src;q.destination=*dst;
        q.color[0]=*c0;q.color[1]=*c1;q.color[2]=*c2;q.color[3]=*c3;
        const bool recorded=cache->BeforeQuad(q);
        auto* result=ForwardSubmit(image,src,dst,c0,c1,c2,c3);
        cache->AfterQuad(recorded,result);
        return result;
    }
    // Invoke BEFORE any unsupported drawing, RT/viewport/scissor change, shader
    // uniform mutation, texture update, flush or allocation invalidating pending
    // vertices. During replay these guards must not invalidate caches themselves.
    static void Unsupported(std::uint32_t reason=0) { if(GetCurrentThreadId()==gameThreadId && cache && !cache->Replaying()) cache->Unsafe(reason); }
};
}
#endif
