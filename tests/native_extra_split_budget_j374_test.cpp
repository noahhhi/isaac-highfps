#include "native_extra_split_budget_j374.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace highfps::extra_split19715;
static std::uint8_t*image;static bool allow=true,nested=false,throwing=false;static unsigned costSeen=0,calls=0;static const void*testFrame;static const void*testStack;
static std::uintptr_t failPoke=0;static std::uintptr_t*returnSlot;
static std::uintptr_t Address(std::uintptr_t a){return reinterpret_cast<std::uintptr_t>(image)+a-0x400000;}
static bool Poke(std::uintptr_t a,const std::uint8_t*b,std::size_t n){if(a==failPoke)return false;std::memcpy(reinterpret_cast<void*>(Address(a)),b,n);return true;}
namespace highfps::extra_split19715 {
struct TestAccess {
 static unsigned Decide(void*t,unsigned k,const void*f,const void*s){return Adapter::Decide(t,k,f,s);}
 static void Activate(Adapter&a){Adapter::active_=&a;a.enabled_=true;a.tearVtable_=Address(0xB3C128);a.haemoReturn_=Address(0x6693D0);returnSlot=&a.haemoReturn_;}
 static void Clear(){Adapter::active_=nullptr;}
 static void*Stub(Adapter&a,unsigned k){std::memcpy(a.saved_[k],reinterpret_cast<void*>(Address(Adapter::sites_[k])),Adapter::lengths_[k]);return a.MakeStub(k);}
};
}
static bool Admit(Kind kind,void*t,unsigned cost){++calls;costSeen=cost;assert(cost==(kind==Kind::BonePair?2u:1u));if(nested){nested=false;assert(TestAccess::Decide(t,0,testFrame,testStack)==1);}if(throwing)RaiseException(0xE1234567,0,0,nullptr);
 __asm { pxor xmm1,xmm1 }
 __asm { pxor xmm2,xmm2 }
 __asm { fninit }
 return allow;
}
static unsigned landed=0,allocated=0,flags=0,ecxOut=0,esiOut=0,ediOut=0,boneLocal=0,indexLocal=0,leftLocal=0;
static float number=3.25f,replayNumber=9.5f,xmm1Out=0,xmm2Out=0,x87Out=0;
static __declspec(naked)void Invoke(void*stub,void*tear){__asm {
 push ebp
 mov ebp,esp
 sub esp,0x2100
 push ebx
 push esi
 push edi
 sub esp,0x40
 mov eax,[ebp+4]
 mov edx,returnSlot
 mov [edx],eax
 mov dword ptr[ebp-0x64],55
 mov dword ptr[ebp-0x884],5
 mov dword ptr[ebp-0x88c],1
 mov dword ptr[esp+0x08],0x55aa33
 mov dword ptr[esp+0x10],3
 mov eax,replayNumber
 mov [esp+0x18],eax
 mov edi,[ebp+12]
 mov esi,0x2468abcd
 mov ecx,0x13579bdf
 mov eax,1
 cmp eax,eax
 movss xmm1,dword ptr[number]
 movss xmm2,dword ptr[number]
 fld dword ptr[number]
 call dword ptr[ebp+8]
 pushfd
 pop eax
 mov flags,eax
 mov ecxOut,ecx
 mov esiOut,esi
 mov ediOut,edi
 movss dword ptr[xmm1Out],xmm1
 movss dword ptr[xmm2Out],xmm2
 fstp dword ptr[x87Out]
 mov eax,[ebp-0x64]
 mov boneLocal,eax
 mov eax,[ebp-0x88c]
 mov indexLocal,eax
 mov eax,[esp+0x10]
 mov leftLocal,eax
 add esp,0x40
 pop edi
 pop esi
 pop ebx
 mov esp,ebp
 pop ebp
 ret
}}
template<std::size_t N>static void Put(std::uintptr_t a,const std::uint8_t(&b)[N]){Poke(a,b,N);}
static void Landing(std::uintptr_t a,unsigned value,unsigned prefix=0){auto*p=reinterpret_cast<std::uint8_t*>(Address(a));p+=prefix;
 if(value==1){p[0]=0x9c;p[1]=0xff;p[2]=0x05;auto ptr=reinterpret_cast<std::uint32_t>(&allocated);std::memcpy(p+3,&ptr,4);p[7]=0x9d;p+=8;}
 const std::uint8_t b[]={0xc7,0x05,0,0,0,0,0,0,0,0,0xc3};std::memcpy(p,b,sizeof b);auto ptr=reinterpret_cast<std::uint32_t>(&landed);std::memcpy(p+2,&ptr,4);std::memcpy(p+6,&value,4);
}
static void Guards();
int main(){
 image=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
 alignas(16)std::uint8_t tear[0x900]{},player[0x500]{},frame[0x2200]{},stack[0x50]{};auto*fp=frame+0x2100;testFrame=fp;testStack=stack;
 *reinterpret_cast<std::uintptr_t*>(tear)=Address(0xB3C128);*reinterpret_cast<unsigned*>(tear+0x28)=2;*reinterpret_cast<void**>(tear+0x3c8)=player;*reinterpret_cast<unsigned*>(player+0x28)=1;
 *reinterpret_cast<std::uint64_t*>(tear+0x428)=(1ull<<51)|(1ull<<59)|(1ull<<62);
 *reinterpret_cast<unsigned*>(fp-0x884)=5;*reinterpret_cast<unsigned*>(fp-0x88c)=1;*reinterpret_cast<unsigned*>(fp+4)=std::uint32_t(Address(0x6693d0));*reinterpret_cast<unsigned*>(stack+0x14)=5;
 bool runtimeEnabled=false;Host host{Address,Poke,Admit,GetCurrentThreadId(),true,&runtimeEnabled};Adapter a(host);TestAccess::Activate(a);
 const auto offCalls=calls;const auto offExceptions=a.GetStats().exceptions,offUnsupported=a.GetStats().unsupported;
 assert(TestAccess::Decide(reinterpret_cast<void*>(1),1,reinterpret_cast<void*>(1),reinterpret_cast<void*>(1))==1);
 assert(calls==offCalls&&a.GetStats().exceptions==offExceptions&&a.GetStats().unsupported==offUnsupported);
 runtimeEnabled=true;
 allow=false;for(unsigned k=0;k<3;++k){assert(TestAccess::Decide(tear,k,fp,stack)==0);assert(costSeen==(k==0?2u:1u));}
 *reinterpret_cast<unsigned*>(fp-0x884)=19;assert(TestAccess::Decide(tear,1,fp,stack)==1);*reinterpret_cast<unsigned*>(fp-0x884)=5;
 *reinterpret_cast<unsigned*>(stack+0x14)=12;assert(TestAccess::Decide(tear,2,fp,stack)==1);*reinterpret_cast<unsigned*>(stack+0x14)=5;
 *reinterpret_cast<unsigned*>(fp+4)=1;assert(TestAccess::Decide(tear,2,fp,stack)==1);*reinterpret_cast<unsigned*>(fp+4)=std::uint32_t(Address(0x6693d0));
 *reinterpret_cast<unsigned*>(tear+0x28)=9;assert(TestAccess::Decide(tear,0,fp,stack)==1);*reinterpret_cast<unsigned*>(tear+0x28)=1000;assert(TestAccess::Decide(tear,2,fp,stack)==1);*reinterpret_cast<unsigned*>(tear+0x28)=2;
 *reinterpret_cast<void**>(tear+0x3c8)=tear;assert(TestAccess::Decide(tear,0,fp,stack)==1);*reinterpret_cast<void**>(tear+0x3c8)=player;
 nested=true;assert(TestAccess::Decide(tear,0,fp,stack)==0);throwing=true;assert(TestAccess::Decide(tear,0,fp,stack)==1);throwing=false;
 void*bad=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);assert(bad);assert(TestAccess::Decide(bad,0,fp,stack)==1);assert(TestAccess::Decide(tear,1,static_cast<std::uint8_t*>(bad)+0x900,stack)==1);VirtualFree(bad,0,MEM_RELEASE);
 const unsigned sites[]={0x676984,0x66e5fa,0x6780dc},denies[]={0x676d0c,0x66e858,0x6783ac},lengths[]={7,6,6};
 for(unsigned k=0;k<3;++k){Guards();void*stub=TestAccess::Stub(a,k);Landing(sites[k]+lengths[k],1);Landing(denies[k],2,k==1?13:(k==2?9:0));
  for(unsigned pass=0;pass<2;++pass){allow=pass!=0;landed=0;const unsigned before=allocated;Invoke(stub,tear);assert(landed==(allow?1u:2u)&&allocated==before+(allow?1u:0u));
   assert(ecxOut==0x13579bdf&&ediOut==reinterpret_cast<std::uintptr_t>(tear));assert(esiOut==(!allow&&k==2?0x55aa33u:0x2468abcdu));assert(xmm2Out==3.25f&&x87Out==3.25f&&xmm1Out==(allow&&k==2?9.5f:3.25f));
   assert(boneLocal==(allow&&k==0?0u:55u));assert(indexLocal==(!allow&&k==1?2u:1u));assert(leftLocal==(!allow&&k==2?2u:3u));assert(bool(flags&0x40)==(allow||k==0));
  }
 }
 a.Disable();const auto before=calls;assert(TestAccess::Decide(tear,0,fp,stack)==1&&calls==before);TestAccess::Clear();
 Guards();Adapter installed(host);assert(installed.Install());installed.Shutdown();TestAccess::Clear();
 Guards();*reinterpret_cast<std::uint8_t*>(Address(0x66e871))=0x90;Adapter changed(host);assert(!changed.Install());assert(*reinterpret_cast<std::uint8_t*>(Address(0x676984))==0xc7);
 Guards();failPoke=0x6780dc;Adapter partial(host);assert(!partial.Install());failPoke=0;assert(*reinterpret_cast<std::uint8_t*>(Address(0x676984))==0xc7&&*reinterpret_cast<std::uint8_t*>(Address(0x66e5fa))==0x8d);TestAccess::Clear();
 std::puts("extra split budgets: costs/ownership, per-child loop restoration, actual x86 GPR/FXSAVE, no allocation sentinel on denial, guards/rollback: passed");
}

