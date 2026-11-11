#include "input_result_cache_j374.h"
#include <cassert>
#include <cstdio>
using namespace highfps_input_cache;
static bool inside=false,handled=true,boolValue=true,reenter=false,replaceEpoch=false,closeInside=false;
static std::uint32_t valueBits=0x3f000000;static std::uint64_t epoch=1;
static unsigned boolCalls=0,valueCalls=0,shaderCloses=0,luaCloses=0;
static std::uintptr_t unusedSeen=0;
static Runtime::BoolFn reenterFn=nullptr;static Runtime::CloseFn observedClose=nullptr;
static bool __stdcall BoolOriginal(void*entity,int hook,int action,bool*out){
 ++boolCalls;
 if(reenter){reenter=false;const bool old=handled;handled=false;bool inner=true;assert(!reenterFn(entity,hook,action,&inner)&&inner);handled=old;}
 if(replaceEpoch){replaceEpoch=false;++epoch;}
 if(closeInside){closeInside=false;observedClose(reinterpret_cast<void*>(0x1234));}
 if(handled)*out=boolValue;return handled;
}
static bool __stdcall ValueOriginal(void*,std::uintptr_t unused,int,float*out){++valueCalls;unusedSeen=unused;if(handled)std::memcpy(out,&valueBits,4);return handled;}
static void __cdecl ShaderCloseChain(void*){++shaderCloses;++luaCloses;}
namespace highfps_input_cache {
struct RuntimeTestAccess {
 inline static std::uint8_t engine[0x20]{},entity[0x400]{};
 inline static std::uintptr_t enginePointer=reinterpret_cast<std::uintptr_t>(engine);
 inline static std::uint32_t mask=0x2000;
 inline static std::uintptr_t dispatcher[2]={0x1234,77};
 inline static Runtime::CloseFn closeSlot=&ShaderCloseChain;
 inline static std::uint8_t*code=nullptr;inline static unsigned pokeCalls=0,failPoke=0;
 static std::uintptr_t Address(std::uintptr_t va){
  if(va==0xC0C08C)return reinterpret_cast<std::uintptr_t>(&enginePointer);
  if(va==0xAD01D0)return reinterpret_cast<std::uintptr_t>(&closeSlot);
  if(va==0x451FB0){static std::uint8_t index[]={0x8B,0x41,0x20,0xC3};return reinterpret_cast<std::uintptr_t>(index);}
  if(va==0x86E330){static std::uint8_t call[]={0xFF,0x15,0,0,0,0};*reinterpret_cast<std::uint32_t*>(call+2)=unsigned(Address(0xAD01D0));return reinterpret_cast<std::uintptr_t>(call);}
  assert(va>=0x864900&&va<0x864D00);return reinterpret_cast<std::uintptr_t>(code+va-0x864900);
 }
 static Host MakeHost(){
  *reinterpret_cast<std::uintptr_t*>(engine)=reinterpret_cast<std::uintptr_t>(&mask);
  *reinterpret_cast<std::uintptr_t*>(engine+0x10)=reinterpret_cast<std::uintptr_t>(dispatcher);
  Host h;h.address=&Address;h.insideRender=[](){return inside;};h.epoch=[](){return epoch;};
  h.expect=[](std::uintptr_t va,const std::uint8_t*b,std::size_t n,const char*){return std::memcmp(reinterpret_cast<void*>(Address(va)),b,n)==0;};
  h.poke=[](std::uintptr_t va,const std::uint8_t*b,std::size_t n){if(++pokeCalls==failPoke)return false;std::memcpy(reinterpret_cast<void*>(Address(va)),b,n);return true;};
  h.writeJump=[](std::uint8_t*p,const void*t){p[0]=0xE9;*reinterpret_cast<std::int32_t*>(p+1)=std::int32_t(reinterpret_cast<std::uintptr_t>(t)-(reinterpret_cast<std::uintptr_t>(p)+5));};return h;
 }
 static void DataTests(){
  Runtime r(MakeHost());r.installed_=true;r.thread_=GetCurrentThreadId();Runtime::active_=&r;
  Runtime::boolOriginal_=&BoolOriginal;Runtime::valueOriginal_=&ValueOriginal;Runtime::closeOriginal_=&ShaderCloseChain;
  reenterFn=&Runtime::BoolHook;observedClose=&Runtime::CloseHook;
  *reinterpret_cast<unsigned*>(entity+0x20)=10;*reinterpret_cast<unsigned*>(entity+0x28)=1;*reinterpret_cast<unsigned*>(entity+0x3EC)=99;
  inside=false;handled=true;boolValue=true;r.BeginTick();bool out=false;
  for(int i=0;i<10;++i)assert(Runtime::BoolHook(entity,0,4,&out)&&out);
  assert(boolCalls==10);inside=true;r.BeginRender(true);boolValue=false;
  assert(Runtime::BoolHook(entity,0,4,&out)&&!out);const unsigned canonical=boolCalls;
  r.BeginRender(false);out=true;
  for(int i=0;i<200;++i)assert(Runtime::BoolHook(entity,0,4,&out)&&!out);
  assert(boolCalls==canonical); // False override is handled, unlike nil.
  out=true;assert(!Runtime::BoolHook(entity,1,4,&out)&&out); // Different hook, raw fallback untouched.
  inside=false;handled=false;assert(!Runtime::BoolHook(entity,1,4,&out)&&out);
  inside=true;r.BeginRender(false);assert(!Runtime::BoolHook(entity,1,4,&out)&&out);assert(r.counts_.nil[1]==1);
  // Same entity pointer, different lifetime fingerprint cannot inherit input.
  ++*reinterpret_cast<unsigned*>(entity+0x20);assert(!Runtime::BoolHook(entity,0,4,&out)&&out);
  --*reinterpret_cast<unsigned*>(entity+0x20);
  r.BeginTick();assert(!Runtime::BoolHook(entity,0,4,&out)&&out);
  // Null entity is valid; float wrapper arg2 is unused, not a controller key.
  inside=false;handled=true;float number=0;valueBits=0x80000000;
  assert(Runtime::ValueHook(nullptr,0xdeadbeef,11,&number)&&unusedSeen==0xdeadbeef);
  inside=true;r.BeginRender(false);number=1;
  assert(Runtime::ValueHook(nullptr,0,11,&number));unsigned bits=0;std::memcpy(&bits,&number,4);assert(bits==0x80000000&&valueCalls==1);
  inside=false;valueBits=0x7fc12345;assert(Runtime::ValueHook(nullptr,9,11,&number));
  inside=true;r.BeginRender(false);assert(Runtime::ValueHook(nullptr,7,11,&number));std::memcpy(&bits,&number,4);assert(bits==0x7fc12345);
  inside=false;handled=false;number=9;
  assert(!Runtime::ValueHook(nullptr,0,12,&number)&&number==9);
  inside=true;r.BeginRender(false);number=10;
  assert(!Runtime::ValueHook(nullptr,99,12,&number)&&number==10);
  assert(!Runtime::ValueHook(nullptr,99,123,&number)&&number==10);handled=true;
  // Recursive nil result is stored first, then the authoritative outer override.
  inside=false;reenter=true;boolValue=true;assert(Runtime::BoolHook(nullptr,0,2,&out));
  inside=true;r.BeginRender(false);out=false;assert(Runtime::BoolHook(nullptr,0,2,&out)&&out);
  const auto before=boolCalls;inside=false; // TimeMachine-like multiple updates always execute.
  for(int i=0;i<7;++i)assert(Runtime::BoolHook(nullptr,0,2,&out));assert(boolCalls==before+7);
  inside=true;r.BeginRender(false);Runtime::CloseHook(reinterpret_cast<void*>(0x1234));out=true;
  assert(!Runtime::BoolHook(nullptr,0,2,&out)&&out);assert(shaderCloses==1&&luaCloses==1);
  inside=false;closeInside=true;assert(Runtime::BoolHook(nullptr,0,2,&out));
  inside=true;r.BeginRender(false);assert(!Runtime::BoolHook(nullptr,0,2,&out)); // Close/reuse during original: no stale store.
  inside=false;replaceEpoch=true;assert(Runtime::BoolHook(nullptr,0,2,&out));
  inside=true;r.BeginRender(false);assert(!Runtime::BoolHook(nullptr,0,2,&out));
  inside=false;assert(Runtime::BoolHook(nullptr,0,2,&out));++dispatcher[1];
  inside=true;r.BeginRender(false);assert(!Runtime::BoolHook(nullptr,0,2,&out));
  mask=0;assert(!Runtime::BoolHook(nullptr,0,2,&out));mask=0x2000;
  r.thread_=0;const auto foreign=boolCalls;assert(Runtime::BoolHook(nullptr,0,2,&out));assert(boolCalls==foreign+1);
  r.installed_=false;Runtime::active_=nullptr;
  Samples bounded;for(unsigned i=0;i<Samples::Limit;++i){Key k;k.action=int(i);assert(bounded.Store(k,{}));}
  Key over;over.action=9999;assert(!bounded.Store(over,{}));over.action=0;assert(bounded.Store(over,{true,1}));
  std::puts("typed scalar cache: nil/false, float bits, entity/null keys, latest sample, native extra updates, tick/epoch/VM invalidation and bounds passed");
 }
 static void InstallTests(){
  code=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(code);
  Host h=MakeHost();
  auto stub=[&](std::uintptr_t va,void*fn){auto*p=reinterpret_cast<std::uint8_t*>(Address(va));const std::uint8_t prefix[]={0x55,0x8B,0xEC,0x6A,0xFF,0x83,0xC4,4,0x5D};std::memcpy(p,prefix,sizeof prefix);h.writeJump(p+9,fn);};
  stub(0x864970,reinterpret_cast<void*>(&BoolOriginal));stub(0x864AD0,reinterpret_cast<void*>(&ValueOriginal));
  const std::uint8_t ret[]={0xC2,0x10,0},bc[]={0xE8,0x4E,0xF8,0,0},vc[]={0xE8,0xEC,0xF6,0,0};
  std::memcpy(reinterpret_cast<void*>(Address(0x864ABA)),ret,3);std::memcpy(reinterpret_cast<void*>(Address(0x864C21)),ret,3);
  std::memcpy(reinterpret_cast<void*>(Address(0x8649DD)),bc,5);std::memcpy(reinterpret_cast<void*>(Address(0x864B3F)),vc,5);
  pokeCalls=0;failPoke=3;closeSlot=&ShaderCloseChain;Runtime failed(h);assert(!failed.Install());
  assert(*reinterpret_cast<std::uint8_t*>(Address(0x864970))==0x55&&closeSlot==&ShaderCloseChain&&Runtime::active_==nullptr);
  pokeCalls=0;failPoke=0;Runtime r(h);assert(r.Install());
  auto boolean=reinterpret_cast<Runtime::BoolFn>(Address(0x864970));auto numeric=reinterpret_cast<Runtime::ValueFn>(Address(0x864AD0));
  inside=false;handled=true;boolValue=true;valueBits=0x3f400000;r.BeginTick();bool out=false;float value=0;
  for(int i=0;i<1000;++i){assert(boolean(nullptr,0,3,&out)&&out);assert(numeric(nullptr,unsigned(i),3,&value)&&value==.75f);}
  const auto n=boolCalls,v=valueCalls;inside=true;r.BeginRender(false);
  for(int i=0;i<1000;++i){assert(boolean(nullptr,0,3,&out)&&out);assert(numeric(nullptr,999,3,&value)&&value==.75f);}
  assert(boolCalls==n&&valueCalls==v);const auto closes=shaderCloses;closeSlot(reinterpret_cast<void*>(0x1234));assert(shaderCloses==closes+1);
  out=true;assert(!boolean(nullptr,0,3,&out)&&out);
  r.Shutdown();assert(closeSlot==&ShaderCloseChain&&*reinterpret_cast<std::uint8_t*>(Address(0x864970))==0x55);
  VirtualFree(code,0,MEM_RELEASE);std::puts("actual guarded detours: RET16 ABI, native/pure dispatch, chained close and partial-install rollback passed");
 }
};
}
int main(){RuntimeTestAccess::DataTests();RuntimeTestAccess::InstallTests();}
