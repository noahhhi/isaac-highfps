#pragma once
// Optional attack-work admission BEFORE native allocation/search. This policy
// owns only scalar accounting. The host must first establish supported player
// ownership/context and must preserve unknown contexts outside this policy.
// Accepted work may still fail to create an entity: reservations are deliberately
// not refunded within the world tick, avoiding optimistic same-tick overshoot.
// Single authoritative game-thread owner; no synchronization or game resources.
// The population snapshot must include all existing effects for the total pool
// guard. New unrelated native allocations after that snapshot are not observable
// here: hard ceilings are conservative admission estimates, not pool guarantees.
#include <cstdint>
#include <limits>

namespace highfps::work_budget {
enum class Kind : unsigned {
    ParasitePair = 0, TechZeroSearch = 1, QuadSplit = 2,
    TrisagionCreate = 3, Poof = 4, BonePair = 5, LachryChild = 6, HaemoChild = 7
};
inline constexpr unsigned KindCount = 8;
struct Population {
    std::uint64_t tears = 0, lasers = 0, playerLasers = 0, poofs = 0;
    std::uint64_t effects = 0; // ALL ENTITY_EFFECT, including protected TARGETs
};
struct Config {
    unsigned childrenPerTick = 6;
    unsigned techSearchesPerTick = 4;
    unsigned trisCreatesPerTick = 4;
    unsigned poofsPerTick = 4;
    unsigned softTears = 128, softLasers = 32, softPoofs = 96;
    unsigned minimumPlayerLasers = 4;
};
enum class Reason : unsigned {
    Allowed, Uninitialized, InvalidKind, TickQuota, LivePopulation, SevereEligibility
};
struct Counters {
    std::uint64_t admitted[KindCount]{}, denied[KindCount]{};
    std::uint64_t reasons[KindCount][6]{}; // Kind x Reason, including Allowed
    std::uint64_t invalidRequests = 0;
};
struct State {
    bool valid = false;
    std::uint64_t epoch = 0, worldStamp = 0;
    std::uintptr_t roomIdentity = 0;
    Population sampled{}, reserved{};
    unsigned childrenRemaining = 0, techSearchesRemaining = 0;
    unsigned trisCreatesRemaining = 0, poofsRemaining = 0;
    unsigned eligibilityDivisor = 1;
};
class FrameWorkBudget {
    static constexpr unsigned NativeTears = 512, NativeLasers = 128, NativeEffects = 1024;
    Config config_;
    State state_{};
    Counters counters_{};
    Reason last_ = Reason::Uninitialized;

