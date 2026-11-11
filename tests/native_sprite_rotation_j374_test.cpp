// Standalone synthetic ABI fixture: no game executable, memory dump, or saves.
// Original-game instruction differential is a separate private validation.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <xmmintrin.h>
#include "native_sprite_rotation_j374.h"
namespace rotation_pair_j374=highfps::sprite_rotation19715::detail;
namespace {
void* crtCos;void* crtSin;std::uint32_t cosCalls=0,sinCalls=0;std::uintptr_t selected;
void Require(bool okay,const char* text){if(!okay){std::printf("FAIL %s\n",text);std::exit(1);}}
std::uint32_t Bits(float x){std::uint32_t b;std::memcpy(&b,&x,4);return b;}
float Float(std::uint32_t x){float f;std::memcpy(&f,&x,4);return f;}
__declspec(naked) void Cosine(){__asm {
 push ebp
 mov ebp,esp
 and esp,0xfffffff8
 inc dword ptr [cosCalls]
 cvtss2sd xmm0,xmm0
 call dword ptr [crtCos]
 cvtsd2ss xmm0,xmm0
 mov esp,ebp
 pop ebp
 ret
}}
__declspec(naked) void Sine(){__asm {
 push ebp
 mov ebp,esp
 and esp,0xfffffff8
 inc dword ptr [sinCalls]
 cvtss2sd xmm0,xmm0
 call dword ptr [crtSin]
 cvtsd2ss xmm0,xmm0
 mov esp,ebp
 pop ebp
 ret
}}
float Add(float a,float b){return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a),_mm_set_ss(b)));}
float Sub(float a,float b){return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a),_mm_set_ss(b)));}
float Mul(float a,float b){return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a),_mm_set_ss(b)));}
// Independent generic scalar formula; no original executable instructions.
void __stdcall Apply(float* quad,const float* pivot,float cosine,float sine){
 for(unsigned i=0;i<8;i+=2){float x=Sub(quad[i],pivot[0]),y=Sub(quad[i+1],pivot[1]);
  quad[i]=Add(Sub(Mul(x,cosine),Mul(y,sine)),pivot[0]);
  quad[i+1]=Add(Add(Mul(y,cosine),Mul(x,sine)),pivot[1]);}
}
__declspec(naked) void Tail(){__asm {
 // Reproduce the documented handoff contract, not game code.
 sub esp,4
 movss dword ptr [esp],xmm0
 push dword ptr [esp+0xc]
 push dword ptr [ebp+8]
 push esi
 call Apply
 mov eax,esi
 pop esi
 mov esp,ebp
 pop ebp
 ret 4
}}
__declspec(naked) void NativeFixture(){__asm {
 push ebp
 mov ebp,esp
 and esp,0xfffffff8
 sub esp,0x14
 push esi
 mov esi,ecx
 movaps xmm0,xmm2
 xorps xmm1,xmm1
 ucomiss xmm0,xmm1
 jp calculate
 jne calculate
 mov eax,esi
 pop esi
 mov esp,ebp
 pop ebp
 ret 4
calculate:
 movss dword ptr [esp+0xc],xmm0
 call Cosine
 movss dword ptr [esp+8],xmm0
 movss xmm0,dword ptr [esp+0xc]
 call Sine
 jmp Tail
}}
__declspec(naked) float* __cdecl Invoke(float*,float,const float*){__asm {
 mov ecx,dword ptr [esp+4]
 movss xmm2,dword ptr [esp+8]
 push dword ptr [esp+12]
 call dword ptr [selected]
 ret
}}
unsigned short CW(){unsigned short value;__asm {fnstcw value}return value;}
void SetCW(unsigned short value){__asm {fldcw value}}
void Check(float angle,unsigned csr,unsigned short cw){
 float a[8]={-17.25f,-.125f,37.5f,-.125f,-17.25f,28.5f,37.5f,28.5f},b[8],pivot[2]={31.875f,-8.625f};std::memcpy(b,a,32);
 SetCW(cw);_mm_setcsr(csr);selected=(std::uintptr_t)&NativeFixture;Require(Invoke(a,angle,pivot)==a,"native fixture output pointer");
 SetCW(cw);_mm_setcsr(csr);selected=(std::uintptr_t)&rotation_pair_j374::Rotate;Require(Invoke(b,angle,pivot)==b,"adapter output pointer");
 Require(std::memcmp(a,b,32)==0,"synthetic ABI rotated coordinates bitwise");
}
unsigned char signatureBytes[32];unsigned pokeCalls=0;
std::uintptr_t SignatureAddress(std::uintptr_t va){return (std::uintptr_t)signatureBytes+va-0x1000;}
std::uintptr_t InvalidAddress(std::uintptr_t){return 0;}
bool CountPoke(std::uintptr_t,const std::uint8_t*,std::size_t){++pokeCalls;return true;}
void SignatureChecks(){
 using namespace highfps::sprite_rotation19715;unsigned char normalized[32];for(unsigned i=0;i<32;++i)normalized[i]=(unsigned char)(i*17+3);unsigned original=0x2000;std::memcpy(normalized+4,&original,4);
 std::uint64_t h=14695981039346656037ull,g=0x9e3779b97f4a7c15ull;for(auto value:normalized){h=(h^value)*1099511628211ull;g=(g+value)*0xc2b2ae3d27d4eb4full;g=(g<<29)|(g>>35);}
 detail::Relocation relocation{4,0x2000};detail::Signature signature{0x1000,32,h,g,&relocation,1};Host host;host.address=SignatureAddress;
 std::memcpy(signatureBytes,normalized,32);auto relocated=(std::uint32_t)SignatureAddress(0x2000);std::memcpy(signatureBytes+4,&relocated,4);Require(detail::Match(host,signature),"relocation-normalized signature");
 for(unsigned i=0;i<32;++i){signatureBytes[i]^=1;Require(!detail::Match(host,signature),"changed signature byte rejected");signatureBytes[i]^=1;}
 Host invalid;invalid.address=InvalidAddress;invalid.poke=CountPoke;invalid.gameThread=GetCurrentThreadId();Adapter rejected(invalid);Require(!rejected.Install()&&pokeCalls==0&&!rejected.HasRemainingPatches(),"invalid code signature never patches");
 invalid.gameThread=GetCurrentThreadId()+1;Adapter foreignInstall(invalid);Require(!foreignInstall.Install()&&pokeCalls==0,"foreign installer rejected");
}
DWORD WINAPI ForeignThread(void*){
 Check(.3125f,0x1f80,0x027f);return 0;
}

}
int main(){
 auto crt=LoadLibraryA("ucrtbase.dll");Require(crt!=nullptr,"load precise CRT");crtCos=(void*)GetProcAddress(crt,"_libm_sse2_cos_precise");crtSin=(void*)GetProcAddress(crt,"_libm_sse2_sin_precise");Require(crtCos&&crtSin,"precise CRT exports");
 auto savedCSR=_mm_getcsr();auto savedCW=CW();
 rotation_pair_j374::cosSlot=&crtCos;rotation_pair_j374::sinSlot=&crtSin;rotation_pair_j374::cosTarget=crtCos;rotation_pair_j374::sinTarget=crtSin;rotation_pair_j374::gameThread=GetCurrentThreadId();rotation_pair_j374::healthy=1;
 rotation_pair_j374::nativeEntry=(std::uintptr_t)&NativeFixture;rotation_pair_j374::nativeTail=(std::uintptr_t)&Tail;rotation_pair_j374::cosEntry=(std::uintptr_t)&Cosine;rotation_pair_j374::sinEntry=(std::uintptr_t)&Sine;
 const unsigned special[]={0,0x80000000u,1,0x80000001u,0x7f800000u,0xff800000u,0x7fc12345u,0xffc12345u,0x47800001u};
 for(auto bits:special)for(unsigned repeat=0;repeat<2;++repeat)Check(Float(bits),0x1f80,0x027f);
 for(unsigned i=0;i<4096;++i){float angle=(int(i)-2048)*.03125f;Check(angle,0x1f80,0x027f);Check(angle,0x1f80,0x027f);}
 for(unsigned i=0;i<4;++i){Check(.725f,0x1f80|(i<<13),0x027f);Check(.725f,0x1f80|0x8040,0x027f);Check(.725f,0x1f80,(unsigned short)(0x027f|(i<<10)));}
 rotation_pair_j374::skipBudget=rotation_pair_j374::probeCalls=rotation_pair_j374::probeHits=0;SetCW(0x027f);_mm_setcsr(0x1f80);const auto hits=rotation_pair_j374::stats.hits;
 const auto* pair=rotation_pair_j374::Get(Bits(.4375f));Require(pair!=nullptr,"supported pair");const unsigned c=cosCalls,s=sinCalls;const float expectedCos=pair->cosine,expectedSin=pair->sine;
 pair=rotation_pair_j374::Get(Bits(.4375f));Require(pair&&Bits(pair->cosine)==Bits(expectedCos)&&Bits(pair->sine)==Bits(expectedSin),"cached pair exact");Require(c==cosCalls&&s==sinCalls&&rotation_pair_j374::stats.hits>hits,"cache hit skips both trig calls");
 // Force two different exact angles into the same direct-map slot, then revisit.
 unsigned occupied[1024]={};unsigned first=0,second=0;
 for(unsigned i=1;i<65536 && !second;++i){unsigned angle=Bits(float(i)*.0009765625f);unsigned hash=(angle^(angle>>13))*0x85ebca6bu;hash^=hash>>16;unsigned slot=hash&1023;
  if(occupied[slot] && occupied[slot]!=angle){first=occupied[slot];second=angle;}else occupied[slot]=angle;}
 Require(second!=0,"construct cache collision");
 Check(Float(first),0x1f80,0x027f);Check(Float(second),0x1f80,0x027f);auto previousMisses=rotation_pair_j374::stats.misses;Check(Float(first),0x1f80,0x027f);Require(rotation_pair_j374::stats.misses>previousMisses,"collision recomputes evicted exact angle");
 for(auto bits:special)if((bits&0x7fffffff)!=0)Require(rotation_pair_j374::Get(bits)==nullptr,"exceptional angle uses native fallback");
 SetCW(0x037f);Require(rotation_pair_j374::Get(Bits(.4375f))==nullptr,"alternate x87 precision fallback");SetCW(0x027f);
 _mm_setcsr(0x9fc0);Require(rotation_pair_j374::Get(Bits(.4375f))==nullptr,"FTZ DAZ fallback");_mm_setcsr(0x1f80);
 // Sustained misses suspend lookup work; then the next probe can recover.
 std::memset(rotation_pair_j374::entries,0,sizeof(rotation_pair_j374::entries));rotation_pair_j374::probeCalls=rotation_pair_j374::probeHits=rotation_pair_j374::skipBudget=0;
 for(unsigned i=1;i<=64;++i)Require(rotation_pair_j374::Get(Bits(float(i)*.00137f))!=nullptr,"fill adaptive miss probe");Require(rotation_pair_j374::skipBudget==4096,"low hit rate enters native bypass");
 rotation_pair_j374::skipBudget=1;auto probeCount=rotation_pair_j374::stats.calls;Check(.637f,0x1f80,0x027f);Require(rotation_pair_j374::stats.calls==probeCount&&rotation_pair_j374::skipBudget==0,"cooldown skips cache lookup");Check(.637f,0x1f80,0x027f);Require(rotation_pair_j374::stats.calls==probeCount+1,"probe resumes after cooldown");
 // Foreign render callers take original path without touching shared cache/counters.
 auto beforeThread=rotation_pair_j374::stats;HANDLE thread=CreateThread(nullptr,0,ForeignThread,nullptr,0,nullptr);Require(thread!=nullptr,"create foreign caller");Require(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0,"foreign caller completed");DWORD threadResult=1;Require(GetExitCodeThread(thread,&threadResult)&&threadResult==0,"foreign caller result");CloseHandle(thread);Require(std::memcmp(&beforeThread,&rotation_pair_j374::stats,sizeof(beforeThread))==0,"foreign caller leaves cache counters untouched");
 // Runtime IAT identity mismatch fails closed before accessing a cache entry.
 auto oldCos=crtCos;crtCos=nullptr;Require(rotation_pair_j374::Get(Bits(.4375f))==nullptr&&rotation_pair_j374::healthy==0,"changed IAT disables reuse");crtCos=oldCos;rotation_pair_j374::healthy=1;
 rotation_pair_j374::healthy=0;auto disabledCalls=rotation_pair_j374::stats.calls;Check(.875f,0x1f80,0x027f);Require(disabledCalls==rotation_pair_j374::stats.calls,"disabled adapter forwards original");rotation_pair_j374::healthy=1;
 highfps::sprite_rotation19715::Host disabledHost;disabledHost.enabled=false;highfps::sprite_rotation19715::Adapter disabledAdapter(disabledHost);Require(!disabledAdapter.Install()&&!disabledAdapter.HasRemainingPatches(),"disabled installer does not patch");
 SignatureChecks();
 // Known scalar geometry check independent of the trampoline and native helper.
 float q[8]={1,0,0,1,-1,0,0,-1},p[2]={0,0};Apply(q,p,0,1);const float wanted[8]={0,1,-1,0,0,-1,1,0};for(unsigned i=0;i<8;++i)Require(q[i]==wanted[i],"quarter turn preserves separate vertices");
 _mm_setcsr(savedCSR);SetCW(savedCW);
 std::printf("PASS standalone synthetic ABI/cache cases=8229, hit skips both native trig calls; no game resources used\n");return 0;
}
