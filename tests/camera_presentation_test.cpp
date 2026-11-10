#include "camera_presentation.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <algorithm>
using namespace highfps::camera_presentation;
static bool Near(float a,float b,float eps=2e-5f){return std::fabs(a-b)<eps;}
int main(){
    constexpr double step=1.0/60.0;
    // Monotone camera easing while the player outruns it: no reverse room motion
    // at noninteger display/logic ratios. The legacy lock can reverse by 0.8 px
    // for these samples at90Hz despite a strictly monotone native camera.
    for(int fps:{45,60,80,90,100,144}){
        float last=1;bool first=true;
        for(int k=0;k<fps*2;++k){
            const double t=step+double(k)/fps;const int tick=int(std::floor(t/step+1e-9));
            const float p=-.2f*(tick-1),c=-.2f*tick;
            const float shown=Axis(p,c,Alpha(t,tick*step));
            assert(Near(shown,float(-.2*(t/step-1))));
            if(!first)assert(shown<=last+2e-5f);last=shown;first=false;
        }
    }
    // Stop/reverse/hard clamp: changing slopes must never create a boundary jump
    // or leave the segment between two authoritative, legally clamped poses.
    const float path[]={0,-.2f,-.6f,-1.1f,-1.7f,-2.2f,-2.2f,-2.2f,-1,0,2};
    for(unsigned n=1;n+1<sizeof(path)/sizeof(path[0]);++n){
        assert(Near(Axis(path[n-1],path[n],1),Axis(path[n],path[n+1],0)));
        for(float a:{0.f,.13f,.67f,1.f}){
            const float value=Axis(path[n-1],path[n],a);
            assert(value>=std::min(path[n-1],path[n])-1e-6f);
            assert(value<=std::max(path[n-1],path[n])+1e-6f);
        }
    }
    // Frozen world/player is not a frozen camera: native transition samples
    // continue interpolating. A stationary paused camera instead stays fixed.
    History camera;camera.Sample({0,-100});camera.Sample({0,-104});
    assert(Near(camera.Present(.5f).y,-102));
    camera.Sample({0,-104});assert(Near(camera.Present(.75f).y,-104));
    // Room/epoch changes snap to a fresh native pose instead of crossing rooms.
    camera.Sample({300,240},true);assert(Near(camera.Present(.1f).x,300));
    camera.Sample({-300,-240});assert(Near(camera.Present(.9f).x,-300));
    camera.Sample({-298,-238});assert(Near(camera.Present(.5f).x,-299));
    // Variable output times only choose sample alpha; no output-time accumulation
    // changes the native camera or creates rate-dependent drift.
    const double intervals[]={1./48,1./80,1./144,1./100,1./61};double time=step;
    for(unsigned i=0;i<1000;++i){
        time+=intervals[i%5];const auto tick=std::floor(time/step+1e-9);
        const float shown=Axis(float((tick-1)*.1),float(tick*.1),Alpha(time,tick*step));
        assert(Near(shown,float((time/step-1)*.1),2e-4f));
    }
    assert(Alpha(1,2)==0&&Alpha(5,0)==1);
    assert(Alpha(1,0,0)==0&&Alpha(std::numeric_limits<double>::quiet_NaN(),0)==0);
    assert(Near(Axis(4,8,std::numeric_limits<float>::quiet_NaN()),4));
    assert(!camera.Sample({std::numeric_limits<float>::infinity(),0}));
    camera.Sample({7,9});assert(Near(camera.Present(.3f).x,7));
    std::puts("camera display: monotone easing, continuous reversal/clamp, frozen-world native camera, pause, lifecycle reset, variable 48-144Hz and invalid-time guards passed");
}