    static unsigned Bound(unsigned value, unsigned ceiling) {
        return value < ceiling ? value : ceiling;
    }
    static void Increment(std::uint64_t& value) {
        if (value != (std::numeric_limits<std::uint64_t>::max)()) ++value;
    }
    // Subtraction checks avoid overflow even for hostile UINT64_MAX snapshots.
    static bool Fits(std::uint64_t sampled, std::uint64_t reserved,
                     unsigned cost, unsigned cap) {
        return sampled <= cap && reserved <= cap - sampled
            && cost <= cap - sampled - reserved;
    }
    bool Deny(unsigned kind, Reason reason) {
        last_ = reason;
        if (kind < KindCount) {
            Increment(counters_.denied[kind]);
            Increment(counters_.reasons[kind][static_cast<unsigned>(reason)]);
        }
        else Increment(counters_.invalidRequests);
        return false;
    }
    bool Eligible(unsigned seed) const {
        const unsigned divisor = state_.eligibilityDivisor;
        if (divisor == 1) return true;
        // Stable, non-RNG-consuming rotation. Across divisor consecutive world
        // stamps every seed is eligible once; this is eligibility fairness, not
        // a guarantee of admission when earlier eligible work exhausts quota.
        std::uint32_t hash = static_cast<std::uint32_t>(seed) * 0x9e3779b9u;
        hash ^= hash >> 16;
        return (std::uint64_t(hash % divisor) + state_.worldStamp % divisor) % divisor == 0;
    }
public:
    FrameWorkBudget() : FrameWorkBudget(Config{}) {}
    explicit FrameWorkBudget(Config config) : config_(config) {
        config_.softTears = Bound(config_.softTears, NativeTears);
        config_.softLasers = Bound(config_.softLasers, NativeLasers);
        config_.softPoofs = Bound(config_.softPoofs, NativeEffects);
        config_.minimumPlayerLasers = Bound(config_.minimumPlayerLasers, NativeLasers);
    }
    // Explicit lifecycle reset. The host must change epoch on rewind/new run
    // even when world/room numerically match. Counters remain cumulative.
    void Reset() { state_ = State{}; last_ = Reason::Uninitialized; }
    void ClearCounters() { counters_ = Counters{}; }
    // Returns true iff a fresh snapshot/quota was accepted. Repeated entry in
    // the SAME epoch/world/room never replaces population or refills quota,
    // regardless of presentation frame, reentry, changed config inputs or time.
    // A backwards stamp is a new timeline state; no old reservation survives.
    bool Begin(std::uint64_t epoch, std::uint64_t worldStamp,
               std::uintptr_t roomIdentity, Population population,
               bool knownOverload = false, unsigned severeDivisor = 1) {
        if (state_.valid && state_.epoch == epoch && state_.worldStamp == worldStamp
            && state_.roomIdentity == roomIdentity) return false;
        state_ = State{};
        state_.valid = true; state_.epoch = epoch; state_.worldStamp = worldStamp;
        state_.roomIdentity = roomIdentity; state_.sampled = population;
        // If a subset count exceeds total, keep the more conservative total.
        if (state_.sampled.playerLasers > state_.sampled.lasers)
            state_.sampled.lasers = state_.sampled.playerLasers;
        if (state_.sampled.poofs > state_.sampled.effects)
            state_.sampled.effects = state_.sampled.poofs;
        state_.childrenRemaining = config_.childrenPerTick;
        state_.techSearchesRemaining = config_.techSearchesPerTick;
        state_.trisCreatesRemaining = config_.trisCreatesPerTick;
        state_.poofsRemaining = config_.poofsPerTick;
        state_.eligibilityDivisor = knownOverload && severeDivisor > 1 ? severeDivisor : 1;
        last_ = Reason::Allowed;
        return true;
    }
    bool Admit(Kind kind, unsigned seed = 0) {
        const unsigned raw = static_cast<unsigned>(kind);
        if (raw >= KindCount) return Deny(raw, Reason::InvalidKind);
        if (!state_.valid) return Deny(raw, Reason::Uninitialized);
        unsigned cost = 1;
        unsigned* remaining = nullptr;
        std::uint64_t* reservation = nullptr;
        bool capacity = false;
        switch (kind) {
        case Kind::ParasitePair:
        case Kind::QuadSplit:
        case Kind::BonePair:
        case Kind::LachryChild:
        case Kind::HaemoChild:
            cost = kind == Kind::QuadSplit ? 4 :
                (kind == Kind::ParasitePair || kind == Kind::BonePair ? 2 : 1);
            remaining = &state_.childrenRemaining;
            reservation = &state_.reserved.tears;
            capacity = Fits(state_.sampled.tears, *reservation, cost, config_.softTears);
            break;
        case Kind::TechZeroSearch:
        case Kind::TrisagionCreate: {
            remaining = kind == Kind::TechZeroSearch ? &state_.techSearchesRemaining : &state_.trisCreatesRemaining;
            reservation = &state_.reserved.lasers;
            const bool belowSoft = Fits(state_.sampled.lasers, *reservation, 1, config_.softLasers);
            const bool playerAllowance = Fits(state_.sampled.playerLasers,
                state_.reserved.playerLasers, 1, config_.minimumPlayerLasers);
            capacity = Fits(state_.sampled.lasers, *reservation, 1, NativeLasers)
                && (belowSoft || playerAllowance);
            break;
        }
        case Kind::Poof:
            remaining = &state_.poofsRemaining;
            reservation = &state_.reserved.poofs;
            capacity = Fits(state_.sampled.poofs, *reservation, 1, config_.softPoofs)
                && Fits(state_.sampled.effects, state_.reserved.effects, 1, NativeEffects);
            break;
        }
        // Commit all accounting together only on success. A denied quad never
        // burns the two remaining children that a subsequent pair can use.
        if (cost > *remaining) return Deny(raw, Reason::TickQuota);
        if (!capacity) return Deny(raw, Reason::LivePopulation);
        if (!Eligible(seed)) return Deny(raw, Reason::SevereEligibility);
        *remaining -= cost;
        *reservation += cost; // bounded by Fits and a native cap above
        if (kind == Kind::TechZeroSearch || kind == Kind::TrisagionCreate)
            ++state_.reserved.playerLasers;
        if (kind == Kind::Poof) ++state_.reserved.effects;
        last_ = Reason::Allowed;
        Increment(counters_.admitted[raw]);
        Increment(counters_.reasons[raw][static_cast<unsigned>(Reason::Allowed)]);
        return true;
    }
    const State& Snapshot() const { return state_; }
    const Counters& Stats() const { return counters_; }
    const Config& Limits() const { return config_; }
    Reason LastDecision() const { return last_; }
};
} // namespace highfps::work_budget
