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
 static void TestTextureDelete();
 static void TestTextureMutation();
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

 static void TestObserverFastPath(){
  Host h;h.address=[](std::uintptr_t a){return a;};
  h.writeJump=[](std::uint8_t*p,const void*t){p[0]=0xe9;*reinterpret_cast<std::int32_t*>(p+1)=std::int32_t(reinterpret_cast<std::uintptr_t>(t)-(reinterpret_cast<std::uintptr_t>(p)+5));};
  auto*target=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(target);
  const std::uint8_t sumCode[]={0x8b,0x44,0x24,4,0x03,0x44,0x24,8,0xc2,8,0};std::memcpy(target,sumCode,sizeof sumCode);
  const std::uint8_t fastCode[]={0x8b,0xc1,0x03,0xc2,0xc3};std::memcpy(target+64,fastCode,sizeof fastCode);
  const std::uint8_t xmmCode[]={0x66,0x0f,0x7e,0xf8,0xc3};std::memcpy(target+128,xmmCode,sizeof xmmCode); // XMM7 -> EAX
  const std::uint8_t x87Code[]={0x83,0xec,4,0xd9,0x1c,0x24,0x58,0xc3};std::memcpy(target+192,x87Code,sizeof x87Code); // FSTP float, POP EAX
  using Sum=int(__stdcall*)(int,int);using Fast=int(__fastcall*)(int,int);
  Cache::ObserverCaptureFlag*retiredFlag=nullptr;void*retiredGate=nullptr;
  {
   MainBackendMock backend;Runtime r(h);r.cache_.~Cache();new(&r.cache_)Cache(backend);
   Runtime::active_=&r;J374Hooks::gameThreadId=GetCurrentThreadId();J374Hooks::cache=&r.cache_;
   auto*observed=r.ObserverGate(target,1);auto*relocated=r.ObserverGate(target+8,1,target,8);
   auto*fast=r.ObserverGate(target+64,1);auto*xmm=r.ObserverGate(target+128,1);auto*x87=r.ObserverGate(target+192,1);
   auto*lazy=r.ObserverGate(target,0x80000001u);
   retiredFlag=r.cache_.ObserverFlag();retiredGate=observed;assert(retiredFlag&&retiredFlag->load()==0);
   // A native test target records every GPR and incoming EFLAGS before touching
   // them. Its driver supplies known register/flag values without C++ asm-register
   // allocation assumptions, then restores the caller's complete GPR/flags state.
   std::uint32_t registers[9]{},expectedEsp=0;std::size_t at=256;
   for(unsigned reg=0;reg<8;++reg){target[at++]=0x89;target[at++]=std::uint8_t(0x05+(reg<<3));*reinterpret_cast<std::uint32_t*>(target+at)=unsigned(reinterpret_cast<std::uintptr_t>(&registers[reg]));at+=4;}
   target[at++]=0x9c;target[at++]=0x58;target[at++]=0xa3;*reinterpret_cast<std::uint32_t*>(target+at)=unsigned(reinterpret_cast<std::uintptr_t>(&registers[8]));at+=4;target[at++]=0xc3;
   auto*registerGate=r.ObserverGate(target+256,1);
   auto driver=[&](std::uint32_t flags){std::size_t i=512;target[i++]=0x9c;target[i++]=0x60;
    for(unsigned reg=0;reg<8;++reg)if(reg!=4){target[i++]=std::uint8_t(0xb8+reg);*reinterpret_cast<std::uint32_t*>(target+i)=0x11111111u*(reg+1);i+=4;}
    target[i++]=0x68;*reinterpret_cast<std::uint32_t*>(target+i)=flags;i+=4;target[i++]=0x9d;
    target[i++]=0x89;target[i++]=0x25;*reinterpret_cast<std::uint32_t*>(target+i)=unsigned(reinterpret_cast<std::uintptr_t>(&expectedEsp));i+=4;
    target[i++]=0xe8;*reinterpret_cast<std::int32_t*>(target+i)=std::int32_t(reinterpret_cast<std::uintptr_t>(registerGate)-(reinterpret_cast<std::uintptr_t>(target+i)+4));i+=4;
    target[i++]=0x61;target[i++]=0x9d;target[i++]=0xc3;FlushInstructionCache(GetCurrentProcess(),target+512,i-512);
    reinterpret_cast<void(__cdecl*)()>(target+512)();
    for(unsigned reg=0;reg<8;++reg)assert(registers[reg]==(reg==4?expectedEsp-4:0x11111111u*(reg+1)));
    assert((registers[8]&0x8d5)==(flags&0x8d5));
   };
   for(unsigned capture=0;capture<2;++capture){
    if(capture){assert(r.cache_.Begin(Key{}));assert(retiredFlag->load()==1);}
    const auto before=r.reasons_[1];
    for(int i=0;i<100;++i){assert(reinterpret_cast<Sum>(observed)(i,6)==i+6);assert(reinterpret_cast<Sum>(relocated)(i,9)==i+9);assert(reinterpret_cast<Fast>(fast)(i,7)==i+7);}
    float input=1.25f;unsigned bits=0,want=0;std::memcpy(&want,&input,4);
    __asm__ __volatile__("movss %1, %%xmm7\n\tcall *%2":"=a"(bits):"m"(input),"r"(xmm):"ecx","edx","memory","xmm7");assert(bits==want);
    __asm__ __volatile__("flds %1\n\tcall *%2":"=a"(bits):"m"(input),"r"(x87):"ecx","edx","memory","st");assert(bits==want);
    driver(0x246);driver(0xa97);
    assert(r.reasons_[1]==before+(capture?304:0));
    struct ThreadCall{void*gate;int result;};ThreadCall call{observed,0};
    auto thread=CreateThread(nullptr,0,[](void*p)->DWORD{auto*c=static_cast<ThreadCall*>(p);c->result=reinterpret_cast<Sum>(c->gate)(4,5);return 0;},&call,0,nullptr);
    assert(thread&&WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);assert(call.result==9&&r.reasons_[1]==before+(capture?304:0));
    if(capture){r.cache_.End();assert(retiredFlag->load()==0);assert(!r.cache_.Replay(Key{}));}
    r.frameUnsafe_=false;assert(reinterpret_cast<Sum>(lazy)(2,3)==5&&r.frameUnsafe_); // Lazy never bypasses Notify.
   }
   // Preserve the pre-optimization observer as a test-only benchmark control.
   auto oldGate=[&](void*original,unsigned code){auto*p=r.Allocate();const std::uint8_t prefix[]={0x9c,0x60,0x89,0xe5,0x81,0xec,0x10,0x02,0,0,0x83,0xe4,0xf0,0x0f,0xae,0x04,0x24};
    std::memcpy(p,prefix,sizeof prefix);std::size_t i=sizeof prefix;p[i++]=0x68;*reinterpret_cast<unsigned*>(p+i)=code;i+=4;p[i++]=0xe8;*reinterpret_cast<std::int32_t*>(p+i)=std::int32_t(reinterpret_cast<std::uintptr_t>(&Runtime::Notify)-(reinterpret_cast<std::uintptr_t>(p+i)+4));i+=4;
    const std::uint8_t suffix[]={0x83,0xc4,4,0x0f,0xae,0x0c,0x24,0x89,0xec,0x61,0x9d};std::memcpy(p+i,suffix,sizeof suffix);i+=sizeof suffix;h.writeJump(p+i,original);FlushInstructionCache(GetCurrentProcess(),p,i+5);return p;};
   auto*old=oldGate(target,0x40000008u);auto*quick=r.ObserverGate(target,0x40000008u);
   J374Hooks::submitDepth=1;LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);volatile int sink=0;
   for(unsigned capture=0;capture<2;++capture)for(unsigned which=0;which<2;++which){
    auto f=reinterpret_cast<Sum>(which?quick:old);LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);
    if(capture){for(int n=0;n<2000;++n){assert(r.cache_.Begin(Key{}));for(int i=0;i<64;++i)sink=f(i,6);r.cache_.End();}}
    else for(int i=0;i<200000;++i)sink=f(i,6);
    QueryPerformanceCounter(&end);std::printf("observer %s %s calls=%u elapsedMs=%.3f\n",which?"fast":"old",capture?"empty-capture":"outside-capture",capture?128000u:200000u,1000.0*(end.QuadPart-begin.QuadPart)/frequency.QuadPart);
   }
   (void)sink;J374Hooks::submitDepth=0;
   assert(r.cache_.Begin(Key{}));assert(retiredFlag->load()==1); // Destructor must lower the external flag too.
   Runtime::active_=nullptr;J374Hooks::cache=nullptr;
  }
  assert(retiredFlag->load()==0);assert(reinterpret_cast<Sum>(retiredGate)(8,9)==17); // No dangling Cache storage read.
  VirtualFree(target,0,MEM_RELEASE);
  std::puts("observer fast/slow paths: stdcall, fastcall, displaced prologue, GPR/EFLAGS, SSE/x87, foreign thread, lazy and retired flag: passed");
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
// Actual NativeBackend with a relocated J374 data image and GL reflection stubs.
// This exercises production Describe/CanReplay/PrepareResource, not a lookalike.
namespace highfps_entity_probe {
struct NativeBackendTestAccess {
 inline static std::uint8_t memory[0x900000]{};
 inline static std::uint8_t attrs[4][9]={{5,6},{5,6,7},{5,6,7,4,3,2,1,3},{5,6,7,4,3,2,1,3,4}};
 inline static bool extraUniform=false;
 inline static unsigned formatCalls=0;
 inline static int fbo=0,attachmentType=0x1702,attachmentName=77,boundTexture=99;
 inline static int parameters[4]={0x2601,0x2702,0x2901,0x2901};
 static void WINAPI TextureParameter(unsigned,unsigned pname,int*out){assert(pname>=0x2800&&pname<=0x2803);*out=parameters[pname-0x2800];}
 static HGLRC WINAPI Context(){return reinterpret_cast<HGLRC>(0x12345678);}
 static void WINAPI Attachment(unsigned,unsigned attachment,unsigned property,int*out){
  if(attachment!=0x8CE0){*out=0;return;}
  *out=property==0x8CD0?attachmentType:attachmentName;
 }
 static void Configure(NativeBackend&b){b.getInt_=&GetInt;b.isEnabled_=&Enabled;b.isTexture_=&IsTexture;b.getTextureParameter_=&TextureParameter;b.context_=&Context;b.knownContext_=Context();b.getAttachment_=&Attachment;b.getProgram_=&Program;b.getUniform_=&Uniform;}
 static std::uint64_t ForeignEpoch(){return NativeBackend::foreignTextureEpoch_.load();}
 static std::uintptr_t Address(std::uintptr_t va){assert(va>=0x400000&&va<0xD00000);return reinterpret_cast<std::uintptr_t>(memory+va-0x400000);}
 template<class T>static T& At(std::uintptr_t va){return *reinterpret_cast<T*>(Address(va));}
 static void WINAPI GetInt(unsigned property,int*p){
  *p=property==0x8CA6?fbo:property==0x8CDF?1:property==0x84E0?0x84C0:property==0x8069?boundTexture:0;
  if(property==0x0BA2 || property==0x0C10)for(int i=1;i<4;++i)p[i]=0;
 }
 static unsigned char WINAPI Enabled(unsigned){return 0;}
 static unsigned char WINAPI IsTexture(unsigned name){return name&&name!=0xffffffffu;}
 static void WINAPI Program(unsigned,unsigned property,int*out){*out=property==0x8B82?1:(extraUniform?3:2);}
 static void WINAPI Uniform(unsigned,unsigned i,int,int*length,int*size,unsigned*type,char*out){
  const char*name=i?"Texture0":"Transform";*length=int(std::strlen(name));*size=1;*type=i?0x8B5E:0x8B5C;std::strcpy(out,name);
 }
 static void __thiscall Format(void*image,const void*descriptor){
  ++formatCalls;auto*p=static_cast<std::uint8_t*>(image);unsigned index=4;
  const std::uintptr_t va[]={0xBCBCF0,0xBCBCD0,0xBAF720,0xBAF5E8};
  for(unsigned i=0;i<4;++i)if(descriptor==reinterpret_cast<void*>(Address(va[i])))index=i;
  assert(index<4);*reinterpret_cast<unsigned*>(p+0x20)=index<2?index:3;
  *reinterpret_cast<void**>(p+0x34)=attrs[index];p[0x38]=index==0?2:index==1?3:index==2?8:9;
  *reinterpret_cast<unsigned short*>(p+0x3A)=index==0?28:index==1?36:index==2?88:104;
 }
 static void Test(){
  const std::uintptr_t descriptors[]={0xBCBCF0,0xBCBCD0,0xBAF720,0xBAF5E8};
  const unsigned counts[]={2,3,8,9};
  for(unsigned d=0;d<4;++d)for(unsigned i=0;i<counts[d];++i)At<unsigned>(descriptors[d]+8*i+4)=attrs[d][i];
  const std::uintptr_t shaders[]={0xBD56F8,0xBD5724,0xBD5750,0xBD296C,0xBD2A74,0xBD2890,0xBD2788};
  const unsigned tableIndices[]={0,10,15,16};
  for(unsigned i=0;i<7;++i){
   At<unsigned char>(shaders[i]+4)=1;At<unsigned>(shaders[i]+0x28)=100+i;
   const auto descriptor=descriptors[i==4?3:2];At<unsigned>(shaders[i]+8)=unsigned(Address(descriptor));
   At<unsigned>(shaders[i]+0xC)=i==4?9:8;
   if(i>=3)At<unsigned>(0xAD2AF8+4*tableIndices[i-3])=unsigned(Address(shaders[i]));
  }
  NativeBackend b(Address);Configure(b);
  void*vt[37]{};vt[2]=reinterpret_cast<void*>(Address(0xA06530));vt[3]=reinterpret_cast<void*>(Address(0xA06510));vt[30]=reinterpret_cast<void*>(Address(0xA711E0));vt[33]=reinterpret_cast<void*>(Address(0xA71260));
  alignas(4) std::uint8_t image[0x80]{};*reinterpret_cast<void***>(image)=vt;
  *reinterpret_cast<unsigned*>(image+0x78)=42;*reinterpret_cast<unsigned*>(image+0x7C)=43;
  J374Hooks::setFormat=&Format;J374Hooks::gameThreadId=GetCurrentThreadId();
  for(unsigned i=3;i<7;++i){
   Format(image,reinterpret_cast<void*>(Address(descriptors[i==4?3:2])));
   At<unsigned>(0xBD56C8)=unsigned(Address(shaders[i]));Command c;c.quad.image=image;
   assert(b.Describe(image,c.layout,c.state));assert(c.layout.stride==(i==4?104:88));
   assert(c.state.word[9]==Address(shaders[i])&&b.StateRepresentable(c.state));c.vertices.resize(4*c.layout.stride);
   // Native scene rendering may have changed the same image to another known
   // format. Preflight must allow ordered restoration, not require final==all.
   Format(image,reinterpret_cast<void*>(Address(0xBCBCD0)));assert(b.CanReplay(c));
   assert(b.PrepareResource(c));assert(image[0x38]==(i==4?9:8));
   b.TextureDeleted(99);assert(b.CanReplay(c)); // Unrelated old image.
   b.TextureDeleted(42);assert(!b.CanReplay(c));assert(std::strcmp(b.ReplayFailure(),"texture-name-or-generation-changed")==0);
   assert(b.Describe(image,c.layout,c.state));assert(b.CanReplay(c)); // Same GLuint, new generation.
   b.TextureDeleted(43);assert(!b.CanReplay(c));assert(b.Describe(image,c.layout,c.state));
   b.ForeignTextureDelete();assert(!b.CanReplay(c));assert(b.Describe(image,c.layout,c.state));
   extraUniform=true;assert(!b.CanReplay(c));extraUniform=false;
   ++At<unsigned>(shaders[i]+0x28);assert(!b.CanReplay(c));--At<unsigned>(shaders[i]+0x28);
   ++At<unsigned>(shaders[i]+8);assert(!b.CanReplay(c));--At<unsigned>(shaders[i]+8);
  }
  At<unsigned>(0xBD56C8)=0x1234;Layout layout;State state;assert(!b.Describe(image,layout,state));
  At<unsigned>(0xBD56C8)=unsigned(Address(0xBD296C));Format(image,reinterpret_cast<void*>(Address(0xBAF5E8)));
  assert(!b.Describe(image,layout,state)); // Champion layout with non-champion shader.
  Format(image,reinterpret_cast<void*>(Address(0xBCBCD0)));At<unsigned>(0xBD56C8)=0x1234;
  assert(b.Describe(image,layout,state)&&state.word[9]==Address(0xBD5724)); // get_batch overrides current shader.
  Command final;final.quad.image=image;final.resourceOnly=true;assert(b.DescribeResource(image,final.layout,final.state));
  final.state.word[21]=std::uint64_t(8)<<32|7;final.state.word[22]=std::uint64_t(10)<<32|9;
  Format(image,reinterpret_cast<void*>(Address(0xBAF720)));assert(b.CanReplay(final)&&b.PrepareResource(final));
  assert(*reinterpret_cast<unsigned*>(image+0x24)==7&&*reinterpret_cast<unsigned*>(image+0x30)==10);
  unsigned texture=77;fbo=1;assert(b.AttachmentDependency(1,&texture)==1);
  texture=99;assert(b.AttachmentDependency(1,&texture)==0);
  attachmentType=-1;assert(b.AttachmentDependency(1,&texture)==-1);attachmentType=0x1702;fbo=0;
  At<unsigned>(0xBD56CC)=unsigned(reinterpret_cast<std::uintptr_t>(image));texture=42;
  assert(b.AttachmentDependency(1,&texture)==1);At<unsigned>(0xBD56CC)=0;
  // Exercise the actual thiscall -> fastcall detour ABI repeatedly, including
  // foreign-thread passthrough and an active capture's no-op/init/change paths.
  MainBackendMock mock;Cache cache(mock);J374Hooks::cache=&cache;
  J374Hooks::formatNoop=J374Hooks::formatInit=J374Hooks::formatChange=0;
  volatile std::uintptr_t hookAddress=reinterpret_cast<std::uintptr_t>(&J374Hooks::SetFormat);
  auto hook=reinterpret_cast<J374Hooks::FormatFn>(hookAddress);
  assert(cache.Begin(Key{}));
  for(int i=0;i<1000;++i)hook(image,reinterpret_cast<void*>(Address(0xBCBCD0)));
  hook(image,reinterpret_cast<void*>(Address(0xBAF720)));
  *reinterpret_cast<void**>(image+0x34)=nullptr;image[0x38]=0;
  hook(image,reinterpret_cast<void*>(Address(0xBAF720)));cache.End();
  assert(J374Hooks::formatNoop==1000&&J374Hooks::formatChange==1&&J374Hooks::formatInit==1);
  J374Hooks::gameThreadId=0;hook(image,reinterpret_cast<void*>(Address(0xBCBCD0)));assert(image[0x38]==3);
  assert(b.stats.uniformChecks>0&&b.stats.uniformGlQueries>=2*b.stats.uniformChecks);
  J374Hooks::cache=nullptr;J374Hooks::setFormat=nullptr;
  std::puts("native backend Sprite contracts, program/layout invalidation, format replay and setter ABI: passed");
 }
};
}

