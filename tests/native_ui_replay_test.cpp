#include "native_ui_replay.h"
#include <stdio.h>
#include <string.h>
using namespace highfps::native_ui_replay;
static unsigned char image[0x10000];
static bool readMem(uint32_t a,void* p,size_t n) {
    if(a<0x10000 || n>sizeof image || a-0x10000>sizeof image-n)return false;
    memcpy(p,image+a-0x10000,n);return true;
}
static bool writeMem(uint32_t a,const void* p,size_t n) {
    if(a<0x10000 || n>sizeof image || a-0x10000>sizeof image-n)return false;
    memcpy(image+a-0x10000,p,n);return true;
}
static void put(uint32_t a,uint32_t v) {writeMem(a,&v,4);}
static uint32_t get(uint32_t a) {uint32_t v=0;readMem(a,&v,4);return v;}
static int bad=0;
static void check(bool b,const char* name) {if(!b){++bad;printf("FAIL %s\n",name);}}
int main() {
    const Memory mem={readMem,writeMem};
    const uint32_t owner=0x11000,state=owner+0x30,layers=0x12000,nulls=0x13000;
    const uint32_t fields[]={owner,0x14000,layers,nulls,0x3f800000,1,4,12};
    writeMem(state,fields,sizeof fields);put(owner+0x80,3);put(owner+0x88,2);
    put(layers,2);put(layers+4,3);put(layers+8,4);put(nulls,5);put(nulls+4,6);
    AnimationSnapshot saved;
    check(Capture(mem,state,saved),"capture full state and layer indices");
    put(state+0x10,0x40000000);put(state+0x18,16);put(layers+4,99);put(nulls,99);
    check(Restore(mem,saved,true),"restore stable animation identity");
    check(get(state+0x10)==0x3f800000&&get(state+0x18)==4&&get(layers+4)==3&&get(nulls)==5,"persistent clocks, events and indices restored");
    put(state+4,0x15000);
    check(!Restore(mem,saved,true),"different replayed animation refused");
    check(Restore(mem,saved,false)&&get(state+4)==0x14000,"known owner can restore pre-render template identity");
    put(state+8,0x18000);
    check(!Restore(mem,saved,false),"reallocated layer storage refused");put(state+8,layers);
    put(owner+0x80,2);
    check(!Restore(mem,saved,false),"shrunk allocation refused");put(owner+0x80,3);
    put(owner+0x80,129);check(!Capture(mem,state,saved),"bounded snapshot refuses excessive layers");put(owner+0x80,3);
    check(!Capture(mem,0,saved),"invalid object refused");
    static ReplayScope scope={};ResetCanonical(scope,0x16000);
    for(unsigned n=0;n<3;++n) {Record* r=Append(scope);r->object=state;r->kind=n;r->inputType=1;}
    scope.cursor=0;
    check(Next(scope,state,0)&&Next(scope,state,1)&&Next(scope,state,2),"same sprite replayed per canonical call sequence");
    check(!Next(scope,state,2)&&scope.misses==1,"extra pure update is diagnosed, never simulated");
    ResetCanonical(scope,0x16000);Record* r=Append(scope);r->object=state;r->kind=5;
    check(!Next(scope,state,4),"callsite mismatch refused");
    ResetCanonical(scope,0x16000);
    for(unsigned n=0;n<kMaxRecords;++n) check(Append(scope)!=nullptr,"record within bounds");
    check(!Append(scope)&&!scope.valid,"overflow invalidates replay, no heap growth");
    const uint64_t capturedBefore=scope.captured,missesBefore=scope.misses;
    InvalidateScope(scope);
    check(!scope.valid&&scope.owner==0&&scope.count==0&&scope.cursor==0,"epoch invalidates owner and all replay entries");
    check(scope.captured==capturedBefore&&scope.misses==missesBefore,"epoch keeps cumulative diagnostics");
    ResetCanonical(scope,0x16000); // pointer reused in a new epoch
    check(scope.valid&&scope.count==0,"reused pointer requires a fresh canonical capture");
    Record* fresh=Append(scope);fresh->object=state;fresh->kind=9;
    check(Next(scope,state,9)!=nullptr,"new epoch uses only newly captured record");
    printf("native_ui_replay_test: %s (%d failures)\n",bad?"FAIL":"PASS",bad);return bad?1:0;
}
