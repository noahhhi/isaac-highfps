#include "menu_presentation.h"
#include <cstdio>
#include <limits>
using namespace highfps::menu_presentation;
static int failures = 0;
static void Check(bool ok, const char* name) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", name); }
}
static bool Near(float a, float b) { return std::fabs(a-b) < 0.0001f; }
struct Menu { float fields[32]{}; float* View() { return fields+17; } };
int main() {
    Menu menu, other;
    Context c{reinterpret_cast<uintptr_t>(&menu), 4, 1, 1280, 800};
    History h; Scope s; float* xy=menu.View();
    xy[0]=100; xy[1]=200;
    Check(h.Sample(c,{100,200}) && Equal(h.previous,h.current),"first sample has no invented movement");
    xy[0]=90; xy[1]=180;
    Check(h.Sample(c,{90,180}) && Equal(h.previous,{100,200}),"next fixed tick records authoritative endpoints");
    Check(!s.Begin(h,c,xy,1,1,false) && xy[0]==90 && !s.Applied(),"canonical render never modified");
    Check(s.Begin(h,c,xy,1+1.0/120,1,true) && Near(xy[0],95) && Near(xy[1],190),"pure presentation uses 60Hz midpoint");
    Check(!s.Begin(h,c,xy,1,1,true),"nested begin cannot overwrite saved authority");
    Check(s.Restore(c) && xy[0]==90 && xy[1]==180 && !s.Applied(),"scope restores exact native coordinates");
    Check(s.Restore(c),"second restore is harmless");
    Check(s.Begin(h,c,xy,1+1.0/90,1,true) && Near(xy[0],93.333333f),"90Hz presentation uses independent main-clock alpha");
    Check(s.Restore(c),"90Hz scope restored");
    // current_menu is intentionally not part of Context: page navigation retains history.
    xy[0]=81; xy[1]=162; h.Sample(c,{81,162});
    Check(Equal(h.previous,{90,180}),"page transition carries prior viewport sample");
    Check(s.Begin(h,c,xy,4,1,true) && xy[0]==81,"late display clamps to current endpoint"); s.Restore(c);
    Check(s.Begin(h,c,xy,0,1,true) && xy[0]==90,"early display clamps to previous endpoint"); s.Restore(c);
    // Multiple native ticks in a <60Hz output frame must keep only adjacent samples.
    xy[0]=72.9f; xy[1]=145.8f; h.Sample(c,{xy[0],xy[1]});
    xy[0]=65.61f; xy[1]=131.22f; h.Sample(c,{xy[0],xy[1]});
    Check(Near(h.previous.x,72.9f) && Near(h.current.x,65.61f),"low output rate samples every fixed tick");
    Context resized=c; resized.width=1920;
    Check(!s.Begin(h,resized,xy,1,1,true) && !h.valid,"resize invalidates unsampled old history");
    h.Sample(resized,{xy[0],xy[1]}); Check(Equal(h.previous,h.current),"resize begins with fresh endpoints");
    Context next=c; next.menu=reinterpret_cast<uintptr_t>(&other);
    h.Sample(next,{4,5}); Check(Equal(h.previous,{4,5}),"new menu owner resets history");
    h.Sample(c,{8,9}); Context epoch=c; ++epoch.epoch;
    h.Sample(epoch,{10,11}); Check(Equal(h.previous,{10,11}),"reused pointer new lifecycle resets history");
    Context inactive=c; inactive.managerState=3;
    Check(!h.Sample(inactive,{1,2}) && !h.valid,"non-menu Manager state invalidates history");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    Check(!h.Sample(c,{nan,1}),"nonfinite native position rejected");
    Context badsize=c; badsize.height=nan;
    Check(!h.Sample(badsize,{1,2}),"nonfinite display size rejected");
    Check(Alpha60(nan,1)==0 && Alpha60(1,nan)==0,"invalid time cannot produce NaN viewport");
    xy[0]=10; xy[1]=20; h.Sample(c,{10,20});
    xy[0]=11; Check(!s.Begin(h,c,xy,1,1,true) && !h.valid && xy[0]==11,"unsampled native write preserved");
    h.Sample(c,{11,20}); xy[0]=12; h.Sample(c,{12,20});
    Check(s.Begin(h,c,xy,1,1,true),"scope begins before concurrent-write check");
    xy[0]=999;
    Check(!s.Restore(c) && xy[0]==999 && !h.valid,"render-side write is not silently overwritten");
    h.Sample(c,{999,20}); xy[0]=998; h.Sample(c,{998,20});
    Check(s.Begin(h,c,xy,1,1,true),"scope begins before lifetime change");
    Check(!s.Restore(next) && !h.valid,"changed identity avoids stale-owner restoration");
    xy[0]=20; xy[1]=30; h.Sample(c,{20,30});
    xy[0]=0; xy[1]=0; h.Sample(c,{0,0},true);
    Check(Equal(h.previous,{0,0}),"explicit native hard placement is not smeared");
    Check(!s.Begin(h,c,other.View(),1,1,true),"viewport pointer must belong to current menu");
    Check(s.Begin(h,c,xy,1,1,true),"valid scope before exception fallback"); s.Abandon();
    Check(!h.valid && !s.Applied(),"exception fallback invalidates without memory access");
    std::printf("menu_presentation_test: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