namespace highfps_entity_probe {
struct TextureBackendMock:MainBackendMock {
 bool StateUsesTexture(const State&s,std::uint32_t name)const override{return name&&s.word[29]==name;}
 bool CommandUsesTexture(const Command&c,std::uint32_t name)const override{return name&&c.state.word[24]==name;}
};
static void** lazyDeleteSlot=nullptr;static unsigned deleteOriginalCalls=0;
static void WINAPI DeleteMock(int,const unsigned*){++deleteOriginalCalls;}
static void WINAPI LazyDeleteMock(int n,const unsigned*p){*lazyDeleteSlot=reinterpret_cast<void*>(&DeleteMock);DeleteMock(n,p);}
void RuntimeTestAccess::TestTextureDelete(){
 Host host;host.address=[](std::uintptr_t va){return va==0xA27220?reinterpret_cast<std::uintptr_t>(&LazyDeleteMock):NativeBackendTestAccess::Address(va);};
 Runtime r(host);NativeBackendTestAccess::Configure(r.backend_);
 TextureBackendMock backend;backend.state.word[24]=42;backend.state.word[29]=88;
 r.cache_.~Cache();new(&r.cache_)Cache(backend);r.installed_=true;Runtime::active_=&r;
 J374Hooks::gameThreadId=GetCurrentThreadId();J374Hooks::cache=&r.cache_;
 Runtime::deleteTarget_.store(&DeleteMock);
 Key key{};Quad quad;quad.image=&backend;
 auto capture=[&](){assert(r.cache_.Begin(key));const bool b=r.cache_.BeforeQuad(quad);assert(b);r.cache_.AfterQuad(b,backend.vertices);};
 capture();float extra=123;std::memcpy(backend.vertices+12,&extra,4);
 unsigned unrelated=99;Runtime::DeleteTextures(1,&unrelated);
 // Delete hook must seal before native work can disturb pending storage.
 std::memset(backend.vertices,0,sizeof backend.vertices);r.cache_.End();assert(r.cache_.Replay(key));
 float got;std::memcpy(&got,backend.vertices+12,4);assert(got==123&&r.deleteUnrelated_==1);
 capture();unsigned used=42;Runtime::DeleteTextures(1,&used);r.cache_.End();assert(!r.cache_.Replay(key));assert(r.deleteDependent_==1);
 capture();used=88;Runtime::DeleteTextures(1,&used);r.cache_.End();assert(!r.cache_.Replay(key));assert(r.deleteDependent_==2);
 capture();NativeBackendTestAccess::fbo=1;used=77;Runtime::DeleteTextures(1,&used);r.cache_.End();assert(!r.cache_.Replay(key));NativeBackendTestAccess::fbo=0;
 capture();Runtime::DeleteTextures(1,nullptr);r.cache_.End();assert(!r.cache_.Replay(key));assert(r.deleteUnknown_==1);
 capture();Runtime::DeleteTextures(0,nullptr);r.cache_.End();assert(r.cache_.Replay(key));
 backend.state.word[24]=0xffffffffu;capture();unsigned absent=0xffffffffu;
 Runtime::DeleteTextures(1,&absent);r.cache_.End();assert(r.cache_.Replay(key));backend.state.word[24]=42;
 // A lazy resolver overwrites the engine import slot. The next delete in the
 // same native unload must still pass through our observer and its original.
 void*slot=reinterpret_cast<void*>(&Runtime::DeleteTextures);r.deleteSlot_=&slot;lazyDeleteSlot=&slot;
 Runtime::deleteTarget_.store(&LazyDeleteMock);const auto before=deleteOriginalCalls;
 capture();auto invoke=reinterpret_cast<Runtime::DeleteTexturesFn>(slot);invoke(1,&unrelated);
 assert(slot==reinterpret_cast<void*>(&Runtime::DeleteTextures));assert(Runtime::deleteTarget_.load()==&DeleteMock);
 reinterpret_cast<Runtime::DeleteTexturesFn>(slot)(1,&unrelated);r.cache_.End();assert(r.cache_.Replay(key));assert(deleteOriginalCalls==before+2);
 const auto epoch=NativeBackendTestAccess::ForeignEpoch();J374Hooks::gameThreadId=0;
 Runtime::DeleteTextures(1,&used);assert(NativeBackendTestAccess::ForeignEpoch()==epoch+1);
 r.cache_.Clear();Runtime::active_=nullptr;J374Hooks::cache=nullptr;
 std::puts("selective texture delete: seal, unrelated load, captured/RT/FBO dependencies, invalid args, lazy second delete and foreign epoch: passed");
}
}

