#pragma once
#include "entity_draw_cache_probe.h"
// ABI adapter for a 32-bit Windows build. No runtime installation or detour here.
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
namespace highfps_entity_probe {
using ResolveAddress=std::uintptr_t(*)(std::uintptr_t);
struct NativeSmartImage { void* image; void* counter; };
static_assert(sizeof(NativeSmartImage)==8,"x86 only");
// Native CreateRT locks this same image registry at A13CCD and iterates its
// 32 vector buckets at A13D31. This retains existing PNG/font/procedural images
// without depending on which API originally loaded them.
inline std::shared_ptr<void> RetainJ374Image(void* image,ResolveAddress resolve) {
    if (!image || !resolve) return {};
    void* mutex=reinterpret_cast<void*>(resolve(kImageTableMutex));
    auto** mvt=*reinterpret_cast<void***>(mutex);
    using Lock=void(__thiscall*)(void*,int);
    using Unlock=void(__thiscall*)(void*);
    reinterpret_cast<Lock>(mvt[3])(mutex,-1);
    struct Guard {
        void* mutex; Unlock unlock;bool active=true;
        ~Guard(){if(active)unlock(mutex);}
    } guard{mutex,reinterpret_cast<Unlock>(mvt[4])};
    struct Bucket { NativeSmartImage* begin; NativeSmartImage* end; NativeSmartImage* capacity; };
    auto* buckets=*reinterpret_cast<Bucket**>(resolve(kImageBuckets));
    NativeSmartImage held{};
    if (buckets) for (unsigned i=0;i<32 && !held.image;++i) {
        const auto begin=reinterpret_cast<std::uintptr_t>(buckets[i].begin);
        const auto end=reinterpret_cast<std::uintptr_t>(buckets[i].end);
        if (!begin || end<begin || (end-begin)%8 || end-begin>16384*8) continue;
        for (auto* p=buckets[i].begin;p!=buckets[i].end;++p) {
            if(p->image!=image || !p->counter) continue;
            auto* raw=static_cast<std::uint8_t*>(p->counter);
            // Counter+0x14 is the owned image; +4 is 16-bit strong count.
            // Do not overflow it or resurrect an expired registry entry.
            if (*reinterpret_cast<void**>(raw+0x14)!=image ||
                *reinterpret_cast<std::uint16_t*>(raw+4)>=65530) continue;
            auto** vt=*reinterpret_cast<void***>(p->counter);
            using Acquire=bool(__thiscall*)(void*);
            if(reinterpret_cast<Acquire>(vt[1])(p->counter)) {held=*p;break;}
        }
    }
    if(!held.image) return {};
    // Constructing shared_ptr while manager mutex is held uses no game callback.
    // Native destructor can call a registry notification, so unlock BEFORE either
    // construction failure or a later deleter releases the retained reference.
    guard.unlock(guard.mutex);
    guard.active=false;
    using Release=void(__thiscall*)(NativeSmartImage*);
    const auto release=reinterpret_cast<Release>(resolve(kSmartImageDecrRef));
    return std::shared_ptr<void>(image,[held,release](void*) mutable {release(&held);});
}
}
#endif
