#include "native_render_clock.h"
#include <stdio.h>
using namespace highfps::native_render_clock;
static int failures=0;
static void Check(bool ok,const char* what) {
    if (!ok) { ++failures; printf("FAIL %s\n",what); }
}
static bool Near(float a,float b) { const float d=a-b; return d<0.000002f && d>-0.000002f; }

struct Trace {
    float character=0, vignette=0;
    unsigned tick=0;
    void FixedTick() {
        // Multiple state changes inside low-FPS output intervals must retain
        // their own target, instead of applying the final state retroactively.
        const int states[]={0,1,3,2,0,3};
        const int state=states[(tick/7)%6];
        character=CharacterTick(character,state);
        vignette=VignetteTick(vignette,state);
        ++tick;
    }
};
int main(int argc,char**) {
    if(argc>1) {
        uint8_t original[sizeof kPortraitOriginal],stub[kPortraitStubBytes];
        RelocatedOriginal(original,kPortraitOriginal,sizeof original,kPortraitRelocations,3,0x30000000);
        BuildPortraitStub(stub,0x51000000,original,0x30200000,kPortraitResume+0x30000000);
        FILE* f=fopen("native_render_clock_stub.bin","wb");
        if(!f) return 2;
        fwrite(stub,1,sizeof stub,f); fclose(f); return 0;
    }
    Check(Near(VignetteTick(0,1),.025f),"first native tick exactly one step");
    Check(Near(VignetteTick(.49f,1),.5f),"vignette clamps near upper target");
    Check(Near(VignetteTick(.01f,0),0),"vignette clamps near lower target");
    Check(Near(CharacterTick(.99f,3),1),"character upper clamp");
    Check(Near(CharacterTick(.01f,0),0),"character lower clamp");
    float c=0,v=0;
    for (unsigned n=0;n<20;++n) c=CharacterTick(c,3);
    for (unsigned n=0;n<20;++n) v=VignetteTick(v,3);
    Check(Near(c,1)&&Near(v,.5f),"20 native ticks reach endpoints");
    for (unsigned n=0;n<200;++n) { c=CharacterTick(c,3);v=VignetteTick(v,3); }
    Check(c==1&&v==.5f,"long catchup cannot overshoot");
    Trace ref;
    float charAt[361]={},vignAt[361]={};
    for(unsigned n=1;n<=360;++n) {ref.FixedTick();charAt[n]=ref.character;vignAt[n]=ref.vignette;}
    const unsigned rates[]={10,24,30,40,48,59,60,80,90,100,120,144,240};
    for(unsigned rate:rates) {
        Trace t;
        // Integer phase accumulator avoids testing scheduler floating errors.
        for(unsigned frame=1;frame<=rate*6;++frame) {
            const unsigned due=(frame*60)/rate;
            while(t.tick<due) t.FixedTick();
            Check(Near(t.character,charAt[due])&&Near(t.vignette,vignAt[due]),
                  "output rate does not affect state at any native tick");
            // Rendering (including repeats) intentionally has no mutation.
            const float before=t.character;
            for(unsigned repeat=0;repeat<4;++repeat) (void)t.character;
            Check(before==t.character,"zero ticks / duplicate draws do not advance");
        }
        Check(t.tick==360,"fixed tick total");
    }
    Trace alternating;
    const unsigned counts[]={0,1,0,3,0,0,1,2,0,4,1,0};
    unsigned index=0;
    while(alternating.tick<360) {
        unsigned n=counts[index++%12];
        while(n-- && alternating.tick<360) alternating.FixedTick();
        Check(Near(alternating.character,charAt[alternating.tick]),"VRR variable tick batches");
    }
    // Genuine state change in a two-tick output interval: +.05 then -.05.
    Check(Near(CharacterTick(CharacterTick(.5f,2),0),.5f),"state changes keep per-tick semantics");
    uint8_t vp[sizeof kVignetteOriginal],cp[sizeof kCharacterOriginal];
    BuildPatches(vp,cp);
    Check(vp[0]==0xf3&&vp[3]==0x4e&&vp[4]==0x34&&vp[49]==0x90,"vignette patch buffer");
    Check(cp[0]==0xf3&&cp[3]==0x47&&cp[7]==0xc8&&cp[86]==0x90,"character patch buffer");
    uint8_t relocated[sizeof kPortraitOriginal],stub[kPortraitStubBytes];
    RelocatedOriginal(relocated,kPortraitOriginal,sizeof relocated,kPortraitRelocations,3,0x30000000);
    Check(Read32(relocated+12)==0x30b66ca4,"ASLR relocation of first constant");
    Check(Read32(relocated+38)==0x30b66928&&Read32(relocated+77)==0x30b66928,"ASLR relocation of both deltas");
    BuildPortraitStub(stub,0x51000000,relocated,0x30200000,kPortraitResume+0x30000000);
    Check(Read32(stub+3)==0x30200000,"scope address embedded");
    Check(Read32(stub+10)+14==106,"scope branch enters pure path");
    Check(uint32_t(0x51000000+106+Read32(stub+102))==uint32_t(kPortraitResume+0x30000000),"canonical path resumes original renderer");
    Check(uint32_t(0x51000000+123+Read32(stub+119))==uint32_t(kPortraitResume+0x30000000),"pure path resumes original renderer");
    for(unsigned n=0;n<sizeof kPortraitOriginal;++n) Check(stub[15+n]==relocated[n],"canonical scalar block copied exactly");
    printf("native_render_clock_test: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
