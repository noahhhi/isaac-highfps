#pragma once
// J374 MC_GET_SHADER_PARAMS=21 snapshot cache. No installation occurs implicitly.
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
namespace highfps_shader_params {
constexpr int Registry=-1001000,NilRef=-1;
struct LuaRef{void* state=nullptr;int ref=NilRef;};
static_assert(sizeof(LuaRef)==8,"J374 Lua reference ABI");
struct LuaApi {
 int(__cdecl*gettop)(void*)=nullptr;
 int(__cdecl*checkstack)(void*,int)=nullptr;
 void(__cdecl*settop)(void*,int)=nullptr;
 void(__cdecl*pushvalue)(void*,int)=nullptr;
 int(__cdecl*rawgeti)(void*,int,std::int64_t)=nullptr;
 int(__cdecl*ref)(void*,int)=nullptr;
 void(__cdecl*unref)(void*,int,int)=nullptr;
 int(__cdecl*type)(void*,int)=nullptr;
 int(__cdecl*getmetatable)(void*,int)=nullptr;
 const void*(__cdecl*topointer)(void*,int)=nullptr;
 void(__cdecl*createtable)(void*,int,int)=nullptr;
 void(__cdecl*pushnil)(void*)=nullptr;
 int(__cdecl*next)(void*,int)=nullptr;
 void(__cdecl*rawset)(void*,int)=nullptr;
 bool Load(HMODULE module=nullptr){
  if(!module)module=GetModuleHandleA("Lua5.3.3r.dll");if(!module)return false;
#define HF_LOAD(member,name) member=reinterpret_cast<decltype(member)>(GetProcAddress(module,name));if(!member)return false
  HF_LOAD(gettop,"lua_gettop");HF_LOAD(checkstack,"lua_checkstack");HF_LOAD(settop,"lua_settop");HF_LOAD(pushvalue,"lua_pushvalue");
  HF_LOAD(rawgeti,"lua_rawgeti");HF_LOAD(ref,"luaL_ref");HF_LOAD(unref,"luaL_unref");
  HF_LOAD(type,"lua_type");HF_LOAD(getmetatable,"lua_getmetatable");HF_LOAD(topointer,"lua_topointer");
  HF_LOAD(createtable,"lua_createtable");HF_LOAD(pushnil,"lua_pushnil");HF_LOAD(next,"lua_next");HF_LOAD(rawset,"lua_rawset");
#undef HF_LOAD
  return true;
 }
};
enum class Reject {None,Metatable,UnsupportedType,Cycle,Limit,MissingVm,BadName};
inline const char* RejectName(Reject r){switch(r){case Reject::Metatable:return "metatable";case Reject::UnsupportedType:return "unsupported-value";case Reject::Cycle:return "cycle";case Reject::Limit:return "bounds";case Reject::MissingVm:return "missing-VM";case Reject::BadName:return "shader-name";default:return "none";}}
class TableCache {
 friend struct RuntimeTestAccess;
 struct Entry{std::string name;unsigned ordinal;int ref=NilRef;bool valid=false;};
 LuaApi& lua_;void* vm_=nullptr;std::uint64_t epoch_=~std::uint64_t(0);std::vector<Entry> entries_;
 Reject reject_=Reject::None;
 bool Clone(int index,unsigned depth,unsigned& nodes,std::vector<const void*>& active){
  if(depth>8 || ++nodes>4096){reject_=Reject::Limit;return false;}
  if(!lua_.checkstack(vm_,12)){reject_=Reject::Limit;return false;}
  const int t=lua_.type(vm_,index);
  if(t==0 || t==1 || t==3 || t==4){lua_.pushvalue(vm_,index);return true;}
  if(t!=5){reject_=Reject::UnsupportedType;return false;}
  if(lua_.getmetatable(vm_,index)){lua_.settop(vm_,lua_.gettop(vm_)-1);reject_=Reject::Metatable;return false;}
  const void* identity=lua_.topointer(vm_,index);
  if(std::find(active.begin(),active.end(),identity)!=active.end()){reject_=Reject::Cycle;return false;}
  active.push_back(identity);lua_.createtable(vm_,0,0);const int dest=lua_.gettop(vm_);
  lua_.pushnil(vm_);
  while(lua_.next(vm_,index)){
   const int value=lua_.gettop(vm_),key=value-1,keyType=lua_.type(vm_,key);
   if(keyType!=3 && keyType!=4){reject_=Reject::UnsupportedType;return false;}
   lua_.pushvalue(vm_,key);
   if(!Clone(value,depth+1,nodes,active))return false;
   lua_.rawset(vm_,dest);lua_.settop(vm_,value-1); // retain original next() key
  }
  active.pop_back();return true;
 }
 void ReleaseEntries(){for(const auto&e:entries_)if(e.ref>=0)lua_.unref(vm_,Registry,e.ref);entries_.clear();}
public:
 explicit TableCache(LuaApi& lua):lua_(lua){}
 // BeforeClose is called by the verified native lua_close import chain. It
 // clears refs while the state is live and forgets its address before reuse.
 void Begin(std::uint64_t epoch,void* current){
  if(current!=vm_){entries_.clear();vm_=current;epoch_=epoch;}
  else if(epoch!=epoch_){if(vm_)ReleaseEntries();else entries_.clear();epoch_=epoch;}
 }
 bool BeforeClose(void* closing){if(vm_!=closing)return false;ReleaseEntries();vm_=nullptr;epoch_=~std::uint64_t(0);return true;}
 void Clear(void* knownLive){if(vm_ && vm_==knownLive)ReleaseEntries();else entries_.clear();}
 bool Store(const std::string& name,unsigned ordinal,const LuaRef& original){
  reject_=Reject::None;
  if(!original.state || original.state!=vm_){reject_=Reject::MissingVm;return false;}
  Entry* entry=nullptr;for(auto&e:entries_)if(e.name==name && e.ordinal==ordinal){entry=&e;break;}
  if(!entry){if(entries_.size()>=128){reject_=Reject::Limit;return false;}entries_.push_back({name,ordinal});entry=&entries_.back();}
  if(entry->ref>=0)lua_.unref(vm_,Registry,entry->ref);entry->ref=NilRef;entry->valid=false;
  if(original.ref<0){entry->valid=true;return true;}
  if(!lua_.checkstack(vm_,4)){reject_=Reject::Limit;return false;}
  const int top=lua_.gettop(vm_);lua_.rawgeti(vm_,Registry,original.ref);
  unsigned nodes=0;std::vector<const void*> active;
  if(!Clone(top+1,0,nodes,active)){lua_.settop(vm_,top);return false;}
  entry->ref=lua_.ref(vm_,Registry);entry->valid=true;lua_.settop(vm_,top);return true;
 }
 bool Acquire(const std::string&name,unsigned ordinal,LuaRef&out){
  out={vm_,NilRef};
  if(!vm_)return false;
  for(const auto&e:entries_)if(e.name==name && e.ordinal==ordinal && e.valid){
   if(e.ref>=0){if(!lua_.checkstack(vm_,2))return false;const int top=lua_.gettop(vm_);lua_.rawgeti(vm_,Registry,e.ref);out.ref=lua_.ref(vm_,Registry);lua_.settop(vm_,top);}return true;
  }
  return false;
 }
 Reject LastReject()const{return reject_;}
 unsigned LiveRefs()const{unsigned n=0;for(const auto&e:entries_)n+=e.ref>=0;return n;}
};
struct Host {
 std::uintptr_t(*address)(std::uintptr_t)=nullptr;
 bool(*expect)(std::uintptr_t,const std::uint8_t*,std::size_t,const char*)=nullptr;
 bool(*poke)(std::uintptr_t,const std::uint8_t*,std::size_t)=nullptr;
 void(*writeJump)(std::uint8_t*,const void*)=nullptr;
 void(*log)(const char*)=nullptr;
 bool(*insideRender)()=nullptr;
};
class Runtime {
public:
 using Original=LuaRef*(__stdcall*)(LuaRef*,const void*);
 struct Stats{std::uint64_t canonical=0,hits=0,misses=0,rejected=0,closes=0;};
private:
 friend struct RuntimeTestAccess;
 Host host_;LuaApi lua_;TableCache cache_;Original original_=nullptr;DWORD thread_=0;
 bool installed_=false,canonical_=true;std::uint64_t epoch_=0;
 std::vector<std::pair<std::string,unsigned>> ordinals_;
 std::uint8_t originalBytes_[5]{};std::uint8_t* trampoline_=nullptr;
 Stats stats_;ULONGLONG nextLog_=0;
 inline static Runtime* active_=nullptr;
 using CloseFn=void(__cdecl*)(void*);
 inline static CloseFn closeChain_=nullptr;
 void* CurrentVm()const{
  const auto engine=*reinterpret_cast<std::uintptr_t*>(host_.address(0xC0C08C));
  return engine>=0x10000?*reinterpret_cast<void**>(engine+0x18):nullptr;
 }
 bool Name(const void* raw,std::string& out){
  if(!raw)return false;auto*p=static_cast<const std::uint8_t*>(raw);
  const auto size=*reinterpret_cast<const std::uint32_t*>(p+0x10),capacity=*reinterpret_cast<const std::uint32_t*>(p+0x14);
  if(size>4096 || capacity<size)return false;
  const char* text=capacity<16?reinterpret_cast<const char*>(p):*reinterpret_cast<const char*const*>(p);
  if(!text && size)return false;out.assign(text?text:"",size);return true;
 }
 unsigned Ordinal(const std::string&name){for(auto&e:ordinals_)if(e.first==name)return e.second++;ordinals_.push_back({name,1});return 0;}
 void Log(const char*s){if(host_.log)host_.log(s);}
 void Report(){
  const auto now=GetTickCount64();if(now<nextLog_)return;nextLog_=now+1000;
  if(stats_.canonical||stats_.hits||stats_.misses||stats_.rejected){char b[320];
   std::snprintf(b,sizeof b,"[shader-cache] canonical=%llu hit=%llu miss=%llu rejected=%llu refs=%u",
      static_cast<unsigned long long>(stats_.canonical),static_cast<unsigned long long>(stats_.hits),static_cast<unsigned long long>(stats_.misses),static_cast<unsigned long long>(stats_.rejected),cache_.LiveRefs());Log(b);}
  stats_={};
 }
 static void __cdecl CloseHook(void* state){
  if(active_){if(active_->cache_.BeforeClose(state))active_->ordinals_.clear();++active_->stats_.closes;}
  closeChain_(state);
 }
 static LuaRef* __stdcall Hook(LuaRef*out,const void*name){return active_->Get(out,name);}
 LuaRef* Get(LuaRef*out,const void* raw){
  if(GetCurrentThreadId()!=thread_ || !host_.insideRender())return original_(out,raw);
  std::string name;
  if(!Name(raw,name)){++stats_.rejected;if(canonical_)return original_(out,raw);*out={CurrentVm(),NilRef};return out;}
  const unsigned ordinal=Ordinal(name);
  void* vm=CurrentVm();cache_.Begin(epoch_,vm);
  if(canonical_){
   ++stats_.canonical;LuaRef*result=original_(out,raw);
   if(!cache_.Store(name,ordinal,*out)){++stats_.rejected;
    if(stats_.rejected<=4){char b[256];std::snprintf(b,sizeof b,"[shader-cache] cannot snapshot %.160s: %s",name.c_str(),RejectName(cache_.LastReject()));Log(b);}}
   return result;
  }
  if(cache_.Acquire(name,ordinal,*out))++stats_.hits;else ++stats_.misses;
  return out;
 }
public:
 explicit Runtime(Host host):host_(host),cache_(lua_){}
 bool Install(){
  if(installed_)return true;
  if(active_ || !host_.address || !host_.expect || !host_.poke || !host_.writeJump || !host_.insideRender || !lua_.Load())return false;
  const std::uint8_t prologue[]={0x55,0x8B,0xEC,0x6A,0xFF},ret[]={0xC2,8,0},call[]={0xE8,0xE3,0x1F,0xF7,0xFF};
  if(!host_.expect(0x865210,prologue,5,"J374 shader callback21 wrapper") || !host_.expect(0x8653D8,ret,3,"J374 shader callback21 RET8") || !host_.expect(0x8F3228,call,5,"J374 shader callback21 caller"))return false;
  std::uint8_t closeCall[]={0xFF,0x15,0,0,0,0};*reinterpret_cast<std::uint32_t*>(closeCall+2)=std::uint32_t(host_.address(0xAD01D0));
  if(!host_.expect(0x86E330,closeCall,sizeof closeCall,"J374 LuaEngine destroy lua_close import"))return false;
  closeChain_=*reinterpret_cast<CloseFn*>(host_.address(0xAD01D0));if(!closeChain_)return false;
  trampoline_=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!trampoline_)return false;
  std::memcpy(originalBytes_,reinterpret_cast<void*>(host_.address(0x865210)),5);std::memcpy(trampoline_,originalBytes_,5);
  host_.writeJump(trampoline_+5,reinterpret_cast<void*>(host_.address(0x865215)));
  original_=reinterpret_cast<Original>(trampoline_);thread_=GetCurrentThreadId();active_=this;
  std::uint8_t patch[5]={0xE9};*reinterpret_cast<std::int32_t*>(patch+1)=std::int32_t(reinterpret_cast<std::uintptr_t>(&Hook)-(host_.address(0x865210)+5));
  const auto closeTarget=reinterpret_cast<std::uintptr_t>(&CloseHook);
  if(!host_.poke(0xAD01D0,reinterpret_cast<const std::uint8_t*>(&closeTarget),4)){active_=nullptr;return false;}
  if(!host_.poke(0x865210,patch,5)){const auto restore=reinterpret_cast<std::uintptr_t>(closeChain_);host_.poke(0xAD01D0,reinterpret_cast<const std::uint8_t*>(&restore),4);active_=nullptr;return false;}
  installed_=true;Log("[shader-cache] J374 callback21 snapshots and verified lua_close lifetime observer installed");return true;
 }
 void BeginRender(std::uint64_t epoch,bool canonical){
  if(GetCurrentThreadId()!=thread_)return;epoch_=epoch;canonical_=canonical;ordinals_.clear();cache_.Begin(epoch,CurrentVm());Report();
 }
 void Shutdown(){
  if(!installed_)return;cache_.Clear(CurrentVm());host_.poke(0x865210,originalBytes_,5);const auto restore=reinterpret_cast<std::uintptr_t>(closeChain_);host_.poke(0xAD01D0,reinterpret_cast<const std::uint8_t*>(&restore),4);installed_=false;active_=nullptr;
  // Trampoline memory remains allocated to avoid releasing an unwinding caller.
 }
};
}
#endif
