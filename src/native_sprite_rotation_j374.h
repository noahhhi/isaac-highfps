#pragma once
// J374 ANM2 rotation reuse: exact angle bits share native sin/cos results.
// Each instance retains the original native pivot/quad arithmetic, draw and entity.
// Cache hits intentionally do not reproduce transient MXCSR/x87 status flags.
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <xmmintrin.h>
#if !defined(_MSC_VER) || !defined(_M_IX86)
#error J374 rotation adapter requires MSVC-compatible x86 assembly
#endif
namespace highfps::sprite_rotation19715 {
struct Host {
 std::uintptr_t(*address)(std::uintptr_t)=nullptr;
 bool(*poke)(std::uintptr_t,const std::uint8_t*,std::size_t)=nullptr;
 DWORD gameThread=0;bool enabled=true;
};
struct Stats {std::uint64_t calls,hits,misses,bypasses,nativeSkips,iatChanges;};
namespace detail {
static std::uintptr_t nativeEntry=0xa055f0,nativeTail=0xa0562c,cosEntry=0x412cc0,sinEntry=0x412cf0;
static void** cosSlot=nullptr;static void** sinSlot=nullptr;static void* cosTarget=nullptr;static void* sinTarget=nullptr;
static DWORD gameThread=0;static volatile unsigned healthy=0,skipBudget=0;
static unsigned probeCalls=0,probeHits=0,skipped=0;
static Stats stats{};
struct Pair {std::uint32_t angle;float cosine,sine;std::uint32_t control;};
static Pair entries[1024]{};
static __declspec(naked) void __cdecl Compute(Pair*,std::uint32_t){__asm {
 push ebp
 mov ebp,esp
 and esp,0xfffffff8
 push esi
 sub esp,4
 mov esi,dword ptr [ebp+8]
 movd xmm0,dword ptr [ebp+12]
 call dword ptr [cosEntry]
 movss dword ptr [esi+4],xmm0
 movd xmm0,dword ptr [ebp+12]
 call dword ptr [sinEntry]
 movss dword ptr [esi+8],xmm0
 add esp,4
 pop esi
 mov esp,ebp
 pop ebp
 ret
}}

static const Pair* __stdcall Get(std::uint32_t angle){
 ++stats.calls;
 if(*reinterpret_cast<void*volatile*>(cosSlot)!=cosTarget||*reinterpret_cast<void*volatile*>(sinSlot)!=sinTarget){healthy=0;++stats.iatChanges;++stats.bypasses;return nullptr;}
 const auto control=_mm_getcsr()&0xffc0u;unsigned short cw;__asm {fnstcw cw}
 const auto magnitude=angle&0x7fffffffu;
 if(control!=0x1f80u||cw!=0x027fu||magnitude>0x47800000u||(magnitude&&magnitude<0x00800000u)){++stats.bypasses;return nullptr;}
 auto hash=(angle^(angle>>13))*0x85ebca6bu;hash^=hash>>16;auto&e=entries[hash&1023u];
 if(e.control==control&&e.angle==angle){++stats.hits;++probeHits;}
 else {++stats.misses;Compute(&e,angle);e.angle=angle;e.control=control;}
 if(++probeCalls==64){if(probeHits<48)skipBudget=4096;probeCalls=probeHits=0;}
 return &e;
}
// Only two verified callers: neither reads zero-path XMM scratch registers.
static __declspec(naked) void* Rotate(){__asm {
 movd eax,xmm2
 and eax,0x7fffffff
 jz zero
 mov eax,dword ptr fs:[0x24]
 cmp eax,dword ptr [gameThread]
 jne original
 cmp dword ptr [healthy],0
 je original
 cmp dword ptr [skipBudget],0
 je begin
 dec dword ptr [skipBudget]
 inc dword ptr [skipped]
 jmp original
zero:
 mov eax,ecx
 ret 4
begin:
 push ebp
 mov ebp,esp
 and esp,0xfffffff8
 sub esp,0x14
 push esi
 mov esi,ecx
 movss dword ptr [esp+0xc],xmm2
 movd eax,xmm2
 push eax
 call Get
 test eax,eax
 jz fallback
 movss xmm0,dword ptr [eax+4]
 movss dword ptr [esp+8],xmm0
 movss xmm0,dword ptr [eax+8]
 jmp dword ptr [nativeTail]
fallback:
 movss xmm2,dword ptr [esp+0xc]
 mov ecx,esi
 pop esi
 mov esp,ebp
 pop ebp
original:
 jmp dword ptr [nativeEntry]
}}

struct Relocation;
struct Signature {std::uintptr_t va;std::size_t size;std::uint64_t hash,secondHash;const Relocation* reloc;std::size_t count;};
struct Relocation {std::uint16_t offset;std::uint32_t value;};
static constexpr Relocation RelocRotation[]={{15,0xb6683cu}};
static constexpr Signature SigRotation={0xa055f0, 514, 0x76854f4e86162839ull, 0xe43c2bd3daa91c50ull, RelocRotation, 1};
static constexpr Relocation RelocCos[]={{0,0}};
static constexpr Signature SigCos={0x412cc0, 23, 0x9cfd0cf1002dfe1dull, 0x11e48792f36b78baull, RelocCos, 0};
static constexpr Relocation RelocSin[]={{0,0}};
static constexpr Signature SigSin={0x412cf0, 23, 0x78ad09108dc9441cull, 0x6765eac0a74d215cull, RelocSin, 0};
static constexpr Relocation RelocCallers[]={{4,0xb668e0u},{37,0xb668e0u}};
static constexpr Signature SigCallers={0x4059a7, 56, 0x7951a7701311c319ull, 0xed331437b6fed256ull, RelocCallers, 2};
// Two independent normalized 64-bit checksums, plus exact relocated operands.
// These are executable-version integrity guards, not cryptographic trust checks.
inline bool Match(const Host& host,const Signature& sig){
 const auto*bytes=reinterpret_cast<const unsigned char*>(host.address(sig.va));std::uint64_t h=14695981039346656037ull,g=0x9e3779b97f4a7c15ull;
 std::size_t r=0;
 for(std::size_t i=0;i<sig.size;++i){unsigned char value=bytes[i];
  if(r<sig.count&&i>=sig.reloc[r].offset&&i<sig.reloc[r].offset+4){const auto& fix=sig.reloc[r];if(i==fix.offset){std::uint32_t word;std::memcpy(&word,bytes+i,4);if(word!=host.address(fix.value))return false;}value=static_cast<unsigned char>(fix.value>>(8*(i-fix.offset)));if(i==fix.offset+3)++r;}
  h=(h^value)*1099511628211ull;g=(g+value)*0xc2b2ae3d27d4eb4full;g=(g<<29)|(g>>35);
 }
 return h==sig.hash&&g==sig.secondHash;
}
inline bool Verify(const Host& host){
 __try {
  if(!Match(host,SigRotation)||!Match(host,SigCos)||!Match(host,SigSin)||!Match(host,SigCallers))return false;
  if(*reinterpret_cast<const std::uint32_t*>(host.address(0xb6683c))!=0)return false;
  const std::uintptr_t thunks[]={0xaad1d1,0xaad1ef},slots[]={0xad05e4,0xad05f8};
  for(unsigned i=0;i<2;++i){auto*p=reinterpret_cast<const unsigned char*>(host.address(thunks[i]));std::uint32_t slot;std::memcpy(&slot,p+2,4);if(p[0]!=0xff||p[1]!=0x25||slot!=host.address(slots[i]))return false;}
  const std::uintptr_t sites[]={0x4059b9,0x4059d3};
  for(auto va:sites){auto*p=reinterpret_cast<const unsigned char*>(host.address(va));std::int32_t delta;std::memcpy(&delta,p+1,4);if(p[0]!=0xe8||std::uint32_t(host.address(va)+5+delta)!=host.address(0xa055f0))return false;}
  return true;
 } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static void* owner=nullptr;
} // namespace detail
class Adapter {
 Host host_;unsigned attempted_=0;bool installed_=false;
 unsigned char original_[2][5]{};unsigned char patch_[2][5]{};
 static constexpr std::uintptr_t sites_[2]={0x4059b9,0x4059d3};
 bool Restore(bool installationFailure=false){bool ok=true;for(unsigned i=attempted_;i>0;--i){const unsigned n=i-1;auto*live=reinterpret_cast<const unsigned char*>(host_.address(sites_[n]));if(std::memcmp(live,original_[n],5)!=0){if(!installationFailure&&std::memcmp(live,patch_[n],5)!=0){ok=false;continue;}if(!host_.poke(sites_[n],original_[n],5)||std::memcmp(live,original_[n],5)!=0)ok=false;}}if(ok){attempted_=0;installed_=false;if(detail::owner==this)detail::owner=nullptr;}return ok;}
public:
 explicit Adapter(Host host):host_(host){}
 Adapter(const Adapter&)=delete;Adapter&operator=(const Adapter&)=delete;
 bool Install(){
  using namespace detail;if(installed_)return true;
  if(!host_.enabled||!host_.address||!host_.poke||!host_.gameThread||GetCurrentThreadId()!=host_.gameThread||owner||!Verify(host_))return false;
  auto**c=reinterpret_cast<void**>(host_.address(0xad05e4));auto**s=reinterpret_cast<void**>(host_.address(0xad05f8));
  auto crt=GetModuleHandleA("ucrtbase.dll");if(!crt)return false;
  auto expectedCos=reinterpret_cast<void*>(GetProcAddress(crt,"_libm_sse2_cos_precise"));auto expectedSin=reinterpret_cast<void*>(GetProcAddress(crt,"_libm_sse2_sin_precise"));
  if(!expectedCos||!expectedSin||*c!=expectedCos||*s!=expectedSin)return false;
  nativeEntry=host_.address(0xa055f0);nativeTail=host_.address(0xa0562c);cosEntry=host_.address(0x412cc0);sinEntry=host_.address(0x412cf0);
  cosSlot=c;sinSlot=s;cosTarget=*c;sinTarget=*s;gameThread=host_.gameThread;healthy=0;skipBudget=probeCalls=probeHits=skipped=0;stats={};std::memset(entries,0,sizeof(entries));owner=this;
  for(unsigned i=0;i<2;++i){std::memcpy(original_[i],reinterpret_cast<void*>(host_.address(sites_[i])),5);patch_[i][0]=0xe8;const std::uint32_t displacement=std::uint32_t(reinterpret_cast<std::uintptr_t>(&Rotate)-host_.address(sites_[i])-5);std::memcpy(patch_[i]+1,&displacement,4);}
  for(unsigned i=0;i<2;++i){attempted_=i+1;if(!host_.poke(sites_[i],patch_[i],5)||std::memcmp(reinterpret_cast<void*>(host_.address(sites_[i])),patch_[i],5)!=0){Restore(true);return false;}}
  installed_=true;healthy=1;return true;
 }
 Stats GetStats()const{auto value=detail::stats;value.nativeSkips=detail::skipped;return value;}
 void Disable(){if(detail::owner==this)detail::healthy=0;}
 bool Shutdown(){Disable();return attempted_?Restore():true;}
 bool Installed()const{return installed_;}
 bool HasRemainingPatches()const{return attempted_!=0;}
 // A failed rollback keeps static, disabled transparent hooks alive. The host
 // must keep this module loaded while HasRemainingPatches() is true.
};
} // namespace highfps::sprite_rotation19715
