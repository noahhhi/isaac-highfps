#include "shader_params_cache_j374.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
using namespace highfps_shader_params;
static const char* requestedLuaPath=nullptr;
struct TestLua {
 HMODULE module;LuaApi api;void* state=nullptr;
 void*(__cdecl*newstate)();void(__cdecl*close)(void*);void(__cdecl*openlibs)(void*);
 int(__cdecl*loadstring)(void*,const char*);int(__cdecl*pcall)(void*,int,int,int,std::intptr_t,void*);
 int(__cdecl*getfield)(void*,int,const char*);double(__cdecl*number)(void*,int,int*);
 TestLua(){const char* supplied=requestedLuaPath ? requestedLuaPath : std::getenv("ISAAC_TEST_LUA_DLL");
 module=LoadLibraryA(supplied && *supplied ? supplied : "Lua5.3.3r.dll");assert(module);assert(api.Load(module));
#define LOAD(f,n) f=reinterpret_cast<decltype(f)>(GetProcAddress(module,n));assert(f)
 LOAD(newstate,"luaL_newstate");LOAD(close,"lua_close");LOAD(openlibs,"luaL_openlibs");LOAD(loadstring,"luaL_loadstring");LOAD(pcall,"lua_pcallk");LOAD(getfield,"lua_getfield");LOAD(number,"lua_tonumberx");
#undef LOAD
 state=newstate();assert(state);openlibs(state);}
 void Run(const char*code,int results=0){assert(loadstring(state,code)==0);assert(pcall(state,0,results,0,0,nullptr)==0);}
 LuaRef Eval(const char*code){Run(code,1);return{state,api.ref(state,Registry)};}
 double Field(const LuaRef&r,const char*name){const int top=api.gettop(state);api.rawgeti(state,Registry,r.ref);getfield(state,-1,name);double n=number(state,-1,nullptr);api.settop(state,top);return n;}
 void Release(const LuaRef&r){if(r.ref>=0)api.unref(r.state,Registry,r.ref);}
};
static TestLua* activeLua=nullptr;static unsigned originalCalls=0;static bool inRender=true;
static LuaRef* __stdcall Original(LuaRef*out,const void*){
 ++originalCalls;char text[128];std::snprintf(text,sizeof text,"return {counter=%u, ProbeTint={0.1,0.2,0.3,1}}",originalCalls);*out=activeLua->Eval(text);return out;
}
struct Name24{char data[16];std::uint32_t size,capacity;};
static Name24 Name(const char*s){Name24 n{};n.size=unsigned(std::strlen(s));n.capacity=15;if(n.size<16)std::memcpy(n.data,s,n.size);else{n.capacity=n.size;*reinterpret_cast<const char**>(n.data)=s;}return n;}
static int fakeGeneration=1,fakeUnrefs=0;
static void __cdecl FakeUnref(void*,int,int ref){assert(fakeGeneration==1&&ref==7);++fakeUnrefs;}
namespace highfps_shader_params {
struct RuntimeTestAccess {
 static void TestAddressReuse(){
  LuaApi api;api.unref=FakeUnref;TableCache cache(api);int sameAddress=0;
  cache.vm_=&sameAddress;cache.epoch_=1;cache.entries_.push_back({"same-address",0,7,true});
  assert(cache.BeforeClose(&sameAddress));assert(fakeUnrefs==1&&cache.vm_==nullptr);
  fakeGeneration=2;cache.Begin(2,&sameAddress);assert(fakeUnrefs==1&&cache.LiveRefs()==0);
  std::puts("exact same VM address reuse cannot unref a new generation: passed");
 }

