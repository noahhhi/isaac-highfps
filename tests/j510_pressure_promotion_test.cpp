#include "../src/j510_pressure_promotion.h"
#include "../src/render_timing.h"
#include <cassert>
#include <initializer_list>
using namespace highfps::j510;
int main() {
    PressurePromotion p;
    p.Observe(true, 383, 767);
    assert(!p.ShouldPromote(true, true, true));
    p.Observe(true, 512, 0);
    assert(p.ShouldPromote(true, true, true));
    assert(!p.ShouldPromote(false, true, true) && !p.ShouldPromote(true, false, true));
    assert(!p.ShouldPromote(true, true, false));
    for (int i = 0; i < 29; ++i) p.Observe(true, 0, 0);
    assert(p.active);
    p.Observe(true, 0, 0);
    assert(!p.active);
    p.Observe(true, 0, 800);
    assert(p.active);
    p.Observe(false, 500, 800);
    assert(!p.active);
    PresentedEndpoint e;
    e.Note(5, true, 0);
    assert(!e.Hold(5));
    e.Note(5, false, 1);
    assert(!e.Hold(5));
    e.Note(5, true, 2);
    assert(!e.Hold(5));
    e.Note(5, true, 1);
    assert(e.Hold(5) && !e.Hold(6));
    e.Invalidate();
    assert(!e.Hold(5));
    PlayerRest motion;
    for (unsigned i = 0; i < PlayerRest::required - 1; ++i) motion.Observe(true, false);
    assert(!motion.CanShowEndpoint(.999f) && motion.CanShowEndpoint(1));
    motion.Observe(true, false);
    assert(motion.CanShowEndpoint(.1f));
    motion.Observe(true, true);
    assert(!motion.CanShowEndpoint(.95f));
    for (unsigned i = 0; i < PlayerRest::required; ++i) motion.Observe(true, false);
    motion.Observe(false, false);
    assert(!motion.CanShowEndpoint(.5f));
    assert(SamePoseIdentity(2, 2, 101, 101));
    assert(!SamePoseIdentity(2, 2, 101, 102)); // same pool address, new tear
    assert(!SamePoseIdentity(2, 9, 101, 101));
    for (unsigned hz : {90u, 120u}) {
        for (unsigned i = 1; i <= hz; ++i) {
            const auto t = 1.0 + static_cast<double>(i) / hz;
            const auto tick = 1.0 + static_cast<unsigned>((t - 1.0) * 60) / 60.0;
            const float alpha = highfps::RenderAlpha(t, tick, 1.0/60);
            // No native endpoint substitution while the same output would
            // still be displaying an intermediate position of a moving player.
            if (alpha < 1) assert(!motion.CanShowEndpoint(alpha));
        }
    }
}
