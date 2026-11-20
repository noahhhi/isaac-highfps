#include "../src/j510_scroll_pressure.h"
#include <cassert>
#include <limits>
using namespace highfps::j510_scroll;
int main() {
    Frame p;
    assert(p.CanSwap());
    assert(!p.Prepare({0,0},{40,0},.5f));
    assert(!p.Prepare({0,0},{2,0},std::numeric_limits<float>::quiet_NaN()));
    assert(p.Prepare({1,4},{9,12},.25f));
    assert(Same(p.source,{9,12}) && Same(p.visual,{3,6}));
    assert(!p.CanSwap() && !p.Enter({8,12}));
    assert(p.Enter({9,12}));
    assert(p.Finish(true,{3,6}) && p.CanSwap());
    assert(!p.Enter({9,12}) && !p.CanSwap()); // repeated world renderer
    assert(p.Prepare({1,4},{9,12},.25f));
    assert(p.Enter({9,12}));
    assert(!p.Finish(false,{3,6}) && !p.CanSwap()); // room ownership changed
    assert(p.Prepare({1,4},{9,12},.25f));
    assert(p.Enter({9,12}));
    assert(!p.Finish(true,{8,10}) && !p.CanSwap()); // separate native writer won
    p.Clear();
    assert(p.CanSwap());
}
