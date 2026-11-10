#include "shader_location_cache.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
using highfps::shader_locations::Cache;
using highfps::shader_locations::Kind;
static std::uint64_t allocations=0;
void*operator new(std::size_t size){++allocations;if(auto*p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
namespace highfps::shader_locations {
struct CacheTestAccess {
 static std::uint64_t Hash(const char*name){return Cache::Hash(1,1,Kind::Uniform,{name,std::uint8_t(std::strlen(name))});}
 static std::size_t Front(std::uintptr_t c,std::uint32_t p,Kind k,const char*n){return Cache::FrontIndex(c,p,Cache::HotClass(k,{n,std::uint8_t(std::strlen(n))}));}
 static void ForceNextGenerationWrap(Cache&cache){cache.generation_=std::numeric_limits<std::uint32_t>::max();}
};
}
static void IdentityAndNoNegative(){
 const auto before=allocations;Cache cache;assert(allocations==before+1);
 int out=91;assert(!cache.Find(1,1,Kind::Uniform,"absent",out)&&out==91);
 assert(cache.Store(1,1,Kind::Attribute,"Position",0));
 assert(cache.Store(1,1,Kind::Uniform,"Position",7));
 assert(cache.Store(2,1,Kind::Attribute,"Position",3));
 assert(cache.Store(1,2,Kind::Attribute,"Position",4));
 assert(cache.Store(1,1,Kind::Attribute,"position",5));
 assert(cache.Store(1,1,Kind::Attribute,"Position[0]",6));
 assert(cache.Find(1,1,Kind::Attribute,"Position",out)&&out==0);
 assert(cache.Find(1,1,Kind::Uniform,"Position",out)&&out==7);
 assert(cache.Find(2,1,Kind::Attribute,"Position",out)&&out==3);
 assert(cache.Find(1,2,Kind::Attribute,"Position",out)&&out==4);
 assert(cache.Find(1,1,Kind::Attribute,"position",out)&&out==5);
 assert(cache.Find(1,1,Kind::Attribute,"Position[0]",out)&&out==6);
 char source[]="OwnedName";assert(cache.Store(1,1,Kind::Uniform,source,17));source[0]='X';
 assert(cache.Find(1,1,Kind::Uniform,"OwnedName",out)&&out==17);out=82;assert(!cache.Find(1,1,Kind::Uniform,source,out)&&out==82);
 assert(!cache.Store(1,1,Kind::Attribute,"Position",-1));assert(cache.Find(1,1,Kind::Attribute,"Position",out)&&out==0);
 unsigned driverCalls=0;
 auto query=[&](const char*name,int driverResult){int value=0;if(cache.Find(9,9,Kind::Uniform,name,value))return value;++driverCalls;cache.Store(9,9,Kind::Uniform,name,driverResult);return driverResult;};
 assert(query("missing",-1)==-1&&query("missing",-1)==-1&&driverCalls==2);
 assert(query("validZero",0)==0&&query("validZero",123)==0&&driverCalls==3);
 const auto size=cache.Size();cache.ResetStats();assert(cache.Size()==size&&cache.GetStats().finds==0);
 assert(cache.Find(1,1,Kind::Attribute,"Position",out)&&out==0);
 assert(allocations==before+1);std::puts("context/program/kind/exact copied-name identity, location zero and no negative caching: passed");
}
static void BoundsAndReset(){
 Cache cache;char longest[Cache::NameBytes];std::memset(longest,'a',sizeof longest);longest[sizeof longest-1]=0;
 char tooLong[Cache::NameBytes+1];std::memset(tooLong,'a',sizeof tooLong);tooLong[sizeof tooLong-1]=0;
 char unterminated[Cache::NameBytes];std::memset(unterminated,'a',sizeof unterminated);
 assert(cache.Store(7,8,Kind::Uniform,longest,0));int out=9;assert(cache.Find(7,8,Kind::Uniform,longest,out)&&out==0);
 assert(!cache.Store(7,8,Kind::Uniform,tooLong,1)&&!cache.Store(7,8,Kind::Uniform,unterminated,2));
 out=55;assert(!cache.Find(7,8,Kind::Uniform,tooLong,out)&&out==55);
 assert(!cache.Store(7,8,Kind::Uniform,"",1));assert(!cache.Store(7,8,Kind::Uniform,nullptr,1));
 assert(!cache.Store(0,8,Kind::Uniform,"a",1));assert(!cache.Store(7,0,Kind::Uniform,"a",1));
 assert(!cache.Store(7,8,static_cast<Kind>(99),"a",1));
 assert(cache.Size()==1&&cache.GetStats().rejectedInvalid==8);
 cache.BeginFrame();assert(cache.Size()==0&&!cache.Find(7,8,Kind::Uniform,longest,out)&&out==55);
 assert(cache.Store(7,8,Kind::Uniform,longest,42));assert(cache.Find(7,8,Kind::Uniform,longest,out)&&out==42);
 cache.Clear();assert(!cache.Find(7,8,Kind::Uniform,longest,out)); // Same program/context reused after relink/deletion.
 assert(cache.Store(7,8,Kind::Uniform,longest,73));assert(cache.Find(7,8,Kind::Uniform,longest,out)&&out==73);
 // A real 32-bit epoch wrap must erase old stamp=1 slots before reusing them.
 Cache wrapping;assert(wrapping.Store(1,1,Kind::Uniform,"old",10));
 highfps::shader_locations::CacheTestAccess::ForceNextGenerationWrap(wrapping);wrapping.Clear();
 assert(!wrapping.Find(1,1,Kind::Uniform,"old",out));
 assert(wrapping.Store(1,1,Kind::Uniform,"old",20)&&wrapping.Find(1,1,Kind::Uniform,"old",out)&&out==20);
 std::puts("bounded names without truncation, invalid inputs, frame/lifetime clears and generation wrap: passed");
}
static void CollisionOverflowAndNoAlloc(){
 Cache cache;const auto before=allocations;
 struct Key{std::uintptr_t context;std::uint32_t program;Kind kind;char name[32];};
 std::array<Key,Cache::Capacity> keys{};
 for(unsigned i=0;i<Cache::Capacity;++i){auto&k=keys[i];k.context=0x10000u+0x1000u*(i%7);k.program=1+i%13;k.kind=i%2?Kind::Uniform:Kind::Attribute;
  std::snprintf(k.name,sizeof k.name,"Location_%u",i);assert(cache.Store(k.context,k.program,k.kind,k.name,int(i)));
 }
 assert(cache.Size()==Cache::Capacity);const auto build=cache.GetStats();assert(build.probes>build.stores); // Actual occupied-slot collisions exercised.
 assert(build.probes<8*Cache::Capacity);
 assert(!cache.Store(1,1,Kind::Uniform,"overflow",9));assert(cache.GetStats().rejectedFull==1);
 for(unsigned i=0;i<Cache::Capacity;++i){const auto&k=keys[i];int out=-9;assert(cache.Find(k.context,k.program,k.kind,k.name,out)&&out==int(i));}
 const auto&first=keys[0];assert(cache.Store(first.context,first.program,first.kind,first.name,std::numeric_limits<int>::max()));
 int out=-9;assert(cache.Find(first.context,first.program,first.kind,first.name,out)&&out==std::numeric_limits<int>::max());
 assert(cache.Size()==Cache::Capacity);assert(allocations==before);
 std::printf("4096 entries, collisions, overflow without eviction: build probes=%llu storageBytes=%zu allocationsDuringQueries=0\n",static_cast<unsigned long long>(build.probes),Cache::StorageBytes());
 cache.BeginFrame();assert(cache.Size()==0&&cache.Store(1,1,Kind::Uniform,"overflow",9));
 const auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<200000;++i)assert(cache.Find(1,1,Kind::Uniform,"overflow",out)&&out==9);
 const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 assert(allocations==before);std::printf("200000 hot finds: %.3f ms (portable core only, no driver calls)\n",elapsed);
}
static void ValidatedAndFullHashCollision(){
 Cache cache;const auto before=allocations;int out=72;
 assert(cache.StoreValidated(1,1,Kind::Attribute,"Position",8,0));
 assert(cache.Find(1,1,Kind::Attribute,"Position",out)&&out==0);
 assert(cache.Store(1,1,Kind::Uniform,"TexCoord",4));
 assert(cache.FindValidated(1,1,Kind::Uniform,"TexCoord",8,out)&&out==4);
 out=72;
 assert(!cache.FindValidated(1,1,Kind::Uniform,"TexCoord",7,out)&&out==72);
 assert(!cache.FindValidated(1,1,Kind::Uniform,"TexCoord",Cache::NameBytes,out)&&out==72);
 assert(!cache.StoreValidated(1,1,Kind::Uniform,"",0,2));
 assert(!cache.StoreValidated(1,1,Kind::Uniform,nullptr,3,2));
 assert(!cache.StoreValidated(0,1,Kind::Uniform,"x",1,2));
 assert(!cache.StoreValidated(1,0,Kind::Uniform,"x",1,2));
 assert(!cache.StoreValidated(1,1,static_cast<Kind>(99),"x",1,2));
 assert(!cache.StoreValidated(1,1,Kind::Uniform,"TexCoord",8,-1));
 const char*a=sizeof(std::uintptr_t)==4?"Collision_105939":"Collision_35892";
 const char*b=sizeof(std::uintptr_t)==4?"Collision_174558":"Collision_66135";
 assert(highfps::shader_locations::CacheTestAccess::Hash(a)==highfps::shader_locations::CacheTestAccess::Hash(b));
 assert(cache.StoreValidated(1,1,Kind::Uniform,a,std::strlen(a),91));
 assert(cache.StoreValidated(1,1,Kind::Uniform,b,std::strlen(b),92));
 assert(cache.FindValidated(1,1,Kind::Uniform,a,std::strlen(a),out)&&out==91);
 assert(cache.FindValidated(1,1,Kind::Uniform,b,std::strlen(b),out)&&out==92);
 assert(cache.StoreValidated(1,1,Kind::Uniform,a,std::strlen(a),93));
 assert(cache.FindValidated(1,1,Kind::Uniform,b,std::strlen(b),out)&&out==92);
 char longest[Cache::NameBytes];std::memset(longest,'x',sizeof longest);longest[sizeof longest-1]=0;
 assert(cache.StoreValidated(1,1,Kind::Uniform,longest,sizeof longest-1,17));
 assert(cache.Find(1,1,Kind::Uniform,longest,out)&&out==17);
 cache.BeginFrame();assert(!cache.FindValidated(1,1,Kind::Uniform,a,std::strlen(a),out));
 assert(allocations==before);std::puts("validated-name bounds/interoperability, actual full-hash collision isolation, no allocations: passed");
}
static void DirectFrontSafety(){
 Cache cache;const auto allocationsBefore=allocations;
 const char*names[]={"Position","Color","TexCoord","ColorizeIn","ColorOffsetIn","TextureSize","PixelationAmount","ClipPlane","ChampionColorIn","Texture0","Transform"};
 for(unsigned p=1;p<=7;++p)for(unsigned n=0;n<11;++n)
  assert(cache.StoreValidated(0x12340000,p,n<9?Kind::Attribute:Kind::Uniform,names[n],std::strlen(names[n]),int(p*20+n)));
 int out=-1;
 // Warm all 77 keys once; subsequent exact hits must perform no hash-index probes.
 for(unsigned round=0;round<2;++round){const auto before=cache.GetStats();
  for(unsigned p=1;p<=7;++p)for(unsigned n=0;n<11;++n)
   assert(cache.FindValidated(0x12340000,p,n<9?Kind::Attribute:Kind::Uniform,names[n],std::strlen(names[n]),out)&&out==int(p*20+n));
  if(round){assert(cache.GetStats().probes==before.probes);assert(cache.GetStats().frontHits==before.frontHits+77);}
 }
 // Same first/last byte and length deliberately select one front slot. Exact
 // interior bytes remain authoritative, and misses use only one front candidate.
 assert(highfps::shader_locations::CacheTestAccess::Front(1,1,Kind::Attribute,"Position")==highfps::shader_locations::CacheTestAccess::Front(1,1,Kind::Attribute,"Positixn"));
 assert(cache.Store(1,1,Kind::Attribute,"Position",91)&&cache.Store(1,1,Kind::Attribute,"Positixn",92));
 for(unsigned round=0;round<20;++round){assert(cache.Find(1,1,Kind::Attribute,"Position",out)&&out==91);assert(cache.Find(1,1,Kind::Attribute,"Positixn",out)&&out==92);}
 assert(cache.Store(1,1,Kind::Attribute,"Positixn",0));
 assert(cache.Find(1,1,Kind::Attribute,"Positixn",out)&&out==0); // Updating authoritative entry updates front hits immediately.
 out=73;assert(!cache.Find(1,1,Kind::Attribute,"Positizn",out)&&out==73);
 assert(cache.Find(1,1,Kind::Attribute,"Positixn",out)&&out==0); // A miss cannot poison the front entry.
 assert(!cache.Store(1,1,Kind::Attribute,"Positixn",-1));assert(cache.Find(1,1,Kind::Attribute,"Positixn",out)&&out==0);
 // Deliberately alias the cheap context/program group. Full identity still wins.
 assert(cache.Store(1,17,Kind::Attribute,"Positixn",17));
 assert(cache.Store(0x1000001,1,Kind::Attribute,"Positixn",18));
 for(unsigned round=0;round<10;++round){assert(cache.Find(1,17,Kind::Attribute,"Positixn",out)&&out==17);assert(cache.Find(1,1,Kind::Attribute,"Positixn",out)&&out==0);assert(cache.Find(0x1000001,1,Kind::Attribute,"Positixn",out)&&out==18);}
 cache.ResetStats();assert(cache.Find(0x1000001,1,Kind::Attribute,"Positixn",out)&&out==18&&cache.GetStats().frontHits==1);
 cache.BeginFrame();out=65;assert(!cache.Find(0x1000001,1,Kind::Attribute,"Positixn",out)&&out==65);
 assert(cache.Store(1,1,Kind::Attribute,"Positixn",44)&&cache.Find(1,1,Kind::Attribute,"Positixn",out)&&out==44);
 highfps::shader_locations::CacheTestAccess::ForceNextGenerationWrap(cache);cache.Clear();
 assert(!cache.Find(1,1,Kind::Attribute,"Positixn",out));
 assert(cache.Store(1,1,Kind::Attribute,"Positixn",45)&&cache.Find(1,1,Kind::Attribute,"Positixn",out)&&out==45);
 // Non-hot names take the original index without adding any front candidate.
 assert(cache.Store(1,1,Kind::Uniform,"AbcZ",17));const auto beforeBypass=cache.GetStats().frontHits;
 for(unsigned i=0;i<20;++i)assert(cache.Find(1,1,Kind::Uniform,"AbcZ",out)&&out==17);
 assert(cache.GetStats().frontHits==beforeBypass);
 cache.Clear();assert(cache.Store(1,1,Kind::Attribute,"Position",61));
 assert(cache.Find(1,1,Kind::Attribute,"Position",out)&&out==61);
 for(unsigned i=1;i<Cache::Capacity;++i){char name[32];std::snprintf(name,sizeof name,"Capacity_%u",i);assert(cache.Store(1,1,Kind::Uniform,name,int(i)));}
 assert(!cache.Store(1,1,Kind::Uniform,"full",999));
 assert(cache.Find(1,1,Kind::Attribute,"Position",out)&&out==61);
 assert(cache.Store(1,1,Kind::Attribute,"Position",62));
 assert(cache.Find(1,1,Kind::Attribute,"Position",out)&&out==62);
 assert(allocations==allocationsBefore);
 std::puts("direct front: 77 hot keys skip hash probes, exact collision fallback, updates, zero, misses, clear/wrap: passed");
}
int main(){IdentityAndNoNegative();BoundsAndReset();CollisionOverflowAndNoAlloc();ValidatedAndFullHashCollision();DirectFrontSafety();}
