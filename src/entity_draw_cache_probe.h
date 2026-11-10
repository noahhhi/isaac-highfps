#pragma once
// Standalone prototype, not installed. No hooks are installed by this file.
// Host adapters must prove full draw coverage and render-context compatibility.
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace highfps_entity_probe {
struct Color { float rgba[4]; std::int32_t extra; };
struct Source { float xy[8]; std::int32_t space; };
struct Destination { float xy[8]; Color corner[4]; };
static_assert(sizeof(Color)==20 && sizeof(Source)==36 && sizeof(Destination)==112, "J374 ABI");
struct Key {
    std::uint64_t epoch=0, generation=0;
    std::uintptr_t entity=0;
    std::uint32_t callback=0, pass=0, ordinal=0;
    std::uint32_t seed=0,type=0,variant=0;
    bool operator==(const Key& b) const {
        return epoch==b.epoch && generation==b.generation && entity==b.entity &&
            callback==b.callback && pass==b.pass && ordinal==b.ordinal &&
            seed==b.seed && type==b.type && variant==b.variant;
    }
};
// Includes all pass identity/graphics state the host promises to preserve.
// RT, viewport, ortho, blend, shader and scissor must be represented, not only RT.
struct State { std::array<std::uint64_t,32> word{};
    bool operator==(const State& b) const { return word==b.word; }
};
struct Quad {
    void* image=nullptr;
    Source source{};
    Destination destination{};
    Color color[4]{};
};
struct Layout {
    std::uint16_t stride=0, positionOffset=0;
    std::vector<std::uint8_t> attributes;
    bool Valid() const {
        if (!stride || stride>512 || attributes.empty() || attributes.size()>32) return false;
        static constexpr unsigned sizes[]={0,4,8,12,16,12,16,8,4};
        unsigned total=0, positions=0;
        for (auto a:attributes) {
            if (!a || a>8) return false;
            if (a==5) { if (total!=positionOffset) return false; ++positions; }
            total+=sizes[a];
        }
        return positions==1 && total==stride && unsigned(positionOffset)+12<=stride;
    }
    bool operator==(const Layout& b) const {
        return stride==b.stride && positionOffset==b.positionOffset && attributes==b.attributes;
    }
};
struct Command {
    Quad quad{};
    bool resourceOnly=false; // Ordered image format/filter/wrap state, no geometry.
    State state{};
    Layout layout{};
    // Custom deleter releases an actual native strong reference, not a raw pointer.
    std::shared_ptr<void> imageLease;
    std::vector<std::uint8_t> vertices;
};
struct Backend {
    virtual ~Backend()=default;
    virtual State ReadState()=0;
    virtual bool ContextCompatible(const State& a,const State& b) { return a==b; }
    virtual bool StateRepresentable(const State&) { return true; }
    virtual bool Describe(void* image, Layout& layout, State& state)=0;
    virtual std::shared_ptr<void> Retain(void* image)=0;
    // Optional native resource-state support. A retained image remains alive while
    // End snapshots its final state, including changes after its last quad.
    virtual bool HasResourceState() const { return false; }
    virtual bool DescribeResource(void*,Layout&,State&) { return false; }
    virtual bool SameResource(const Command& a,const Command& b) const {
        return a.layout==b.layout && a.state==b.state;
    }
    virtual bool PrepareResource(const Command&) { return true; }
    virtual bool StateUsesTexture(const State&,std::uint32_t) const { return true; }
    virtual bool CommandUsesTexture(const Command&,std::uint32_t) const { return true; }
    // Must validate every command BEFORE replay starts. Unknown shader/RT/layout fails.
    virtual bool CanReplay(const Command& command)=0;
    virtual const char* ReplayFailure() const { return "resource-or-context"; }
    virtual void ApplyState(const State& state)=0;
    // Calls the UNHOOKED A06820 trampoline. Must preserve native frame depth allocation.
    virtual std::uint8_t* Submit(const Quad& quad)=0;
};
class Cache {
    struct Entry { Key key; State entryState; State exitState; bool valid=false,noOp=false; const char* reject="none";std::uint32_t unsafeCode=0;std::uint64_t canonicalFrame=0; std::vector<Command> commands; };
    Backend& backend_;
    std::vector<Entry> entries_;
    Entry current_{};
    std::uint8_t* pending_=nullptr;
    bool capturing_=false, replaying_=false, bad_=false;
    static constexpr std::size_t kEntries=512, kCommands=128;
    std::uint64_t canonicalFrame_=0;
    void Seal() {
        if (!pending_) return;
        auto& c=current_.commands.back();
        // Call before any buffer resize/flush/free. An unsupported allocator must
        // invoke Unsafe BEFORE invalidating the returned pointer, never afterwards.
        c.vertices.assign(pending_,pending_+4u*c.layout.stride);
        pending_=nullptr;
    }
public:
    struct ReplayInfo {const char* reason="none";const char* captureReject="none";std::uint32_t unsafeCode=0;bool entitySeen=false;std::uint64_t lastFrame=0;std::uint32_t priorPass=0,priorOrdinal=0;};
private:
    ReplayInfo lastReplay_{};
    void FindPrior(const Key&key){
        for(const auto&e:entries_)if(e.key.epoch==key.epoch && e.key.entity==key.entity && e.key.seed==key.seed && e.key.type==key.type && e.key.variant==key.variant){
            if(!lastReplay_.entitySeen || e.canonicalFrame>=lastReplay_.lastFrame){lastReplay_.entitySeen=true;lastReplay_.lastFrame=e.canonicalFrame;lastReplay_.priorPass=e.key.pass;lastReplay_.priorOrdinal=e.key.ordinal;}
        }
    }
public:
    explicit Cache(Backend& backend):backend_(backend) {}
    void SetCanonicalFrame(std::uint64_t frame){canonicalFrame_=frame;}
    const ReplayInfo& LastReplay()const{return lastReplay_;}
    struct Counts {std::size_t entries=0,valid=0,commands=0,refs=0;};
    Counts GetCounts() const {Counts n;n.entries=entries_.size();for(const auto&e:entries_){n.valid+=e.valid;n.commands+=e.commands.size();for(const auto&c:e.commands)n.refs+=bool(c.imageLease);}return n;}
    bool Capturing() const { return capturing_; }
    bool Replaying() const { return replaying_; }
    void Clear() { pending_=nullptr; capturing_=false; entries_.clear(); current_=Entry{}; }
    bool Begin(const Key& key) {
        if (capturing_ || replaying_) { Unsafe(); if(capturing_)current_.reject="nested-scope";return false; }
        current_=Entry{}; current_.key=key; current_.canonicalFrame=canonicalFrame_;current_.entryState=backend_.ReadState();
        capturing_=true; bad_=false; return true;
    }
    // Called immediately BEFORE the native quad submit. Must still call native even
    // if false: a rejected capture must not damage the authoritative render.
    bool BeforeQuad(const Quad& quad) {
        if (!capturing_ || replaying_) return false;
        Seal();
        if(bad_)return false;
        if(current_.commands.size()>=(current_.key.callback==2?4096:kCommands)){bad_=true;current_.reject="command-limit";return false;}
        Command c; c.quad=quad;
        if (!backend_.Describe(quad.image,c.layout,c.state) || !c.layout.Valid()) { bad_=true;current_.reject="unsupported-image-layout-or-shader";return false; }
        c.imageLease=backend_.Retain(quad.image);
        if (!c.imageLease) { bad_=true;current_.reject="image-reference";return false; }
        current_.commands.push_back(std::move(c)); return true;
    }
    // recorded is the return of BeforeQuad for this exact call, not a global flag.
    void AfterQuad(bool recorded, std::uint8_t* vertices) {
        if (!recorded || bad_) return;
        if (!vertices) { current_.commands.pop_back(); return; } // native culled draw
        pending_=vertices;
    }
    // RT changes/clears, unsupported draw types, unmanaged buffers, shader uniform
    // writes, resource changes and nested callback scopes all invalidate the cache.
    void SealPending() { if(capturing_ && !replaying_) Seal(); }
    bool TextureDeleted(std::uint32_t texture,std::uint32_t reason=145) {
        if(!capturing_ || replaying_ || !texture)return false;
        Seal();
        bool used=backend_.StateUsesTexture(current_.entryState,texture);
        for(const auto&c:current_.commands)used=used || backend_.CommandUsesTexture(c,texture);
        if(used)Unsafe(reason); // Checked texture delete/upload/parameter dispatch.
        return used;
    }
    void Unsafe(std::uint32_t reason=0) { if (capturing_) { Seal();if(!bad_){current_.reject="unsafe-operation";current_.unsafeCode=reason;}bad_=true; } }
    void End() {
        if (!capturing_) return;
        Seal();
        if(!bad_ && backend_.HasResourceState()) {
            // One final resource command per retained image, only if its final
            // state differs from its last captured use. Iterate the original
            // command count; appended state commands must not extend this loop.
            const auto count=current_.commands.size();
            for(std::size_t i=0;i<count && !bad_;++i) {
                const auto image=current_.commands[i].quad.image;
                bool later=false;
                for(std::size_t j=i+1;j<count;++j)if(current_.commands[j].quad.image==image){later=true;break;}
                if(later)continue;
                Command final;final.quad.image=image;final.resourceOnly=true;
                final.imageLease=current_.commands[i].imageLease;
                if(!backend_.DescribeResource(image,final.layout,final.state)) {
                    bad_=true;current_.reject="unsupported-final-image-state";break;
                }
                if(!backend_.SameResource(current_.commands[i],final))current_.commands.push_back(std::move(final));
            }
        }
        current_.exitState=backend_.ReadState();
        current_.noOp=current_.commands.empty() && current_.exitState==current_.entryState;
        current_.valid=!bad_ && (current_.noOp || (backend_.ContextCompatible(current_.exitState,current_.entryState) && backend_.StateRepresentable(current_.exitState)));
        if (!current_.valid) {
            if(!bad_)current_.reject=backend_.ContextCompatible(current_.exitState,current_.entryState)?"unsupported-exit-state":"context-changed-in-callback";
            current_.commands.clear();
        }
        capturing_=false;
        for (auto& e:entries_) if (e.key==current_.key) { e=std::move(current_); return; }
        if (entries_.size()==kEntries) entries_.erase(entries_.begin());
        entries_.push_back(std::move(current_));
    }
    // No fallback Lua on extra render frames. False means no cached drawing; the
    // next canonical tick will execute Lua and rebuild. Log this compatibility gap.
    bool Replay(const Key& key) {
        lastReplay_={};FindPrior(key);
        if (capturing_ || replaying_) {lastReplay_.reason="scope-busy";return false;}
        Entry* found=nullptr;
        for (auto& e:entries_) if (e.key==key) { found=&e; break; }
        if(!found){lastReplay_.reason="missing-key";return false;}
        if(!found->valid){lastReplay_.reason="invalid-capture";lastReplay_.captureReject=found->reject;lastReplay_.unsafeCode=found->unsafeCode;return false;}
        if(found->noOp)return true; // Nothing drawn and native state unchanged.
        if(found->commands.empty() && !backend_.ContextCompatible(backend_.ReadState(),found->entryState)){lastReplay_.reason="context-changed";return false;}
        if(!backend_.StateRepresentable(found->exitState)){lastReplay_.reason="exit-shader-generation-or-contract";return false;}
        // Fixed sampled positions are intentional; no guessed entity translation.
        for (const auto& c:found->commands) if (!backend_.CanReplay(c)){lastReplay_.reason=backend_.ReplayFailure();return false;}
        const State saved=backend_.ReadState(); replaying_=true;
        bool ok=true;
        for (const auto& c:found->commands) {
            if(!backend_.PrepareResource(c)){ok=false;lastReplay_.reason="resource-prepare-failed";break;}
            if(c.resourceOnly)continue;
            backend_.ApplyState(c.state);
            auto* dst=backend_.Submit(c.quad);
            if (!dst) { ok=false;lastReplay_.reason="native-submit-null";break; }
            // Native submit assigns this frame's ordering/depth. Keep it while
            // restoring the captured final XY/UV/colors/custom attributes.
            float z[4];
            for (unsigned v=0;v<4;++v) std::memcpy(&z[v],dst+v*c.layout.stride+c.layout.positionOffset+8,4);
            std::memcpy(dst,c.vertices.data(),c.vertices.size());
            for (unsigned v=0;v<4;++v) std::memcpy(dst+v*c.layout.stride+c.layout.positionOffset+8,&z[v],4);
        }
        if(!ok && backend_.HasResourceState()) {
            // A failed native submission must not strand a shared image in an
            // intermediate format. Restore each image's captured final resource
            // state (its last command, including an explicit trailing change).
            for(std::size_t i=0;i<found->commands.size();++i) {
                bool later=false;
                for(std::size_t j=i+1;j<found->commands.size();++j)
                    if(found->commands[j].quad.image==found->commands[i].quad.image){later=true;break;}
                if(!later)backend_.PrepareResource(found->commands[i]);
            }
        }
        backend_.ApplyState(ok ? found->exitState : saved); replaying_=false; return ok;
    }
};
// Identified J374 addresses. Verify exact prologues before installing any detour.
inline constexpr std::uintptr_t kPlayerPostRender=0x00863F90;
inline constexpr std::uintptr_t kTearPostRender=0x008627B0;
inline constexpr std::uintptr_t kEffectPostRender=0x008610D0;
inline constexpr std::uintptr_t kQuadSubmit=0x00A06820;
inline constexpr std::uintptr_t kImageBuckets=0x00BF5AA0;
inline constexpr std::uintptr_t kImageTableMutex=0x00BF5AA4;
inline constexpr std::uintptr_t kSmartImageDecrRef=0x00408330;
// All three entity wrappers: void __stdcall(void* entity, const float* offsetXY).
// Submit: float* __thiscall(image,Source*,Destination*,Color*,Color*,Color*,Color*).
}
