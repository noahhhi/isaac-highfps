#pragma once

// Included by dllmain only in the exact-build J510 candidate. Cache positive,
// scalar locations inside a single native render pass, on the GL owner thread.
#include "shader_location_cache.h"
#include <atomic>
#include <type_traits>

namespace highfps::j510_locations {
namespace {
using Context = HGLRC(WINAPI*)();
using Location = int(WINAPI*)(unsigned, const char*);
using Kind = shader_locations::Kind;
using Cache = shader_locations::Cache;

enum Slot : unsigned {
    Attrib, Uniform, Link, Delete, LinkArb, DeleteArb, Binary, BinaryOes,
    Make, DeleteContext, Share, Copy, MakeArb, MakeExt, Count
};
struct Binding { const char* exportName; uintptr_t slot, resolver; };
constexpr Binding bindings[Count] = {
    {"epoxy_glGetAttribLocation", 0xC3781C, 0xA53140},
    {"epoxy_glGetUniformLocation", 0xC377E0, 0xA568D0},
    {"epoxy_glLinkProgram", 0xC37868, 0xA58B70},
    {"epoxy_glDeleteProgram", 0xC378AC, 0xA4F750},
    {"epoxy_glLinkProgramARB", 0xC36BBC, 0xA58BA0},
    {"epoxy_glDeleteObjectARB", 0xC37648, 0xA4F6B0},
    {"epoxy_glProgramBinary", 0xC39874, 0xA5DB90},
    {"epoxy_glProgramBinaryOES", 0xC384B4, 0xA5DBC0},
    {"epoxy_wglMakeCurrent", 0xC3A868, 0xACE650},
    {"epoxy_wglDeleteContext", 0xC3A68C, 0xAB0430},
    {"epoxy_wglShareLists", 0xC3A83C, 0xAB0F90},
    {"epoxy_wglCopyContext", 0xC3A7A8, 0xAB0120},
    {"epoxy_wglMakeContextCurrentARB", 0xC5AB34, 0xACE660},
    {"epoxy_wglMakeContextCurrentEXT", 0xC5AB30, 0xACE670},
};
constexpr unsigned DeleteSlots = 256;
std::array<unsigned, DeleteSlots> deleted{}; // GL owner only; includes deferred deletion.
std::atomic<void*> targets[Count]{};
void** slots[Count]{};
void* resolver[Count]{};
void* hook[Count]{};
std::atomic<bool> healthy{false};
DWORD owner = 0;
Context current = nullptr;
Cache* cache = nullptr; // Process lifetime, even on a partial rollback.
uintptr_t callers[2][2]{};
uintptr_t passContext = 0;
bool passValid = false;
unsigned depth = 0;
struct Counters { unsigned long long hits, forwarded, negative, invalidations; };
Counters counters[2]{};

bool Executable(void* fn) {
    MEMORY_BASIC_INFORMATION m{};
    if (!fn || !VirtualQuery(fn, &m, sizeof m) || m.State != MEM_COMMIT ||
        (m.Protect & PAGE_GUARD)) return false;
    const auto p = m.Protect & 0xff;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
           p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}
void Invalidate() {
    // Any mutation in the pass discards the rest of the pass. A mutation on
    // another thread could affect a shared namespace; thereafter only forward.
    if (GetCurrentThreadId() != owner) {
        healthy.store(false, std::memory_order_release);
        return;
    }
    passValid = false;
    ++counters[0].invalidations;
}
bool Retired(unsigned p, bool remember = false) {
    if (!p) return true;
    unsigned i = (p * 0x9e3779b9u ^ (p >> 8)) & (DeleteSlots - 1);
    for (unsigned n = 0; n < DeleteSlots; ++n, i = (i + 1) & (DeleteSlots - 1)) {
        if (deleted[i] == p) return true;
        if (!deleted[i]) {
            if (remember) deleted[i] = p;
            return false;
        }
    }
    healthy.store(false, std::memory_order_release);
    return true;
}
void Rearm(Slot slot, void* was) {
    if (was != resolver[slot]) return;
    if (GetCurrentThreadId() != owner) {
        healthy.store(false, std::memory_order_release);
        return;
    }
    auto* next = *slots[slot];
    if (next == hook[slot]) return;
    if (next == was || !Executable(next)) {
        healthy.store(false, std::memory_order_release);
        return;
    }
    targets[slot].store(next, std::memory_order_release);
    if (InterlockedCompareExchangePointer(slots[slot], hook[slot], next) != next)
        healthy.store(false, std::memory_order_release);
}
bool StillOwned() {
    for (unsigned i = 0; i < Count; ++i)
        if (*slots[i] != hook[i]) {
            healthy.store(false, std::memory_order_release);
            return false;
        }
    return true;
}
bool ReadName(const char* name, char (&copy)[Cache::NameBytes], std::size_t& len) {
    if (!name) return false;
    __try {
        for (len = 0; len < sizeof copy; ++len)
            if (!(copy[len] = name[len])) return len != 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
int Query(Slot slot, unsigned program, const char* name, uintptr_t caller) {
    auto* was = targets[slot].load(std::memory_order_acquire);
    auto query = reinterpret_cast<Location>(was);
    if (GetCurrentThreadId() != owner) {
        const auto value = query(program, name);
        Rearm(slot, was);
        return value;
    }
    auto& count = counters[slot];
    const auto kind = slot == Attrib ? Kind::Attribute : Kind::Uniform;
    char copy[Cache::NameBytes];
    std::size_t len = 0;
    const bool eligible = depth == 1 && passValid && was != resolver[slot] &&
        (caller == callers[slot][0] || caller == callers[slot][1]) &&
        healthy.load(std::memory_order_acquire) && !Retired(program) && StillOwned() &&
        reinterpret_cast<uintptr_t>(current()) == passContext && ReadName(name, copy, len);
    int value = -1;
    if (eligible && cache->FindValidated(passContext, program, kind, copy, len, value) &&
        healthy.load(std::memory_order_acquire)) {
        ++count.hits;
        return value;
    }
    ++count.forwarded;
    value = query(program, name);
    Rearm(slot, was);
    if (value < 0) ++count.negative;
    else if (eligible && passValid && healthy.load(std::memory_order_acquire) &&
             reinterpret_cast<uintptr_t>(current()) == passContext)
        cache->StoreValidated(passContext, program, kind, copy, len, value);
    return value;
}
int WINAPI GetAttrib(unsigned p, const char* n) {
    return Query(Attrib, p, n, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}
int WINAPI GetUniform(unsigned p, const char* n) {
    return Query(Uniform, p, n, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}
template<Slot slot, typename R, typename... Args>
R WINAPI Mutation(Args... args) {
    const auto was = targets[slot].load(std::memory_order_acquire);
    Invalidate();
    if constexpr (slot == Delete || slot == DeleteArb) {
        if (GetCurrentThreadId() == owner) Retired(args..., true);
    }
    auto fn = reinterpret_cast<R(WINAPI*)(Args...)>(was);
    if constexpr (std::is_void_v<R>) {
        fn(args...);
        Rearm(slot, was);
    } else {
        const auto result = fn(args...);
        Rearm(slot, was);
        return result;
    }
}
} // namespace

bool Install(HMODULE executable, uintptr_t (*address)(uintptr_t), DWORD thread,
             void (*log)(const char*, ...)) {
    auto reject = [log](const char* phase, unsigned index) {
        log("[j510-shader-locations] rejected=%s index=%u", phase, index);
        return false;
    };
    if (cache || !thread || GetCurrentThreadId() != thread) return reject("owner", 0);
    auto gl = GetModuleHandleA("opengl32.dll");
    current = gl ? reinterpret_cast<Context>(GetProcAddress(gl, "wglGetCurrentContext")) : nullptr;
    if (!current) return reject("get-current-context", 0);
    // Verify each admitted native indirect call still loads the expected slot.
    struct Anchor { uintptr_t code; Slot slot; unsigned length, pointerAt; std::uint8_t bytes[14]; };
    constexpr Anchor anchors[] = {
        {0xA3E4E3, Attrib, 14, 2, {0x8b, 0x0d, 0, 0, 0, 0, 0xff, 0x34, 0xf8, 0xff, 0x76, 0x50, 0xff, 0xd1}},
        {0xA3E603, Attrib, 14, 2, {0x8b, 0x0d, 0, 0, 0, 0, 0xff, 0x34, 0xf0, 0xff, 0x77, 0x50, 0xff, 0xd1}},
        {0xA3E288, Uniform, 12, 2, {0x8b, 0x0d, 0, 0, 0, 0, 0x50, 0xff, 0x77, 0x50, 0xff, 0xd1}},
        {0xA3E2D6, Uniform, 10, 1, {0xa1, 0, 0, 0, 0, 0xff, 0x71, 0x50, 0xff, 0xd0}},
    };
    for (auto anchor : anchors) {
        const auto ptr = std::uint32_t(address(bindings[anchor.slot].slot));
        std::memcpy(anchor.bytes + anchor.pointerAt, &ptr, sizeof ptr);
        if (std::memcmp(reinterpret_cast<const void*>(address(anchor.code)), anchor.bytes, anchor.length))
            return reject("caller-bytes", unsigned(anchor.code));
    }
    void* hooks[Count] = {
        reinterpret_cast<void*>(&GetAttrib), reinterpret_cast<void*>(&GetUniform),
        reinterpret_cast<void*>(&Mutation<Link, void, unsigned>),
        reinterpret_cast<void*>(&Mutation<Delete, void, unsigned>),
        reinterpret_cast<void*>(&Mutation<LinkArb, void, unsigned>),
        reinterpret_cast<void*>(&Mutation<DeleteArb, void, unsigned>),
        reinterpret_cast<void*>(&Mutation<Binary, void, unsigned, unsigned, const void*, int>),
        reinterpret_cast<void*>(&Mutation<BinaryOes, void, unsigned, unsigned, const void*, int>),
        reinterpret_cast<void*>(&Mutation<Make, BOOL, HDC, HGLRC>),
        reinterpret_cast<void*>(&Mutation<DeleteContext, BOOL, HGLRC>),
        reinterpret_cast<void*>(&Mutation<Share, BOOL, HGLRC, HGLRC>),
        reinterpret_cast<void*>(&Mutation<Copy, BOOL, HGLRC, HGLRC, UINT>),
        reinterpret_cast<void*>(&Mutation<MakeArb, BOOL, HDC, HDC, HGLRC>),
        reinterpret_cast<void*>(&Mutation<MakeExt, BOOL, HDC, HDC, HGLRC>),
    };
    owner = thread;
    callers[Attrib][0] = address(0xA3E4F1);
    callers[Attrib][1] = address(0xA3E611);
    callers[Uniform][0] = address(0xA3E294);
    callers[Uniform][1] = address(0xA3E2E0);
    for (unsigned i = 0; i < Count; ++i) {
        const auto& spec = bindings[i];
        const auto exported = reinterpret_cast<uintptr_t>(GetProcAddress(executable, spec.exportName));
        if (exported != address(spec.slot)) return reject("export", i);
        slots[i] = reinterpret_cast<void**>(exported);
        MEMORY_BASIC_INFORMATION mem{};
        if (!VirtualQuery(slots[i], &mem, sizeof mem) || mem.State != MEM_COMMIT ||
            (mem.Protect != PAGE_READWRITE && mem.Protect != PAGE_WRITECOPY))
            return reject("slot-protection", i);
        resolver[i] = reinterpret_cast<void*>(address(spec.resolver));
        hook[i] = hooks[i];
        auto* original = *slots[i];
        if (original == hook[i] || !Executable(original)) return reject("slot-target", i);
        targets[i].store(original, std::memory_order_release);
    }
    try { cache = new Cache; } catch (...) { return reject("allocation", 0); }
    for (unsigned i = 0; i < Count; ++i) {
        auto* was = targets[i].load(std::memory_order_acquire);
        if (InterlockedCompareExchangePointer(slots[i], hook[i], was) != was) {
            const auto failed = i;
            while (i) {
                --i;
                InterlockedCompareExchangePointer(slots[i], targets[i].load(), hook[i]);
            }
            return reject("claim-slot", failed);
        }
    }
    healthy.store(true, std::memory_order_release);
    return true;
}

void Begin() {
    if (!cache || GetCurrentThreadId() != owner) return;
    if (++depth != 1) { passValid = false; return; }
    passContext = reinterpret_cast<uintptr_t>(current());
    cache->BeginFrame();
    passValid = passContext && healthy.load(std::memory_order_acquire) && StillOwned();
}
void End() {
    if (cache && GetCurrentThreadId() == owner && depth) {
        --depth;
        passValid = false;
    }
}

void Report(void (*log)(const char*, ...)) {
    if (!cache) return;
    log("[j510-shader-locations] active=%u attrib=%llu/%llu/%llu uniform=%llu/%llu/%llu invalidations=%llu",
        unsigned(healthy.load(std::memory_order_acquire)),
        counters[Attrib].hits, counters[Attrib].forwarded, counters[Attrib].negative,
        counters[Uniform].hits, counters[Uniform].forwarded, counters[Uniform].negative,
        counters[Attrib].invalidations);
}
} // namespace highfps::j510_locations
