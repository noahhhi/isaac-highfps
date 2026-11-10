#include "native_spawn_budget_j374.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <initializer_list>
#include <utility>
using namespace highfps::spawn_budget19715;
static std::uint8_t*image;static bool permitWork=true;static unsigned calls,costSeen;static bool nested,throwing;static void*entity;
static std::uintptr_t Address(std::uintptr_t va){return reinterpret_cast<std::uintptr_t>(image)+(va-0x400000);}
static std::uintptr_t failPoke=0;
static bool Poke(std::uintptr_t va,const std::uint8_t*p,std::size_t n){if(va==failPoke)return false;std::memcpy(reinterpret_cast<void*>(Address(va)),p,n);return true;}
namespace highfps::spawn_budget19715 {
struct TestAccess {
 static unsigned Decide(void*p,unsigned k){return Adapter::Decide(p,k);}
 static void Set(Adapter&a){Adapter::active_=&a;a.enabled_=true;}
 static void*Stub(Adapter&a,unsigned k){std::memcpy(a.saved_[k],reinterpret_cast<void*>(Address(Adapter::sites_[k])),Adapter::lengths_[k]);return a.MakeStub(k);}
 static void Clear(){Adapter::active_=nullptr;}
};}
static bool Admit(Kind,void*p,unsigned cost){++calls;costSeen=cost;if(nested){nested=false;assert(TestAccess::Decide(p,0)==1);}if(throwing)RaiseException(0xE1234567,0,0,nullptr);__asm { pxor xmm2,xmm2 }
__asm { fninit }
return permitWork;}
static float testFloat=3.25f,afterX87=0;
static unsigned landed,afterFlags,afterEcx,afterEax;static float afterXmm;
static __declspec(naked) void Invoke(void*stub,void*self){__asm {
 push ebp
 mov ebp,esp
 push ebx
 push esi
 push edi
 sub esp,0x900
 mov dword ptr [ebp-0x8a8],0x2468ace0
 mov edi,[ebp+12]
 mov ecx,0x13579bdf
 mov eax,1
 cmp eax,eax
 movss xmm2, dword ptr [testFloat]
 fld dword ptr [testFloat]
 call dword ptr [ebp+8]
 mov afterEax,eax
 pushfd
 pop eax
 mov afterFlags,eax
 mov afterEcx,ecx
 movss dword ptr [afterXmm],xmm2
 fstp dword ptr [afterX87]
 add esp,0x900
 pop edi
 pop esi
 pop ebx
 mov esp,ebp
 pop ebp
 ret
}}
static void Load(std::uintptr_t at,unsigned xmm,std::uintptr_t constant){
 std::uint8_t b[]={0xf3,0x0f,0x10,std::uint8_t(5+8*xmm),0,0,0,0};auto v=std::uint32_t(Address(constant));std::memcpy(b+4,&v,4);Poke(at,b,8);
}
static void Landing(std::uintptr_t at,unsigned value){std::uint8_t b[]={0xc7,0x05,0,0,0,0,0,0,0,0,0xc3};auto p=reinterpret_cast<std::uint32_t>(&landed);std::memcpy(b+2,&p,4);std::memcpy(b+6,&value,4);Poke(at,b,sizeof b);}
template<std::size_t N>static void Put(std::uintptr_t va,const std::uint8_t(&b)[N]){Poke(va,b,N);}
static void Guards(){
 Put(0x667BED,{0x8b,0x85,0x58,0xf7,0xff,0xff});Put(0x667BDF,{0x8b,0x87,0x90,4,0,0,0x85,0xc0,0x0f,0x85,0x25,2,0,0});
 Put(0x667EE4,{0x8d,0xb7,0x28,4,0,0,0xf3,0x0f,0x10,0x87,0x14,4,0,0});Put(0x667C55,{0x56,0x6a,3,0xe8,0x83,0xec,0x2f,0});

 Put(0x668F97,{0x8d,0x85,0x80,0xf6,0xff,0xff});Put(0x668F8F,{0x84,0xc0,0x0f,0x84,0x0c,4,0,0});
 Put(0x6690E0,{0xff,0x77,0x28,0xe8,0x68,0xb5,0xdb,0xff});
 Put(0x669394,{0x83,0xf8,4,0x0f,0x8c,0x73,0xfc,0xff,0xff,0x8d,0xb7,0x28,4,0,0});
 for(auto pair:{std::pair<unsigned,unsigned>{0x668F73,0xBD2310},{0x6693A3,0xBD23F0}}){std::uint8_t b[]={0x68,0,0,0,0,0x8b,0xd6};auto v=std::uint32_t(Address(pair.second));std::memcpy(b+1,&v,4);Poke(pair.first,b,7);}

 Put(0x676574,{0x8d,0xb7,0x28,4,0,0});
 Put(0x667628,{0x83,0xbf,0x8c,4,0,0,0,0x8d,0xb7,0x8c,4,0,0,0xc6,0x85,0x6f,0xf7,0xff,0xff,0,0x89,0xb5,0x78,0xf7,0xff,0xff});
 Load(0x6765CD,2,0xB692B0);Load(0x676947,0,0xB67578);Load(0x667648,0,0xB6745C);
 Put(0x67659B,{0x0f,0x84,0xa6,3,0,0});Put(0x67668E,{0x6a,0,0x6a,2,0xe8,0xb9,0xdf,0xda,0xff});
 Put(0x676922,{0x8b,6,0x8b,0xce,0xff,0x50,0x0c});Put(0x667642,{0x0f,0x85,0x35,3,0,0});
 Put(0x66797D,{0x83,0xbf,0x88,4,0,0,0,0x8d,0x87,0x88,4,0,0});Put(0x667853,{0x6a,0x0a,0xe8,0x86,0xf0,0x2f,0});
}
int main(){
 WorldQuota shared;shared.Begin(1,6,1);assert(shared.Admit(Kind::QuadSplit,0,4));assert(shared.Admit(Kind::ParasitePair,0,2));assert(!shared.Admit(Kind::QuadSplit,0,4));
 WorldQuota lasers;lasers.Begin(1,2,1,1,2);assert(lasers.Admit(Kind::TrisagionCreate,0,1));assert(lasers.Admit(Kind::TrisagionCreate,0,1));assert(!lasers.Admit(Kind::TrisagionCreate,0,1));assert(lasers.Admit(Kind::TechZeroSearch,0,1));assert(lasers.Admit(Kind::ParasitePair,0,2));
 WorldQuota q;q.Begin(1,4,2);assert(q.Admit(Kind::ParasitePair,7,2));assert(q.Admit(Kind::ParasitePair,7,2));assert(!q.Admit(Kind::ParasitePair,7,2));q.Begin(1,99,99);assert(!q.Admit(Kind::ParasitePair,7,2));q.Begin(2,2,1);assert(q.Admit(Kind::ParasitePair,7,2));q.Reset();assert(q.Admit(Kind::TechZeroSearch,7,1));
 unsigned rotating=0;for(unsigned t=0;t<16;++t){q.Begin(100+t,2,1,4);rotating+=q.Admit(Kind::TechZeroSearch,42,1);}assert(rotating==4);
 image=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x900000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
 alignas(16) std::uint8_t tear[0x900]{},player[0x500]{};entity=tear;
 *reinterpret_cast<std::uintptr_t*>(tear)=Address(0xB3C128);*reinterpret_cast<unsigned*>(tear+0x28)=2;
 *reinterpret_cast<std::uint64_t*>(tear+0x428)=(1ull<<6)|(1ull<<57)|(1ull<<18)|(1ull<<60);
 *reinterpret_cast<void**>(tear+0x3c8)=player;*reinterpret_cast<unsigned*>(player+0x28)=1;
 Host h{Address,Poke,Admit,GetCurrentThreadId(),true};Adapter a(h);TestAccess::Set(a);
 assert(TestAccess::Decide(tear,0)==1&&costSeen==2);permitWork=false;assert(TestAccess::Decide(tear,1)==0&&costSeen==1);
 assert(TestAccess::Decide(tear,2)==0&&costSeen==4);
 assert(TestAccess::Decide(tear,3)==0&&costSeen==1);
 nested=true;assert(TestAccess::Decide(tear,0)==0);assert(a.GetStats().reentry==1);
 throwing=true;assert(TestAccess::Decide(tear,0)==1);throwing=false;assert(a.GetStats().exceptions==1);
 *reinterpret_cast<unsigned*>(player+0x28)=10;unsigned old=calls;assert(TestAccess::Decide(tear,0)==1&&calls==old);*reinterpret_cast<unsigned*>(player+0x28)=1;
 *reinterpret_cast<void**>(tear+0x3c8)=tear;assert(TestAccess::Decide(tear,0)==1);*reinterpret_cast<void**>(tear+0x3c8)=player;
 assert(TestAccess::Decide(reinterpret_cast<void*>(1),0)==1);
 for(unsigned k=0;k<4;++k){
  auto site=k==3?0x667BED:(k==2?0x668F97:(k?0x667648:0x6765CD)),deny=k==3?0x667EE4:(k==2?0x6693A3:(k?0x66797D:0x676947)),constant=k?0xB6745C:0xB692B0;
  if(k==3)Put(site,{0x8b,0x85,0x58,0xf7,0xff,0xff});else if(k==2)Put(site,{0x8d,0x85,0x80,0xf6,0xff,0xff});else Load(site,k?0:2,constant);*reinterpret_cast<float*>(Address(constant))=9.5f;
  Landing(site+(k>=2?6:8),1);Landing(deny,2);void*stub=TestAccess::Stub(a,k);assert(stub);
  for(unsigned pass=0;pass<2;++pass){permitWork=pass!=0;landed=0;Invoke(stub,tear);assert(landed==(permitWork?1u:2u));assert(afterEcx==0x13579bdf);assert(afterFlags&0x40);assert(afterX87==3.25f);if(k==3&&permitWork)assert(afterEax==0x2468ace0);assert(afterXmm==((permitWork&&k==0)?9.5f:3.25f));}
 }
 a.Disable();permitWork=false;old=calls;assert(TestAccess::Decide(tear,0)==1&&calls==old);TestAccess::Clear();
 Guards();Adapter installed(h);assert(installed.Install());assert(*reinterpret_cast<std::uint8_t*>(Address(0x6765CD))==0xe9);installed.Shutdown();assert(*reinterpret_cast<std::uint8_t*>(Address(0x6765CD))==0xf3);TestAccess::Clear();
 Guards();*reinterpret_cast<std::uint8_t*>(Address(0x667642))=0x90;Adapter bad(h);assert(!bad.Install());assert(*reinterpret_cast<std::uint8_t*>(Address(0x6765CD))==0xf3);
 for(auto prerequisite:{0x676574u,0x66762Fu,0x667635u,0x66763Cu,0x668F97u,0x668F8Fu,0x669394u,0x667BDFu,0x667BEDu,0x667EE4u}){Guards();*reinterpret_cast<std::uint8_t*>(Address(prerequisite))^=1;Adapter changed(h);assert(!changed.Install());assert(*reinterpret_cast<std::uint8_t*>(Address(0x6765CD))==0xf3);}
 for(auto failedSite:{0x667648u,0x668F97u,0x667BEDu}){Guards();failPoke=failedSite;Adapter partial(h);assert(!partial.Install());assert(*reinterpret_cast<std::uint8_t*>(Address(0x6765CD))==0xf3);assert(*reinterpret_cast<std::uint8_t*>(Address(0x667648))==0xf3);failPoke=0;TestAccess::Clear();}
 std::puts("native spawn budget: quota, ownership, reentry, SEH, actual x86 stubs/registers/flags/SSE passed");
}
