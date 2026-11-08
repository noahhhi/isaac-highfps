#include "entity_draw_cache_j374_runtime.h"
#include <cassert>
#include <cstdio>
using namespace highfps_entity_probe;
struct MainBackendMock:Backend {
 State state;std::uint8_t vertices[64]{};int submits=0,leases=0;bool representable=true;
 State ReadState()override{return state;}
 bool ContextCompatible(const State&a,const State&b)override{return a.word[0]==b.word[0];}
 bool StateRepresentable(const State&)override{return representable;}
 bool Describe(void*,Layout&l,State&s)override{l={16,0,{5,1}};s=state;return true;}
 std::shared_ptr<void>Retain(void*p)override{++leases;return{p,[this](void*){--leases;}};}
 bool CanReplay(const Command&)override{return true;}
 void ApplyState(const State&s)override{state=s;}
 std::uint8_t*Submit(const Quad&)override{++submits;return vertices;}
};
static MainBackendMock* mainMock=nullptr;static unsigned mainOriginalCalls=0;static bool mainDraw=true;
static int __cdecl OriginalMainMock(){
 ++mainOriginalCalls;
 if(mainDraw){Quad q;q.image=mainMock;const bool recorded=J374Hooks::cache->BeforeQuad(q);J374Hooks::cache->AfterQuad(recorded,mainMock->vertices);mainMock->state.word[9]=77;}
 return 123;
}
namespace highfps_entity_probe {
struct RuntimeTestAccess {
 static void TestMain(){
  MainBackendMock backend;mainMock=&backend;
  static std::uint32_t rt=0;
  Host h;h.address=[](std::uintptr_t va){assert(va==0xBD56CC);return reinterpret_cast<std::uintptr_t>(&rt);};
  Runtime r(h);r.cache_.~Cache();new(&r.cache_)Cache(backend);
  r.installed_=true;Runtime::active_=&r;J374Hooks::gameThreadId=GetCurrentThreadId();J374Hooks::cache=&r.cache_;
  r.BeginRender(4,true);assert(r.MainPostRender(OriginalMainMock)==123);assert(mainOriginalCalls==1&&backend.leases==1);
  r.BeginRender(4,false);assert(r.MainPostRender(OriginalMainMock)==0);assert(mainOriginalCalls==1&&backend.submits==1);
  assert(r.canonicalCounts_[2]==1&&r.hitCounts_[2]==1&&!r.missCounts_[2]);assert(backend.state.word[9]==77);
  // An unchanged empty scope is valid even with a shader outside the supported
  // draw whitelist. Pure replay must not write the old captured shader pointer.
  mainDraw=false;backend.representable=false;backend.state.word[9]=999;
  r.BeginRender(4,true);r.MainPostRender(OriginalMainMock);assert(backend.leases==0);
  backend.state.word[9]=888;r.BeginRender(4,false);r.MainPostRender(OriginalMainMock);
  assert(backend.state.word[9]==888&&mainOriginalCalls==2&&r.missCounts_[2]==0);
  r.BeginRender(5,false);r.MainPostRender(OriginalMainMock);assert(r.missCounts_[2]==1&&mainOriginalCalls==2);
  r.host_.insideRender=[](){return false;};r.MainPostRender(OriginalMainMock);assert(mainOriginalCalls==3&&r.outsideCounts_[2]==1);
  Runtime::active_=nullptr;J374Hooks::cache=nullptr;
  std::puts("main callback canonical/replay, return value, exit state, no-op and epoch invalidation: passed");
 }