 static void TestRollback(TestLua& t){
  static std::uint8_t engine[0x20],entry[16]={0x55,0x8B,0xEC,0x6A,0xFF};
  static std::uintptr_t enginePtr=reinterpret_cast<std::uintptr_t>(engine);
  static unsigned closeCalls=0,pokeCalls=0;static unsigned failMask=0;
  static Runtime::CloseFn nativeClose=[](void*){++closeCalls;};
  *reinterpret_cast<void**>(engine+0x18)=t.state;
  Host h;h.address=[](std::uintptr_t a)->std::uintptr_t{
   if(a==0xC0C08C)return reinterpret_cast<std::uintptr_t>(&enginePtr);
   if(a==0xAD01D0)return reinterpret_cast<std::uintptr_t>(&nativeClose);
   if(a==0x865210 || a==0x865215)return reinterpret_cast<std::uintptr_t>(entry)+(a-0x865210);
   assert(false);return 0;
  };
  h.expect=[](std::uintptr_t,const std::uint8_t*,std::size_t,const char*){return true;};
  h.writeJump=[](std::uint8_t*,const void*){};
  h.insideRender=[](){return inRender;};
  h.poke=[](std::uintptr_t,const std::uint8_t*,std::size_t){return (failMask & (1u<<pokeCalls++))==0;};
  activeLua=&t;inRender=true;auto name=Name("rollback");LuaRef out;
  // Complete installation, then a coordinator health failure rejects BOTH
  // restores. Retained entry must forward without inspecting even a bad name.
  Runtime r(h);failMask=0;pokeCalls=0;assert(r.Install());assert(pokeCalls==2);
  r.original_=Original;r.BeginRender(21,true);Runtime::Hook(&out,&name);t.Release(out);
  assert(r.cache_.LiveRefs()==1);failMask=~0u;pokeCalls=0;r.Shutdown();
  assert(!r.installed_ && r.entryPatched_ && r.closePatched_ && Runtime::active_==&r);
  assert(r.cache_.LiveRefs()==0);const auto calls=originalCalls;const auto epoch=r.epoch_;
  r.BeginRender(999,false);assert(r.epoch_==epoch);
  Runtime::Hook(&out,reinterpret_cast<void*>(1));t.Release(out);assert(originalCalls==calls+1);
  const auto closes=closeCalls;Runtime::CloseHook(t.state);assert(closeCalls==closes+1);
  assert(r.stats_.closes==0 && !r.Install());
  // Partial restoration still retains the owner until the final reachable hook
  // is restored. A second Shutdown retries only the still-owned patch.
  pokeCalls=0;failMask=2;r.Shutdown();assert(!r.entryPatched_ && r.closePatched_ && Runtime::active_==&r);
  pokeCalls=0;failMask=0;r.Shutdown();assert(pokeCalls==1 && Runtime::active_==nullptr);
  // Entry installation failed after successful close-hook installation and the
  // close rollback failed too. Install(false) must retain its forwarding owner.
  Runtime partial(h);pokeCalls=0;failMask=6;assert(!partial.Install());
  assert(!partial.entryPatched_ && partial.closePatched_ && Runtime::active_==&partial);
  const auto partialCloses=closeCalls;Runtime::CloseHook(t.state);assert(closeCalls==partialCloses+1);
  partial.BeginRender(1,false);assert(partial.epoch_==0 && partial.cache_.LiveRefs()==0);
  pokeCalls=0;failMask=0;partial.Shutdown();assert(pokeCalls==1 && Runtime::active_==nullptr);
  Runtime rejected(h);pokeCalls=0;failMask=1;assert(!rejected.Install());
  assert(!rejected.entryPatched_ && !rejected.closePatched_ && Runtime::active_==nullptr);
  std::puts("shader rollback failures retain native-forwarding hook ownership, retry safely, and never run caches: passed");
 }

