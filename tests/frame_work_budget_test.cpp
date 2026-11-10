#include "../src/frame_work_budget.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
using namespace highfps::work_budget;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"CHECK line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
int main() {
    static_assert(static_cast<unsigned>(Kind::ParasitePair)==0);
    static_assert(static_cast<unsigned>(Kind::TechZeroSearch)==1);
    static_assert(static_cast<unsigned>(Kind::QuadSplit)==2);
    static_assert(static_cast<unsigned>(Kind::TrisagionCreate)==3);
    static_assert(static_cast<unsigned>(Kind::Poof)==4);
    static_assert(static_cast<unsigned>(Kind::BonePair)==5);
    static_assert(static_cast<unsigned>(Kind::LachryChild)==6);
    static_assert(static_cast<unsigned>(Kind::HaemoChild)==7);
    static_assert(KindCount==8);
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    // Baseline work below limits has no hash/phase rejection, even for many seeds.
    for(unsigned seed=0;seed<1024;++seed) {
        FrameWorkBudget p;
        CHECK(p.Begin(1,seed,3,{}));
        CHECK(p.Admit(Kind::ParasitePair,seed));
        CHECK(p.Admit(Kind::QuadSplit,seed));
        for(int i=0;i<4;++i) {
            CHECK(p.Admit(Kind::TechZeroSearch,seed));
            CHECK(p.Admit(Kind::TrisagionCreate,seed));
            CHECK(p.Admit(Kind::Poof,seed));
        }
        CHECK(!p.Admit(Kind::ParasitePair));
        CHECK(!p.Admit(Kind::TechZeroSearch));
        CHECK(!p.Admit(Kind::TrisagionCreate));
        CHECK(!p.Admit(Kind::Poof));
        CHECK(p.Snapshot().reserved.tears==6 && p.Snapshot().reserved.lasers==8);
    }
    // Shared child costs; a rejected four-child split does not partially commit.
    { FrameWorkBudget p;p.Begin(1,10,2,{});
      CHECK(p.Admit(Kind::QuadSplit));CHECK(!p.Admit(Kind::QuadSplit));
      CHECK(p.Snapshot().childrenRemaining==2);CHECK(p.Admit(Kind::ParasitePair));
      CHECK(p.Snapshot().reserved.tears==6); }
    // New item paths have exact child cost2/1/1 and all share the existing
    // Parasite/quad quota and reservation. Denied children never borrow quota.
    { FrameWorkBudget p;p.Begin(1,10,2,{});
      CHECK(p.Admit(Kind::BonePair));CHECK(p.Admit(Kind::LachryChild));
      CHECK(p.Admit(Kind::HaemoChild));CHECK(p.Admit(Kind::ParasitePair));
      CHECK(p.Snapshot().reserved.tears==6 && p.Snapshot().childrenRemaining==0);
      for(Kind k:{Kind::BonePair,Kind::LachryChild,Kind::HaemoChild,Kind::QuadSplit})
          CHECK(!p.Admit(k));
      CHECK(p.Stats().admitted[5]==1 && p.Stats().admitted[6]==1 && p.Stats().admitted[7]==1);
      CHECK(p.Stats().reasons[7][static_cast<unsigned>(Reason::TickQuota)]==1);
      p.Begin(1,11,2,{});CHECK(p.Admit(Kind::QuadSplit));CHECK(p.Admit(Kind::BonePair)); }
    for(Kind child:{Kind::LachryChild,Kind::HaemoChild}) {
        FrameWorkBudget p;p.Begin(1,1,1,{127,0,0,0,0});
        CHECK(!p.Admit(Kind::BonePair));CHECK(p.Snapshot().childrenRemaining==6);
        CHECK(p.Admit(child));CHECK(!p.Admit(child));
        CHECK(p.LastDecision()==Reason::LivePopulation);
        CHECK(p.Snapshot().reserved.tears==1);
        CHECK(!p.Begin(1,1,1,{}));CHECK(!p.Admit(child));
        p.Begin(1,2,1,{});for(int i=0;i<6;++i)CHECK(p.Admit(child));CHECK(!p.Admit(child));
        CHECK(p.LastDecision()==Reason::TickQuota);
    }
    { FrameWorkBudget p;p.Begin(1,1,1,{126,0,0,0,0});CHECK(p.Admit(Kind::BonePair));
      CHECK(!p.Admit(Kind::LachryChild));CHECK(!p.Admit(Kind::HaemoChild)); }
    // Live caps include conservative same-world reservations BEFORE allocation.
    { FrameWorkBudget p;p.Begin(1,10,2,{125,30,4,94});
      CHECK(p.Admit(Kind::ParasitePair));CHECK(!p.Admit(Kind::ParasitePair));
      CHECK(p.LastDecision()==Reason::LivePopulation);CHECK(p.Snapshot().childrenRemaining==4);
      CHECK(p.Admit(Kind::TechZeroSearch));CHECK(p.Admit(Kind::TrisagionCreate));
      CHECK(!p.Admit(Kind::TechZeroSearch));CHECK(p.Snapshot().reserved.lasers==2);
      CHECK(p.Admit(Kind::Poof));CHECK(p.Admit(Kind::Poof));CHECK(!p.Admit(Kind::Poof)); }
    // Enemy/unknown lasers filling the soft cap cannot entirely starve known
    // player work: shared player allowance is exactly four INCLUDING reservations.
    for(unsigned total:{32u,80u}) {
        FrameWorkBudget p;p.Begin(1,1,1,{0,total,0,0});
        CHECK(p.Admit(Kind::TechZeroSearch));CHECK(p.Admit(Kind::TrisagionCreate));
        CHECK(p.Admit(Kind::TechZeroSearch));CHECK(p.Admit(Kind::TrisagionCreate));
        CHECK(!p.Admit(Kind::TechZeroSearch));CHECK(!p.Admit(Kind::TrisagionCreate));
        CHECK(p.Snapshot().reserved.playerLasers==4);
    }
    { FrameWorkBudget p;p.Begin(1,1,1,{0,80,3,0});CHECK(p.Admit(Kind::TrisagionCreate));CHECK(!p.Admit(Kind::TechZeroSearch)); }
    // The minimum allowance cannot exceed the actual native total laser pool.
    { FrameWorkBudget p;p.Begin(1,1,1,{0,127,0,0});CHECK(p.Admit(Kind::TechZeroSearch));CHECK(!p.Admit(Kind::TrisagionCreate)); }
    { FrameWorkBudget p;p.Begin(1,1,1,{0,128,0,0});CHECK(!p.Admit(Kind::TechZeroSearch)); }
    // Cosmetic quota also respects the whole effect pool, not just poof count.
    { FrameWorkBudget p;p.Begin(1,1,1,{0,0,0,2,1023});
      CHECK(p.Admit(Kind::Poof));CHECK(!p.Admit(Kind::Poof));
      CHECK(p.Snapshot().reserved.effects==1);CHECK(p.Snapshot().poofsRemaining==3);
      CHECK(p.Stats().reasons[4][static_cast<unsigned>(Reason::LivePopulation)]==1);
      p.Begin(1,2,1,{0,0,0,0,1024});CHECK(!p.Admit(Kind::Poof));
      p.Begin(1,3,1,{0,0,0,0,max});CHECK(!p.Admit(Kind::Poof)); }
    // No same-world refill/population replacement, including nested hook entry
    // after an admitted request but BEFORE native allocation has occurred.
    { FrameWorkBudget p;p.Begin(1,20,2,{124,30,4,95});CHECK(p.Admit(Kind::QuadSplit));
      CHECK(!p.Begin(1,20,2,{},true,17));CHECK(p.Snapshot().sampled.tears==124);
      CHECK(!p.Admit(Kind::ParasitePair));CHECK(p.Snapshot().eligibilityDivisor==1);
      CHECK(p.Begin(1,21,2,{}));CHECK(p.Admit(Kind::QuadSplit));
      CHECK(p.Begin(1,19,2,{}));CHECK(p.Snapshot().reserved.tears==0);
      CHECK(p.Admit(Kind::QuadSplit));CHECK(!p.Begin(1,19,2,{}));
      CHECK(p.Begin(1,19,3,{}));CHECK(p.Snapshot().childrenRemaining==6);
      CHECK(p.Admit(Kind::QuadSplit));CHECK(p.Begin(2,19,3,{}));
      CHECK(p.Snapshot().reserved.tears==0);p.Reset();CHECK(!p.Admit(Kind::Poof));
      CHECK(p.LastDecision()==Reason::Uninitialized);CHECK(p.Begin(2,19,3,{}));CHECK(p.Admit(Kind::Poof)); }
    // Overflow, inconsistent subset count and oversized configurable limits
    // stay conservative; failed admissions never decrement quota or wrap counts.
    { Config c;c.childrenPerTick=~0u;c.techSearchesPerTick=~0u;c.trisCreatesPerTick=~0u;c.poofsPerTick=~0u;
      c.softTears=c.softLasers=c.softPoofs=c.minimumPlayerLasers=~0u;
      FrameWorkBudget p(c);p.Begin(max,max,~std::uintptr_t(0),{max,max,max,max,max});
      for(unsigned k=0;k<KindCount;++k)CHECK(!p.Admit(static_cast<Kind>(k)));
      CHECK(p.Snapshot().childrenRemaining==~0u);CHECK(p.Snapshot().reserved.tears==0);
      CHECK(p.Limits().softTears==512 && p.Limits().softLasers==128 && p.Limits().softPoofs==1024);
      p.Begin(0,0,0,{});for(unsigned i=0;i<128;++i)CHECK(p.Admit(Kind::QuadSplit));CHECK(!p.Admit(Kind::QuadSplit));
      for(unsigned i=0;i<128;++i) { CHECK(p.Admit(Kind::TechZeroSearch)); }
      CHECK(!p.Admit(Kind::TrisagionCreate));
      for(unsigned i=0;i<1024;++i) { CHECK(p.Admit(Kind::Poof)); }
      CHECK(!p.Admit(Kind::Poof)); }
    { FrameWorkBudget p;p.Begin(1,1,1,{0,0,max,0});CHECK(p.Snapshot().sampled.lasers==max);CHECK(!p.Admit(Kind::TechZeroSearch)); }
    { Config c;c.childrenPerTick=c.techSearchesPerTick=c.trisCreatesPerTick=c.poofsPerTick=0;
      FrameWorkBudget p(c);p.Begin(1,1,1,{});for(unsigned k=0;k<KindCount;++k)CHECK(!p.Admit(static_cast<Kind>(k))); }
    // Rotation is explicit overload-only, never automatic feedback. Denied
    // eligibility does not spend quota. Every seed rotates through a 7-tick span.
    for(unsigned seed=0;seed<100;++seed) {
        FrameWorkBudget p;unsigned accepted=0;
        for(unsigned stamp=0;stamp<7;++stamp) {
            p.Begin(1,stamp,1,{},true,7);
            if(p.Admit(Kind::ParasitePair,seed))++accepted;
            else {CHECK(p.LastDecision()==Reason::SevereEligibility);CHECK(p.Snapshot().childrenRemaining==6);}
        }
        CHECK(accepted==1);
        p.Begin(1,8,1,{},false,~0u);CHECK(p.Admit(Kind::QuadSplit,seed));
        p.Begin(1,9,1,{},true,0);CHECK(p.Admit(Kind::QuadSplit,seed));
        p.Begin(1,max,1,{},true,~0u);p.Admit(Kind::Poof,seed);
    }
    { FrameWorkBudget p;CHECK(!p.Admit(static_cast<Kind>(~0u)));CHECK(p.LastDecision()==Reason::InvalidKind);
      CHECK(p.Stats().invalidRequests==1);p.Begin(1,1,1,{});CHECK(p.Admit(Kind::Poof));
      p.Reset();CHECK(p.Stats().admitted[4]==1);p.ClearCounters();CHECK(p.Stats().admitted[4]==0); }
    std::puts("PASS frame work budget: atomic shared quotas, conservative live reservations, player minimum, lifecycle/no-refill, explicit rotation, overflow");
}
