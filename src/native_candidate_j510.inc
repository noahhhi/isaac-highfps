using J510Native = void(__cdecl*)();
J510Native g_j510Update = nullptr, g_j510Render = nullptr, g_j510Gc = nullptr;
bool g_j510NeedsGc = true, g_j510CanDraw = true, g_j510World = false, g_j510Extra = false;
double g_j510Sample = 0.0;
double g_j510DrawSample = 0.0, g_j510LastPoseSample = 0.0, g_j510DrawLead = 0.002;
std::uint32_t g_j510Updates = 0, g_j510Pure = 0, g_j510Canonical = 0;
double g_j510Report = 0.0;
double g_j510WorkBegan = 0.0, g_j510NativeUntil = 0.0;
unsigned g_j510SlowFrames = 0;
void* g_j510WindowPresent = nullptr;
#include "native_menu_j510.inc"

// The same nested load accounting as J374, with J510 entry points and stack
// cleanup verified separately. All detours commit together with the frame loop.
constexpr uintptr_t kJ510PendingStart = 0x96F340;
constexpr uintptr_t kJ510PendingStartFlag = 0x4B408;
void __fastcall HookedJ510PendingStart(void* self, void*) {
    if (!*reinterpret_cast<const uint8_t*>(static_cast<const uint8_t*>(self) + kJ510PendingStartFlag)) {
        g_pendingGameStartOriginal(self, nullptr);
        return;
    }
    BeginLifecycleLoad("game-start-continue");
    __try { g_pendingGameStartOriginal(self, nullptr); }
    __finally { EndLifecycleLoad(); }
}
const LifecycleHookSite kJ510LifecycleSites[] = {
    {0x747630, {0x55,0x8b,0xec,0x6a,0xff}, 5, reinterpret_cast<void*>(&HookedChangeRoom), "J510 Level::ChangeRoom"},
    {0x74C3F0, {0x53,0x8b,0xdc,0x83,0xec,0x08}, 6, reinterpret_cast<void*>(&HookedInitLevel), "J510 Level::Init"},
    {0x704600, {0x55,0x8b,0xec,0x6a,0xff}, 5, reinterpret_cast<void*>(&HookedStartRoom), "J510 Game::StartRoomTransition"},
    {0x704A60, {0x55,0x8b,0xec,0x83,0xe4,0xf8}, 6, reinterpret_cast<void*>(&HookedStartStage), "J510 Game::StartStageTransition"},
    {0x6FCE80, {0x53,0x8b,0xdc,0x83,0xec,0x08}, 6, reinterpret_cast<void*>(&HookedRestoreState), "J510 Game::RestoreState"},
    {0x6FD6C0, {0x55,0x8b,0xec,0x6a,0xff}, 5, reinterpret_cast<void*>(&HookedRerunState), "J510 Game::StartFromRerunState"},
    {kJ510PendingStart, {0x53,0x8b,0xdc,0x83,0xec,0x08}, 6, reinterpret_cast<void*>(&HookedJ510PendingStart), "J510 Manager::ProcessPendingGameStart"},
};
constexpr size_t kJ510LifecycleHookCount = sizeof kJ510LifecycleSites / sizeof kJ510LifecycleSites[0];

void LogJ510Transition(const char* pass, double now) {
    if (!g_cfg.diag) return;
    const auto game = GameObj();
    if (game < 0x10000) return;
    const auto* room = reinterpret_cast<const uint32_t*>(game + 0x1B83C);
    const auto* stage = reinterpret_cast<const uint32_t*>(game + 0x1BA78);
    static uint32_t lastActiveTick = 0;
    const auto tick = FrameCounter();
    const bool running = room[0] || stage[0];
    if (running) lastActiveTick = tick;
    else if (!lastActiveTick || tick - lastActiveTick > 1) return;
    float alpha;
    std::memcpy(&alpha, room + 3, sizeof alpha);
    Log("[j510-transition] pass=%s t=%.6f tick=%u gf=%u serial=%llu room=%u count=%u total=%u alpha=%.6f stage=%u scount=%u stotal=%u index=%d",
        pass, now, tick, GameFrame(), g_presentSerial,
        room[0], room[1], room[2], alpha, stage[0], stage[1], stage[2], int(RoomIdentity(0x18304)));
}

using J510Scroll = highfps::camera_presentation::Point;
struct J510RoomKey {
    uintptr_t game, room, descriptor;
    uint64_t epoch;
    uint32_t index, dimension, entered, seed;

    bool operator==(const J510RoomKey& b) const {
        return game == b.game && room == b.room && descriptor == b.descriptor && epoch == b.epoch &&
               index == b.index && dimension == b.dimension && entered == b.entered && seed == b.seed;
    }
};
J510RoomKey g_j510HistoryRoom{};
J510RoomKey g_j510PressureRoom{};
highfps::j510::PressurePromotion g_j510PressurePromotion{};
highfps::j510::PresentedEndpoint g_j510PresentedEndpoint{};
highfps::j510::PlayerRest g_j510PlayerRest{};
std::uint32_t g_j510Promoted = 0, g_j510Held = 0;
std::uint32_t g_j510MovingVeto = 0, g_j510Tears = 0, g_j510Effects = 0;
std::uint32_t g_j510CameraVeto = 0, g_j510PromotionWithoutSwap = 0;
std::uint32_t g_j510TrackSeeds[kTrackSlots]{};
double g_j510LoadBeforeWork = 0;
highfps::camera_presentation::History g_j510Camera{};
uint32_t g_j510HistoryFrame = 0;