 static void Test(){
  Host h;h.address=[](std::uintptr_t a){return a;};
  h.writeJump=[](std::uint8_t*at,const void*dst){at[0]=0xe9;*reinterpret_cast<std::int32_t*>(at+1)=std::int32_t(reinterpret_cast<std::uintptr_t>(dst)-(reinterpret_cast<std::uintptr_t>(at)+5));};
  Runtime r(h);Runtime::active_=&r;J374Hooks::gameThreadId=GetCurrentThreadId();J374Hooks::cache=nullptr;
  auto* target=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(target);
  const std::uint8_t addStdcall[]={0x8b,0x44,0x24,0x04,0x03,0x44,0x24,0x08,0xc2,0x08,0};
  std::memcpy(target,addStdcall,sizeof addStdcall);
  auto* observed=r.ObserverGate(target,0x80000001u);
  using Sum=int(__stdcall*)(int,int);
  for(int i=0;i<1000;++i)assert(reinterpret_cast<Sum>(observed)(i,6)==i+6);
  assert(r.frameUnsafe_);r.frameUnsafe_=false;
  auto* relocated=r.ObserverGate(target+8,0x80000001u,target,8);
  for(int i=0;i<1000;++i)assert(reinterpret_cast<Sum>(relocated)(i,9)==i+9);
  const std::uint8_t addFastcall[]={0x8b,0xc1,0x03,0xc2,0xc3};
  std::memcpy(target+64,addFastcall,sizeof addFastcall);
  auto* fast=r.ObserverGate(target+64,0x80000001u);
  using Fast=int(__fastcall*)(int,int);
  for(int i=0;i<1000;++i)assert(reinterpret_cast<Fast>(fast)(i,7)==i+7);
  // Read XMM0 after observer's C++/FXSAVE round-trip.
  const std::uint8_t readXmm[]={0x66,0x0f,0x7e,0xc0,0xc3};
  std::memcpy(target+128,readXmm,sizeof readXmm);
  auto* xmm=r.ObserverGate(target+128,0x80000001u);float input=1.25f;unsigned bits=0,want=0;std::memcpy(&want,&input,4);
  __asm__ __volatile__("movss %1, %%xmm0\n\tcall *%2":"=a"(bits):"m"(input),"r"(xmm):"ecx","edx","memory","xmm0");
  assert(bits==want);
  Runtime::active_=nullptr;VirtualFree(target,0,MEM_RELEASE);
  std::puts("observer stdcall/fastcall/relocated prologue/XMM preservation: passed");
 }
};
}
static unsigned calls=0;
static void __stdcall Original(void*,const float*){++calls;}
struct RefCounterMock { void** vt;std::uint16_t strong,weak;std::uint8_t mutex[12];void* image; };
static_assert(sizeof(RefCounterMock)==0x18,"native ref layout");
struct BucketMock {NativeSmartImage*begin;NativeSmartImage*end;NativeSmartImage*capacity;};
static int lockDepth=0,releases=0;
static void __fastcall LockMock(void*,void*,int){assert(!lockDepth);++lockDepth;}
static void __fastcall UnlockMock(void*,void*){assert(lockDepth==1);--lockDepth;}
static bool __fastcall AcquireMock(RefCounterMock*c,void*){assert(lockDepth==1);if(!c->strong)return false;++c->strong;return true;}
static void __fastcall ReleaseMock(NativeSmartImage*p,void*){assert(!lockDepth);auto*c=static_cast<RefCounterMock*>(p->counter);assert(c->strong);--c->strong;++releases;}
static void* mutexVt[5]={nullptr,nullptr,nullptr,reinterpret_cast<void*>(&LockMock),reinterpret_cast<void*>(&UnlockMock)};
static void** mutexMock=mutexVt;
static BucketMock buckets[32];static BucketMock* bucketPtr=buckets;
static std::uintptr_t ResolveMock(std::uintptr_t va){
 if(va==kImageTableMutex)return reinterpret_cast<std::uintptr_t>(&mutexMock);
 if(va==kImageBuckets)return reinterpret_cast<std::uintptr_t>(&bucketPtr);
 if(va==kSmartImageDecrRef)return reinterpret_cast<std::uintptr_t>(&ReleaseMock);
 assert(false);return 0;
}
static void TestRefs(){
 int image=7;void*vt[2]={nullptr,reinterpret_cast<void*>(&AcquireMock)};RefCounterMock c{vt,1,1,{},&image};
 NativeSmartImage pair{&image,&c};buckets[3]={&pair,&pair+1,&pair+1};
 {auto lease=RetainJ374Image(&image,ResolveMock);assert(lease&&c.strong==2&&!lockDepth);auto copy=lease;assert(c.strong==2);}
 assert(c.strong==1&&releases==1&&!lockDepth);
 c.strong=0;assert(!RetainJ374Image(&image,ResolveMock));
 c.strong=65530;assert(!RetainJ374Image(&image,ResolveMock));
 c.strong=1;c.image=nullptr;assert(!RetainJ374Image(&image,ResolveMock));
 std::puts("native registry lease/expiration/overflow/ownership/unlocked release: passed");
}
struct EmptyBackend:Backend {
 State ReadState()override{return {};}
 bool Describe(void*,Layout&,State&)override{assert(false);return false;}
 std::shared_ptr<void> Retain(void*)override{assert(false);return {};}
 bool CanReplay(const Command&)override{return false;}
 void ApplyState(const State&)override{}
 std::uint8_t* Submit(const Quad&)override{return nullptr;}
};
static unsigned expectedDepth=0,submitCalls=0;
static std::uint8_t* __thiscall NullSafeSubmit(void*,const Source*,const Destination*,const Color*,const Color*,const Color*,const Color*){
 assert(J374Hooks::submitDepth==expectedDepth);++submitCalls;return reinterpret_cast<std::uint8_t*>(0x1234);
}
static void TestSubmitFastPath(){
 J374Hooks::submit=&NullSafeSubmit;J374Hooks::gameThreadId=GetCurrentThreadId();
 J374Hooks::cache=nullptr;expectedDepth=1;
 assert(J374Hooks::Submit(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)==reinterpret_cast<std::uint8_t*>(0x1234));
 assert(J374Hooks::submitDepth==0);
 EmptyBackend backend;Cache cache(backend);J374Hooks::cache=&cache;
 J374Hooks::Submit(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);assert(J374Hooks::submitDepth==0);
 assert(cache.Begin(Key{}));J374Hooks::Submit(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);cache.End();
 assert(!cache.Replay(Key{}));assert(J374Hooks::submitDepth==0);
 J374Hooks::gameThreadId=0;expectedDepth=0;J374Hooks::Submit(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr);
 assert(submitCalls==4);J374Hooks::cache=nullptr;
 std::puts("quad fast-path and null-parameter pass-through preserve submit depth: passed");
}
int main(){
 TestSubmitFastPath();
 TestRefs();
 RuntimeTestAccess::TestMain();
 RuntimeTestAccess::Test();
 J374Hooks::player=Original;J374Hooks::canonical=false;J374Hooks::gameThreadId=0;
 J374Hooks::Player(nullptr,nullptr);assert(calls==1); // Other thread always reaches native.
 J374Hooks::gameThreadId=GetCurrentThreadId();J374Hooks::Player(nullptr,nullptr);assert(calls==1);
 J374Hooks::canonical=true;J374Hooks::Player(nullptr,nullptr);assert(calls==2);
 J374Hooks::canonical=false;J374Hooks::InsideRender=[](){return false;};J374Hooks::Player(nullptr,nullptr);assert(calls==3);
 J374Hooks::InsideRender=[](){return true;};J374Hooks::Player(nullptr,nullptr);assert(calls==3);
 std::puts("foreign-thread/Update-context native forwarding and pure-render suppression: passed");
}