 static void Test(TestLua& t){
  static std::uint8_t engine[0x20];static std::uintptr_t enginePtr=reinterpret_cast<std::uintptr_t>(engine);*reinterpret_cast<void**>(engine+0x18)=t.state;
  Host h;h.address=[](std::uintptr_t a){assert(a==0xC0C08C);return reinterpret_cast<std::uintptr_t>(&enginePtr);};h.insideRender=[](){return inRender;};
  Runtime r(h);assert(r.lua_.Load(t.module));r.thread_=GetCurrentThreadId();r.original_=Original;r.installed_=true;Runtime::active_=&r;
  activeLua=&t;originalCalls=0;auto a=Name("HF_A"),b=Name("HFShaderProbeLongNameB");LuaRef out;
  r.BeginRender(1,true);assert(Runtime::Hook(&out,&a)==&out);assert(t.Field(out,"counter")==1);t.Release(out);
  Runtime::Hook(&out,&b);assert(t.Field(out,"counter")==2);t.Release(out);
  Runtime::Hook(&out,&a);assert(t.Field(out,"counter")==3);t.Release(out);
  r.BeginRender(1,false);Runtime::Hook(&out,&a);assert(t.Field(out,"counter")==1);t.Release(out);
  Runtime::Hook(&out,&b);assert(t.Field(out,"counter")==2);t.Release(out);
  Runtime::Hook(&out,&a);assert(t.Field(out,"counter")==3);t.Release(out);assert(originalCalls==3);
  auto absent=Name("absent");Runtime::Hook(&out,&absent);assert(out.state==t.state&&out.ref==NilRef&&originalCalls==3);
  inRender=false;Runtime::Hook(&out,&a);t.Release(out);assert(originalCalls==4);inRender=true;
  r.thread_=0;Runtime::Hook(&out,&a);t.Release(out);assert(originalCalls==5);r.thread_=GetCurrentThreadId();
  r.BeginRender(2,false);Runtime::Hook(&out,&a);assert(out.ref==NilRef&&originalCalls==5);
  r.BeginRender(3,true);Runtime::Hook(&out,&a);t.Release(out);assert(r.cache_.LiveRefs()==1);
  Runtime::closeChain_=t.close;Runtime::CloseHook(t.state);assert(r.cache_.vm_==nullptr&&r.cache_.LiveRefs()==0);
  t.state=t.newstate();t.openlibs(t.state);*reinterpret_cast<void**>(engine+0x18)=t.state;
  auto sentinel=t.Eval("return {counter=987}");r.BeginRender(4,false);Runtime::Hook(&out,&a);
  assert(out.ref==NilRef&&t.Field(sentinel,"counter")==987);t.Release(sentinel);
  r.cache_.Clear(t.state);Runtime::active_=nullptr;
  std::puts("runtime callback21 ABI, names/ordinals, pure suppression, forwarding and close/reopen reference safety: passed");
 }
};
}
static LONG CALLBACK Fault(EXCEPTION_POINTERS*p){std::printf("FAULT code=%08lx addr=%p eip=%08lx esp=%08lx\n",p->ExceptionRecord->ExceptionCode,p->ExceptionRecord->ExceptionAddress,p->ContextRecord->Eip,p->ContextRecord->Esp);std::printf("lua_type L=%p idx=%d ecx=%08lx\n",*reinterpret_cast<void**>(p->ContextRecord->Ebp+8),*reinterpret_cast<int*>(p->ContextRecord->Ebp+12),p->ContextRecord->Ecx);return EXCEPTION_CONTINUE_SEARCH;}
int main(int argc,char** argv){
 if(argc>1) requestedLuaPath=argv[1];
 AddVectoredExceptionHandler(1,Fault);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);std::setvbuf(stdout,nullptr,_IONBF,0);std::puts("shader unit start");
 TestLua t;std::puts("Lua loaded and created");TableCache cache(t.api);cache.Begin(1,t.state);
 auto original=t.Eval("params={enabled=1,tint={0.1,0.2,0.3,1}};return params");
 t.api.pushnil(t.state);const int top=t.api.gettop(t.state);
 assert(cache.Store("A",0,original));assert(t.api.gettop(t.state)==top);
 t.Run("params.enabled=99;params.tint[1]=77");t.Release(original);
 LuaRef out;assert(cache.Acquire("A",0,out));assert(t.Field(out,"enabled")==1);
 t.api.rawgeti(t.state,Registry,out.ref);t.getfield(t.state,-1,"tint");t.api.rawgeti(t.state,-1,1);assert(t.number(t.state,-1,nullptr)==0.1);t.api.settop(t.state,top);t.Release(out);
 assert(cache.Acquire("A",0,out));t.Release(out);assert(cache.LiveRefs()==1);
 auto meta=t.Eval("metaRuns=0;return setmetatable({x=1},{__index=function()metaRuns=metaRuns+1;return 9 end})");
 assert(!cache.Store("A",0,meta));assert(cache.LastReject()==Reject::Metatable);assert(t.api.gettop(t.state)==top);t.Release(meta);assert(!cache.Acquire("A",0,out));
 auto check=t.Eval("return {count=metaRuns}");assert(t.Field(check,"count")==0);t.Release(check);
 auto cycle=t.Eval("local t={};t.self=t;return t");assert(!cache.Store("cycle",0,cycle));assert(cache.LastReject()==Reject::Cycle);t.Release(cycle);assert(t.api.gettop(t.state)==top);
 auto fn=t.Eval("return {x=function()return 1 end}");assert(!cache.Store("function",0,fn));assert(cache.LastReject()==Reject::UnsupportedType);t.Release(fn);
 auto deep=t.Eval("local root={};local p=root;for i=1,12 do p.child={};p=p.child end;return root");assert(!cache.Store("deep",0,deep));assert(cache.LastReject()==Reject::Limit);t.Release(deep);
 assert(cache.Store("nil",0,LuaRef{t.state,NilRef}));assert(cache.Acquire("nil",0,out)&&out.ref==NilRef);
 auto a=t.Eval("return {enabled=7}");assert(cache.Store("A",0,a));t.Release(a);cache.Begin(2,t.state);assert(!cache.Acquire("A",0,out));assert(cache.LiveRefs()==0);
 RuntimeTestAccess::TestAddressReuse();RuntimeTestAccess::Test(t);RuntimeTestAccess::TestRollback(t);
 cache.Clear(t.state);t.close(t.state);t.state=t.newstate();t.openlibs(t.state);cache.Begin(3,t.state);assert(!cache.Acquire("A",0,out));cache.Clear(t.state);t.close(t.state);
 std::puts("real Lua DLL immutable deep copy, caller refs, stack balance, metatable/cycle/type/bounds rejection, nil and VM epoch: passed");
}