J510RoomKey ReadJ510Room() {
    J510RoomKey key{};
    key.game = GameObj();
    key.epoch = g_lifecycleEpoch;
    if (key.game < 0x10000) return key;
    key.room = *reinterpret_cast<const uintptr_t*>(key.game + 0x18300);
    key.index = *reinterpret_cast<const uint32_t*>(key.game + 0x18304);
    key.dimension = *reinterpret_cast<const uint32_t*>(key.game + 0x1830C);
    if (key.room < 0x10000) return key;
    key.entered = *reinterpret_cast<const uint32_t*>(key.room + 0x11F0);
    key.descriptor = *reinterpret_cast<const uintptr_t*>(key.room + 0x04);
    if (key.descriptor >= 0x10000)
        key.seed = *reinterpret_cast<const uint32_t*>(key.descriptor + 0x5C);
    return key;
}

float* J510ScrollField(const J510RoomKey& key) {
    return reinterpret_cast<float*>(key.room + 0x1204);
}

J510Scroll ReadJ510Scroll(const J510RoomKey& key) {
    const auto* xy = J510ScrollField(key);
    return {xy[0], xy[1]};
}

void ResetJ510History() {
    g_j510HistoryRoom = {};
    g_j510Camera = {};
    g_j510PresentedEndpoint.Invalidate();
    g_j510PlayerRest.Reset();
}

#include "native_player_projection_j510_local.inc"
#include "native_scroll_projection_j510_local.inc"

void RecordJ510MotionPresent(double presented) {
    static unsigned records = 0;
    if (!g_inRenderPhase || GameObj() < 0x10000 || records >= 8000) return;
    bool recorded = false;
    ForEachEntity([&](uintptr_t e) {
        if (recorded || *reinterpret_cast<const std::uint32_t*>(e + isaac::kEntType) != 1) return;
        const auto* xy = reinterpret_cast<const float*>(e + isaac::kEntPosition);
        const bool projected = g_j510ProjectionThread && g_j510ProjectionApplied && e == g_j510ProjectionOwner;
        const auto room = ReadJ510Room();
        const bool scrollView = g_j510ScrollView.armed && g_j510ScrollView.completed;
        const auto scroll = scrollView ? g_j510ScrollView.visual :
            (room.room >= 0x10000 ? ReadJ510Scroll(room) : J510Scroll{});
        Log("[j510-motion-present] t=%.6f serial=%llu tick=%u gf=%u phase=%s held=%u rest=%u pressure=%u projected=%u x=%.6f y=%.6f room=%u seed=%u sx=%.6f sy=%.6f",
            presented, g_presentSerial, g_j510Updates, GameFrame(),
            g_luaFrameIntermediate ? "extra" : "native",
            unsigned(g_j510PresentedEndpoint.Hold(g_j510Updates)), g_j510PlayerRest.ticks,
            unsigned(g_j510PressurePromotion.active), unsigned(projected),
            projected ? g_j510ProjectionPose.x : xy[0], projected ? g_j510ProjectionPose.y : xy[1],
            room.index, room.seed, scroll.x, scroll.y);
        recorded = true;
        ++records;
    });
}

double J510DrawSample(double now) {
    using namespace highfps::refresh;
    if (g_presentAnchorValid && g_presentVsync && !g_swapPacing.useSoftware &&
        g_presentationPolicy.pacer == Pacer::External && g_presentationPolicy.vrr != VRR::ReportedActive &&
        Equal(g_presentationPolicy.output, g_displayReading.display.mode))
        now = FixedAnimationSample(now, g_lastPresent, g_presentationPolicy.output, g_j510DrawLead);
    return now < g_j510LastPoseSample ? g_j510LastPoseSample : now;
}

bool J510PromotionStable(const J510RoomKey& room) {
    return g_j510World && room.game >= 0x10000 && room.room >= 0x10000 &&
        room.descriptor >= 0x10000 && !g_loadDepth && g_j510Camera.valid &&
        room == g_j510HistoryRoom && !NativeGamePaused19715() &&
        *reinterpret_cast<const std::uint32_t*>(room.game + 0x1B83C) == 0 &&
        *reinterpret_cast<const std::uint32_t*>(room.game + 0x1BA78) == 0;
}

bool ObserveJ510Promotion(const J510RoomKey& room) {
    if (!g_cfg.j510PressurePromotion) return false;
    const bool stable = J510PromotionStable(room);
    if (!stable || !(room == g_j510PressureRoom)) {
        g_j510PressureRoom = room;
        g_j510PressurePromotion.Observe(false, 0, 0);
        return false;
    }
    g_j510PressurePromotion.Observe(true, g_j510Tears, g_j510Effects);
    const auto scroll = ReadJ510Scroll(room);
    const auto prev = g_j510Camera.previous, cur = g_j510Camera.current;
    const bool still = scroll.x == cur.x && scroll.y == cur.y &&
        prev.x == cur.x && prev.y == cur.y;
    if (g_j510Extra && g_j510PressurePromotion.active && !still) {
        const double sample = J510DrawSample(Now());
        const auto alpha = highfps::RenderAlpha(sample, g_lastWrapperTime, g_simClock.period);
        if (!g_overlayFailed && PrepareJ510Scroll(room,cur,alpha) && PrepareJ510Projection(room,alpha)) {
            g_j510DrawSample = sample;
            return true;
        }
        g_j510ScrollView.Clear();
        ++g_j510CameraVeto;
    }
    if (!g_j510PressurePromotion.ShouldPromote(g_j510Extra && !g_overlayFailed, stable, still)) return false;
    const auto earliest = highfps::RenderAlpha(J510DrawSample(Now()), g_lastWrapperTime, g_simClock.period);
    if (g_cfg.j510MotionGuard && !g_j510PlayerRest.CanShowEndpoint(earliest)) {
        const auto sample = J510DrawSample(Now());
        if (g_cfg.j510MotionProjection &&
            PrepareJ510Projection(room, highfps::RenderAlpha(sample, g_lastWrapperTime, g_simClock.period))) {
            g_j510DrawSample = sample;
            return true;
        }
        ++g_j510MovingVeto;
        return false;
    }
    return true;
}

