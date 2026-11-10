#pragma once
// J374 MC_INPUT_ACTION typed result snapshots. No Lua references are retained.
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
namespace highfps_input_cache {
struct Host {
 std::uintptr_t(*address)(std::uintptr_t)=nullptr;
 bool(*expect)(std::uintptr_t,const std::uint8_t*,std::size_t,const char*)=nullptr;
 bool(*poke)(std::uintptr_t,const std::uint8_t*,std::size_t)=nullptr;
 void(*writeJump)(std::uint8_t*,const void*)=nullptr;
 void(*log)(const char*)=nullptr;
 bool(*insideRender)()=nullptr;
 std::uint64_t(*epoch)()=nullptr;
};
struct Key {
 std::uintptr_t entity=0;std::uint32_t index=0,type=0,variant=0,seed=0;
 int hook=0,action=0;
 bool operator==(const Key&b)const{return entity==b.entity&&index==b.index&&type==b.type&&variant==b.variant&&seed==b.seed&&hook==b.hook&&action==b.action;}
};
struct Context {
 std::uintptr_t dispatcher=0,vm=0;int ref=0;
 std::uint64_t epoch=0,closes=0;bool registered=false;
 bool operator==(const Context&b)const{return dispatcher==b.dispatcher&&vm==b.vm&&ref==b.ref&&epoch==b.epoch&&closes==b.closes&&registered==b.registered;}
 bool Valid()const{return dispatcher&&vm;}
};
struct Result {bool handled=false;std::uint32_t bits=0;};
class Samples {
 struct Entry{Key key;Result value;};std::vector<Entry> entries_;
public:
 static constexpr unsigned Limit=4096;
 void Clear(){entries_.clear();}
 unsigned Size()const{return unsigned(entries_.size());}
 bool Store(const Key&key,Result value){
  for(auto&e:entries_)if(e.key==key){e.value=value;return true;}
  if(entries_.size()>=Limit)return false;
  entries_.push_back({key,value});return true;
 }
 bool Find(const Key&key,Result&value)const{for(const auto&e:entries_)if(e.key==key){value=e.value;return true;}return false;}
};
class Runtime {
 friend struct RuntimeTestAccess;
public:
 // Both native wrappers return AL=handled, not the action's result. False leaves
 // the out parameter untouched and the original caller performs its raw query.
 using BoolFn=bool(__stdcall*)(void*,int,int,bool*);
 using ValueFn=bool(__stdcall*)(void*,std::uintptr_t,int,float*);
 using CloseFn=void(__cdecl*)(void*);
private:
 Host host_;Samples samples_;Context context_{};DWORD thread_=0;
 bool installed_=false,canonical_=true,boolPatched_=false,valuePatched_=false,closePatched_=false;
 std::atomic<std::uint64_t> closeGeneration_{0};
 std::uint64_t tick_=0;ULONGLONG nextReport_=0;unsigned detailedMisses_=0;
 struct Counts{std::uint64_t native[3]{},hits[3]{},nil[3]{},misses[3]{},unregistered=0,skippedStore=0;};Counts counts_;
 std::uint8_t boolBytes_[5]{},valueBytes_[5]{};std::uint8_t*trampolines_=nullptr;
 inline static Runtime*active_=nullptr;
 inline static BoolFn boolOriginal_=nullptr;
 inline static ValueFn valueOriginal_=nullptr;
 inline static CloseFn closeOriginal_=nullptr;
 void Log(const char*s){if(host_.log)host_.log(s);}
 Context CurrentContext()const{
  Context c;c.epoch=host_.epoch();c.closes=closeGeneration_.load();
  const auto engine=*reinterpret_cast<std::uintptr_t*>(host_.address(0xC0C08C));
  if(!engine)return c;
  const auto mask=*reinterpret_cast<std::uintptr_t*>(engine);
  c.registered=mask&&(*reinterpret_cast<const std::uint32_t*>(mask)&0x2000)!=0;
  c.dispatcher=*reinterpret_cast<const std::uintptr_t*>(engine+0x10);
  if(c.dispatcher){c.vm=*reinterpret_cast<const std::uintptr_t*>(c.dispatcher);c.ref=*reinterpret_cast<const int*>(c.dispatcher+4);}
  return c;
 }
 Context Sync(){const auto c=CurrentContext();if(!(c==context_)){samples_.Clear();context_=c;}return c;}
 static Key EntityKey(void*entity,int hook,int action){
  Key k;k.entity=reinterpret_cast<std::uintptr_t>(entity);k.hook=hook;k.action=action;
  if(entity){auto*p=static_cast<const std::uint8_t*>(entity);
   k.index=*reinterpret_cast<const std::uint32_t*>(p+0x20);k.type=*reinterpret_cast<const std::uint32_t*>(p+0x28);
   k.variant=*reinterpret_cast<const std::uint32_t*>(p+0x2C);k.seed=*reinterpret_cast<const std::uint32_t*>(p+0x3EC);
  }return k;
 }
 bool Pure()const{return !canonical_&&host_.insideRender();}
 void Miss(const Key&k,const char*reason){
  const unsigned h=k.hook>=0&&k.hook<=2?unsigned(k.hook):0;++counts_.misses[h];
  if(detailedMisses_++<24&&host_.log){char line[320];
   std::snprintf(line,sizeof line,"[input-cache-miss] tick=%llu epoch=%llu hook=%d action=%d entity=0x%08X index=%u seed=%u reason=%s fallback=native-raw",
    static_cast<unsigned long long>(tick_),static_cast<unsigned long long>(context_.epoch),k.hook,k.action,unsigned(k.entity),k.index,k.seed,reason);Log(line);
  }
 }
 bool ReadPure(const Key&key,Result&value){
  if(!context_.registered){++counts_.unregistered;return false;}
  if(!context_.Valid()){Miss(key,"dispatcher-or-VM-unavailable");return false;}
  if(!samples_.Find(key,value)){Miss(key,"not-sampled-this-tick");return false;}
  ++counts_.hits[key.hook];if(!value.handled)++counts_.nil[key.hook];return value.handled;
 }
 void Record(const Context&before,const Key&key,Result value){
  // Native input callbacks may re-enter input queries or replace the run/VM.
  // Store AFTER the outer original returns; never reuse a result across such a
  // context change. Every authoritative call still executes, including nested
  // TimeMachine updates; same-key results retain the latest completed sample.
  const auto after=Sync();
  if(before==after&&after.Valid()){if(!samples_.Store(key,value))++counts_.skippedStore;}
  else ++counts_.skippedStore;
 }
 static bool __stdcall BoolHook(void*entity,int hook,int action,bool*out){
  auto*r=active_;if(!r||!r->installed_||GetCurrentThreadId()!=r->thread_)return boolOriginal_(entity,hook,action,out);
  const auto context=r->Sync();const auto key=EntityKey(entity,hook,action);
  if(!r->Pure()){
   const bool handled=boolOriginal_(entity,hook,action,out);
   if(hook>=0&&hook<2){++r->counts_.native[hook];Result v;v.handled=handled;if(handled&&out)v.bits=*out?1:0;
    if(!handled||out)r->Record(context,key,v);else ++r->counts_.skippedStore;}
   return handled;
  }
  if(hook<0||hook>1){r->Miss(key,"unsupported-bool-hook");return false;}
  if(!out){r->Miss(key,"null-output");return false;}
  Result value;if(!r->ReadPure(key,value))return false;*out=value.bits!=0;return true;
 }
 static bool __stdcall ValueHook(void*entity,std::uintptr_t unused,int action,float*out){
  auto*r=active_;if(!r||!r->installed_||GetCurrentThreadId()!=r->thread_)return valueOriginal_(entity,unused,action,out);
  const auto context=r->Sync();const auto key=EntityKey(entity,2,action);
  if(!r->Pure()){
   const bool handled=valueOriginal_(entity,unused,action,out);++r->counts_.native[2];Result v;v.handled=handled;
   if(handled&&out)std::memcpy(&v.bits,out,4);
   if(!handled||out)r->Record(context,key,v);else ++r->counts_.skippedStore;return handled;
  }
  if(!out){r->Miss(key,"null-output");return false;}
  Result value;if(!r->ReadPure(key,value))return false;std::memcpy(out,&value.bits,4);return true;
 }
 static void __cdecl CloseHook(void*vm){
  // No cache-vector or Lua access on a closing thread. The next game-thread
  // observation clears snapshots even if the new VM reuses the same addresses.
  if(active_)++active_->closeGeneration_;closeOriginal_(vm);
 }
 void Report(){
  const auto now=GetTickCount64();if(now<nextReport_)return;nextReport_=now+1000;
  if(host_.log)for(unsigned h=0;h<3;++h)if(counts_.native[h]||counts_.hits[h]||counts_.misses[h]){char line[320];
   std::snprintf(line,sizeof line,"[input-cache] hook=%u native=%llu hit=%llu nilRaw=%llu missRaw=%llu samples=%u closes=%llu skippedStore=%llu",
    h,static_cast<unsigned long long>(counts_.native[h]),static_cast<unsigned long long>(counts_.hits[h]),static_cast<unsigned long long>(counts_.nil[h]),
    static_cast<unsigned long long>(counts_.misses[h]),samples_.Size(),static_cast<unsigned long long>(closeGeneration_.load()),static_cast<unsigned long long>(counts_.skippedStore));Log(line);
  }counts_={};
 }
public:
 explicit Runtime(Host host):host_(host){}
 // Called before EACH fixed input/update pass, before native input sampling.
 // Do not call this for each pure presentation or individual nested Game::Update.
 void BeginTick(){if(!installed_||GetCurrentThreadId()!=thread_)return;++tick_;samples_.Clear();Sync();Report();}
 void BeginRender(bool canonical){if(!installed_||GetCurrentThreadId()!=thread_)return;canonical_=canonical;Sync();Report();}
 bool Install(){
  if(installed_)return true;
  if(active_||!host_.address||!host_.expect||!host_.poke||!host_.writeJump||!host_.insideRender||!host_.epoch)return false;
  const std::uint8_t prologue[]={0x55,0x8B,0xEC,0x6A,0xFF},ret[]={0xC2,0x10,0},index[]={0x8B,0x41,0x20,0xC3};
  if(!host_.expect(0x864970,prologue,5,"J374 MC13 bool wrapper")||!host_.expect(0x864AD0,prologue,5,"J374 MC13 value wrapper")||
     !host_.expect(0x864ABA,ret,3,"J374 MC13 bool RET16")||!host_.expect(0x864C21,ret,3,"J374 MC13 value RET16")||
     !host_.expect(0x451FB0,index,4,"J374 entity Index lifetime field"))return false;
  const std::uint8_t boolCall[]={0xE8,0x4E,0xF8,0,0},valueCall[]={0xE8,0xEC,0xF6,0,0};
  if(!host_.expect(0x8649DD,boolCall,5,"J374 MC13 bool dispatcher")||!host_.expect(0x864B3F,valueCall,5,"J374 MC13 value dispatcher"))return false;
  std::uint8_t closeCall[]={0xFF,0x15,0,0,0,0};*reinterpret_cast<std::uint32_t*>(closeCall+2)=unsigned(host_.address(0xAD01D0));
  if(!host_.expect(0x86E330,closeCall,6,"J374 lua_close import call"))return false;
  closeOriginal_=*reinterpret_cast<CloseFn*>(host_.address(0xAD01D0));if(!closeOriginal_)return false;
  trampolines_=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,64,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));if(!trampolines_)return false;
  std::memcpy(boolBytes_,reinterpret_cast<void*>(host_.address(0x864970)),5);std::memcpy(valueBytes_,reinterpret_cast<void*>(host_.address(0x864AD0)),5);
  std::memcpy(trampolines_,boolBytes_,5);host_.writeJump(trampolines_+5,reinterpret_cast<void*>(host_.address(0x864975)));
  std::memcpy(trampolines_+32,valueBytes_,5);host_.writeJump(trampolines_+37,reinterpret_cast<void*>(host_.address(0x864AD5)));
  FlushInstructionCache(GetCurrentProcess(),trampolines_,64);
  boolOriginal_=reinterpret_cast<BoolFn>(trampolines_);valueOriginal_=reinterpret_cast<ValueFn>(trampolines_+32);
  thread_=GetCurrentThreadId();active_=this;
  const auto close=reinterpret_cast<std::uintptr_t>(&CloseHook);
  closePatched_=host_.poke(0xAD01D0,reinterpret_cast<const std::uint8_t*>(&close),4);
  if(closePatched_){std::uint8_t patch[5]={0xE9};*reinterpret_cast<std::int32_t*>(patch+1)=std::int32_t(reinterpret_cast<std::uintptr_t>(&BoolHook)-(host_.address(0x864970)+5));boolPatched_=host_.poke(0x864970,patch,5);}
  if(boolPatched_){std::uint8_t patch[5]={0xE9};*reinterpret_cast<std::int32_t*>(patch+1)=std::int32_t(reinterpret_cast<std::uintptr_t>(&ValueHook)-(host_.address(0x864AD0)+5));valuePatched_=host_.poke(0x864AD0,patch,5);}
  if(!valuePatched_){Shutdown();return false;}
  installed_=true;Log("[input-cache] J374 MC13 scalar snapshots and chained VM-close invalidation installed");return true;
 }
 void Shutdown(){
  installed_=false;canonical_=true;samples_.Clear();
  if(valuePatched_&&host_.poke(0x864AD0,valueBytes_,5))valuePatched_=false;
  if(boolPatched_&&host_.poke(0x864970,boolBytes_,5))boolPatched_=false;
  const auto close=reinterpret_cast<std::uintptr_t>(closeOriginal_);
  if(closePatched_&&host_.poke(0xAD01D0,reinterpret_cast<const std::uint8_t*>(&close),4))closePatched_=false;
  if(!valuePatched_&&!boolPatched_&&!closePatched_){if(active_==this)active_=nullptr;}
  else Log("[FATAL] input-cache rollback incomplete; remaining hooks forward natively");
  // Trampolines/runtime remain alive while a patched caller may be unwinding.
 }
};
}
#endif
