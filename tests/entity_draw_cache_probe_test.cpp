#include "entity_draw_cache_probe.h"
#include <cassert>
#include <cstdio>
#include <chrono>
using namespace highfps_entity_probe;
struct Fake:Backend {
    State state; bool allowReplay=true;int live=0,submitted=0; std::uint8_t buffer[64]{};
    State ReadState() override { return state; }
    bool ContextCompatible(const State&a,const State&b)override{return a.word[0]==b.word[0];}
    bool Describe(void*,Layout& l,State& s) override { l={16,0,{5,1}};s=state;return true; }
    std::shared_ptr<void> Retain(void* p) override {++live;return {p,[this](void*){--live;}};}
    bool CanReplay(const Command& c) override {return allowReplay && c.layout.Valid();}
    const char* ReplayFailure()const override{return "mock-resource";}
 void ApplyState(const State& s) override {state=s;}
    std::uint8_t* Submit(const Quad&) override {++submitted;for(int i=0;i<4;++i){float z=100.f+i;std::memcpy(buffer+i*16+8,&z,4);}return buffer;}
};
struct ResourceFake:Backend {
 State global;unsigned format=1;bool finalSupported=true;int live=0,submits=0;bool failSubmit=false;std::uint8_t buffer[80]{};
 std::vector<unsigned> prepared;std::vector<float> observed;
 State ReadState()override{return global;}
 bool ContextCompatible(const State&a,const State&b)override{return a.word[0]==b.word[0];}
 bool DescribeResource(void*,Layout&l,State&s)override{l=format==1?Layout{16,0,{5,1}}:Layout{20,0,{5,2}};s.word[20]=format;return finalSupported;}
 bool Describe(void*p,Layout&l,State&s)override{s=global;return DescribeResource(p,l,s);}
 bool HasResourceState()const override{return true;}
 bool SameResource(const Command&a,const Command&b)const override{return a.layout==b.layout&&a.state.word[20]==b.state.word[20];}
 std::shared_ptr<void>Retain(void*p)override{++live;return{p,[this](void*){--live;}};}
 bool CanReplay(const Command&c)override{return c.resourceOnly || c.vertices.size()==4*c.layout.stride;}
 bool PrepareResource(const Command&c)override{if(unsigned(submits)>observed.size()){float value;std::memcpy(&value,buffer+12,4);observed.push_back(value);}format=unsigned(c.state.word[20]);prepared.push_back(format);return true;}
 void ApplyState(const State&s)override{global=s;}
 std::uint8_t*Submit(const Quad&)override{++submits;return failSubmit?nullptr:buffer;}
};
static void TestResourceOrdering(){
 ResourceFake b;Cache cache(b);Key key{};Quad q;q.image=&b;
 assert(cache.Begin(key));bool draw=cache.BeforeQuad(q);cache.AfterQuad(draw,b.buffer);
 float value=123;std::memcpy(b.buffer+12,&value,4);
 // Exactly the setter hook's order: seal BEFORE format/buffer changes.
 cache.SealPending();b.format=2;std::memset(b.buffer,0,sizeof b.buffer);
 draw=cache.BeforeQuad(q);cache.AfterQuad(draw,b.buffer);value=456;std::memcpy(b.buffer+12,&value,4);
 cache.SealPending();b.format=1;b.global.word[9]=88;cache.End();
 assert(cache.GetCounts().commands==3&&b.live==2);
 assert(cache.Replay(key));assert((b.prepared==std::vector<unsigned>{1,2,1}));
 assert(b.submits==2&&b.format==1&&b.global.word[9]==88);
 assert((b.observed==std::vector<float>{123,456}));
 b.failSubmit=true;b.prepared.clear();b.format=2;
 assert(!cache.Replay(key));assert(std::strcmp(cache.LastReplay().reason,"native-submit-null")==0);
 assert(b.format==1&&b.prepared.back()==1);b.failSubmit=false;
 cache.Clear();assert(b.live==0);
 assert(cache.Begin(key));draw=cache.BeforeQuad(q);cache.AfterQuad(draw,b.buffer);b.finalSupported=false;cache.End();
 assert(!cache.Replay(key));assert(std::strcmp(cache.LastReplay().captureReject,"unsupported-final-image-state")==0&&b.live==0);
 std::puts("ordered shared-image formats, final resource state, delayed seal and unsupported final format: passed");
}