static void Guards(){
    Put(0x676984,{0xc7,0x45,0x9c,0x00,0x00,0x00,0x00});
    Put(0x676971,{0x0f,0x84,0x95,0x03,0x00,0x00});
    Put(0x676A8D,{0x56,0x6a,0x02,0xe8,0xbb,0xdb,0xda,0xff});
    Put(0x676CF3,{0xff,0x50,0x0c});
    Put(0x676CFD,{0x83,0xf8,0x02,0x0f,0x8c,0x8a,0xfc,0xff,0xff,0x8d,0xb7,0x28,0x04,0x00,0x00});
    Put(0x66E5FA,{0x8d,0x85,0x04,0xe3,0xff,0xff});
    Put(0x66E70C,{0x56,0x6a,0x02,0xe8,0x3c,0x5f,0xdb,0xff});
    Put(0x66E858,{0x8b,0x85,0x74,0xf7,0xff,0xff,0x40,0x89,0x85,0x74,0xf7,0xff,0xff,0x3b,0x85,0x7c,0xf7,0xff,0xff,0x0f,0x8c,0x83,0xfd,0xff,0xff});
    Put(0x66E871,{0x8b,0x07,0x8b,0xcf,0x8b,0x40,0x28,0xff,0xd0});
    Put(0x6780D0,{0xe8,0x9b,0x82,0x07,0x00,0x8b,0xf0,0xe8,0x94,0x82,0x07,0x00});
    Put(0x6780DC,{0xf3,0x0f,0x10,0x4c,0x24,0x1c});
    Put(0x678185,{0x6a,0x02,0xf3,0x0f,0x58,0xd8,0xf3,0x0f,0x11,0x54,0x24,0x78,0xf3,0x0f,0x11,0x5c,0x24,0x7c,0xe8,0x14,0x74,0x08,0x00});
    Put(0x6783AC,{0x8b,0x74,0x24,0x0c,0x83,0x6c,0x24,0x14,0x01,0x0f,0x85,0x2a,0xf6,0xff,0xff,0x5f,0x5e,0x8b,0xe5,0x5d,0xc2,0x04,0x00});
    Put(0x6693C3,{0xff,0xb5,0x94,0xf7,0xff,0xff,0x8b,0xcf,0xe8,0x00,0xe5,0x00,0x00});
    Put(0x6778D0,{0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,0x68,0x56,0x57,0x8b,0xf9});
    Put(0xBD2340,{0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00});
    Put(0xBD2250,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00});
    Put(0xBD23F0,{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00});
}
