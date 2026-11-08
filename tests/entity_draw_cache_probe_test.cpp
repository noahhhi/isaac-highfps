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
int main(){
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
