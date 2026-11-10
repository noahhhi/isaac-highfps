#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "render_timing.h"

namespace highfps {
// Scheduling policy only: never calls the engine, skips an authoritative state,
// edits a game counter, renders, or sleeps. Integration owns exact U -> R -> GC.
class OverloadPolicy {
public:
    enum class Mode { Adaptive, Native };
    enum class Transition { None, EnterNative, RecoverAdaptive };
    enum class Cause { None, FixedCost, CombinedWorkAndDebt, CheapRecovery };
    struct Config {
        double frameBudgetSeconds=FixedTimeline::period;
        double heavyStepSeconds=FixedTimeline::period;
        double recoveryStepSeconds=0.008;
        unsigned heavyStepsToEnter=3;
        unsigned cheapStepsToRecover=60;
        unsigned maxAdaptiveSteps=16;
    };
    struct Frame {
        Mode mode=Mode::Adaptive;
        Transition transition=Transition::None;
        Cause cause=Cause::None;
        bool clockRebased=false;
        double discardedScheduleSeconds=0;
        double nextDeadline=0;
    };
    struct Counters {
        std::uint64_t enteredNative=0,recoveredAdaptive=0,clockRebases=0;
        std::uint64_t pureCostSamples=0,combinedPressureSamples=0,combinedPressureEntries=0;
        std::uint64_t fixedSteps=0,nativeSteps=0,ineligibleSamples=0,invalidSamples=0;
        unsigned maxStepsInFrame=0;
        double discardedScheduleSeconds=0;
    };
private:
    Config config_;
    Mode mode_=Mode::Adaptive;
    Transition pending_=Transition::None;
    Cause pendingCause_=Cause::None;
    Counters counters_;
    unsigned heavyRun_=0,cheapRun_=0,pressureRun_=0,frameSteps_=0;
    double frameStart_=0,frameWork_=0,lastCost_=0,lastStepStart_=0;
    bool haveStepStart_=false,frameValid_=false,stepActive_=false;
    bool trustedFixedCost_=false,pureObservedThisFrame_=false;
    double lastEligibleFixedCost_=0,lastCombinedCost_=0,lastObservedDebt_=0;
    static bool FiniteNonnegative(double x){return std::isfinite(x)&&x>=0;}
    void Rebase(double now,FixedTimeline& clock,Frame& result){
        // Never schedule an early extra update after recovery. Native pacing is
        // start-to-start, NOT finish+period (which would double-count work time).
        double target=(std::max)(now,clock.next);
        if(haveStepStart_)target=(std::max)(target,lastStepStart_+FixedTimeline::period);
        const double discarded=target-clock.next;
        if(discarded>0){
            clock.next=target;result.clockRebased=true;result.discardedScheduleSeconds=discarded;
            ++counters_.clockRebases;counters_.discardedScheduleSeconds+=discarded;
        }
    }
public:
    OverloadPolicy()=default;
    explicit OverloadPolicy(Config config):config_(config){
        if(!FiniteNonnegative(config_.frameBudgetSeconds)||config_.frameBudgetSeconds==0||config_.frameBudgetSeconds>FixedTimeline::period)config_.frameBudgetSeconds=FixedTimeline::period;
        if(!FiniteNonnegative(config_.heavyStepSeconds)||config_.heavyStepSeconds==0)config_.heavyStepSeconds=FixedTimeline::period;
        if(!FiniteNonnegative(config_.recoveryStepSeconds)||config_.recoveryStepSeconds==0||config_.recoveryStepSeconds>=config_.heavyStepSeconds)config_.recoveryStepSeconds=config_.heavyStepSeconds*.48;
        if(!config_.heavyStepsToEnter)config_.heavyStepsToEnter=3;
        if(!config_.cheapStepsToRecover)config_.cheapStepsToRecover=60;
        if(!config_.maxAdaptiveSteps||config_.maxAdaptiveSteps>16)config_.maxAdaptiveSteps=16;
    }
    // Call once per outer iteration, with fresh monotonic WALL time. An overload
    // transition is committed only here, keeping render mode stable for a frame.
    Frame BeginFrame(double wallNow,FixedTimeline& clock){
        Frame result;result.mode=mode_;result.nextDeadline=clock.next;
        frameValid_=FiniteNonnegative(wallNow)&&FiniteNonnegative(clock.next)&&!stepActive_;
        frameStart_=wallNow;frameWork_=0;frameSteps_=0;pureObservedThisFrame_=false;
        if(!frameValid_){++counters_.invalidSamples;return result;}
        if(pending_!=Transition::None){
            result.transition=pending_;result.cause=pendingCause_;
            if(pendingCause_==Cause::CombinedWorkAndDebt)++counters_.combinedPressureEntries;
            if(pending_==Transition::EnterNative){mode_=Mode::Native;++counters_.enteredNative;}
            else {mode_=Mode::Adaptive;++counters_.recoveredAdaptive;}
            pending_=Transition::None;pendingCause_=Cause::None;heavyRun_=cheapRun_=pressureRun_=0;
            Rebase(wallNow,clock,result);
        }else if(mode_==Mode::Native){
            // This is the sole recurring debt-forgiveness path. Adaptive frames,
            // including an ordinary long external render stall, never rebase.
            Rebase(wallNow,clock,result);
        }
        result.mode=mode_;result.nextDeadline=clock.next;return result;
    }
    bool ShouldNative()const{return mode_==Mode::Native;}
    Transition PendingTransition()const{return pending_;}
    // Budget permission only. Caller additionally checks its FixedTimeline::Due:
    // adaptive uses its existing sampling cutoff; native MUST use fresh wall time.
    // A single indivisible U/R/GC may exceed budget. We can only refuse MORE work.
    bool CanStartFixedStep(double wallNow)const{
        if(!frameValid_||stepActive_||!FiniteNonnegative(wallNow)||wallNow<frameStart_)return false;
        if(frameSteps_==0)return true;
        if(mode_==Mode::Native||pending_!=Transition::None||frameSteps_>=config_.maxAdaptiveSteps)return false;
        const double used=(std::max)(frameWork_,wallNow-frameStart_);
        if(used>=config_.frameBudgetSeconds)return false;
        // Avoid knowingly starting a second expensive pass with insufficient
        // remaining budget. Unpredicted cost growth can still overrun once.
        return lastCost_<=config_.frameBudgetSeconds-used;
    }
    bool BeginFixedStep(double wallNow){
        if(!CanStartFixedStep(wallNow))return false;
        stepActive_=true;lastStepStart_=wallNow;haveStepStart_=true;
        ++frameSteps_;++counters_.fixedSteps;
        if(mode_==Mode::Native)++counters_.nativeSteps;
        counters_.maxStepsInFrame=(std::max)(counters_.maxStepsInFrame,frameSteps_);return true;
    }
    // Exactly once after a completed U -> canonical R -> GC. Cost is measured
    // work wall time, EXCLUDING explicit lifecycle spans and visible Swap/pacing.
    // Mark lifecycle/restart/invalid-lifetime samples ineligible for hysteresis.
    // This never changes the current frame's mode or the simulation timeline.
    Transition ObserveFixedCost(double workSeconds,bool classificationEligible=true){
        if(!stepActive_){++counters_.invalidSamples;return pending_;}
        stepActive_=false;
        if(!FiniteNonnegative(workSeconds)){
            ++counters_.invalidSamples;frameWork_=config_.frameBudgetSeconds;
            heavyRun_=cheapRun_=pressureRun_=0;trustedFixedCost_=false;lastCost_=config_.frameBudgetSeconds;return pending_;
        }
        frameWork_+=workSeconds;lastCost_=workSeconds;
        if(!classificationEligible){++counters_.ineligibleSamples;heavyRun_=cheapRun_=pressureRun_=0;trustedFixedCost_=false;return pending_;}
        trustedFixedCost_=true;lastEligibleFixedCost_=workSeconds;
        if(pending_!=Transition::None)return pending_;
        if(mode_==Mode::Adaptive){
            heavyRun_=workSeconds>config_.heavyStepSeconds?heavyRun_+1:0;
            if(heavyRun_>=config_.heavyStepsToEnter){pending_=Transition::EnterNative;pendingCause_=Cause::FixedCost;}
        }else{
            cheapRun_=workSeconds<config_.recoveryStepSeconds?cheapRun_+1:0;
            if(cheapRun_>=config_.cheapStepsToRecover){pending_=Transition::RecoverAdaptive;pendingCause_=Cause::CheapRecovery;}
        }
        return pending_;
    }
    // Call once after an actual adaptive pure R completes, with its work wall
    // time EXCLUDING Swap/pacing and lifecycle spans. This catches native-capable
    // fixed13ms + extra-pure10ms overload without lowering the fixed-cost rule.
    // A slow external wait alone is never evidence: BOTH combined measured work
    // and at least one full overdue deadline must persist for3 observations.
    // Use ONE latest eligible fixed cycle, not summed catch-up batch work; cheap
    // 2ms+2ms recovery after an external stall must retain its ordinary clock debt.
    Transition ObservePureCost(double pureWorkSeconds,double wallNow,const FixedTimeline& clock,bool classificationEligible=true){
        if(mode_!=Mode::Adaptive)return pending_;
        if(!frameValid_||stepActive_||pureObservedThisFrame_){++counters_.invalidSamples;return pending_;}
        pureObservedThisFrame_=true;
        if(!classificationEligible){
            ++counters_.ineligibleSamples;heavyRun_=cheapRun_=pressureRun_=0;trustedFixedCost_=false;return pending_;
        }
        if(!FiniteNonnegative(pureWorkSeconds)||!FiniteNonnegative(wallNow)||!FiniteNonnegative(clock.next)||wallNow<frameStart_){
            ++counters_.invalidSamples;pressureRun_=0;trustedFixedCost_=false;return pending_;
        }
        ++counters_.pureCostSamples;
        if(pending_!=Transition::None)return pending_;
        if(!trustedFixedCost_){pressureRun_=0;return pending_;}
        lastCombinedCost_=lastEligibleFixedCost_+pureWorkSeconds;
        lastObservedDebt_=(std::max)(0.0,wallNow-clock.next);
        const bool pressured=lastCombinedCost_>config_.heavyStepSeconds && lastObservedDebt_+1e-9>=FixedTimeline::period;
        pressureRun_=pressured?pressureRun_+1:0;
        if(pressured)++counters_.combinedPressureSamples;
        if(pressureRun_>=config_.heavyStepsToEnter){pending_=Transition::EnterNative;pendingCause_=Cause::CombinedWorkAndDebt;}
        return pending_;
    }
    unsigned PressureRun()const{return pressureRun_;}
    double LastCombinedCostSeconds()const{return lastCombinedCost_;}
    double LastObservedDebtSeconds()const{return lastObservedDebt_;}
    unsigned HeavyRun()const{return heavyRun_;}
    unsigned CheapRun()const{return cheapRun_;}
    unsigned StepsThisFrame()const{return frameSteps_;}
    double FrameWorkSeconds()const{return frameWork_;}
    const Counters& Stats()const{return counters_;}
};
} // highfps
