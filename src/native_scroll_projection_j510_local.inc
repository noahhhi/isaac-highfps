// J510's two authoritative scroll writers execute inside Manager::Update.
// Scope the presentation-only offset to the actual Game::Render call from
// Manager::Render (world, shadows, room passes together), and restore before
// publishing native camera history or allowing the display swap.
highfps::j510_scroll::Frame g_j510ScrollView;
bool g_j510ScrollHook = false;
J510RoomKey g_j510ScrollRoom{};
std::uint32_t g_j510ScrollTick = 0, g_j510ScrollPromoted = 0, g_j510ScrollVeto = 0,
    g_j510ScrollScoped = 0;
using J510GameDraw = void(__fastcall*)(void*,void*);
J510GameDraw g_j510GameDraw = nullptr;

bool PrepareJ510Scroll(const J510RoomKey& room, J510Scroll prior, float alpha) {
    g_j510ScrollView.Clear();
    if (!g_cfg.j510ScrollPressurePromotion || !g_j510ScrollHook ||
        !J510ProjectionSupported(room) || room.room < 0x10000) return false;
    if (!g_j510ScrollView.Prepare(prior,ReadJ510Scroll(room),alpha)) return false;
    g_j510ScrollRoom = room;
    return true;
}

void __fastcall J510GameDrawWithView(void* game, void*) {
    auto& v = g_j510ScrollView;
    if (!v.armed || GetCurrentThreadId() != g_gameThreadId ||
        reinterpret_cast<uintptr_t>(game) != g_j510ScrollRoom.game ||
        !(ReadJ510Room()==g_j510ScrollRoom) || !v.Enter(ReadJ510Scroll(g_j510ScrollRoom))) {
        g_j510GameDraw(game,nullptr);
        return;
    }
    auto* p = J510ScrollField(g_j510ScrollRoom);
    p[0] = v.visual.x; p[1] = v.visual.y;
    __try { g_j510GameDraw(game,nullptr); }
    __finally {
        const bool matches = ReadJ510Room()==g_j510ScrollRoom;
        const auto current = matches ? ReadJ510Scroll(g_j510ScrollRoom) : J510Scroll{};
        const bool restore = v.Finish(matches, current);
        if (restore) { p[0]=v.source.x; p[1]=v.source.y; ++g_j510ScrollScoped; }
        // Unknown native writes during render are authoritative. Never
        // overwrite them; the caller vetoes presenting this uncertain pass.
    }
}

bool InstallJ510ScrollPressure() {
    constexpr std::uint8_t site[]{0xe8,0x6a,0x75,0xd9,0xff};
    constexpr std::uint8_t writer1[]{0xf3,0x0f,0x11,0x89,0x04,0x12,0x00,0x00};
    if (!g_j510ProjectionInstalled ||
        !Expect(0x959FAD,writer1,sizeof writer1,"J510 native view writer")) return false;
    g_j510GameDraw = reinterpret_cast<J510GameDraw>(Addr(0x7028A0));
    const auto call = highfps::j510::Relative(0xe8,
        std::uint32_t(reinterpret_cast<uintptr_t>(Addr(0x96B331))),
        std::uint32_t(reinterpret_cast<uintptr_t>(&J510GameDrawWithView)));
    const J510Patch patches[]{{0x96B331,site,call.data(),sizeof site}};
    g_j510ScrollHook = CommitJ510Patches(patches);
    return g_j510ScrollHook;
}