bool __cdecl SkipJ510WindowPresent() {
    const bool missedProjection = g_j510ProjectionThread && !g_luaFrameIntermediate && !g_j510ProjectionApplied;
    return (g_hiddenFixedRender || missedProjection || !g_j510ScrollView.CanSwap() ||
            (g_promotingCanonical && g_promotionEpoch != g_lifecycleEpoch)) &&
           GetCurrentThreadId() == g_gameThreadId;
}

__declspec(naked) void RunJ510WindowPresent() {
    __asm {
        pushfd
        pushad
        call SkipJ510WindowPresent
        test al, al
        jne skip_present
        popad
        popfd
        jmp dword ptr [g_j510WindowPresent]
    skip_present:
        popad
        popfd
        mov eax, 1
        ret
    }
}

void CallJ510Gc() {
    if (GameObj() >= 0x10000) g_j510Gc();
}

void DrawJ510(bool pure, bool hidden) {
    g_promotionEpoch = g_lifecycleEpoch;
    g_promotingCanonical = !hidden;
    g_hiddenFixedRender = hidden;
    g_inRenderPhase = 1;
    g_luaFrameIntermediate = pure;
    highfps::menu_presentation::Scope menuScope;
    highfps::carousel_presentation::Scope carouselScope;
    bool shaderPass = false;
    __try {
        if (!pure) TickJ510MenuScalars();
        else if (g_cfg.interpolate) {
            const auto menu = MenuPresentationContext();
            if (g_menuPresentationEnabled &&
                menuScope.Begin(g_menuHistory, menu, reinterpret_cast<float*>(menu.menu + 0x44),
                    g_j510DrawSample, g_lastWrapperTime, true) && g_cfg.diag && g_menuPresentLines++ < 12000) {
                const auto* viewport = reinterpret_cast<const float*>(menu.menu + 0x44);
                Log("[j510-menu-present] t=%.6f page=%d x=%.6f y=%.6f",
                    g_j510DrawSample, *reinterpret_cast<const int*>(menu.menu + 0x40), viewport[0], viewport[1]);
            }
            const auto carousel = CarouselPresentationContext();
            if (g_carouselPresentationEnabled &&
                carouselScope.Begin(g_carouselHistory, carousel, reinterpret_cast<float*>(carousel.positions.begin),
                    reinterpret_cast<float*>(carousel.scales.begin), g_j510DrawSample, g_lastWrapperTime, true) &&
                g_cfg.diag && g_carouselPresentLines++ < 9000)
                LogCarousel("present", carousel,
                    highfps::menu_presentation::Alpha60(g_j510DrawSample, g_lastWrapperTime), g_j510DrawSample);
        }
        shaderPass = true;
        highfps::j510_locations::Begin();
        g_j510Render();
    }
    __finally {
        if (shaderPass) highfps::j510_locations::End();
        if (carouselScope.Applied() && !carouselScope.Restore(CarouselPresentationContext()))
            g_carouselPresentationEnabled = false;
        if (menuScope.Applied() && !menuScope.Restore(MenuPresentationContext()))
            g_menuPresentationEnabled = false;
        g_hiddenFixedRender = false;
        g_promotingCanonical = false;
        g_inRenderPhase = 0;
        g_luaFrameIntermediate = 0;
    }
    if (!pure) {
        SampleMenuPresentation();
        SampleCarouselPresentation();
    }
    if (g_cfg.diag) LogJ510Transition(pure ? "extra" : "canonical", Now());
}

void SampleJ510Pose(const J510RoomKey& room) {
    const auto frame = GameFrame();
    if (!(room == g_j510HistoryRoom) || frame - g_j510HistoryFrame > 1) {
        std::memset(g_track, 0, sizeof g_track);
        g_trackStamp = 0;
        g_j510Camera = {};
        g_j510HistoryRoom = room;
        g_j510PlayerRest.Reset();
    }
    g_j510HistoryFrame = frame;
    ++g_trackStamp;
    g_j510Tears = g_j510Effects = 0;
    g_j510PlayerCount = 0;
    g_j510ProjectionPlayer = 0;
    bool sawPlayer = false, playersKnown = true, playerMoved = false;
    ForEachEntity([&](uintptr_t entity) {
        const auto type = *reinterpret_cast<const std::uint32_t*>(entity + isaac::kEntType);
        g_j510Tears += type == 2;
        g_j510Effects += type == 1000;
        sawPlayer |= type == 1;
        if (type == 1) { ++g_j510PlayerCount; g_j510ProjectionPlayer = entity; }
        auto* track = FindTrack(entity, true);
        if (!track) { if (type == 1) playersKnown = false; return; }
        const auto fingerprint = Fingerprint(entity);
        const auto seed = *reinterpret_cast<const std::uint32_t*>(entity + 0x3EC);
        auto& savedSeed = g_j510TrackSeeds[track - g_track];
        const auto* position = reinterpret_cast<float*>(entity + isaac::kEntPosition);
        const double dx = double(position[0]) - track->cur[0];
        const double dy = double(position[1]) - track->cur[1];
        const bool same = highfps::j510::SamePoseIdentity(track->fingerprint, fingerprint, savedSeed, seed) &&
                          track->stamp + 1 == g_trackStamp &&
                          dx * dx + dy * dy <= kMaxInterpDistanceSq;
        if (type == 1) {
            playersKnown &= same && std::isfinite(position[0]) && std::isfinite(position[1]);
            // Subpixel numerical noise must not reset settled stationary shots.
            playerMoved |= dx * dx + dy * dy > 0.015625 * 0.015625;
        }
        for (unsigned axis = 0; axis < 2; ++axis) {
            track->prev[axis] = same ? track->cur[axis] : position[axis];
            track->cur[axis] = position[axis];
        }
        track->fingerprint = fingerprint;
        track->stamp = g_trackStamp;
        savedSeed = seed;
    });
    g_j510PlayerRest.Observe(sawPlayer && playersKnown, playerMoved);
}

struct J510Restore {
    uintptr_t entity;
    std::uint32_t fingerprint, seed;
    float xy[2];
};
J510Restore g_j510Restore[kTrackSlots];

