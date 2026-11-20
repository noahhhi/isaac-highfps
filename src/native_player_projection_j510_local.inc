// Exact J510 render-only adapter. Both native position and the caller's
// supplied offset remain untouched. Only the single projected player's body
// and shadow primitive get a private correction while a verified pass draws.
using J510Position = highfps::j510_motion::Position;
using J510PlayerDraw = void(__fastcall*)(void*, void*, const J510Position*);
using J510PlayerShadow = bool(__fastcall*)(void*, void*, const J510Position*);
J510PlayerDraw g_j510NativePlayerDraw = nullptr;
J510PlayerShadow g_j510NativePlayerShadow = nullptr;
bool g_j510ProjectionInstalled = false;
uintptr_t g_j510ProjectionOwner = 0, g_j510ProjectionPlayer = 0;
std::uint32_t g_j510ProjectionSeed = 0, g_j510ProjectionTick = 0, g_j510PlayerCount = 0;
bool g_j510ProjectedTickValid = false, g_j510ProjectionApplied = false;
J510Position g_j510ProjectionDelta{}, g_j510ProjectionPose{};
DWORD g_j510ProjectionThread = 0;
uintptr_t g_j510ShadowContinuation = 0;
std::uint32_t g_j510ProjectedPresents = 0, g_j510ProjectedBodies = 0,
    g_j510ProjectedShadows = 0;

bool J510ProjectionSupported(const J510RoomKey& room) {
    if (!g_j510ProjectionInstalled || g_j510PlayerCount != 1 ||
        g_j510ProjectionPlayer < 0x10000 || !(room == g_j510HistoryRoom) ||
        *reinterpret_cast<const std::uint8_t*>(Addr(0xC9E6DD))) return false;
    const auto manager = *reinterpret_cast<const uintptr_t*>(Addr(0xC9A0D4));
    if (manager < 0x10000 || *reinterpret_cast<const std::uint8_t*>(manager + 0x2A665) != 0)
        return false; // mods may draw their own markers off authoritative Position
    const auto* p = reinterpret_cast<const uintptr_t*>(g_j510ProjectionPlayer);
    return *p == reinterpret_cast<uintptr_t>(Addr(0xB8F260)) &&
        p[0x28/4] == 1 && p[0x2c/4] <= 1 && p[0x3C4/4] == 0 && p[0x3D4/4] == 0;
}

J510Position J510ProjectToDisplay(J510Position p, float scale) {
    const auto w = *reinterpret_cast<const float*>(Addr(0xC9DDBC));
    const auto h = *reinterpret_cast<const float*>(Addr(0xC9E010));
    return {std::floor(((p.x - 60.0f) * .65f + (w - 338.0f) * .5f) * scale + .5f) / scale,
            std::floor(((p.y - 140.0f) * .65f + (h - 182.0f) * .5f) * scale + .5f) / scale};
}

bool PrepareJ510Projection(const J510RoomKey& room, float alpha) {
    if (!J510ProjectionSupported(room)) return false;
    const auto e = g_j510ProjectionPlayer;
    const auto* t = FindTrack(e, false);
    const auto seed = *reinterpret_cast<const std::uint32_t*>(e + 0x3EC);
    if (!t || t->stamp != g_trackStamp ||
        !highfps::j510::SamePoseIdentity(t->fingerprint, Fingerprint(e),
            g_j510TrackSeeds[t-g_track], seed)) return false;
    const auto* xy = reinterpret_cast<const float*>(e + isaac::kEntPosition);
    if (t->cur[0] != xy[0] || t->cur[1] != xy[1]) return false;
    const float scale = *reinterpret_cast<const float*>(Addr(0xC203E8)) *
        *reinterpret_cast<const float*>(Addr(0xC20420));
    if (!std::isfinite(scale) || scale < .25f || scale > 32.0f) return false;
    const J510Position now{xy[0],xy[1]};
    const auto target = highfps::j510_motion::At({t->prev[0],t->prev[1]}, now, alpha);
    const auto currentPixels = J510ProjectToDisplay(now,scale);
    const auto previewPixels = J510ProjectToDisplay(target,scale);
    const J510Position delta{previewPixels.x-currentPixels.x,previewPixels.y-currentPixels.y};
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) ||
        std::abs(delta.x) > 12 || std::abs(delta.y) > 12) return false;
    g_j510ProjectionPose = target;
    g_j510ProjectionDelta = delta;
    g_j510ProjectionOwner = e;
    g_j510ProjectionSeed = seed;
    g_j510ProjectionApplied = false;
    return true;
}

