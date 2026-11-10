#include "entity_draw_cache_j374_runtime.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace highfps_entity_probe;
namespace {
void*slots[11]{};std::uint32_t programs[7][12]{};
std::uintptr_t attributeReturn=0,uniformReturn=0;
unsigned addressCalls=0,attributeCalls=0,uniformCalls=0,linkCalls=0,binaryCalls=0,contextCalls=0,negativeCalls=0;
int generationValue=7;HGLRC currentContext=reinterpret_cast<HGLRC>(0x90000);
void (*duringQuery)()=nullptr;
bool inside=true,blockLink=false,deletedTestAlive=true;HANDLE entered=nullptr,releaseLink=nullptr;
int WINAPI Attribute(unsigned p,const char*n){assert(p);++attributeCalls;if(duringQuery){auto callback=duringQuery;duringQuery=nullptr;callback();}if(!std::strcmp(n,"missing")||(p==456&&!deletedTestAlive)){++negativeCalls;return -1;}return generationValue;}
int WINAPI Uniform(unsigned p,const char*n){assert(p&&n);++uniformCalls;return generationValue+100;}
void WINAPI Link(unsigned p){assert(p);++linkCalls;++generationValue;if(blockLink){SetEvent(entered);assert(WaitForSingleObject(releaseLink,2000)==WAIT_OBJECT_0);}}
void WINAPI Binary(unsigned p,unsigned format,const void*b,int n){assert(p==123&&format==9&&b&&n==4);++binaryCalls;++generationValue;}
BOOL WINAPI ContextPair(HDC,HGLRC c){currentContext=c;++contextCalls;return FALSE;}
BOOL WINAPI ContextDelete(HGLRC){++contextCalls;++generationValue;return TRUE;}
BOOL WINAPI ContextShare(HGLRC,HGLRC){++contextCalls;++generationValue;return TRUE;}
HGLRC WINAPI Context(){return currentContext;}
int WINAPI LazyAttribute(unsigned p,const char*n){slots[0]=reinterpret_cast<void*>(&Attribute);return Attribute(p,n);}
void WINAPI LazyLink(unsigned p){slots[2]=reinterpret_cast<void*>(&Link);Link(p);}
void WINAPI LazyArbDelete(unsigned p){slots[10]=reinterpret_cast<void*>(&Link);Link(p);}
std::uintptr_t Address(std::uintptr_t a){
 ++addressCalls;
 const std::uintptr_t builtin[]={0xBD56F8,0xBD5724,0xBD5750,0xBD296C,0xBD2A74,0xBD2890,0xBD2788};
 for(unsigned i=0;i<7;++i)if(a==builtin[i])return reinterpret_cast<std::uintptr_t>(programs[i]);
 if(a==0xA15D05)return attributeReturn;if(a==0xA0CCF2)return uniformReturn;
 if(a==0xA2A950)return reinterpret_cast<std::uintptr_t>(&LazyAttribute);
 if(a==0xA30370)return reinterpret_cast<std::uintptr_t>(&LazyLink);
 if(a==0xA26EB0)return reinterpret_cast<std::uintptr_t>(&LazyArbDelete);
 return a;
}
using Query=int(WINAPI*)(unsigned,const char*);
Query Thunk(void*target,std::uintptr_t&returnAddress){
 auto*p=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,64,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(p);
 const std::uint8_t bytes[]={0xff,0x74,0x24,0x08,0xff,0x74,0x24,0x08,0xb8,0,0,0,0,0xff,0xd0,0xc2,0x08,0};
 std::memcpy(p,bytes,sizeof bytes);*reinterpret_cast<std::uintptr_t*>(p+9)=reinterpret_cast<std::uintptr_t>(target);
 returnAddress=reinterpret_cast<std::uintptr_t>(p+15);FlushInstructionCache(GetCurrentProcess(),p,sizeof bytes);return reinterpret_cast<Query>(p);
}
}
namespace highfps_entity_probe {
struct RuntimeTestAccess {
 static DWORD WINAPI ForeignLink(void*){Runtime::LinkProgram(123);return 0;}
 static DWORD WINAPI ForeignQuery(void*){const auto before=attributeCalls;Runtime::AttributeLocation(123,"Color");assert(attributeCalls==before+1);return 0;}
 static void Test(){
  Host h;h.address=&Address;h.insideRender=[](){return inside;};Runtime r(h);Runtime::active_=&r;J374Hooks::gameThreadId=GetCurrentThreadId();
  r.locationContext_=&Context;programs[0][10]=123;programs[0][1]=1;
  const void*targets[]={reinterpret_cast<void*>(&Attribute),reinterpret_cast<void*>(&Uniform),reinterpret_cast<void*>(&Link),reinterpret_cast<void*>(&Link),reinterpret_cast<void*>(&Binary),reinterpret_cast<void*>(&Link),reinterpret_cast<void*>(&Binary),reinterpret_cast<void*>(&ContextPair),reinterpret_cast<void*>(&ContextDelete),reinterpret_cast<void*>(&ContextShare),reinterpret_cast<void*>(&Link)};
  for(unsigned i=0;i<11;++i){slots[i]=Runtime::LocationHook(i);r.locationSlots_[i]=&slots[i];Runtime::locationTargets_[i].store(const_cast<void*>(targets[i]));}
  r.locationHealthy_.store(true);
  Query attribute=Thunk(Runtime::LocationHook(0),attributeReturn),uniform=Thunk(Runtime::LocationHook(1),uniformReturn);
  r.PrepareLocationAddresses();const auto addressesResolved=addressCalls;
  // Actual stdcall8 callsites exercise _ReturnAddress allowlist and preserve results.
  assert(attribute(123,"Color")==7&&attributeCalls==1);assert(attribute(123,"Color")==7&&attributeCalls==1);
  assert(uniform(123,"Color")==107&&uniformCalls==1);assert(uniform(123,"Color")==107&&uniformCalls==1);
  programs[0][1]=0;auto inactiveBefore=attributeCalls;attribute(123,"Color");attribute(123,"Color");assert(attributeCalls==inactiveBefore+2);programs[0][1]=1;
  // MRU is not a cached eligibility decision: changing its live GLuint rejects
  // the old name; another initialized builtin can replace the MRU immediately.
  programs[0][10]=321;auto changedBefore=attributeCalls;attribute(123,"Color");assert(attributeCalls==changedBefore+1);
  programs[1][1]=1;programs[1][10]=322;attribute(322,"Color");auto secondBefore=attributeCalls;
  attribute(322,"Color");assert(attributeCalls==secondBefore);
  programs[1][10]=323;attribute(322,"Color");assert(attributeCalls==secondBefore+1);
  programs[0][10]=123;programs[1][1]=0;
  // Exact string equality, distinct names/contexts/programs and invalid names.
  assert(attribute(123,"Position")==7&&attributeCalls==7);
  assert(attribute(124,"Color")==7&&attribute(124,"Color")==7&&attributeCalls==9); // not builtin
  assert(attribute(123,"missing")==-1&&attribute(123,"missing")==-1&&negativeCalls==2);
  char longName[140];std::memset(longName,'x',139);longName[139]=0;
  auto n=attributeCalls;attribute(123,longName);attribute(123,longName);assert(attributeCalls==n+2);
  n=attributeCalls;Runtime::AttributeLocation(123,"Color");Runtime::AttributeLocation(123,"Color");assert(attributeCalls==n+2); // unknown callers
  inside=false;n=attributeCalls;attribute(123,"Color");attribute(123,"Color");assert(attributeCalls==n+2);
  r.BeginUpdateQueries();n=attributeCalls;attribute(123,"Color");attribute(123,"Color");assert(attributeCalls==n+1);
  r.EndUpdateQueries();n=attributeCalls;attribute(123,"Color");attribute(123,"Color");assert(attributeCalls==n+2);inside=true;
  currentContext=reinterpret_cast<HGLRC>(0x90001);n=attributeCalls;attribute(123,"Color");attribute(123,"Color");assert(attributeCalls==n+1);
  r.locations_.BeginFrame();n=attributeCalls;attribute(123,"Color");assert(attributeCalls==n+1);
  Runtime::LinkProgram(123);n=attributeCalls;assert(attribute(123,"Color")==8);attribute(123,"Color");assert(attributeCalls==n+1);
  Runtime::LinkProgram(123);n=attributeCalls;assert(attribute(123,"Color")==9);assert(attributeCalls==n+1);
  unsigned blob=0;Runtime::ProgramBinary(123,9,&blob,4);assert(attribute(123,"Color")==10&&binaryCalls==1);
  Runtime::LinkProgramARB(123);assert(attribute(123,"Color")==11);
  Runtime::ProgramBinaryOES(123,9,&blob,4);assert(attribute(123,"Color")==12&&binaryCalls==2);
  // Same HGLRC numeric reuse is invalidated through observed lifetime operations.
  assert(Runtime::DeleteContext(currentContext)==TRUE);assert(attribute(123,"Color")==13);
  n=attributeCalls;assert(Runtime::MakeCurrent(nullptr,currentContext)==FALSE);attribute(123,"Color");assert(attributeCalls==n+1);
  assert(Runtime::ShareLists(currentContext,currentContext)==TRUE);assert(attribute(123,"Color")==14);
  // Foreign mutation cannot make a hit/store valid while the operation is active.
  entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);releaseLink=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(entered&&releaseLink);
  blockLink=true;HANDLE thread=CreateThread(nullptr,0,&ForeignLink,nullptr,0,nullptr);assert(thread);
  assert(WaitForSingleObject(entered,2000)==WAIT_OBJECT_0);n=attributeCalls;
  attribute(123,"Color");attribute(123,"Color");assert(attributeCalls==n+2);
  SetEvent(releaseLink);assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);CloseHandle(thread);blockLink=false;
  n=attributeCalls;attribute(123,"Color");attribute(123,"Color");assert(attributeCalls==n+1);
  thread=CreateThread(nullptr,0,&ForeignQuery,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);CloseHandle(thread);
  // A mutation completed inside the real query invalidates its admission snapshot.
  duringQuery=[](){Runtime::LinkProgram(123);};n=attributeCalls;
  attribute(123,"MutationDuringQuery");attribute(123,"MutationDuringQuery");attribute(123,"MutationDuringQuery");assert(attributeCalls==n+2);
  // First lazy resolver executes once, publishes native target, then re-arms.
  r.locations_.Clear();Runtime::locationTargets_[0].store(reinterpret_cast<void*>(&LazyAttribute));n=attributeCalls;
  attribute(123,"Color");assert(attributeCalls==n+1&&slots[0]==Runtime::LocationHook(0));
  attribute(123,"Color");attribute(123,"Color");assert(attributeCalls==n+2);
  Runtime::locationTargets_[2].store(reinterpret_cast<void*>(&LazyLink));n=linkCalls;Runtime::LinkProgram(123);assert(linkCalls==n+1&&slots[2]==Runtime::LocationHook(2));
  n=attributeCalls;attribute(123,"Color");assert(attributeCalls==n+1);
  // Bound deletion is deferred: positive query after DeleteProgram may not be
  // re-stored, otherwise a later UseProgram(0) would leave a stale positive hit.
  programs[1][1]=1;programs[1][10]=456;
  n=attributeCalls;attribute(456,"Color");attribute(456,"Color");assert(attributeCalls==n+1);
  Runtime::DeleteProgram(456);n=attributeCalls;
  attribute(456,"Color");attribute(456,"Color");assert(attributeCalls==n+2);
  deletedTestAlive=false; // native unbind completes deletion, no second Delete call
  n=attributeCalls;assert(attribute(456,"Color")==-1&&attribute(456,"Color")==-1&&attributeCalls==n+2);
  r.locations_.BeginFrame();deletedTestAlive=true; // actual new object reuses numeric name
  Runtime::LinkProgram(456);n=attributeCalls;attribute(456,"Color");attribute(456,"Color");assert(attributeCalls==n+2);
  // ARB deletion can delete a program too; its compact lazy resolver is rearmed.
  programs[2][1]=1;programs[2][10]=789;
  n=attributeCalls;attribute(789,"Color");attribute(789,"Color");assert(attributeCalls==n+1);
  Runtime::locationTargets_[10].store(reinterpret_cast<void*>(&LazyArbDelete));n=linkCalls;
  Runtime::DeleteObjectARB(789);assert(linkCalls==n+1&&slots[10]==Runtime::LocationHook(10));
  assert(Runtime::LocationWasDeleted(789));n=attributeCalls;
  attribute(789,"Color");attribute(789,"Color");assert(attributeCalls==n+2);
  Runtime::DeleteObjectARB(789);assert(Runtime::locationTargets_[10].load()==reinterpret_cast<void*>(&Link));
  // Unexpected new chains are never adopted/re-wrapped. Caching fails closed.
  slots[2]=reinterpret_cast<void*>(&LazyLink);n=attributeCalls;attribute(123,"Color");attribute(123,"Color");assert(attributeCalls==n+2&&!r.locationHealthy_.load());
  // Optional attribution counts are disabled by default and never affect query
  // forwarding. Exercise actual stdcall thunks through all phase selections.
  for(const auto&phase:r.locationProfile_)for(auto count:phase)assert(count==0);
  assert(r.locationProfileForeign_.load()==0);r.host_.profileLocations=true;
  J374Hooks::canonical=true;inside=true;attribute(123,"Color");assert(r.locationProfile_[1][0]==1);
  J374Hooks::canonical=false;attribute(123,"Color");assert(r.locationProfile_[2][0]==1);
  r.BeginUpdateQueries();attribute(123,"Color");assert(r.locationProfile_[0][0]==1);r.EndUpdateQueries();
  inside=false;attribute(123,"Color");assert(r.locationProfile_[3][0]==1);
  Runtime::UniformLocation(123,"Color");assert(r.locationProfile_[3][6]==1);
  for(unsigned i=1;i<5;++i){r.LocationQuery(i<2?0:1,123,"Color",r.locationKnownCallers_[i]);assert(r.locationProfile_[3][i]==1);}
  thread=CreateThread(nullptr,0,&ForeignQuery,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);CloseHandle(thread);
  assert(r.locationProfileForeign_.load()==1);
  r.host_.profileLocations=false;attribute(123,"Color");assert(r.locationProfile_[3][0]==1);
  assert(Runtime::locationMutationsActive_.load()==0&&contextCalls==3);
  assert(addressCalls==addressesResolved); // No resolver callback on any query/mutation hot path.
  for(unsigned id=1000;id<1130;++id)Runtime::LocationRememberDeleted(id);
  assert(Runtime::locationDeletedFull_.load()&&!r.locationHealthy_.load());
  Runtime::active_=nullptr;CloseHandle(entered);CloseHandle(releaseLink);
  std::puts("shader location runtime: stdcall8/native caller, positive/negative, link/delete/binary/context reuse, foreign inflight, lazy and changed-slot fallback passed");
 }
};
}
int main(){RuntimeTestAccess::Test();}
