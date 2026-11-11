#pragma once
// Standalone prototype, not installed. No hooks are installed by this file.
// Host adapters must prove full draw coverage and render-context compatibility.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
#include <unordered_map>

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
public:
    using ObserverCaptureFlag=std::atomic<std::uint8_t>;
    static_assert(sizeof(ObserverCaptureFlag)==1,"observer gate reads one atomic byte");
    struct Limits { std::size_t entries=16384, bytes=64u*1024u*1024u; };
    struct Operations {std::uint64_t hashes=0, comparisons=0, exactLookups=0, lineageLookups=0, writes=0, evictions=0, rejected=0;};
    struct Counts {std::size_t entries=0,valid=0,commands=0,refs=0,noOps=0,bytes=0;};
    struct ReplayInfo {const char* reason="none";const char* captureReject="none";std::uint32_t unsafeCode=0;bool entitySeen=false;std::uint64_t lastFrame=0;std::uint32_t priorPass=0,priorOrdinal=0;};
private:
    struct Payload {State entryState,exitState;std::vector<Command> commands;std::vector<std::size_t> finalResources;};
    struct Capture {Key key;State entryState,exitState;bool valid=false,noOp=false;const char*reject="none";std::uint32_t unsafeCode=0;std::uint64_t canonicalFrame=0;std::vector<Command>commands;std::vector<std::size_t>finalResources;};
    struct Entry {
        Key key;bool valid=false,noOp=false;const char*reject="none";std::uint32_t unsafeCode=0;std::uint64_t canonicalFrame=0;
        std::unique_ptr<Payload> data;std::size_t bytes=0,refs=0;
        Entry *older=nullptr,*newer=nullptr,*lineageOlder=nullptr,*lineageNewer=nullptr;
    };
    struct Hash {
        Operations*ops;
        std::size_t operator()(const Key&k)const {
            ++ops->hashes;
            std::uint64_t h=k.epoch ^ (k.generation*0xa0761d6478bd642full) ^ (std::uint64_t(k.entity)*0xd6e8feb86659fd93ull);
            h^=(std::uint64_t(k.callback)<<32)|k.pass;h^=std::uint64_t(k.ordinal)*0xa0761d6478bd642full;
            h^=(std::uint64_t(k.seed)<<32)|k.type;h^=std::uint64_t(k.variant)*0xe7037ed1a0b428dbull;
            h=(h^(h>>30))*0xbf58476d1ce4e5b9ull;h=(h^(h>>27))*0x94d049bb133111ebull;h^=h>>31;
            return std::size_t(h^(h>>32));
        }
    };
    struct Equal {Operations*ops;bool operator()(const Key&a,const Key&b)const{++ops->comparisons;return a==b;}};
    Backend& backend_;Limits limits_;Operations operations_;Counts counts_;
    std::unordered_map<Key,Entry,Hash,Equal> entries_;
    std::unordered_map<Key,Entry*,Hash,Equal> lineage_;
    Entry*oldest_=nullptr,*newest_=nullptr;
    std::size_t bucketBytes_=0;
    Capture current_{};std::uint8_t*pending_=nullptr;
    bool capturing_=false,replaying_=false,bad_=false;
    ObserverCaptureFlag*observerFlag_=nullptr;
    void MarkCapturing(bool value){capturing_=value;if(observerFlag_)observerFlag_->store(value?1:0,std::memory_order_release);}
    static constexpr std::size_t kCommands=128;
    std::uint64_t canonicalFrame_=0;ReplayInfo lastReplay_{};
    static Key Lineage(Key k){k.generation=0;k.callback=k.pass=k.ordinal=0;return k;}
    void FindPrior(const Key&key){
        ++operations_.lineageLookups;const auto it=lineage_.find(Lineage(key));
        if(it!=lineage_.end()&&it->second){const auto&e=*it->second;lastReplay_.entitySeen=true;lastReplay_.lastFrame=e.canonicalFrame;lastReplay_.priorPass=e.key.pass;lastReplay_.priorOrdinal=e.key.ordinal;}
    }
    bool Fail(const Key&key,const char*reason){lastReplay_.reason=reason;FindPrior(key);return false;}
    void Seal(){if(!pending_)return;auto&c=current_.commands.back();c.vertices.assign(pending_,pending_+4u*c.layout.stride);pending_=nullptr;}
    static std::size_t Charge(const Entry&e){
        // Conservative CPU allocation accounting: node/key/list links, payload
        // capacities and native-reference control blocks. GPU image bytes are
        // owned by the engine and cannot be inferred from a retained pointer.
        std::size_t n=sizeof(Entry)+2*sizeof(Key)+12*sizeof(void*);
        if(e.data){n+=sizeof(Payload)+e.data->commands.capacity()*sizeof(Command)+e.data->finalResources.capacity()*sizeof(std::size_t);
            for(const auto&c:e.data->commands)n+=c.vertices.capacity()+c.layout.attributes.capacity()+(c.imageLease?64:0);
        }return n;
    }
    void Account(const Entry&e,bool add){
        Counts n;n.entries=1;n.valid=e.valid;n.noOps=e.noOp&&e.valid;n.bytes=e.bytes;n.refs=e.refs;n.commands=e.data?e.data->commands.size():0;
#define HF_COUNT(field) if(add)counts_.field+=n.field;else counts_.field-=n.field
        HF_COUNT(entries);HF_COUNT(valid);HF_COUNT(noOps);HF_COUNT(bytes);HF_COUNT(refs);HF_COUNT(commands);
#undef HF_COUNT
    }
    void Detach(Entry&e,bool keepLineage=false){
        if(e.older)e.older->newer=e.newer;else oldest_=e.newer;
        if(e.newer)e.newer->older=e.older;else newest_=e.older;
        if(e.lineageOlder)e.lineageOlder->lineageNewer=e.lineageNewer;
        if(e.lineageNewer)e.lineageNewer->lineageOlder=e.lineageOlder;
        else {auto it=lineage_.find(Lineage(e.key));if(e.lineageOlder||keepLineage)it->second=e.lineageOlder;else lineage_.erase(it);}
        e.older=e.newer=e.lineageOlder=e.lineageNewer=nullptr;
    }
    void Attach(Entry&e){
        e.older=newest_;if(newest_)newest_->newer=&e;else oldest_=&e;newest_=&e;
        auto result=lineage_.try_emplace(Lineage(e.key),nullptr);e.lineageOlder=result.first->second;
        if(e.lineageOlder)e.lineageOlder->lineageNewer=&e;result.first->second=&e;
    }
    void Evict(Entry&e){Key key=e.key;Detach(e);Account(e,false);entries_.erase(key);++operations_.evictions;}
    void Publish(){
        Entry candidate;candidate.key=current_.key;candidate.valid=current_.valid;candidate.noOp=current_.noOp;candidate.reject=current_.reject;
        candidate.unsafeCode=current_.unsafeCode;candidate.canonicalFrame=current_.canonicalFrame;
        if(candidate.valid&&!candidate.noOp){candidate.data=std::make_unique<Payload>();candidate.data->entryState=current_.entryState;candidate.data->exitState=current_.exitState;
            candidate.data->commands=std::move(current_.commands);candidate.data->finalResources=std::move(current_.finalResources);
            for(const auto&c:candidate.data->commands)candidate.refs+=bool(c.imageLease);
        }
        candidate.bytes=Charge(candidate);++operations_.writes;
        bool rejected=false;
        const auto reject=[&](){
            if(!rejected)++operations_.rejected;rejected=true;
            candidate.data.reset();candidate.refs=0;candidate.valid=candidate.noOp=false;
            candidate.reject="cache-capacity-or-byte-budget";candidate.bytes=Charge(candidate);
        };
        // An entry that cannot fit even an empty cache must not evict unrelated
        // history while trying. Publish a small invalid record if space permits.
        if(candidate.bytes>limits_.bytes-bucketBytes_)reject();
        auto prior=entries_.find(candidate.key);
        // Replace a prior result even if the new capture is rejected: retaining
        // yesterday's visible draw for a changed callback would be incorrect.
        if(prior!=entries_.end()){Detach(prior->second,true);Account(prior->second,false);}
        const auto fits=[&](){return counts_.entries<limits_.entries&&candidate.bytes<=limits_.bytes-counts_.bytes;};
        while(!fits()&&oldest_&&oldest_->canonicalFrame<canonicalFrame_)Evict(*oldest_);
        if(!fits()){
            reject();
            // Fresh captures in this frame are pinned. Reject excess admissions
            // instead of evicting this frame's front and thrashing every pass.
            if(!fits()){
                if(prior!=entries_.end()){
                    auto group=lineage_.find(Lineage(prior->first));if(group!=lineage_.end()&&!group->second)lineage_.erase(group);
                    entries_.erase(prior);
                }return;
            }
        }
        if(prior==entries_.end())prior=entries_.try_emplace(candidate.key).first;
        prior->second=std::move(candidate);Attach(prior->second);Account(prior->second,true);
    }
