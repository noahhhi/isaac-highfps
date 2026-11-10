#pragma once
// Portable optional work-pressure classification. No game calls, allocations,
// clock edits, rendering or quota refills. Decisions latch once per world stamp.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <limits>
namespace highfps::pressure {
enum class RequestKind:unsigned {ParasitePair,TechZeroSearch,QuadSplit,TrisagionCreate,Poof,BonePair,LachryChild,HaemoChild};
struct Population {std::uint64_t tears=0,lasers=0,poofs=0,effects=0;};
struct Demand {std::uint64_t children=0,techSearches=0,trisCreates=0,poofs=0;};
inline std::uint64_t Add(std::uint64_t a,std::uint64_t b){const auto max=(std::numeric_limits<std::uint64_t>::max)();return b>max-a?max:a+b;}
// Count requests BEFORE pressure/quota decisions, including denied requests.
// Tech search attempts are work, not predicted/confirmed laser births.
class RequestLedger {
    std::uint64_t epoch_=0,world_=0;std::uintptr_t room_=0;bool valid_=false;Demand demand_{};
public:
    void Reset(){valid_=false;demand_={};}
    bool Begin(std::uint64_t epoch,std::uint64_t world,std::uintptr_t room){
        if(valid_&&epoch_==epoch&&world_==world&&room_==room)return false;
        epoch_=epoch;world_=world;room_=room;valid_=room!=0;demand_={};return true;
    }
    bool Observe(RequestKind kind,unsigned requestedCost){
        if(!valid_)return false;
        const unsigned k=unsigned(kind);if(k>7||requestedCost!=(k==0||k==5?2u:k==2?4u:1u))return false;
        if(kind==RequestKind::TechZeroSearch)demand_.techSearches=Add(demand_.techSearches,1);
        else if(kind==RequestKind::TrisagionCreate)demand_.trisCreates=Add(demand_.trisCreates,1);
        else if(kind==RequestKind::Poof)demand_.poofs=Add(demand_.poofs,1);
        else demand_.children=Add(demand_.children,requestedCost);
        return true;
    }
    Demand Snapshot()const{return demand_;}
};
enum class Severity:unsigned {None,Cosmetic,Attack,Severe};
struct Config {
    double sampleMaxAge=0.75,minimumHold=1.0,quietRecovery=2.0;
    double cosmeticEnter=0.78,attackEnter=0.96,severeEnter=1.20;
    double cosmeticExit=0.60,attackExit=0.76,severeExit=0.90;
    unsigned minimumSamples=8,enterWorlds=3;
    std::uint64_t cosmeticPoofs=96,cosmeticEffects=384;
    std::uint64_t attackTears=128,attackLasers=16,attackSearches=32;
    std::uint64_t severeTears=256,severeLasers=32,severePoofs=256,severeEffects=896,severeSearches=96;
};
struct Context {
    std::uint64_t epoch=0,world=0;std::uintptr_t room=0;
    double now=0,targetHz=90;Population population{};Demand priorRequests{};
    bool populationValid=true,requestsValid=true;
};
struct Decision {
    bool valid=false,budgetAttacks=false,budgetCosmetics=false;
    Severity severity=Severity::None;bool populationCause=false,workCause=false,urgentWork=false;
    double targetHz=0,fixedWorkSeconds=0,pureRenderWorkSeconds=0,estimatedWorkDemand=0;
    double updateWorkSeconds=0,canonicalRenderWorkSeconds=0;
    bool fixedKnown=false,pureKnown=false,fixedBreakdownKnown=false;
    unsigned fixedSamples=0,pureSamples=0;
    std::uint64_t projectedTears=0,projectedLasers=0,projectedPoofs=0,projectedEffects=0,searchRequests=0;
};
struct Counters {std::uint64_t evaluations=0,repeatedWorld=0,scopeResets=0,invalidInputs=0,ineligibleCosts=0,clippedCosts=0,raised=0,recovered=0;};
class Policy {
    static constexpr unsigned WindowSize=32;
    struct Sample {double time=0,total=0,update=-1,render=-1;};
    struct Summary {unsigned count=0;double mean=0,update=0,render=0;bool breakdown=false;};
    struct Window {
        std::array<Sample,WindowSize> values{};unsigned count=0,next=0;
        void Clear(){count=next=0;}
        void Push(Sample s){values[next]=s;next=(next+1)%WindowSize;if(count<WindowSize)++count;}
        bool SlowPair(double now,double newestMaxAge,double threshold)const {
            if(count<2)return false;
            const auto&newest=values[(next+WindowSize-1)%WindowSize];
            const auto&previous=values[(next+WindowSize-2)%WindowSize];
            return newest.time<=now&&now-newest.time<=newestMaxAge&&previous.time<=newest.time&&
                newest.time-previous.time<=2.0&&newest.total>=threshold&&previous.total>=threshold;
        }
        bool SlowFixedWindow(double now,double newestMaxAge,double severeDemand)const {
            if(count<2)return false;const auto&newest=values[(next+WindowSize-1)%WindowSize];
            if(newest.time>now||now-newest.time>newestMaxAge)return false;
            unsigned samples=0,slow=0;double sum=0;
            for(unsigned j=0;j<(std::min)(count,4u);++j){const auto&s=values[(next+WindowSize-1-j)%WindowSize];
                if(s.time>newest.time||newest.time-s.time>2.0)break;
                ++samples;sum+=s.total;if(s.total>=severeDemand/60.0)++slow;
            }
            // J374 alternates full world and cheap UI cycles. Require both two
            // heavy spans and aggregate pressure;20ms/1ms alone is not severe.
            return slow>=2&&sum*60.0/samples>=severeDemand;
        }
        Summary Read(double now,double age,unsigned minimum)const {
            Summary out;double sum=0,update=0,render=0,peak=-1;unsigned peakIndex=0;bool allParts=true;
            for(unsigned i=0;i<count;++i){const auto&s=values[i];if(s.time>now||now-s.time>age)continue;
                ++out.count;sum+=s.total;if(s.total>peak){peak=s.total;peakIndex=i;}
                if(s.update<0||s.render<0)allParts=false;else {update+=s.update;render+=s.render;}
            }
            if(out.count<minimum)return out;
            // Trim ONE maximum after warmup. Repeated slow work still contributes;
            // one scheduler stall or transient frame cannot poison the estimate.
            const auto&trim=values[peakIndex];const double n=double(out.count-1);
            out.mean=(std::max)(0.0,(sum-peak)/n);out.breakdown=allParts;
            if(allParts){out.update=(std::max)(0.0,(update-trim.update)/n);out.render=(std::max)(0.0,(render-trim.render)/n);}
            return out;
        }
    };
    Config config_;Window fixed_,pure_;Counters counters_{};
    std::uint64_t epoch_=0,lastWorld_=0;std::uintptr_t room_=0;bool scoped_=false,evaluated_=false;
    double lastTime_=0,lastChange_=0,quietSince_=0;bool haveQuiet_=false;
    Severity severity_=Severity::None,candidate_=Severity::None;unsigned candidateRun_=0;Decision last_{};
    static bool Finite(double x){return std::isfinite(x)&&x>=0;}
    void ClearState(){fixed_.Clear();pure_.Clear();evaluated_=false;last_={};severity_=candidate_=Severity::None;candidateRun_=0;haveQuiet_=false;lastChange_=0;}
    void BreakEvidence(){candidate_=Severity::None;candidateRun_=0;haveQuiet_=false;}
    bool Scope(std::uint64_t epoch,std::uintptr_t room,double now){
        if(!room){ClearState();scoped_=false;++counters_.invalidInputs;return false;}
        if(!Finite(now)){BreakEvidence();++counters_.invalidInputs;return false;}
        if(!scoped_||epoch!=epoch_||room!=room_){ClearState();scoped_=true;epoch_=epoch;room_=room;lastTime_=now;++counters_.scopeResets;}
        if(now<lastTime_){BreakEvidence();++counters_.invalidInputs;return false;}
        lastTime_=now;return true;
    }
    Severity PopulationLevel(const Context&c,const Decision&d)const {
        if(!c.populationValid||!c.requestsValid)return Severity::None;
        if(d.projectedTears>=config_.severeTears||d.projectedLasers>=config_.severeLasers||d.projectedPoofs>=config_.severePoofs||
           d.projectedEffects>=config_.severeEffects||d.searchRequests>=config_.severeSearches)return Severity::Severe;
        if(d.projectedTears>=config_.attackTears||d.projectedLasers>=config_.attackLasers||d.searchRequests>=config_.attackSearches)return Severity::Attack;
        if(d.projectedPoofs>=config_.cosmeticPoofs||d.projectedEffects>=config_.cosmeticEffects)return Severity::Cosmetic;
        return Severity::None;
    }
    bool Quiet(const Context&c,const Decision&d)const {
        if(!c.populationValid||!c.requestsValid||(!d.fixedKnown||!d.pureKnown))return false;
        double exit=config_.cosmeticExit;std::uint64_t tears=96,lasers=12,poofs=48,effects=256,searches=16;
        if(severity_==Severity::Attack){exit=config_.attackExit;poofs=128;effects=640;searches=24;}
        if(severity_==Severity::Severe){exit=config_.severeExit;tears=192;lasers=24;poofs=192;effects=768;searches=64;}
        return d.estimatedWorkDemand<exit&&d.projectedTears<tears&&d.projectedLasers<lasers&&
            d.projectedPoofs<poofs&&d.projectedEffects<effects&&d.searchRequests<searches;
    }
public:
    Policy():Policy(Config{}){}
    explicit Policy(Config config):config_(config){
        const Config defaults;
        if(!Finite(config_.sampleMaxAge)||config_.sampleMaxAge<.1||config_.sampleMaxAge>5)config_.sampleMaxAge=defaults.sampleMaxAge;
        if(!Finite(config_.minimumHold)||config_.minimumHold>30)config_.minimumHold=defaults.minimumHold;
        if(!Finite(config_.quietRecovery)||config_.quietRecovery<.1||config_.quietRecovery>30)config_.quietRecovery=defaults.quietRecovery;
        if(config_.minimumSamples<4||config_.minimumSamples>WindowSize)config_.minimumSamples=defaults.minimumSamples;
        if(!config_.enterWorlds||config_.enterWorlds>30)config_.enterWorlds=defaults.enterWorlds;
        if(!Finite(config_.cosmeticEnter)||!Finite(config_.attackEnter)||!Finite(config_.severeEnter)||
           config_.cosmeticEnter<=0||config_.attackEnter<=config_.cosmeticEnter||config_.severeEnter<=config_.attackEnter){
            config_.cosmeticEnter=defaults.cosmeticEnter;config_.attackEnter=defaults.attackEnter;config_.severeEnter=defaults.severeEnter;}
        if(!config_.cosmeticPoofs)config_.cosmeticPoofs=defaults.cosmeticPoofs;
        if(!config_.cosmeticEffects)config_.cosmeticEffects=defaults.cosmeticEffects;
        if(!config_.attackTears)config_.attackTears=defaults.attackTears;
        if(!config_.attackLasers)config_.attackLasers=defaults.attackLasers;
        if(!config_.attackSearches)config_.attackSearches=defaults.attackSearches;
        if(config_.severeTears<=config_.attackTears)config_.severeTears=Add(config_.attackTears,1);
        if(config_.severeLasers<=config_.attackLasers)config_.severeLasers=Add(config_.attackLasers,1);
        if(config_.severePoofs<=config_.cosmeticPoofs)config_.severePoofs=Add(config_.cosmeticPoofs,1);
        if(config_.severeEffects<=config_.cosmeticEffects)config_.severeEffects=Add(config_.cosmeticEffects,1);
        if(config_.severeSearches<=config_.attackSearches)config_.severeSearches=Add(config_.attackSearches,1);
        if(!Finite(config_.cosmeticExit)||config_.cosmeticExit>=config_.cosmeticEnter)config_.cosmeticExit=config_.cosmeticEnter*.75;
        if(!Finite(config_.attackExit)||config_.attackExit>=config_.attackEnter)config_.attackExit=config_.attackEnter*.79;
        if(!Finite(config_.severeExit)||config_.severeExit>=config_.severeEnter)config_.severeExit=config_.severeEnter*.75;
    }
    void Reset(){ClearState();scoped_=false;}
    // Costs are measured wall WORK spans excluding Swap/pacing/lifecycle. Fixed
    // includes Update+canonicalRender+GC. Optional components are diagnostics only.
    void ObserveFixed(std::uint64_t epoch,std::uintptr_t room,double now,double total,bool eligible=true,double update=-1,double render=-1){
        if(!Scope(epoch,room,now))return;
        if(!eligible){++counters_.ineligibleCosts;fixed_.Clear();BreakEvidence();return;}
        if(!Finite(total)){++counters_.invalidInputs;fixed_.Clear();BreakEvidence();return;}
        if(total>1.0){total=1.0;++counters_.clippedCosts;}
        if(!Finite(update)||!Finite(render)||update+render>total+1e-9)update=render=-1;
        fixed_.Push({now,total,update,render});
    }
    void ObservePure(std::uint64_t epoch,std::uintptr_t room,double now,double total,bool eligible=true){
        if(!Scope(epoch,room,now))return;
        if(!eligible){++counters_.ineligibleCosts;pure_.Clear();BreakEvidence();return;}
        if(!Finite(total)){++counters_.invalidInputs;pure_.Clear();BreakEvidence();return;}
        if(total>1.0){total=1.0;++counters_.clippedCosts;}
        pure_.Push({now,total,-1,-1});
    }
    Decision Evaluate(const Context&c){
        if(!Scope(c.epoch,c.room,c.now))return {};
        if(evaluated_&&lastWorld_==c.world){++counters_.repeatedWorld;return last_;}
        if(evaluated_&&c.world<lastWorld_){ClearState();++counters_.scopeResets;}
        evaluated_=true;lastWorld_=c.world;++counters_.evaluations;
        if(!Finite(c.targetHz)||c.targetHz<30||c.targetHz>360){++counters_.invalidInputs;BreakEvidence();last_={};return last_;}
        Decision d;d.valid=true;d.targetHz=c.targetHz;d.severity=severity_;
        const auto f=fixed_.Read(c.now,config_.sampleMaxAge,config_.minimumSamples),p=pure_.Read(c.now,config_.sampleMaxAge,config_.minimumSamples);
        d.fixedSamples=f.count;d.pureSamples=p.count;d.fixedKnown=f.count>=config_.minimumSamples;d.pureKnown=p.count>=config_.minimumSamples;
        d.fixedWorkSeconds=f.mean;d.pureRenderWorkSeconds=p.mean;d.fixedBreakdownKnown=d.fixedKnown&&f.breakdown;
        d.updateWorkSeconds=f.update;d.canonicalRenderWorkSeconds=f.render;
        // Desired adaptive work demand, not GPU time or measured CPU utilization.
        // Missing/stale phase means this is a LOWER BOUND, exposed by known flags.
        d.estimatedWorkDemand=60*d.fixedWorkSeconds+c.targetHz*d.pureRenderWorkSeconds;
        if(c.populationValid&&c.requestsValid){
            d.projectedTears=Add(c.population.tears,c.priorRequests.children);
            d.projectedLasers=Add(c.population.lasers,c.priorRequests.trisCreates);
            d.projectedPoofs=Add(c.population.poofs,c.priorRequests.poofs);
            d.projectedEffects=Add(c.population.effects,c.priorRequests.poofs);
            d.searchRequests=c.priorRequests.techSearches;
        }
        const auto population=PopulationLevel(c,d);
        Severity work=Severity::None;
        if(d.estimatedWorkDemand>=config_.severeEnter)work=Severity::Severe;
        else if(d.estimatedWorkDemand>=config_.attackEnter)work=Severity::Attack;
        else if(d.estimatedWorkDemand>=config_.cosmeticEnter)work=Severity::Cosmetic;
        // Very low sampling rates cannot fill the ordinary warm window. Two
        // fixed heavy spans among at most4 (alternating world/UI) AND high mean
        // are enough; pure needs a consecutive severe pair. One stall is not.
        // Latest must be fresh, older evidence spans at most2s.
        d.urgentWork=fixed_.SlowFixedWindow(c.now,config_.sampleMaxAge,config_.severeEnter)||
            pure_.SlowPair(c.now,config_.sampleMaxAge,config_.severeEnter/c.targetHz);
        if(d.urgentWork&&severity_!=Severity::Severe){
            severity_=Severity::Severe;lastChange_=c.now;haveQuiet_=false;candidateRun_=0;d.workCause=true;++counters_.raised;
        }
        if(unsigned(population)>unsigned(severity_)){
            severity_=population;lastChange_=c.now;haveQuiet_=false;candidateRun_=0;d.populationCause=true;++counters_.raised;
        }
        if(unsigned(work)>unsigned(severity_)){
            if(work==candidate_)++candidateRun_;else {candidate_=work;candidateRun_=1;}
            if(candidateRun_>=config_.enterWorlds){severity_=work;lastChange_=c.now;haveQuiet_=false;candidateRun_=0;d.workCause=true;++counters_.raised;}
        }else {candidate_=Severity::None;candidateRun_=0;}
        if(severity_!=Severity::None&&!d.urgentWork&&unsigned(population)<unsigned(severity_)&&unsigned(work)<unsigned(severity_)&&Quiet(c,d)){
            if(!haveQuiet_){quietSince_=c.now;haveQuiet_=true;}
            if(c.now-lastChange_>=config_.minimumHold&&c.now-quietSince_>=config_.quietRecovery){
                severity_=Severity(unsigned(severity_)-1);lastChange_=quietSince_=c.now;haveQuiet_=false;++counters_.recovered;
            }
        }else haveQuiet_=false;
        d.severity=severity_;d.budgetAttacks=unsigned(severity_)>=unsigned(Severity::Attack);d.budgetCosmetics=severity_!=Severity::None;
        last_=d;return d;
    }
    Decision Current()const{return last_;}
    const Counters& Stats()const{return counters_;}
};
}