void ApplyJ510Pose(float fraction) {
    std::memset(g_j510Restore, 0, sizeof g_j510Restore);
    ForEachEntity([fraction](uintptr_t entity) {
        const auto* track = FindTrack(entity, false);
        if (!track || track->stamp != g_trackStamp) return;
        const auto seed = *reinterpret_cast<const std::uint32_t*>(entity + 0x3EC);
        if (!highfps::j510::SamePoseIdentity(track->fingerprint, Fingerprint(entity),
                g_j510TrackSeeds[track - g_track], seed)) return;
        auto* xy = reinterpret_cast<float*>(entity + isaac::kEntPosition);
        g_j510Restore[track - g_track] = {entity, track->fingerprint, seed, {xy[0], xy[1]}};
        for (unsigned axis = 0; axis < 2; ++axis)
            xy[axis] = track->prev[axis] + fraction * (track->cur[axis] - track->prev[axis]);
    });
}

void RestoreJ510Pose() {
    ForEachEntity([](uintptr_t entity) {
        const auto* track = FindTrack(entity, false);
        if (!track) return;
        const auto& saved = g_j510Restore[track - g_track];
        if (entity != saved.entity || !highfps::j510::SamePoseIdentity(saved.fingerprint, Fingerprint(entity),
                saved.seed, *reinterpret_cast<const std::uint32_t*>(entity + 0x3EC))) return;
        auto* xy = reinterpret_cast<float*>(entity + isaac::kEntPosition);
        xy[0] = saved.xy[0];
        xy[1] = saved.xy[1];
    });
}

bool DrawJ510Canonical(bool hidden) {
    const auto before = ReadJ510Room();
    DrawJ510(false, hidden);
    const auto after = ReadJ510Room();
    if (!(before == after)) {
        g_j510World = false;
        ResetJ510History();
        return false;
    }
    if (g_j510World && after == g_j510HistoryRoom && !NativeGamePaused19715()) {
        g_j510Camera.Sample(ReadJ510Scroll(after));
        if (g_cfg.diag && g_j510Camera.valid)
            Log("[j510-camera-tick] tick=%u gf=%u index=%d dim=%u entered=%u seed=%u x=%.6f y=%.6f",
                g_j510Updates, GameFrame(), int(after.index), after.dimension, after.entered, after.seed,
                g_j510Camera.current.x, g_j510Camera.current.y);
    } else {
        ResetJ510History();
    }
    return true;
}

void __cdecl RunJ510Update() {
    ConfigureThreading();
    ConfigurePresentation();
    const double wallNow = Now();
    g_j510WorkBegan = wallNow;
    g_j510LoadBeforeWork = g_excludedLoadSeconds;
    g_frameWorkStarted = wallNow;
    g_j510Sample = wallNow;
    g_frameSample = g_j510Sample;
    if (g_simClock.next == 0.0) g_simClock.next = g_j510Sample;
    ApplyLifecycleTime();
    g_j510CanDraw = false;
    g_j510NeedsGc = false;
    g_j510World = GameObj() >= 0x10000 && !NativeGamePaused19715();
    auto mode = g_presentationPolicy.output.Hz();
    g_j510Extra = !g_overlayFailed && wallNow >= g_j510NativeUntil && (mode == 0 || mode > 60.0);
    g_j510World = g_j510Extra && g_j510World && g_cfg.interpolate;
    const bool due = g_simClock.Due(g_j510Sample);
    if (!due) {
        g_j510CanDraw = g_j510Extra && g_canonicalPresentation.Ready(g_lifecycleEpoch);
        if (!g_j510CanDraw) Sleep(1);
        return;
    }
    for (unsigned consumed = 0; consumed < 8; ++consumed) {
        g_lastWrapperTime = g_simClock.Consume(g_j510Sample);
        const auto beforeRoom = ReadJ510Room();
        g_j510Update();
        ++g_j510Updates;
        if (g_cfg.diag) LogJ510Transition("update", Now());
        const bool loaded = ApplyLifecycleTime() > 0.0;
        const auto room = ReadJ510Room();
        const bool same = beforeRoom == room;
        g_j510World = same && g_j510World && !NativeGamePaused19715();
        if (g_j510World && room.room >= 0x10000 && room.descriptor >= 0x10000) SampleJ510Pose(room);
        else ResetJ510History();
        if (loaded || !same || !g_simClock.Due(g_j510Sample) || consumed == 7) break;
        const auto epoch = g_lifecycleEpoch;
        const bool complete = DrawJ510Canonical(true);
        ++g_j510Canonical;
        CallJ510Gc();
        const bool loadedDuringDraw = ApplyLifecycleTime() > 0.0;
        g_canonicalPresentation.Complete(epoch, g_lifecycleEpoch);
        g_canonicalPresentation.valid &= complete;
        // This tick has already rendered and collected Lua. A callback can
        // load another scene here; do not render the same tick a second time.
        if (!complete || loadedDuringDraw) return;
    }
    g_j510CanDraw = true;
    g_j510NeedsGc = true;
    if (g_j510Sample >= g_j510Report) {
        g_j510Report = g_j510Sample + 1;
        Log("[j510-loop] update=%u canonical=%u extra=%u manager=%u game=%u world=%u present_extra=%u",
            g_j510Updates, g_j510Canonical, g_j510Pure, ReadJ510ManagerCounter(), GameFrame(),
            unsigned(g_j510World), unsigned(g_j510Extra));
        highfps::j510_locations::Report(&Log);
        Log("[j510-pressure-promotion] active=%u promoted=%u held=%u movingVeto=%u emptyOverlay=%llu projected=%u body=%u shadow=%u cameraVeto=%u noSwap=%u scrollPromoted=%u scrollVeto=%u scrollScoped=%u",
            unsigned(g_j510PressurePromotion.active), g_j510Promoted, g_j510Held,
            g_j510MovingVeto, static_cast<unsigned long long>(g_j510EmptyOverlaySkips),
            g_j510ProjectedPresents, g_j510ProjectedBodies, g_j510ProjectedShadows,
            g_j510CameraVeto, g_j510PromotionWithoutSwap, g_j510ScrollPromoted, g_j510ScrollVeto, g_j510ScrollScoped);
    }
}