public:
    explicit Cache(Backend&backend):Cache(backend,Limits{}){}
    Cache(Backend&backend,Limits limits):backend_(backend),limits_(limits),entries_(0,Hash{&operations_},Equal{&operations_}),lineage_(0,Hash{&operations_},Equal{&operations_}){
        if(!limits_.entries)limits_.entries=1;if(limits_.entries>32768)limits_.entries=32768;
        if(limits_.bytes>128u*1024u*1024u)limits_.bytes=128u*1024u*1024u;
        entries_.reserve(limits_.entries);lineage_.reserve(limits_.entries);
        bucketBytes_=(entries_.bucket_count()+lineage_.bucket_count())*sizeof(void*);
        // A tiny test/host byte limit still accounts for its fixed index buckets.
        if(limits_.bytes<bucketBytes_)limits_.bytes=bucketBytes_;counts_.bytes=bucketBytes_;
    }
    ~Cache(){MarkCapturing(false);}
    // The observer owns this process-lifetime flag allocation; generated gates
    // may outlive Cache/Runtime teardown. Never expose the Cache object's storage.
    ObserverCaptureFlag* ObserverFlag()const{return observerFlag_;}
    void BindObserverFlag(ObserverCaptureFlag*flag){
        if(observerFlag_&&observerFlag_!=flag)observerFlag_->store(0,std::memory_order_release);
        observerFlag_=flag;MarkCapturing(capturing_);
    }
    Cache(const Cache&)=delete;Cache&operator=(const Cache&)=delete;
    void SetCanonicalFrame(std::uint64_t frame){if(frame<canonicalFrame_)Clear();canonicalFrame_=frame;}
    const ReplayInfo&LastReplay()const{return lastReplay_;}
    Counts GetCounts()const{return counts_;}
    const Operations&GetOperations()const{return operations_;}
    void ResetOperations(){operations_={};}
    Limits GetLimits()const{return limits_;}
    bool Capturing()const{return capturing_;}bool Replaying()const{return replaying_;}
    void Clear(){pending_=nullptr;MarkCapturing(false);current_=Capture{};lineage_.clear();entries_.clear();oldest_=newest_=nullptr;counts_={};counts_.bytes=bucketBytes_;}
    bool Begin(const Key&key){
        if(capturing_||replaying_){Unsafe();if(capturing_)current_.reject="nested-scope";return false;}
        current_=Capture{};current_.key=key;current_.canonicalFrame=canonicalFrame_;current_.entryState=backend_.ReadState();MarkCapturing(true);bad_=false;return true;
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
            // Index each image's last draw once. Preserve last-use order while
            // sampling its final resource state; no quadratic suffix scans.
            std::unordered_map<void*,std::size_t> last;
            for(std::size_t i=0;i<current_.commands.size();++i)last[current_.commands[i].quad.image]=i;
            const auto count=current_.commands.size();
            for(std::size_t i=0;i<count&&!bad_;++i){
                auto image=current_.commands[i].quad.image;if(last.find(image)->second!=i)continue;
                Command final;final.quad.image=image;final.resourceOnly=true;final.imageLease=current_.commands[i].imageLease;
                if(!backend_.DescribeResource(image,final.layout,final.state)){bad_=true;current_.reject="unsupported-final-image-state";break;}
                if(!backend_.SameResource(current_.commands[i],final)){current_.finalResources.push_back(current_.commands.size());current_.commands.push_back(std::move(final));}
                else current_.finalResources.push_back(i);
            }
        }
        current_.exitState=backend_.ReadState();
        current_.noOp=current_.commands.empty() && current_.exitState==current_.entryState;
        current_.valid=!bad_ && (current_.noOp || (backend_.ContextCompatible(current_.exitState,current_.entryState) && backend_.StateRepresentable(current_.exitState)));
        if (!current_.valid) {
            if(!bad_)current_.reject=backend_.ContextCompatible(current_.exitState,current_.entryState)?"unsupported-exit-state":"context-changed-in-callback";
            current_.commands.clear();
        }
        MarkCapturing(false);Publish();
    }
    // No fallback Lua on extra render frames. False means no cached drawing; the
    // next canonical tick will execute Lua and rebuild. Log this compatibility gap.
    bool Replay(const Key& key) {
        lastReplay_={};
        if(capturing_||replaying_)return Fail(key,"scope-busy");
        ++operations_.exactLookups;auto it=entries_.find(key);
        if(it==entries_.end())return Fail(key,"missing-key");
        Entry*found=&it->second;
        if(!found->valid){lastReplay_.captureReject=found->reject;lastReplay_.unsafeCode=found->unsafeCode;return Fail(key,"invalid-capture");}
        if(found->noOp)return true; // One lookup; no lineage or backend state work.
        auto&data=*found->data;
        if(data.commands.empty()&&!backend_.ContextCompatible(backend_.ReadState(),data.entryState))return Fail(key,"context-changed");
        if(!backend_.StateRepresentable(data.exitState))return Fail(key,"exit-shader-generation-or-contract");
        for(const auto&c:data.commands)if(!backend_.CanReplay(c))return Fail(key,backend_.ReplayFailure());
        const State saved=backend_.ReadState(); replaying_=true;
        bool ok=true;
        for (const auto& c:data.commands) {
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
            for(auto i:data.finalResources)backend_.PrepareResource(data.commands[i]);
        }
        backend_.ApplyState(ok ? data.exitState : saved); replaying_=false;if(!ok)FindPrior(key);return ok;
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