struct NoopBackend:Backend {
 std::uint64_t reads=0,applies=0;
 State ReadState()override{++reads;return {};}
 bool Describe(void*,Layout&,State&)override{assert(false);return false;}
 std::shared_ptr<void>Retain(void*)override{assert(false);return {};}
 bool CanReplay(const Command&)override{assert(false);return false;}
 void ApplyState(const State&)override{++applies;}
 std::uint8_t*Submit(const Quad&)override{assert(false);return nullptr;}
};
static Key PoolKey(unsigned i){
 // 1024 native-effect identities can occupy MORE than 1024 draw-cache keys
 // because different passes/ordinals are separate. No native pool is resized.
 Key k;k.epoch=1;k.entity=0x10000+(i%1024)*0x1000;k.seed=55+i%1024;
 k.type=1000;k.variant=15;k.generation=(std::uint64_t(k.seed)<<32)^(std::uint64_t(k.type)*0x9e3779b1u)^k.variant;k.callback=56;k.pass=i/1024;return k;
}
static void CaptureNoop(Cache&c,const Key&k){assert(c.Begin(k));c.End();}
static void TestLargeNoopIndex(){
 for(unsigned n:{128u,1200u,16384u}){
  NoopBackend b;Cache c(b);std::uint64_t comparisons=0;
  const auto begin=std::chrono::steady_clock::now();
  for(unsigned frame=1;frame<=8;++frame){c.SetCanonicalFrame(frame);for(unsigned i=0;i<n;++i)CaptureNoop(c,PoolKey(i));
   const auto reads=b.reads;c.ResetOperations();
   for(unsigned repeat=0;repeat<2;++repeat)for(unsigned i=0;i<n;++i)assert(c.Replay(PoolKey(i)));
   const auto work=c.GetOperations();assert(work.lineageLookups==0&&work.exactLookups==2*n);
   // Count actual hash-table equality comparisons, not just wrapper calls.
   assert(work.comparisons<8ull*n);comparisons+=work.comparisons;
   assert(b.reads==reads&&b.applies==0);assert(c.GetCounts().noOps==n&&c.GetCounts().entries==n);
   assert(c.GetCounts().bytes<=c.GetLimits().bytes);
  }
  if(n==16384){
   for(unsigned i=n;i<n+32;++i)CaptureNoop(c,PoolKey(i));
   assert(c.GetCounts().entries==n&&c.GetOperations().rejected==32);
   for(unsigned i=0;i<n;++i)assert(c.Replay(PoolKey(i)));
   for(unsigned i=n;i<n+32;++i)assert(!c.Replay(PoolKey(i)));
  }
  const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
  std::printf("indexed no-op keys=%u frames=8 pureHits=%u missing=0 comparisons=%llu trackedBytes=%zu elapsedMs=%.3f\n",n,16*n,static_cast<unsigned long long>(comparisons),c.GetCounts().bytes,ms);
 }
}
static void TestRecycledPointerHash(){
 NoopBackend b;Cache c(b);c.SetCanonicalFrame(1);
 auto key=[](unsigned i){auto k=PoolKey(0);k.seed=100+i;k.generation=(std::uint64_t(k.seed)<<32)^(std::uint64_t(k.type)*0x9e3779b1u)^k.variant;return k;};
 for(unsigned i=0;i<4096;++i)CaptureNoop(c,key(i));c.ResetOperations();
 for(unsigned i=0;i<4096;++i)assert(c.Replay(key(i)));
 assert(c.GetOperations().comparisons<4*4096&&c.GetOperations().lineageLookups==0);
 std::printf("recycled pointer with 4096 native seed/generation identities: %llu comparisons\n",static_cast<unsigned long long>(c.GetOperations().comparisons));
}
static void TestCapacityAndLineage(){
 NoopBackend b;Cache c(b,Cache::Limits{4,1024*1024});c.SetCanonicalFrame(1);
 Key a=PoolKey(0),a2=a,a3=a;a2.pass=1;a3.pass=2;
 CaptureNoop(c,a);CaptureNoop(c,a2);CaptureNoop(c,PoolKey(1));CaptureNoop(c,PoolKey(2));
 CaptureNoop(c,PoolKey(3));assert(c.GetCounts().entries==4);assert(!c.Replay(PoolKey(3)));
 for(auto k:{a,a2,PoolKey(1),PoolKey(2)})assert(c.Replay(k)); // Fresh-frame admissions are pinned.
 assert(c.GetOperations().rejected==1);
 assert(!c.Replay(a3));assert(c.LastReplay().entitySeen&&c.LastReplay().priorPass==1);
 c.SetCanonicalFrame(2);CaptureNoop(c,a); // Refresh exact key and move lineage latest in O(1).
 assert(!c.Replay(a3));assert(c.LastReplay().lastFrame==2&&c.LastReplay().priorPass==0);
 CaptureNoop(c,PoolKey(3));assert(!c.Replay(a2));assert(c.LastReplay().entitySeen&&c.LastReplay().lastFrame==2);
 c.SetCanonicalFrame(3);for(unsigned i=4;i<8;++i)CaptureNoop(c,PoolKey(i));
 assert(!c.Replay(a3)&&!c.LastReplay().entitySeen); // Last member's removal erases lineage too.
 assert(c.GetCounts().entries==4);c.SetCanonicalFrame(0);assert(c.GetCounts().entries==0); // Frame reset cannot reuse old provenance.
 std::puts("capacity admission, old-frame eviction, refreshed lineage, final-group removal and frame reset: passed");
}
static void TestByteBudgetAndReferences(){
 Fake b;Cache c(b,Cache::Limits{64,24000});Quad q;q.image=&b;
 auto draw=[&](Key key,unsigned count){assert(c.Begin(key));for(unsigned i=0;i<count;++i){const bool ok=c.BeforeQuad(q);c.AfterQuad(ok,b.buffer);}c.End();};
 c.SetCanonicalFrame(1);draw(PoolKey(0),1);assert(b.live==1);assert(c.Replay(PoolKey(0)));
 c.SetCanonicalFrame(2);draw(PoolKey(63),64); // Impossible-size admission must not flush unrelated older captures.
 assert(b.live==1&&c.Replay(PoolKey(0))&&!c.Replay(PoolKey(63))&&c.GetOperations().evictions==0);
 // Replacing a valid draw with an oversized capture must drop old references
 // and publish a diagnostic rejection, never display the stale prior result.
 draw(PoolKey(0),64);assert(b.live==0);assert(!c.Replay(PoolKey(0)));
 assert(std::strcmp(c.LastReplay().captureReject,"cache-capacity-or-byte-budget")==0);
 assert(c.GetCounts().bytes<=c.GetLimits().bytes);
 for(unsigned frame=3;frame<=12;++frame){c.SetCanonicalFrame(frame);draw(PoolKey(frame),8);assert(c.Replay(PoolKey(frame)));assert(c.GetCounts().bytes<=c.GetLimits().bytes);assert(std::size_t(b.live)==c.GetCounts().refs);}
 assert(c.GetOperations().evictions>0);c.Clear();assert(b.live==0&&c.GetCounts().commands==0&&c.GetCounts().refs==0);
 std::puts("tracked byte budget, stale-replacement rejection and native reference release under churn: passed");
}

