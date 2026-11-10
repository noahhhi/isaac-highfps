#include "entity_draw_cache_probe.h"
#include <cassert>
#include <cstdio>
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

int main(){
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
