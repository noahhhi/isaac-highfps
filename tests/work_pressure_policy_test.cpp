#include "work_pressure_policy.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace highfps::pressure;
struct Simulation {
 Policy policy;Context c;double time=0;std::uint64_t tick=0;
 explicit Simulation(Config config={}):policy(config){c.epoch=1;c.room=0x12340000;c.population={1,0,0,0};}
 Decision Step(double fixed=.003,double pure=.003,double hz=90,bool eligible=true){
  time+=1.0/30;c.now=time;c.world=++tick;c.targetHz=hz;
  // Two authoritative samples per30Hz world step; pure samples represent the
  // separately timed presentation stream. Evaluate remains world-latched.
  policy.ObserveFixed(c.epoch,c.room,time-1.0/60,fixed,eligible,fixed*.3,fixed*.6);
  policy.ObservePure(c.epoch,c.room,time-1.0/60,pure,eligible);
  policy.ObserveFixed(c.epoch,c.room,time,fixed,eligible,fixed*.3,fixed*.6);
  policy.ObservePure(c.epoch,c.room,time,pure,eligible);
  return policy.Evaluate(c);
 }
};
static void TargetRatesAndOrdinaryBurst(){
 for(double hz:{60.,80.,90.,144.}){
  Simulation s;Decision d;for(unsigned i=0;i<35;++i)d=s.Step(.003,.009,hz);
  assert(d.valid&&d.fixedKnown&&d.pureKnown&&d.fixedBreakdownKnown);
  assert(std::abs(d.estimatedWorkDemand-(.18+.009*hz))<1e-9);
  assert(std::abs(d.updateWorkSeconds-.0009)<1e-9&&std::abs(d.canonicalRenderWorkSeconds-.0018)<1e-9);
  if(hz==60)assert(!d.budgetAttacks&&!d.budgetCosmetics);
  if(hz==80)assert(!d.budgetAttacks&&d.budgetCosmetics);
  if(hz==90)assert(d.severity==Severity::Attack);
  if(hz==144)assert(d.severity==Severity::Severe);
 }
 Simulation normal;normal.c.priorRequests.children=11;
 for(unsigned i=0;i<180;++i){auto d=normal.Step();assert(!d.budgetAttacks&&!d.budgetCosmetics&&d.projectedTears==12);}
 // Controller chooses VRR maximum/target, not measured low instantaneous FPS.
 Simulation vrr;for(unsigned i=0;i<35;++i)vrr.Step(.003,.009,90);assert(vrr.policy.Current().budgetAttacks);
 auto held=vrr.policy.Current();auto same=vrr.c;same.targetHz=60;assert(vrr.policy.Evaluate(same).severity==held.severity);
 std::puts("pressure:60/80/90/VRR demand, optional component diagnostics, low-load Haemo11 unchanged: passed");
}
static void SingleSpikeLifecycleRecovery(){
 Simulation s;for(unsigned i=0;i<40;++i)s.Step();
 // Exactly one outlier in EACH phase: one maximum is trimmed independently.
 s.time+=.01;s.policy.ObserveFixed(1,s.c.room,s.time,.5);s.policy.ObservePure(1,s.c.room,s.time,.5);
 s.c.now=s.time;s.c.world=++s.tick;assert(!s.policy.Evaluate(s.c).budgetAttacks);
 for(unsigned i=0;i<120;++i)assert(!s.Step().budgetAttacks);
 // Ineligible room-loading spans do not become cost pressure.
 for(unsigned i=0;i<20;++i)assert(!s.Step(.5,.5,90,false).budgetAttacks);
 for(unsigned i=0;i<35;++i)s.Step(.02,.01);assert(s.policy.Current().severity==Severity::Severe);
 for(unsigned i=0;i<240;++i)s.Step();assert(s.policy.Current().severity==Severity::None);
 auto changed=s.c;changed.epoch++;changed.room++;changed.world=0;changed.now+=.01;
 auto clean=s.policy.Evaluate(changed);assert(clean.severity==Severity::None&&!clean.fixedKnown&&!clean.pureKnown);
 clean=s.policy.Evaluate(Context{});assert(!clean.valid&&!clean.budgetAttacks);
 std::puts("pressure: single spike trimmed, lifecycle excluded, gradual recovery, scope reset: passed");
}
static void PredictionAndDeniedDemand(){
 Simulation s;s.c.population.tears=30;s.c.priorRequests.children=300;
 auto d=s.Step();assert(d.severity==Severity::Severe&&d.populationCause); // Before allocation, even without warmup.
 // Caps lower cost/population, but unfulfilled demand remains visible. Do not
 // alternate budget off/on just because the budget itself made work cheaper.
 s.c.population.tears=32;s.c.priorRequests.children=160;
 for(unsigned i=0;i<150;++i){d=s.Step(.002,.002);assert(d.budgetAttacks);}
 s.c.priorRequests={};for(unsigned i=0;i<240;++i)d=s.Step(.002,.002);assert(!d.budgetAttacks&&!d.budgetCosmetics);
 Simulation fx;fx.c.priorRequests.poofs=100;d=fx.Step();assert(d.budgetCosmetics&&!d.budgetAttacks);
 Simulation search;search.c.priorRequests.techSearches=40;d=search.Step();assert(d.budgetAttacks&&d.projectedLasers==0&&d.searchRequests==40);
 Simulation first;d=first.Step();assert(!d.budgetAttacks); // Unreported first burst is inherently unseen.
 first.c.priorRequests.children=300;d=first.Step();assert(d.budgetAttacks); // One-world latency, not hard-cap guarantee.
 // Custom lower thresholds must not recover below continuing raw pressure.
 Config custom;custom.attackTears=20;custom.severeTears=40;Simulation tuned(custom);tuned.c.population.tears=25;
 for(unsigned i=0;i<180;++i)assert(tuned.Step(.001,.001).budgetAttacks);
 std::puts("pressure: pre-flood prediction, denied demand retains pressure, search != birth, one-world latency: passed");
}
static void LedgerAndBoundaries(){
 RequestLedger ledger;assert(!ledger.Observe(RequestKind::HaemoChild,1));assert(ledger.Begin(1,10,7));
 for(unsigned i=0;i<11;++i)assert(ledger.Observe(RequestKind::HaemoChild,1));
 assert(ledger.Observe(RequestKind::BonePair,2));assert(ledger.Observe(RequestKind::QuadSplit,4));assert(ledger.Observe(RequestKind::TechZeroSearch,1));assert(ledger.Observe(RequestKind::TrisagionCreate,1));
 assert(!ledger.Begin(1,10,7)&&ledger.Snapshot().children==17&&ledger.Snapshot().techSearches==1&&ledger.Snapshot().trisCreates==1);
 assert(!ledger.Observe(RequestKind::QuadSplit,1));assert(!ledger.Observe(static_cast<RequestKind>(99),1));
 assert(ledger.Begin(1,11,7)&&ledger.Snapshot().children==0);ledger.Reset();assert(!ledger.Observe(RequestKind::Poof,1));
 const auto max=(std::numeric_limits<std::uint64_t>::max)();assert(Add(max-1,7)==max);
 Simulation huge;huge.c.population.tears=max;huge.c.priorRequests.children=max;auto d=huge.Step();assert(d.projectedTears==max&&d.budgetAttacks);
 Simulation invalid;for(unsigned i=0;i<40;++i)invalid.Step();
 invalid.c.now+=.01;invalid.c.world++;invalid.c.targetHz=std::numeric_limits<double>::quiet_NaN();assert(!invalid.policy.Evaluate(invalid.c).valid);
 invalid.policy.ObserveFixed(1,invalid.c.room,invalid.c.now,1e308);invalid.policy.ObservePure(1,invalid.c.room,invalid.c.now,-1);
 invalid.c.world++;invalid.c.now+=1;invalid.c.targetHz=90;d=invalid.policy.Evaluate(invalid.c);assert(!d.fixedKnown&&!d.pureKnown&&!d.budgetAttacks);
 auto backwards=invalid.c;backwards.world=0;backwards.now+=.1;d=invalid.policy.Evaluate(backwards);assert(d.severity==Severity::None);
 Simulation unknown;unknown.c.populationValid=false;unknown.c.population.tears=max;assert(!unknown.Step().budgetAttacks);
 std::puts("pressure ledger: same-world no reset, exact cost units, saturating sums, stale/invalid/backward scopes: passed");
}
static void MissingPhaseAndBrokenEvidence(){
 Simulation s;for(unsigned i=0;i<40;++i)s.Step(.02,.01);assert(s.policy.Current().severity==Severity::Severe);
 // Cheap fixed work without current pure measurements is only a lower bound.
 for(unsigned i=0;i<180;++i){s.time+=1.0/30;s.c.now=s.time;s.c.world=++s.tick;
  s.policy.ObserveFixed(1,s.c.room,s.time,.002);auto d=s.policy.Evaluate(s.c);
  if(i>30)assert(d.fixedKnown&&!d.pureKnown&&d.severity==Severity::Severe);
 }
 for(unsigned i=0;i<240;++i)s.Step(.002,.002);assert(s.policy.Current().severity==Severity::None);
 // A lifecycle-ineligible sample must break an almost-complete quiet period.
 Simulation q;for(unsigned i=0;i<40;++i)q.Step(.02,.01);
 for(unsigned i=0;i<50;++i)q.Step(.002,.002);const auto before=q.policy.Current().severity;
 q.Step(.5,.5,90,false);for(unsigned i=0;i<30;++i)q.Step(.002,.002);assert(q.policy.Current().severity==before);
 // Entry streak also cannot bridge an excluded span.
 Simulation e;for(unsigned i=0;i<5;++i)e.Step(.003,.009,90);assert(!e.policy.Current().budgetAttacks);
 e.Step(.003,.009,90,false);for(unsigned i=0;i<4;++i)e.Step(.003,.009,90);assert(!e.policy.Current().budgetAttacks);
 Context menu=e.c;menu.room=0;assert(!e.policy.Evaluate(menu).valid&&e.policy.Current().severity==Severity::None);
 std::puts("pressure evidence: missing phase cannot prove recovery; lifecycle breaks entry/quiet streaks: passed");
}
static void UrgentLowRate(){
 Policy p;Context c;c.epoch=1;c.room=7;c.targetHz=90;c.now=.25;c.world=1;
 p.ObserveFixed(1,7,c.now,.05);auto d=p.Evaluate(c);assert(!d.urgentWork&&!d.budgetAttacks);
 c.now=.5;c.world=2;p.ObserveFixed(1,7,c.now,.05);d=p.Evaluate(c);
 assert(d.urgentWork&&d.budgetAttacks&&d.severity==Severity::Severe&&!d.fixedKnown);
 // Excluded work and age gaps break the pair even when values would be huge.
 Policy gap;c.now=1;c.world=3;gap.ObserveFixed(1,7,1,.05);gap.ObserveFixed(1,7,1.1,.5,false);
 gap.ObserveFixed(1,7,1.2,.05);c.now=1.2;c.world=4;assert(!gap.Evaluate(c).urgentWork);
 gap.Reset();gap.ObserveFixed(1,7,2,.05);gap.ObserveFixed(1,7,4.1,.05);c.now=4.1;c.world=5;assert(!gap.Evaluate(c).urgentWork);
 gap.Reset();gap.ObserveFixed(1,7,5,.05);gap.ObserveFixed(1,7,5.1,.05);c.now=6;c.world=6;assert(!gap.Evaluate(c).urgentWork);
 for(double hz:{60.,90.}){Policy pure;Context x;x.epoch=1;x.room=7;x.targetHz=hz;x.now=1;x.world=1;
  pure.ObservePure(1,7,.9,.015);pure.ObservePure(1,7,1,.015);auto z=pure.Evaluate(x);
  assert(z.urgentWork==(hz==90)); //15ms pure is severe at90, below20ms at60.
 }
 Policy alternating;Context alt;alt.epoch=1;alt.room=9;alt.targetHz=90;double t=0;
 for(unsigned i=0;i<4;++i){const double cost=i%2?.001:.6;t+=cost;alternating.ObserveFixed(1,9,t,cost);alt.now=t;alt.world=i+1;alternating.Evaluate(alt);}
 assert(alternating.Current().urgentWork&&alternating.Current().budgetAttacks&&!alternating.Current().fixedKnown);
 Policy notSevere;t=0;for(unsigned i=0;i<4;++i){const double cost=i%2?.001:.020;t+=cost;notSevere.ObserveFixed(1,9,t,cost);alt.now=t;alt.world=i+1;notSevere.Evaluate(alt);}
 assert(!notSevere.Current().urgentWork);
 Policy longWork;longWork.ObserveFixed(1,9,1.2,1.2);longWork.ObserveFixed(1,9,1.201,.001);longWork.ObserveFixed(1,9,2.401,1.2);
 alt.now=2.401;alt.world=1;assert(longWork.Evaluate(alt).urgentWork&&longWork.Stats().clippedCosts==2);
 // At an otherwise-due recovery boundary, urgent work overrides the low
 // trimmed mean and restarts quiet evidence instead of down/up oscillation.
 Simulation recovery;for(unsigned i=0;i<40;++i)recovery.Step(.02,.01);bool checked=false;
 for(unsigned i=0;i<180&&!checked;++i){auto trial=recovery.policy;auto next=recovery.c;next.now=recovery.time+1.0/30;next.world=recovery.tick+1;
  trial.ObserveFixed(1,next.room,next.now,.002);trial.ObservePure(1,next.room,next.now,.002);const auto predicted=trial.Evaluate(next);
  if(unsigned(predicted.severity)<unsigned(recovery.policy.Current().severity)){
   recovery.policy.ObservePure(1,next.room,next.now-.001,.03);recovery.policy.ObserveFixed(1,next.room,next.now,.002);recovery.policy.ObservePure(1,next.room,next.now,.03);
   const auto actual=recovery.policy.Evaluate(next);assert(actual.urgentWork&&actual.severity==Severity::Severe);checked=true;
  }else recovery.Step(.002,.002);
 }
 assert(checked);
 std::puts("pressure urgent pair: low-rate overload, one spike ignored, excluded/aged evidence, target-scaled pure threshold: passed");
}
int main(){TargetRatesAndOrdinaryBurst();SingleSpikeLifecycleRecovery();PredictionAndDeniedDemand();LedgerAndBoundaries();MissingPhaseAndBrokenEvidence();UrgentLowRate();}
