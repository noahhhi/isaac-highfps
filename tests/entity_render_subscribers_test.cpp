#include "entity_render_subscribers_j374.h"
#include <cassert>
#include <cstdio>
#include <map>
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include "entity_draw_cache_j374_hooks.h"
#endif
using namespace highfps_entity_probe::subscribers19715;
static std::map<std::uintptr_t,std::uint32_t> memory;
static bool Read(std::uintptr_t p,std::uint32_t& v){auto it=memory.find(p);if(it==memory.end())return false;v=it->second;return true;}
static State Current(unsigned cb){return QueryWith(cb,0x10000,Read);}
static std::uintptr_t Relocate(std::uintptr_t p){return p+0x100000;}
static unsigned signaturesSeen=0;static bool failValidation=false;
static bool ExpectSignature(std::uintptr_t at,const std::uint8_t*bytes,std::size_t size,const char*){
 ++signaturesSeen;if(failValidation)return false;
 const Signature*sig=nullptr;for(const auto&s:signatures)if(s.at==at)sig=&s;
 assert(sig&&size==sig->size);
 std::uint8_t expected[128];std::memcpy(expected,sig->bytes,size);
 for(std::size_t i=0;i<sig->count;++i){std::uint32_t v;std::memcpy(&v,expected+sig->reloc[i],4);v+=0x100000;std::memcpy(expected+sig->reloc[i],&v,4);}
 assert(std::memcmp(expected,bytes,size)==0);return true;
}
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
using namespace highfps_entity_probe;
static unsigned originals=0,keyCalls=0,absentCanonical=0,absentPure=0;
static bool inside=true;
static void __stdcall Original(void*,const float*){++originals;}
static bool KeyMock(void*,unsigned,Key&){++keyCalls;return false;}
static void TestHooks(){
 J374Hooks::gameThreadId=GetCurrentThreadId();J374Hooks::InsideRender=[](){return inside;};
 J374Hooks::Subscribers=Current;J374Hooks::MakeKey=KeyMock;
 // Never dereferenced: KeyMock deliberately fails, proving key entry/avoidance.
 J374Hooks::cache=reinterpret_cast<Cache*>(0x10000);
 J374Hooks::OnNoSubscribers=[](unsigned cb,bool canonical){assert(cb==56);if(canonical)++absentCanonical;else ++absentPure;};
 memory[0x30004]=0;
 J374Hooks::canonical=true;J374Hooks::Entity(56,Original,nullptr,nullptr);
 assert(originals==1&&keyCalls==0&&absentCanonical==1);
 J374Hooks::canonical=false;J374Hooks::Entity(56,Original,nullptr,nullptr);
 assert(originals==1&&keyCalls==0&&absentPure==1);
 memory[0x30004]=1u<<24; // AddCallback: captured path immediately becomes eligible.
 J374Hooks::canonical=true;J374Hooks::Entity(56,Original,nullptr,nullptr);
 assert(originals==2&&keyCalls==1);
 J374Hooks::canonical=false;J374Hooks::Entity(56,Original,nullptr,nullptr);
 assert(originals==2&&keyCalls==2); // Pure never falls back to Lua even on key failure.
 memory[0x30004]=0; // RemoveCallback must suppress old cached output immediately.
 J374Hooks::Entity(56,Original,nullptr,nullptr);assert(originals==2&&keyCalls==2&&absentPure==2);
 memory.erase(0x30004); // Unreadable/unknown must not assert "no subscribers".
 J374Hooks::Entity(56,Original,nullptr,nullptr);assert(originals==2&&keyCalls==3);
 inside=false;J374Hooks::Entity(56,Original,nullptr,nullptr);
 assert(originals==3&&keyCalls==3); // Update-owned RTT callback still forwards.
 J374Hooks::cache=nullptr;J374Hooks::Subscribers=nullptr;
}
#endif
int main(){
 memory={{0x10000,0x20000},{0x20000,0x30000},{0x30000,0},{0x30004,0}};
 for(unsigned cb:{25u,28u,32u,36u,41u,45u,49u,52u,56u,59u}){
  auto&word=memory[0x30000+4*(cb/32)];assert(Current(cb)==highfps_entity_probe::subscribers19715::State::Absent);
  word=1u<<(cb%32);assert(Current(cb)==highfps_entity_probe::subscribers19715::State::Present);word=0;assert(Current(cb)==highfps_entity_probe::subscribers19715::State::Absent);
 }
 assert(Current(2)==highfps_entity_probe::subscribers19715::State::Unknown&&Current(1000)==highfps_entity_probe::subscribers19715::State::Unknown);
 memory[0x20000]=0x40000;assert(Current(56)==highfps_entity_probe::subscribers19715::State::Unknown);
 memory[0x40004]=1u<<24;assert(Current(56)==highfps_entity_probe::subscribers19715::State::Present); // VM/bitset replacement, no stale pointer.
 memory[0x20000]=0x30000;
 assert(Enable(Relocate,ExpectSignature)&&signaturesSeen==20&&luaSlot==0xd0c08c);
 failValidation=true;assert(!Enable(Relocate,ExpectSignature)&&luaSlot==0);failValidation=false;
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
 TestHooks();
#endif
 std::puts("entity render subscriber live bit gates, fallback, relocation, and hook policy: passed");
}