namespace highfps_entity_probe {
static unsigned parameterCalls=0,imageCalls=0;static void**parameterMockSlot=nullptr;static void**imageMockSlot=nullptr;
static void WINAPI ParameterMock(unsigned target,unsigned pname,int value){++parameterCalls;if(target==0xDE1&&pname>=0x2800&&pname<=0x2803)NativeBackendTestAccess::parameters[pname-0x2800]=value;}
static void WINAPI ImageMock(unsigned target,int level,int internal,int width,int height,int border,unsigned format,unsigned type,const void*pixels){
 ++imageCalls;assert(target==0xDE1&&internal==0x1908&&width==32&&height==32&&border==0&&format==0x1908&&type==0x1401&&pixels==reinterpret_cast<void*>(0x1234));assert(level==0||level==1);
}
static void WINAPI LazyParameterMock(unsigned a,unsigned b,int c){*parameterMockSlot=reinterpret_cast<void*>(&ParameterMock);ParameterMock(a,b,c);}
static void WINAPI LazyImageMock(unsigned a,int b,int c,int d,int e,int f,unsigned g,unsigned h,const void*i){*imageMockSlot=reinterpret_cast<void*>(&ImageMock);ImageMock(a,b,c,d,e,f,g,h,i);}
void RuntimeTestAccess::TestTextureMutation(){
 // Machine stubs give production _ReturnAddress a checked native-style callsite
 // and verify stack cleanup for the real three/nine-argument WINAPI hooks.
 auto* code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(code);
 auto stub=[](std::uint8_t*p,void*target,int args){std::size_t i=0;p[i++]=0x55;p[i++]=0x8b;p[i++]=0xec;
  for(int a=args-1;a>=0;--a){p[i++]=0xff;p[i++]=0x75;p[i++]=std::uint8_t(8+4*a);}
  p[i++]=0xb8;*reinterpret_cast<std::uint32_t*>(p+i)=unsigned(reinterpret_cast<std::uintptr_t>(target));i+=4;
  p[i++]=0xff;p[i++]=0xd0;auto*ret=p+i;p[i++]=0x5d;p[i++]=0xc2;p[i++]=std::uint8_t(args*4);p[i++]=0;return ret;};
 static std::uintptr_t parameterReturn=0,imageReturn=0;
 parameterReturn=reinterpret_cast<std::uintptr_t>(stub(code,reinterpret_cast<void*>(&Runtime::TexParameteri),3));
 imageReturn=reinterpret_cast<std::uintptr_t>(stub(code+128,reinterpret_cast<void*>(&Runtime::TexImage2D),9));
 Host host;host.address=[](std::uintptr_t va){
  if(va==0xA7131D)return parameterReturn;if(va==0xA71399)return imageReturn;
  if(va==0xA3B740)return reinterpret_cast<std::uintptr_t>(&LazyParameterMock);
  if(va==0xA3B3D0)return reinterpret_cast<std::uintptr_t>(&LazyImageMock);
  return NativeBackendTestAccess::Address(va);
 };
 Runtime r(host);NativeBackendTestAccess::Configure(r.backend_);TextureBackendMock backend;
 backend.state.word[24]=42;backend.state.word[29]=88;
 r.cache_.~Cache();new(&r.cache_)Cache(backend);Runtime::active_=&r;J374Hooks::gameThreadId=GetCurrentThreadId();J374Hooks::cache=&r.cache_;
 auto parameter=reinterpret_cast<Runtime::TexParameteriFn>(code);auto upload=reinterpret_cast<Runtime::TexImage2DFn>(code+128);
 Runtime::parameterTarget_.store(&ParameterMock);Runtime::imageTarget_.store(&ImageMock);
 auto start=[&](){assert(r.cache_.Begin(Key{}));Quad q;q.image=&backend;const auto b=r.cache_.BeforeQuad(q);assert(b);r.cache_.AfterQuad(b,backend.vertices);};
 auto finish=[&](bool hit,unsigned reason=0){r.cache_.End();assert(r.cache_.Replay(Key{})==hit);if(!hit)assert(r.cache_.LastReplay().unsafeCode==reason);};
 auto put=[&](int level=0){upload(0xDE1,level,0x1908,32,32,0,0x1908,0x1401,reinterpret_cast<void*>(0x1234));};
 NativeBackendTestAccess::boundTexture=99;NativeBackendTestAccess::parameters[0]=0x2601;
 start();float extra=321;std::memcpy(backend.vertices+12,&extra,4);parameter(0xDE1,0x2800,0x2600);std::memset(backend.vertices,0,64);put();finish(true);
 float got;std::memcpy(&got,backend.vertices+12,4);assert(got==321&&r.mutationAllowed_==2);
 NativeBackendTestAccess::boundTexture=42;start();parameter(0xDE1,0x2800,0x2600);finish(true); // Actual no-op on a captured texture.
 start();parameter(0xDE1,0x2800,0x2601);finish(false,241);
 start();put();finish(false,225); // Existing captured image storage mutation.
 NativeBackendTestAccess::boundTexture=77;NativeBackendTestAccess::fbo=1;start();put();finish(false,225);NativeBackendTestAccess::fbo=0;
 NativeBackendTestAccess::boundTexture=99;start();parameter(0xDE1,0x2800,0x2700);finish(false,241); // Unsupported filtering contract.
 start();put(1);finish(false,225); // Non-base upload is not the proved initialization path.
 start();Runtime::TexParameteri(0xDE1,0x2800,0x2600);finish(false,241); // Unknown caller remains guarded.
 NativeBackendTestAccess::boundTexture=0;start();put();finish(false,225);NativeBackendTestAccess::boundTexture=99;
 void*pslot=reinterpret_cast<void*>(&Runtime::TexParameteri),*islot=reinterpret_cast<void*>(&Runtime::TexImage2D);
 r.parameterSlot_=&pslot;r.imageSlot_=&islot;parameterMockSlot=&pslot;imageMockSlot=&islot;
 Runtime::parameterTarget_.store(&LazyParameterMock);Runtime::imageTarget_.store(&LazyImageMock);
 start();parameter(0xDE1,0x2800,0x2600);put();finish(true);
 assert(pslot==reinterpret_cast<void*>(&Runtime::TexParameteri)&&Runtime::parameterTarget_.load()==&ParameterMock);
 assert(islot==reinterpret_cast<void*>(&Runtime::TexImage2D)&&Runtime::imageTarget_.load()==&ImageMock);
 const auto epoch=NativeBackendTestAccess::ForeignEpoch();J374Hooks::gameThreadId=0;parameter(0xDE1,0x2800,0x2601);assert(NativeBackendTestAccess::ForeignEpoch()==epoch+1);
 assert(parameterCalls==7&&imageCalls==6);r.cache_.Clear();Runtime::active_=nullptr;J374Hooks::cache=nullptr;VirtualFree(code,0,MEM_RELEASE);
 std::puts("native texture initialization: verified caller ABI, seal, bound-unit identity, no-op, dependency/FBO/unknown rejection and lazy targets: passed");
}
}

int main(){
 NativeBackendTestAccess::Test();
 RuntimeTestAccess::TestTextureDelete();
 RuntimeTestAccess::TestTextureMutation();
 TestSubmitFastPath();
 TestRefs();
 RuntimeTestAccess::TestMain();
 RuntimeTestAccess::Test();
 RuntimeTestAccess::TestObserverFastPath();
 J374Hooks::player=Original;J374Hooks::canonical=false;J374Hooks::gameThreadId=0;
 J374Hooks::Player(nullptr,nullptr);assert(calls==1); // Other thread always reaches native.
 J374Hooks::gameThreadId=GetCurrentThreadId();J374Hooks::Player(nullptr,nullptr);assert(calls==1);
 J374Hooks::canonical=true;J374Hooks::Player(nullptr,nullptr);assert(calls==2);
 J374Hooks::canonical=false;J374Hooks::InsideRender=[](){return false;};J374Hooks::Player(nullptr,nullptr);assert(calls==3);
 J374Hooks::InsideRender=[](){return true;};J374Hooks::Player(nullptr,nullptr);assert(calls==3);
 std::puts("foreign-thread/Update-context native forwarding and pure-render suppression: passed");
}
