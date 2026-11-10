#include "overload_policy.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using highfps::FixedTimeline;using highfps::OverloadPolicy;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"CHECK failed line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void Near(double a,double b){CHECK(std::abs(a-b)<1e-8);}
static void Step(OverloadPolicy& p,FixedTimeline& c,double at,double cost,bool eligible=true){
    CHECK(c.Due(at));CHECK(p.BeginFixedStep(at));c.Consume(at);p.ObserveFixedCost(cost,eligible);
}
static double Enter(OverloadPolicy& p,FixedTimeline& c){
    double at=0;
    for(int i=0;i<3;++i){auto f=p.BeginFrame(at,c);CHECK(f.transition==OverloadPolicy::Transition::None);CHECK(!p.ShouldNative());Step(p,c,at,.025);CHECK(!p.CanStartFixedStep(at+.025));CHECK(!p.ShouldNative());at+=.045;}
    CHECK(p.PendingTransition()==OverloadPolicy::Transition::EnterNative);
    auto f=p.BeginFrame(at,c);CHECK(f.transition==OverloadPolicy::Transition::EnterNative);CHECK(p.ShouldNative());CHECK(f.clockRebased);CHECK(f.discardedScheduleSeconds>0);Near(c.next,at);return at;
}
int main(){
    // One cold first shot never changes mode, and observed cost alone never
    // rewrites the schedule. A cheap following step breaks the heavy streak.
    {OverloadPolicy p;FixedTimeline c;p.BeginFrame(0,c);Step(p,c,0,.100);Near(c.next,FixedTimeline::period);CHECK(p.HeavyRun()==1);CHECK(!p.ShouldNative());p.BeginFrame(.2,c);Step(p,c,.2,.002);CHECK(p.HeavyRun()==0);CHECK(!p.ShouldNative());CHECK(p.Stats().clockRebases==0);}
    // Known expensive work cannot start another pass under a16.7ms budget.
    {OverloadPolicy p;FixedTimeline c;p.BeginFrame(1,c);Step(p,c,1,.014);CHECK(!p.CanStartFixedStep(1.014));CHECK(p.StepsThisFrame()==1);}
    // Configuration cannot enlarge the public16.7ms/16step ceiling.
    {OverloadPolicy::Config cfg;cfg.frameBudgetSeconds=10;cfg.maxAdaptiveSteps=100;OverloadPolicy p(cfg);FixedTimeline c;p.BeginFrame(1,c);Step(p,c,1,.014);CHECK(!p.CanStartFixedStep(1.014));}
    // External stalls retain ALL clock debt in adaptive mode, including either
    // side of the old250ms boundary. Budgeted batches eventually consume it.
    for(double stall:{.030,.080,.200,.230,.249,.251,.500}){
        OverloadPolicy p;FixedTimeline c;p.BeginFrame(0,c);Step(p,c,0,.001);const double original=c.next;
        unsigned consumed=0,batches=0;
        while(c.Due(stall)){
            double wall=stall+batches*.020;auto f=p.BeginFrame(wall,c);CHECK(!f.clockRebased);CHECK(!p.ShouldNative());
            while(c.Due(stall)&&p.CanStartFixedStep(wall)){Step(p,c,wall,.002);wall+=.002;++consumed;}
            CHECK(++batches<10);
        }
        CHECK(consumed==static_cast<unsigned>(std::floor(stall/FixedTimeline::period+1e-8)));Near(c.next,original+consumed*FixedTimeline::period);CHECK(p.Stats().clockRebases==0);
    }
    // Native overload: one indivisible canonical cycle per frame, max60starts/s,
    // no accumulating backlog. Heavy26ms work is not followed by another16.7ms
    // policy delay: cadence is limited from START, as native slowdown requires.
    {OverloadPolicy p;FixedTimeline c;double at=Enter(p,c);double previous=-1;unsigned updates=0,renders=0,gc=0;
        for(int i=0;i<100;++i){p.BeginFrame(at,c);CHECK(p.ShouldNative());CHECK(c.Due(at));if(previous>=0)CHECK(at-previous>=FixedTimeline::period);Step(p,c,at,.026);++updates;++renders;++gc;CHECK(!p.CanStartFixedStep(at+.026));previous=at;at+=.026;}
        CHECK(updates==renders&&renders==gc);CHECK(p.Stats().nativeSteps==100);CHECK(p.Stats().enteredNative==1);CHECK(p.PendingTransition()==OverloadPolicy::Transition::None);
    }
    // Hysteresis:59cheap steps cannot recover. A non-cheap or lifecycle sample
    // resets the streak. The60th only queues recovery, preserving visible mode
    // for that frame; the next frame cannot run an early extra step.
    {OverloadPolicy p;FixedTimeline c;double at=Enter(p,c);
        for(int i=0;i<59;++i){at=std::max(at,c.next);p.BeginFrame(at,c);Step(p,c,at,.004);at+=.004;CHECK(p.ShouldNative());}
        CHECK(p.CheapRun()==59);at=std::max(at,c.next);p.BeginFrame(at,c);Step(p,c,at,.010);at+=.010;CHECK(p.CheapRun()==0);
        at=std::max(at,c.next);p.BeginFrame(at,c);Step(p,c,at,.001,false);at+=.001;CHECK(p.CheapRun()==0);
        double lastStart=0;
        for(int i=0;i<60;++i){at=std::max(at,c.next);p.BeginFrame(at,c);lastStart=at;Step(p,c,at,.004);at+=.004;CHECK(p.ShouldNative());}
        CHECK(p.PendingTransition()==OverloadPolicy::Transition::RecoverAdaptive);
        auto f=p.BeginFrame(at,c);CHECK(f.transition==OverloadPolicy::Transition::RecoverAdaptive);CHECK(!p.ShouldNative());CHECK(!c.Due(at));Near(c.next,lastStart+FixedTimeline::period);
        at=c.next;p.BeginFrame(at,c);Step(p,c,at,.002);CHECK(!c.Due(at));CHECK(p.Stats().recoveredAdaptive==1);
    }
    // A load is explicitly excluded by the existing lifecycle clock, not by a
    // generic duration threshold. It also cannot complete an overload streak.
    {OverloadPolicy p;FixedTimeline c;double at=0;for(int i=0;i<2;++i){p.BeginFrame(at,c);Step(p,c,at,.025);at+=.03;}p.BeginFrame(at,c);Step(p,c,at,.002,false);c.ExcludeBlocking(.5);CHECK(p.HeavyRun()==0);CHECK(p.PendingTransition()==OverloadPolicy::Transition::None);CHECK(p.Stats().clockRebases==0);}
    // Alternating work and a single expensive pass are not falsely described as
    // three consecutive heavy samples. Invalid samples cannot trigger a mode.
    {OverloadPolicy p;FixedTimeline c;double at=0;for(int i=0;i<20;++i){p.BeginFrame(at,c);Step(p,c,at,i%2?.002:.025);at+=.03;}CHECK(!p.ShouldNative());p.BeginFrame(at,c);CHECK(p.BeginFixedStep(at));c.Consume(at);p.ObserveFixedCost(-1);CHECK(p.Stats().invalidSamples==1);CHECK(!p.CanStartFixedStep(at));}
    // Fixed13ms alone is native-capable, but doubling with pure10ms makes
    // sustained adaptive work fall behind. Independent pressure observations
    // must survive each sub-threshold fixed observation and queue native mode.
    {OverloadPolicy p;FixedTimeline c;double at=0;unsigned pressured=0;
        for(int i=0;i<5;++i){p.BeginFrame(at,c);Step(p,c,at,.013);CHECK(p.HeavyRun()==0);at+=.023;p.ObservePureCost(.010,at,c);if(p.PressureRun())++pressured;CHECK(!p.ShouldNative());}
        CHECK(pressured==3);CHECK(p.PendingTransition()==OverloadPolicy::Transition::EnterNative);
        auto f=p.BeginFrame(at,c);CHECK(f.cause==OverloadPolicy::Cause::CombinedWorkAndDebt);CHECK(p.ShouldNative());CHECK(p.Stats().combinedPressureEntries==1);
        for(int i=0;i<60;++i){at=(std::max)(at,c.next);p.BeginFrame(at,c);Step(p,c,at,.004);at+=.004;}
        CHECK(p.PendingTransition()==OverloadPolicy::Transition::RecoverAdaptive);f=p.BeginFrame(at,c);CHECK(f.cause==OverloadPolicy::Cause::CheapRecovery);CHECK(!c.Due(at));
    }
    // Sum of an8-step cheap catch-up batch is NOT a per-step pressure estimate.
    // Even with ordinary500ms debt,2+2ms work must not trigger overload.
    {OverloadPolicy p;FixedTimeline c;p.BeginFrame(0,c);Step(p,c,0,.002);double at=.500;
        for(int frame=0;frame<3;++frame){p.BeginFrame(at,c);for(int i=0;i<8;++i){Step(p,c,at,.002);at+=.002;}at+=.002;p.ObservePureCost(.002,at,c);CHECK(p.PressureRun()==0);Near(p.LastCombinedCostSeconds(),.004);}
        CHECK(p.PendingTransition()==OverloadPolicy::Transition::None);CHECK(p.Stats().clockRebases==0);
    }
    // Waiting on a slow display/external scheduler does not become measured
    // pure work. Cheap clocks retain debt even with100ms between frames.
    {OverloadPolicy p;FixedTimeline c;double at=0;for(int i=0;i<10;++i){p.BeginFrame(at,c);Step(p,c,at,.002);at+=.104;p.ObservePureCost(.002,at,c);CHECK(p.PressureRun()==0);}CHECK(p.Stats().clockRebases==0);CHECK(!p.ShouldNative());}
    // A single cold pure frame is ignored; duplicate calls cannot turn one
    // expensive frame into three observations. Cheap work resets the streak.
    {OverloadPolicy p;FixedTimeline c;p.BeginFrame(0,c);Step(p,c,0,.002);p.ObservePureCost(.080,.082,c);CHECK(p.PressureRun()==1);p.ObservePureCost(.080,.082,c);CHECK(p.PressureRun()==1);CHECK(p.Stats().invalidSamples==1);p.BeginFrame(.082,c);Step(p,c,.082,.002);p.ObservePureCost(.002,.086,c);CHECK(p.PressureRun()==0);CHECK(p.PendingTransition()==OverloadPolicy::Transition::None);}
    // Combined expensive work without a full overdue deadline is not enough.
    // Lifecycle-ineligible pure work also invalidates the old fixed estimate.
    {OverloadPolicy p;FixedTimeline c;double at=0;for(int i=0;i<4;++i){c.next=at;p.BeginFrame(at,c);Step(p,c,at,.013);c.next=at+1;p.ObservePureCost(.010,at+.023,c);CHECK(p.PressureRun()==0);at+=.023;}p.BeginFrame(at,c);p.ObservePureCost(.100,at+.100,c,false);CHECK(p.PressureRun()==0);p.BeginFrame(at+.2,c);c.next=0;p.ObservePureCost(.100,at+.3,c);CHECK(p.PressureRun()==0);CHECK(p.PendingTransition()==OverloadPolicy::Transition::None);}
    // Pressure must be consecutive, not a running count of earlier bad frames.
    {OverloadPolicy p;FixedTimeline c;double at=.5;for(int i=0;i<5;++i){p.BeginFrame(at,c);const double fixed=i==2?.002:.013;const double pure=i==2?.002:.010;Step(p,c,at,fixed);at+=fixed+pure;p.ObservePureCost(pure,at,c);CHECK(p.PendingTransition()==OverloadPolicy::Transition::None);}CHECK(p.PressureRun()==2);p.BeginFrame(at,c);Step(p,c,at,.013);at+=.023;p.ObservePureCost(.010,at,c);CHECK(p.PendingTransition()==OverloadPolicy::Transition::EnterNative);}
    std::puts("PASS overload: budget,3-heavy entry,next-frame promotion,external debt,one native cycle,hysteresis,60-cheap recovery,no early/burst tick,lifecycle exclusion,combined13+10 pressure,cheap-batch/external-wait protection");return 0;
}