void ObserveJ510SlowDraw(double finish, double swapBefore) {
    const double elapsed = finish - g_j510WorkBegan - (g_prof.swapSum - swapBefore) -
        (g_excludedLoadSeconds - g_j510LoadBeforeWork);
    if (elapsed < g_simClock.period) g_j510SlowFrames = 0;
    else if (g_cfg.overloadFallback && ++g_j510SlowFrames >= 5) {
        g_j510NativeUntil = finish + 0.25;
        g_j510SlowFrames = 0;
    }
}

void __cdecl RunJ510Render() {
    if (!g_j510CanDraw) return;
    const double swapBefore = g_prof.swapSum;
    if (g_j510NeedsGc) {
        const auto epoch = g_lifecycleEpoch;
        const auto beforeRoom = ReadJ510Room();
        const auto serialBefore = g_presentSerial;
        g_j510ProjectionOwner = 0;
        g_j510ScrollView.Clear();
        const bool promote = ObserveJ510Promotion(beforeRoom);
        bool complete = false;
        if (promote && g_j510ProjectionOwner) g_j510ProjectionThread = g_gameThreadId;
        __try { complete = DrawJ510Canonical(g_j510Extra && !promote); }
        __finally { g_j510ProjectionThread = 0; }
        const bool scrolled = g_j510ScrollView.armed && g_j510ScrollView.completed;
        if (g_j510ScrollView.armed && !scrolled) ++g_j510ScrollVeto;
        g_j510ScrollView.Clear();
        ++g_j510Canonical;
        CallJ510Gc();
        g_j510NeedsGc = false;
        ApplyLifecycleTime();
        g_canonicalPresentation.Complete(epoch, g_lifecycleEpoch);
        g_canonicalPresentation.valid &= complete && beforeRoom == ReadJ510Room();
        if (promote && g_presentSerial != serialBefore) {
            g_j510PresentedEndpoint.Note(g_j510Updates,
                g_canonicalPresentation.Ready(g_lifecycleEpoch), g_presentSerial - serialBefore);
            ++g_j510Promoted;
            g_j510ProjectedTickValid = g_j510ProjectionOwner && g_j510ProjectionApplied &&
                g_canonicalPresentation.Ready(g_lifecycleEpoch) && g_presentSerial == serialBefore + 1;
            if (g_j510ProjectedTickValid) {
                ++g_j510ProjectedPresents;
                g_j510ProjectionTick = g_j510Updates;
                g_j510LastPoseSample = g_j510DrawSample;
            }
            if (scrolled && g_j510ProjectedTickValid) {
                g_j510ScrollTick = g_j510Updates;
                ++g_j510ScrollPromoted;
            }
            ObserveJ510SlowDraw(Now(), swapBefore);
            return; // Native R already presented; never issue a second swap.
        }
        if (promote) ++g_j510PromotionWithoutSwap;
        if (!g_j510Extra) return;
        if (g_overlayFailed || !g_canonicalPresentation.Ready(g_lifecycleEpoch)) {
            ResetJ510History();
            return;
        }
    }
    const auto room = ReadJ510Room();
    const bool holdEndpoint = g_j510PresentedEndpoint.Hold(g_j510Updates);
    if (holdEndpoint) ++g_j510Held;
    const bool pose = !holdEndpoint && g_j510World && g_j510Camera.valid && room == g_j510HistoryRoom &&
                      GameFrame() == g_j510HistoryFrame && !NativeGamePaused19715();
    const double drawBegan = Now();
    g_j510DrawSample = J510DrawSample(drawBegan);
    g_j510LastPoseSample = g_j510DrawSample;
    g_frameSample = g_j510DrawSample;
    const auto alpha = highfps::RenderAlpha(g_j510DrawSample, g_lastWrapperTime, g_simClock.period);
    const bool projectedExtra = holdEndpoint && g_j510ProjectedTickValid &&
        g_j510ProjectionTick == g_j510Updates && PrepareJ510Projection(room,alpha);
    g_j510ScrollView.Clear();
    if (projectedExtra && g_j510ScrollTick==g_j510Updates)
        PrepareJ510Scroll(room,g_j510Camera.previous,alpha);
    const auto nativeScroll = pose ? ReadJ510Scroll(room) : J510Scroll{};
    const auto previewScroll = pose ? g_j510Camera.Present(alpha) : J510Scroll{};
    __try {
        if (projectedExtra) g_j510ProjectionThread = g_gameThreadId;
        if (pose) {
            ApplyJ510Pose(alpha);
            auto* scroll = J510ScrollField(room);
            scroll[0] = previewScroll.x;
            scroll[1] = previewScroll.y;
        }
        if (pose && g_cfg.diag) {
            Log("[j510-camera-present] serial=%llu tick=%u sample=%.6f gf=%u seed=%u alpha=%.6f x=%.6f y=%.6f",
                g_presentSerial + 1, g_j510Updates, g_j510DrawSample, GameFrame(), room.seed,
                alpha, previewScroll.x, previewScroll.y);
            ForEachEntity([alpha](uintptr_t entity) {
                if (*reinterpret_cast<uint32_t*>(entity + isaac::kEntType) != 1) return;
                const auto* xy = reinterpret_cast<float*>(entity + isaac::kEntPosition);
                Log("[j510-pose] sample=%.6f gf=%u alpha=%.6f x=%.3f y=%.3f", g_j510DrawSample, GameFrame(), alpha, xy[0], xy[1]);
            });
        }
        DrawJ510(true, false);
    }
    __finally {
        g_j510ProjectionThread = 0;
        if (g_j510ScrollView.armed && !g_j510ScrollView.completed) ++g_j510ScrollVeto;
        g_j510ScrollView.Clear();
        if (pose) {
            if (room == ReadJ510Room()) {
                const auto drawn = ReadJ510Scroll(room);
                auto* scroll = J510ScrollField(room);
                scroll[0] = nativeScroll.x;
                scroll[1] = nativeScroll.y;
                RestoreJ510Pose();
                if (g_cfg.diag && (drawn.x != previewScroll.x || drawn.y != previewScroll.y))
                    Log("[j510-camera-write] extra tick=%u expected=(%.6f,%.6f) actual=(%.6f,%.6f)",
                        g_j510Updates, previewScroll.x, previewScroll.y, drawn.x, drawn.y);
            } else {
                ResetJ510History();
            }
        }
    }
    ++g_j510Pure;
    const double finished = Now();
    const double swap = g_prof.swapSum - swapBefore;
    const double pureWork = finished - drawBegan - swap;
    const double bounded = pureWork > .006 ? .006 : (pureWork > .001 ? pureWork : .001);
    g_j510DrawLead = g_j510DrawLead * .8 + bounded * .2;
    ObserveJ510SlowDraw(finished, swapBefore);
}