bool J510ProjectionApplies(void* player) {
    return g_j510ProjectionThread && GetCurrentThreadId() == g_j510ProjectionThread &&
        reinterpret_cast<uintptr_t>(player) == g_j510ProjectionOwner &&
        *reinterpret_cast<const std::uint32_t*>(g_j510ProjectionOwner+0x3EC) == g_j510ProjectionSeed &&
        !*reinterpret_cast<const std::uint8_t*>(Addr(0xC9E6DD));
}
void __fastcall J510ProjectedBody(void* player, void*, const J510Position* offset) {
    if (offset && J510ProjectionApplies(player) &&
        (_ReturnAddress() == Addr(0x41AED1) || _ReturnAddress() == Addr(0x8196C0))) {
        const J510Position changed{offset->x+g_j510ProjectionDelta.x,offset->y+g_j510ProjectionDelta.y};
        g_j510ProjectionApplied = true;
        ++g_j510ProjectedBodies;
        g_j510NativePlayerDraw(player, nullptr, &changed);
        return;
    }
    g_j510NativePlayerDraw(player, nullptr, offset);
}
bool __fastcall J510ProjectedShadow(void* player, void*, const J510Position* offset) {
    if (offset && J510ProjectionApplies(player) &&
        _ReturnAddress() == Addr(0x41B109)) {
        const J510Position changed{offset->x+g_j510ProjectionDelta.x,offset->y+g_j510ProjectionDelta.y};
        return g_j510NativePlayerShadow(player,nullptr,&changed);
    }
    return g_j510NativePlayerShadow(player,nullptr,offset);
}
__declspec(naked) void J510ProjectGenericShadow() {
    __asm {
        addss xmm0, dword ptr [eax]
        addss xmm3, dword ptr [eax+4]
        pushfd
        cmp dword ptr [g_j510ProjectionThread], 0
        je resume_shadow
        push edx
        mov edx, dword ptr fs:[24h]
        cmp edx, dword ptr [g_j510ProjectionThread]
        jne restore_shadow
        mov edx, dword ptr [ebx+54h]
        mov edx, dword ptr [edx+edi*4]
        cmp edx, dword ptr [g_j510ProjectionOwner]
        jne restore_shadow
        mov edx, dword ptr [edx+3ech]
        cmp edx, dword ptr [g_j510ProjectionSeed]
        jne restore_shadow
        addss xmm0, dword ptr [g_j510ProjectionDelta]
        addss xmm3, dword ptr [g_j510ProjectionDelta+4]
        inc dword ptr [g_j510ProjectedShadows]
    restore_shadow:
        pop edx
    resume_shadow:
        popfd
        jmp dword ptr [g_j510ShadowContinuation]
    }
}
bool InstallJ510MotionProjection() {
    constexpr std::uint8_t bodyEntry[]{0x55,0x8b,0xec,0x6a,0xff};
    constexpr std::uint8_t bodySite[]{0xff,0x75,0x0c,0xff,0x50,0x14};
    constexpr std::uint8_t directBodySite[]{0x57,0x8b,0x0c,0x88,0x8b,0x01,0xff,0x50,0x14};
    constexpr std::uint8_t modsOption[]{0x8a,0x80,0x65,0xa6,0x02,0x00};
    constexpr std::uint8_t genericShadow[]{0xf3,0x0f,0x58,0x00,0xf3,0x0f,0x58,0x58,0x04};
    if (__readfsdword(0x24)!=GetCurrentThreadId() ||
        !Expect(0x7953D0,bodyEntry,sizeof bodyEntry,"J510 player render ABI") ||
        !Expect(0x41AECB,bodySite,sizeof bodySite,"J510 ordinary entity body caller") ||
        !Expect(0x8196B7,directBodySite,sizeof directBodySite,"J510 direct player draw caller") ||
        !Expect(0x9022D0,modsOption,sizeof modsOption,"J510 active mods option field")) return false;
    const auto a = std::uint32_t(reinterpret_cast<uintptr_t>(Addr(0x7953D0)));
    const auto b = std::uint32_t(reinterpret_cast<uintptr_t>(Addr(0x7967F0)));
    const auto h1 = std::uint32_t(reinterpret_cast<uintptr_t>(&J510ProjectedBody));
    const auto h2 = std::uint32_t(reinterpret_cast<uintptr_t>(&J510ProjectedShadow));
    const auto d = std::uint32_t(reinterpret_cast<uintptr_t>(&J510ProjectGenericShadow) -
        (reinterpret_cast<uintptr_t>(Addr(0x41B270))+5));
    std::uint8_t branch[]{0xe9,0,0,0,0,0x90,0x90,0x90,0x90};
    std::memcpy(branch+1,&d,4);
    g_j510NativePlayerDraw = reinterpret_cast<J510PlayerDraw>(a);
    g_j510NativePlayerShadow = reinterpret_cast<J510PlayerShadow>(b);
    g_j510ShadowContinuation = reinterpret_cast<uintptr_t>(Addr(0x41B279));
    const J510Patch patches[]{
        {0xB8F274,reinterpret_cast<const std::uint8_t*>(&a),reinterpret_cast<const std::uint8_t*>(&h1),4},
        {0xB8F278,reinterpret_cast<const std::uint8_t*>(&b),reinterpret_cast<const std::uint8_t*>(&h2),4},
        {0x41B270,genericShadow,branch,sizeof branch},
    };
    g_j510ProjectionInstalled = CommitJ510Patches(patches);
    return g_j510ProjectionInstalled;
}