int main(){
 TestLargeNoopIndex();TestRecycledPointerHash();TestCapacityAndLineage();TestByteBudgetAndReferences();
 TestResourceOrdering();
 Fake b; Cache c(b); Key key{1,22,0x1234,32,0,0}; Quad q;q.image=&b;
 c.SetCanonicalFrame(11);assert(c.Begin(key));bool captured=c.BeforeQuad(q);assert(captured);c.AfterQuad(captured,b.buffer);
 // Caller writes extra shader attribute AFTER native Image::Render returned.
 float extra=42,oldz=-80;std::memcpy(b.buffer+12,&extra,4);std::memcpy(b.buffer+8,&oldz,4);
 b.state.word[9]=77; c.End();assert(b.live==1);std::memset(b.buffer,0,sizeof b.buffer);assert(c.Replay(key));
 float got,z;std::memcpy(&got,b.buffer+12,4);std::memcpy(&z,b.buffer+8,4);assert(got==42 && z==100);assert(b.state.word[9]==77);
 Key missing=key;missing.pass=1;assert(!c.Replay(missing));assert(std::strcmp(c.LastReplay().reason,"missing-key")==0);assert(c.LastReplay().entitySeen&&c.LastReplay().lastFrame==11&&c.LastReplay().priorPass==0);
 b.allowReplay=false;assert(!c.Replay(key));assert(std::strcmp(c.LastReplay().reason,"mock-resource")==0);b.allowReplay=true;
 assert(c.Begin(key));captured=c.BeforeQuad(q);c.AfterQuad(captured,b.buffer);c.Unsafe(56);c.End();
 assert(!c.Replay(key));assert(std::strcmp(c.LastReplay().reason,"invalid-capture")==0&&c.LastReplay().unsafeCode==56);assert(b.live==0);c.Clear();assert(!c.Replay(key));assert(!c.LastReplay().entitySeen);
 assert(c.Begin(key));captured=c.BeforeQuad(q);c.Unsafe(99);c.AfterQuad(captured,reinterpret_cast<std::uint8_t*>(1));c.End();assert(!c.Replay(key));
 std::puts("capture delayed vertex writes, native depth, resource release, unsupported fail-closed: passed");
}
