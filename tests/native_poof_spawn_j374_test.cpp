#include "native_poof_spawn_j374.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace highfps::poof_spawn19715;
static std::uint8_t*image;static bool allow=true,nested=false,throwing=false;static unsigned calls=0,seenVariant=0;static const void*currentFrame;
static std::uintptr_t failPoke=0;
static std::uintptr_t Address(std::uintptr_t a){return reinterpret_cast<std::uintptr_t>(image)+(a-0x400000);}
static bool Poke(std::uintptr_t a,const std::uint8_t*b,std::size_t n){if(a==failPoke)return false;std::memcpy(reinterpret_cast<void*>(Address(a)),b,n);return true;}
namespace highfps::poof_spawn19715 {
struct TestAccess {
 static void Activate(Adapter&a){Adapter::active_=&a;a.enabled_=true;a.tearVtable_=Address(0xB3C128);}
 static void Clear(){Adapter::active_=nullptr;}
 static unsigned Decide(void*e,const void*f){return Adapter::Decide(e,f);}
 static void*Stub(Adapter&a){std::memcpy(a.saved_,reinterpret_cast<void*>(Address(0x669413)),7);return a.MakeStub();}
};
}
static bool Admit(void*tear,unsigned variant,unsigned subtype){assert(!subtype);++calls;seenVariant=variant;
 if(nested){nested=false;assert(TestAccess::Decide(tear,currentFrame)==1);}
 if(throwing)RaiseException(0xE1234567,0,0,nullptr);
 __asm { pxor xmm2,xmm2 }
 __asm { fninit }
 return allow;
}
static unsigned landed=0,flags=0,regECX=0,regESI=0,regEDI=0;static float number=3.25f,xmm=0,x87=0;
static __declspec(naked) void Invoke(void*stub,void*tear,unsigned variant,unsigned subtype){__asm {
 push ebp
 mov ebp,esp
 sub esp,0x900
 push ebx
 push esi
 push edi
 mov eax,[ebp+16]
 mov [ebp-0x89c],eax
 mov eax,[ebp+20]
 mov [ebp-0x874],eax
 mov edi,[ebp+12]
 mov esi,0x2468abcd
 mov ecx,0x13579bdf
 mov eax,1
 cmp eax,eax
 movss xmm2,dword ptr[number]
 fld dword ptr[number]
 call dword ptr[ebp+8]
 pushfd
 pop eax
 mov flags,eax
 mov regECX,ecx
 mov regESI,esi
 mov regEDI,edi
 movss dword ptr[xmm],xmm2
 fstp dword ptr[x87]
 pop edi
 pop esi
 pop ebx
 mov esp,ebp
 pop ebp
 ret
}}
static void Landing(std::uintptr_t a,unsigned value){std::uint8_t b[]={0xc7,0x05,0,0,0,0,0,0,0,0,0xc3};auto p=reinterpret_cast<std::uint32_t>(&landed);std::memcpy(b+2,&p,4);std::memcpy(b+6,&value,4);Poke(a,b,sizeof b);}
template<std::size_t N>static void Put(std::uintptr_t a,const std::uint8_t(&b)[N]){Poke(a,b,N);}
static void Guards(){
 Put(0x669413,{0x83,0xbd,0x64,0xf7,0xff,0xff,0});Put(0x66941a,{0x0f,0x86,0x8d,6,0,0});Put(0x669420,{0xe8,0x4b,0x6f,8,0});
 Put(0x66944d,{0xff,0xb5,0x64,0xf7,0xff,0xff,0x68,0xe8,3,0,0,0xe8,0xf3,0xb1,0xdb,0xff});Put(0x66943a,{0xff,0xb5,0x8c,0xf7,0xff,0xff,0x8d,0x87,0x3c,3,0,0,0x57});
 Put(0x669aa7,{0x8d,0xb7,0x28,4,0,0,0x8b,0x47,0x2c,0x85,0xc0});Put(0x669aad,{0x8b,0x47,0x2c,0x85,0xc0,0x74,0x31,0x83,0xf8,0x18});
}
int main(){
 WorldQuota q;assert(q.Admit());q.Begin(1,2);assert(q.Admit()&&q.Admit()&&!q.Admit());q.Begin(1,100);assert(!q.Admit());q.Begin(2,1);assert(q.Admit()&&!q.Admit());q.Reset();assert(q.Admit());
 image=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,0x900000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
 alignas(16) std::uint8_t tear[0x900]{},player[0x500]{},frame[0x900]{};auto*fp=frame+sizeof frame;
 *reinterpret_cast<std::uintptr_t*>(tear)=Address(0xB3C128);*reinterpret_cast<unsigned*>(tear+0x28)=2;*reinterpret_cast<void**>(tear+0x3c8)=player;*reinterpret_cast<unsigned*>(player+0x28)=1;
 currentFrame=fp;bool runtimeEnabled=false;Host h{Address,Poke,Admit,GetCurrentThreadId(),true,&runtimeEnabled};Adapter a(h);TestAccess::Activate(a);
 const auto offCalls=calls;const auto offExceptions=a.GetStats().exceptions,offUnsupported=a.GetStats().unsupported;
 assert(TestAccess::Decide(reinterpret_cast<void*>(1),reinterpret_cast<void*>(1))==1);
 assert(calls==offCalls&&a.GetStats().exceptions==offExceptions&&a.GetStats().unsupported==offUnsupported);
 runtimeEnabled=true;
 for(unsigned variant:{11u,12u,13u,79u}){*reinterpret_cast<unsigned*>(fp-0x89c)=variant;allow=false;assert(TestAccess::Decide(tear,fp)==0&&seenVariant==variant);}
 for(unsigned variant:{7u,30u,0u,14u}){*reinterpret_cast<unsigned*>(fp-0x89c)=variant;const auto before=calls;assert(TestAccess::Decide(tear,fp)==1&&calls==before);}
 *reinterpret_cast<unsigned*>(fp-0x89c)=12;*reinterpret_cast<unsigned*>(fp-0x874)=10;assert(TestAccess::Decide(tear,fp)==1);*reinterpret_cast<unsigned*>(fp-0x874)=0;
 *reinterpret_cast<unsigned*>(tear+0x28)=9;assert(TestAccess::Decide(tear,fp)==1);*reinterpret_cast<unsigned*>(tear+0x28)=2;
 *reinterpret_cast<unsigned*>(player+0x28)=10;assert(TestAccess::Decide(tear,fp)==1);*reinterpret_cast<unsigned*>(player+0x28)=1;
 *reinterpret_cast<void**>(tear+0x3c8)=tear;assert(TestAccess::Decide(tear,fp)==1);*reinterpret_cast<void**>(tear+0x3c8)=player;
 nested=true;assert(TestAccess::Decide(tear,fp)==0&&a.GetStats().reentry==1);throwing=true;assert(TestAccess::Decide(tear,fp)==1);throwing=false;
 void*bad=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);assert(bad);assert(TestAccess::Decide(tear,static_cast<std::uint8_t*>(bad)+0x900)==1);assert(TestAccess::Decide(bad,fp)==1);VirtualFree(bad,0,MEM_RELEASE);
 Guards();void*stub=TestAccess::Stub(a);Landing(0x66941a,1);Landing(0x669aad,2);
 for(unsigned pass=0;pass<2;++pass){allow=pass!=0;landed=0;Invoke(stub,tear,12,0);assert(landed==(allow?1u:2u));assert(regECX==0x13579bdf&&regESI==0x2468abcd&&regEDI==reinterpret_cast<std::uintptr_t>(tear));assert(xmm==3.25f&&x87==3.25f);assert(bool(flags&0x40)==!allow);}
 a.Disable();const auto before=calls;assert(TestAccess::Decide(tear,fp)==1&&calls==before);TestAccess::Clear();
 Guards();Adapter installed(h);assert(installed.Install());assert(*reinterpret_cast<std::uint8_t*>(Address(0x669413))==0xe9);installed.Shutdown();assert(*reinterpret_cast<std::uint8_t*>(Address(0x669413))==0x83);TestAccess::Clear();
 Guards();*reinterpret_cast<std::uint8_t*>(Address(0x66941a))=0x90;Adapter mismatch(h);assert(!mismatch.Install());assert(*reinterpret_cast<std::uint8_t*>(Address(0x669413))==0x83);
 Guards();failPoke=0x669413;Adapter failed(h);assert(!failed.Install());failPoke=0;assert(*reinterpret_cast<std::uint8_t*>(Address(0x669413))==0x83);TestAccess::Clear();
 std::puts("native poof admission: quota, protected variants/enemies/subtypes, owner chain, SEH, actual CMP7/deny ABI, FXSAVE, guard/failure: passed");
}