void __cdecl RunJ510Gc() {
    if (g_j510NeedsGc) CallJ510Gc();
    g_j510NeedsGc = false;
}

bool InstallJ510Candidate() {
    using namespace highfps::j510;
    constexpr std::uint8_t seedGetter[]{0x8b,0x81,0xec,0x03,0x00,0x00,0xc3};
    if (!Expect(0x427C40, seedGetter, sizeof seedGetter, "J510 entity InitSeed field")) return false;
    const auto engineSlot = std::uint32_t(reinterpret_cast<uintptr_t>(Addr(0xC99554)));
    std::uint8_t engineLoad[]{0x8b,0x0d,0,0,0,0};
    std::memcpy(engineLoad+2, &engineSlot, 4);
    constexpr std::uint8_t listenerBranch[]{0x8b,0x01,0xf6,0x00,0x04,0x74,0x28};
    g_j510EmptyOverlayVerified =
        Expect(0x86D1BB, engineLoad, sizeof engineLoad, "J510 Lua engine slot") &&
        Expect(0x86D1D5, listenerBranch, sizeof listenerBranch, "J510 Lua post-render listener bit");
    LoopSites live;
    const auto read = [](auto& bytes, uintptr_t site) { std::memcpy(bytes.data(), Addr(site), bytes.size()); };
    read(live.update, kUpdateSite);
    read(live.render, kRenderSite);
    read(live.gc, kGcSite);
    read(live.gcGuard, kGcGuard);
    read(live.limitTest, kLimitTest);
    read(live.limitGuard, kLimitGuard);
    read(live.loopBranch, kLoopBranch);
    read(live.wrapper, kUpdateWrapper);
    LoopPlan plan;
    const auto address = [](J510Native call) { return uint32_t(reinterpret_cast<uintptr_t>(call)); };
    if (!PrepareLoopPlan(live, uint32_t(g_base),
            {address(&RunJ510Update), address(&RunJ510Render), address(&RunJ510Gc)}, plan)) return false;
    static const uint8_t gameCounter[] = {
        0x8b, 0xb7, 0x08, 0x65, 0x02, 0x00, 0xb8, 0x89, 0x88, 0x88, 0x88, 0xff, 0x87,
        0x0c, 0x65, 0x02, 0x00, 0x46, 0xf7, 0xee, 0x89, 0xb7, 0x08, 0x65, 0x02, 0x00,
    };
    uint8_t paused[] = {0x8b, 0xd1, 0x56, 0x8b, 0x35, 0, 0, 0, 0};
    const auto owner = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(Addr(kManager)));
    std::memcpy(paused + 5, &owner, sizeof owner);
    static const std::uint8_t luaPostRender[]{0xe8, 0x32, 0xa5, 0x16, 0x00};
    static const std::uint8_t push[]{0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec, 0x08};
    static const std::uint8_t pop[]{0x51, 0x83, 0x3d};
    static const std::uint8_t set[]{0x55, 0x8b, 0xec, 0x56, 0x8b, 0xf1, 0x57, 0x8b, 0x7d, 0x08, 0x89, 0x7e, 0x68};
    static const std::uint8_t clear[]{0x56, 0x8b, 0xf1, 0x51, 0xc7, 0x46, 0x20};
    static const std::uint8_t standard[]{0x55, 0x8b, 0xec, 0x6a, 0xff};
    static const std::uint8_t drainReturn[]{0xc2, 0x04, 0x00};
    static const std::uint8_t factoryReturn[]{0xc2, 0x14, 0x00};
    static const std::uint8_t originalPresent[]{0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8};
    // Native Room:GetRenderScrollOffset/GetSpawnSeed/GetFrameCount and the
    // room-index and dimension operands used by the engine's transition path.
    static const std::uint8_t scrollField[]{0x8d,0x81,0x04,0x12,0x00,0x00,0xc3};
    static const std::uint8_t seedField[]{0x8b,0x41,0x04,0x85,0xc0,0x75,0x01,0xc3,0x8b,0x40,0x5c,0xc3};
    static const std::uint8_t roomEntry[]{0x2b,0x81,0xf0,0x11,0x00,0x00,0xc3};
    static const std::uint8_t roomIndex[]{0x8b,0x81,0x04,0x83,0x01,0x00,0xc3};
    static const std::uint8_t roomDimension[]{0x3b,0xb0,0x0c,0x83,0x01,0x00};
    if (!Expect(0x431160, scrollField, sizeof scrollField, "J510 room scroll") ||
        !Expect(0x43A220, seedField, sizeof seedField, "J510 room descriptor and seed") ||
        !Expect(0x81C624, roomEntry, sizeof roomEntry, "J510 room entry frame") ||
        !Expect(0x43A2A0, roomIndex, sizeof roomIndex, "J510 room index") ||
        !Expect(0x7049B6, roomDimension, sizeof roomDimension, "J510 room dimension")) return false;
    if (!Expect(0x70269E, gameCounter, sizeof gameCounter, "J510 gameplay counter")) return false;
    if (!Expect(0x704190, paused, sizeof paused, "J510 pause predicate")) return false;
    if (!g_cfg.luaVanillaCadence || !g_cfg.luaOverlay ||
        !Expect(0x702C59, luaPostRender, sizeof luaPostRender, "J510 Lua post-render call") ||
        !Expect(0x68A760, push, sizeof push, "J510 GL target push") ||
        !Expect(0x68A7B0, pop, sizeof pop, "J510 GL target pop") ||
        !Expect(0xA319C0, set, sizeof set, "J510 GL target bind") ||
        !Expect(0xA31910, clear, sizeof clear, "J510 GL target clear") ||
        !Expect(0xA2CFB0, standard, sizeof standard, "J510 batch drain") ||
        !Expect(0xA2D1AF, drainReturn, sizeof drainReturn, "J510 batch drain argument cleanup") ||
        !Expect(0xA2B370, standard, sizeof standard, "J510 image factory") ||
        !Expect(0xA2B538, factoryReturn, sizeof factoryReturn, "J510 image factory argument cleanup") ||
        !Expect(0xAA10D0, originalPresent, sizeof originalPresent, "J510 window present")) return false;
    if (!ValidateJ510MenuLayout()) return false;
    const uint8_t pendingStartFlag[]{0x80,0xbf,0x08,0xb4,0x04,0x00,0x00,0x0f,0x84,0x40,0x06,0x00,0x00};
    const uint8_t pendingStartCall[]{0xe8,0x5d,0x4c,0x00,0x00};
    const uint8_t pendingStartReturn[]{0x8b,0xe5,0x5d,0x8b,0xe3,0x5b,0xc3};
    const uint8_t ret4[]{0xc2,0x04,0x00}, ret8[]{0xc2,0x08,0x00};
    const uint8_t ret12[]{0xc2,0x0c,0x00}, ret20[]{0xc2,0x14,0x00};
    if (!Expect(0x96F389, pendingStartFlag, sizeof pendingStartFlag, "J510 pending start flag") ||
        !Expect(0x96A6DE, pendingStartCall, sizeof pendingStartCall, "J510 pending start caller") ||
        !Expect(0x96F9ED, pendingStartReturn, sizeof pendingStartReturn, "J510 pending start return") ||
        !Expect(0x748414, ret8, sizeof ret8, "J510 room load return") ||
        !Expect(0x74D969, ret4, sizeof ret4, "J510 level load return") ||
        !Expect(0x704A4F, ret20, sizeof ret20, "J510 room transition return") ||
        !Expect(0x70501A, ret12, sizeof ret12, "J510 stage transition return") ||
        !Expect(0x6FD6BA, ret8, sizeof ret8, "J510 restore return") ||
        !Expect(0x6FDBE8, ret4, sizeof ret4, "J510 rerun return")) return false;
    for (const auto& site : kJ510LifecycleSites)
        if (!Expect(site.address, site.bytes, site.length, site.name)) return false;
    using namespace highfps::native_render_clock;
    constexpr unsigned vignetteOffsets[]{9, 23};
    constexpr unsigned characterOffsets[]{9, 35, 52, 78};
    constexpr unsigned portraitOffsets[]{12, 38, 77};
    const uint32_t delta = uint32_t(g_base) - kPreferredBase;
    uint8_t beforeVignette[sizeof kJ510VignetteBytes], beforeCharacter[sizeof kJ510CharacterBytes];
    uint8_t beforePortrait[sizeof kJ510PortraitBytes];
    RelocatedOriginal(beforeVignette, kJ510VignetteBytes, sizeof beforeVignette, vignetteOffsets, 2, delta);
    RelocatedOriginal(beforeCharacter, kJ510CharacterBytes, sizeof beforeCharacter, characterOffsets, 4, delta);
    RelocatedOriginal(beforePortrait, kJ510PortraitBytes, sizeof beforePortrait, portraitOffsets, 3, delta);
    static_assert(sizeof beforePortrait == sizeof kPortraitOriginal);
    uint8_t vignettePatch[sizeof beforeVignette], characterPatch[sizeof beforeCharacter];
    uint8_t portraitPatch[sizeof beforePortrait];
    std::memset(vignettePatch, 0x90, sizeof vignettePatch);
    std::memset(characterPatch, 0x90, sizeof characterPatch);
    std::memset(portraitPatch, 0x90, sizeof portraitPatch);
    const uint8_t vignetteLoad[]{0xf3,0x0f,0x10,0x4e,0x34};
    const uint8_t characterLoad[]{0xf3,0x0f,0x10,0x47,0x34,0x0f,0x28,0xc8};
    std::memcpy(vignettePatch, vignetteLoad, sizeof vignetteLoad);
    std::memcpy(characterPatch, characterLoad, sizeof characterLoad);
    constexpr size_t presentLength = sizeof originalPresent;
    auto* presentSite = Addr(0xAA10D0);
    constexpr size_t lifecycleOffset = (32 + kPortraitStubBytes + 15) & ~size_t(15);
    constexpr size_t trampolineLength = lifecycleOffset + 32 * kJ510LifecycleHookCount;
    auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, trampolineLength, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!trampoline) return false;
    std::memcpy(trampoline, originalPresent, presentLength);
    WriteRel32Jmp(trampoline + presentLength, presentSite + presentLength);
    auto* portraitStub = trampoline + 32;
    BuildPortraitStub(portraitStub, reinterpret_cast<uintptr_t>(portraitStub), beforePortrait,
        reinterpret_cast<uintptr_t>(&g_j510HoldPortrait),
        reinterpret_cast<uintptr_t>(Addr(kJ510Portrait + sizeof beforePortrait)));
    portraitStub[110] = 0x4e;
    Rel32(portraitPatch, reinterpret_cast<uintptr_t>(Addr(kJ510Portrait)), reinterpret_cast<uintptr_t>(portraitStub));
    const auto portraitCall = Relative(0xe8, uint32_t(reinterpret_cast<uintptr_t>(Addr(kJ510PortraitCall))),
        uint32_t(reinterpret_cast<uintptr_t>(&J510PortraitRender)));
    auto* lifecycle = trampoline + lifecycleOffset;
    uint8_t lifecyclePatches[kJ510LifecycleHookCount][6]{};
    for (size_t i = 0; i < kJ510LifecycleHookCount; ++i) {
        const auto& site = kJ510LifecycleSites[i];
        auto* entry = Addr(site.address);
        auto* stub = lifecycle + 32 * i;
        std::memcpy(stub, site.bytes, site.length);
        WriteRel32Jmp(stub + site.length, entry + site.length);
        Rel32(lifecyclePatches[i], reinterpret_cast<uintptr_t>(entry), reinterpret_cast<uintptr_t>(site.detour));
        lifecyclePatches[i][5] = 0x90;
    }
    DWORD unused = 0;
    if (!VirtualProtect(trampoline, trampolineLength, PAGE_EXECUTE_READ, &unused) ||
        !FlushInstructionCache(GetCurrentProcess(), trampoline, trampolineLength)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    g_gameThreadId = GetCurrentThreadId();
    g_j510Update = reinterpret_cast<J510Native>(plan.originalUpdate);
    g_j510Render = reinterpret_cast<J510Native>(plan.originalRender);
    g_j510Gc = reinterpret_cast<J510Native>(plan.originalGc);
    g_realSwapBuffers = g_nativeMeasureSwap.load(std::memory_order_acquire);
    g_pushRt = reinterpret_cast<PushRtFn>(Addr(0x68A760));
    g_popRt = reinterpret_cast<PopRtFn>(Addr(0x68A7B0));
    g_setRt = reinterpret_cast<SetRtFn>(Addr(0xA319C0));
    g_clearRt = reinterpret_cast<ClearFn>(Addr(0xA31910));
    g_flushRt = reinterpret_cast<FlushFn>(Addr(0xA2CFB0));
    g_createRt = reinterpret_cast<CreateRtFn>(Addr(0xA2B370));
    g_luaPostRenderTramp = reinterpret_cast<LuaPostRenderFn>(Addr(0x86D190));
    g_j510WindowPresent = trampoline;
    g_changeRoomOriginal = reinterpret_cast<ChangeRoomFn>(lifecycle);
    g_initLevelOriginal = reinterpret_cast<InitLevelFn>(lifecycle + 32);
    g_startRoomOriginal = reinterpret_cast<StartRoomFn>(lifecycle + 64);
    g_startStageOriginal = reinterpret_cast<StartStageFn>(lifecycle + 96);
    g_restoreStateOriginal = reinterpret_cast<RestoreStateFn>(lifecycle + 128);
    g_rerunStateOriginal = reinterpret_cast<RerunStateFn>(lifecycle + 160);
    g_pendingGameStartOriginal = reinterpret_cast<PendingGameStartFn>(lifecycle + 192);
    const auto luaPatch = Relative(0xe8, uint32_t(reinterpret_cast<uintptr_t>(Addr(0x702C59))),
        uint32_t(reinterpret_cast<uintptr_t>(&HookedLuaPostRender)));
    const auto presentPatch = Relative(0xe9, uint32_t(reinterpret_cast<uintptr_t>(presentSite)),
        uint32_t(reinterpret_cast<uintptr_t>(&RunJ510WindowPresent)));
    uint8_t presentBytes[]{presentPatch[0], presentPatch[1], presentPatch[2], presentPatch[3], presentPatch[4], 0x90};
    J510Patch sites[11 + kJ510LifecycleHookCount]{
        {kUpdateSite, live.update.data(), plan.update.data(), live.update.size()},
        {kRenderSite, live.render.data(), plan.render.data(), live.render.size()},
        {kGcSite, live.gc.data(), plan.gc.data(), live.gc.size()},
        {kLimitTest, live.limitTest.data(), plan.limitTest.data(), live.limitTest.size()},
        {kLoopBranch, live.loopBranch.data(), plan.loopBranch.data(), live.loopBranch.size()},
        {0x702C59, luaPostRender, luaPatch.data(), luaPatch.size()},
        {0xAA10D0, originalPresent, presentBytes, sizeof presentBytes},
        {kJ510Vignette, beforeVignette, vignettePatch, sizeof vignettePatch},
        {kJ510Character, beforeCharacter, characterPatch, sizeof characterPatch},
        {kJ510Portrait, beforePortrait, portraitPatch, sizeof portraitPatch},
        {kJ510PortraitCall, kJ510PortraitCallBytes, portraitCall.data(), portraitCall.size()},
    };
    for (size_t i = 0; i < kJ510LifecycleHookCount; ++i) {
        const auto& site = kJ510LifecycleSites[i];
        sites[11 + i] = {site.address, site.bytes, lifecyclePatches[i], site.length};
    }
    if (!CommitJ510Patches(sites)) {
        g_j510WindowPresent = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    g_carouselPresentationEnabled = true;
    g_postRenderHookInstalled = true;
    g_endPacingActive = true;
    g_j510Patched = true;
    Log("[j510] isolated candidate frame loop installed");
    if (g_cfg.cacheShaderLocations) {
        const auto rebased = [](uintptr_t va) -> uintptr_t { return reinterpret_cast<uintptr_t>(Addr(va)); };
        Log("[j510-shader-locations] install=%u",
            unsigned(highfps::j510_locations::Install(GetModuleHandleW(nullptr), rebased, g_gameThreadId, &Log)));
    }
    if (g_cfg.j510MotionProjection)
        Log("[j510-motion-projection] install=%u", unsigned(InstallJ510MotionProjection()));
    if (g_cfg.j510ScrollPressurePromotion)
        Log("[j510-scroll-pressure] install=%u", unsigned(InstallJ510ScrollPressure()));
    return true;
}
