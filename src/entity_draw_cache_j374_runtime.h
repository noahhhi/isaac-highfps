#pragma once
#include "entity_draw_cache_j374_ref.h"
#include "entity_draw_cache_j374_hooks.h"
#include "render_ordinal_index.h"
#include "shader_location_cache.h"
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <atomic>
#include <unordered_map>
#include <intrin.h>
namespace highfps_entity_probe {
struct Host {
    ResolveAddress address=nullptr;
    bool (*expect)(std::uintptr_t,const std::uint8_t*,std::size_t,const char*)=nullptr;
    bool (*poke)(std::uintptr_t,const std::uint8_t*,std::size_t)=nullptr;
    void (*writeJump)(std::uint8_t*,const void*)=nullptr;
    void (*log)(const char*)=nullptr;
    bool (*insideRender)()=nullptr;
    bool cacheShaderLocations=true;
    bool profileLocations=false; // diagnostic counters only; no per-query logging
};
class NativeBackend final:public Backend {
    friend struct NativeBackendTestAccess;
    ResolveAddress address_;
    const char* replayFailure_="none";
    using GetInt=void(WINAPI*)(unsigned,int*);
    using IsEnabled=unsigned char(WINAPI*)(unsigned);
    GetInt getInt_=nullptr; IsEnabled isEnabled_=nullptr;
    IsEnabled isTexture_=nullptr;
    using GetTextureParameter=void(WINAPI*)(unsigned,unsigned,int*);
    GetTextureParameter getTextureParameter_=nullptr;
    using CurrentContext=HGLRC(WINAPI*)();
    CurrentContext context_=nullptr;
    using GetProgram=void(WINAPI*)(unsigned,unsigned,int*);
    using GetUniform=void(WINAPI*)(unsigned,unsigned,int,int*,int*,unsigned*,char*);
    GetProgram getProgram_=nullptr;GetUniform getUniform_=nullptr;
    using GetAttachment=void(WINAPI*)(unsigned,unsigned,unsigned,int*);
    GetAttachment getAttachment_=nullptr;
    HGLRC knownContext_=nullptr;
    std::unordered_map<std::uint32_t,std::uint64_t> textureGenerations_;
    inline static std::atomic<std::uint64_t> foreignTextureEpoch_{0};
    std::uint64_t TextureGeneration(std::uint32_t name)const {
        const auto it=textureGenerations_.find(name);return it==textureGenerations_.end()?0:it->second;
    }
    bool TextureNames(void* image,std::uint64_t&names)const {
        if(!image){names=0;return true;}
        auto**vt=*reinterpret_cast<void***>(image);
        // All J374 ImagePlatform implementations share unload and bind methods.
        // The abstract ImageBase has neither proved texture storage nor ownership.
        if(vt[30]!=reinterpret_cast<void*>(address_(0xA711E0)) || vt[33]!=reinterpret_cast<void*>(address_(0xA71260)))return false;
        const auto*p=static_cast<const std::uint8_t*>(image);
        names=Pair(*reinterpret_cast<const std::uint32_t*>(p+0x78),*reinterpret_cast<const std::uint32_t*>(p+0x7C));return true;
    }
    static bool NamesInclude(std::uint64_t names,std::uint32_t name){return name && (std::uint32_t(names)==name || std::uint32_t(names>>32)==name);}
    template<class T>T Read(std::uintptr_t va)const{return *reinterpret_cast<T*>(address_(va));}
    static std::uint64_t Pair(std::uint32_t a,std::uint32_t b){return a|(std::uint64_t(b)<<32);}
    int ShaderKind(std::uintptr_t shader)const {
        const std::uintptr_t objects[]={0xBD56F8,0xBD5724,0xBD5750,0xBD296C,0xBD2A74,0xBD2890,0xBD2788};
        for(unsigned i=0;i<7;++i)if(shader==address_(objects[i]))return int(i);
        return -1;
    }
    void StampShader(State&s)const {
        if(ShaderKind(std::uintptr_t(s.word[9]))<0)return;
        auto* p=reinterpret_cast<const std::uint8_t*>(std::uintptr_t(s.word[9]));
        s.word[18]=*reinterpret_cast<const std::uint32_t*>(p+0x28);
        s.word[19]=*reinterpret_cast<const std::uint32_t*>(p+8);
    }
    bool UniformContract(unsigned program) {
        struct Timer {
            std::uint64_t& ticks;LARGE_INTEGER begin;
            explicit Timer(std::uint64_t& target):ticks(target){QueryPerformanceCounter(&begin);}
            ~Timer(){LARGE_INTEGER end;QueryPerformanceCounter(&end);ticks+=end.QuadPart-begin.QuadPart;}
        } timer(stats.uniformQpc);
        ++stats.uniformChecks;
        if(!getProgram_ || !getUniform_) {
            auto gl=GetModuleHandleA("opengl32.dll");
            using GetProc=PROC(WINAPI*)(LPCSTR);
            auto get=gl?reinterpret_cast<GetProc>(GetProcAddress(gl,"wglGetProcAddress")):nullptr;
            if(!get)return false;
            auto a=get("glGetProgramiv"),b=get("glGetActiveUniform");
            const auto valid=[](PROC p){const auto v=reinterpret_cast<std::uintptr_t>(p);return v>3 && v!=~std::uintptr_t(0);};
            if(!valid(a)||!valid(b))return false;
            getProgram_=reinterpret_cast<GetProgram>(a);getUniform_=reinterpret_cast<GetUniform>(b);
        }
        stats.uniformGlQueries+=2;
        int linked=0,count=0;getProgram_(program,0x8B82,&linked);getProgram_(program,0x8B86,&count);
        if(!linked || count<0 || count>2)return false;
        unsigned seen=0;
        for(int i=0;i<count;++i) {
            char name[64]{};int length=0,size=0;unsigned type=0;
            ++stats.uniformGlQueries;getUniform_(program,unsigned(i),sizeof name,&length,&size,&type,name);
            if(length<0 || length>=int(sizeof name) || size!=1)return false;
            name[length]=0;
            unsigned bit=0;
            if(std::strcmp(name,"Transform")==0 && type==0x8B5C)bit=1;
            if(std::strcmp(name,"Texture0")==0 && type==0x8B5E)bit=2;
            if(!bit || (seen&bit))return false;seen|=bit;
        }
        return true;
    }
    bool ShaderValid(const State&s) {
        if(!s.word[9])return true;
        const int kind=ShaderKind(std::uintptr_t(s.word[9]));if(kind<0)return false;
        auto* p=reinterpret_cast<const std::uint8_t*>(std::uintptr_t(s.word[9]));
        if(!p[4] || !s.word[18] || s.word[18]==0xffffffffu ||
           s.word[18]!=*reinterpret_cast<const std::uint32_t*>(p+0x28) ||
           s.word[19]!=*reinterpret_cast<const std::uint32_t*>(p+8))return false;
        if(kind<3)return true; // Existing native simple shader contracts.
        const unsigned indices[]={0,10,15,16};
        if(Read<std::uint32_t>(0xAD2AF8+4*indices[kind-3])!=s.word[9])return false;
        const auto n=*reinterpret_cast<const std::uint32_t*>(p+0xC);
        const auto* attrs=reinterpret_cast<const std::uint32_t*>(std::uintptr_t(s.word[19]));
        const unsigned expected[]={5,6,7,4,3,2,1,3,4};
        if(!attrs || n!=unsigned(kind==4?9:8))return false;
        for(unsigned i=0;i<n;++i)if(attrs[2*i+1]!=expected[i])return false;
        // Reflect every validation, not only by program ID: GL may recycle names
        // after a shader reload. Extra user uniforms are still unsupported.
        return UniformContract(unsigned(s.word[18]));
    }
    static bool Match(const Layout&l,std::initializer_list<std::uint8_t> a) {
        return l.attributes.size()==a.size() && std::equal(a.begin(),a.end(),l.attributes.begin());
    }
    std::uintptr_t FormatDescriptor(unsigned predefined,const Layout&l)const {
        std::uintptr_t va=0;
        if(predefined==0 && Match(l,{5,6}))va=0xBCBCF0;
        if(predefined==1 && Match(l,{5,6,7}))va=0xBCBCD0;
        if(predefined==3 && Match(l,{5,6,7,4,3,2,1,3}))va=0xBAF720;
        if(predefined==3 && Match(l,{5,6,7,4,3,2,1,3,4}))va=0xBAF5E8;
        if(!va)return 0;
        for(unsigned i=0;i<l.attributes.size();++i)if(Read<std::uint32_t>(va+8*i+4)!=l.attributes[i])return 0;
        if(Read<std::uint32_t>(va+8*l.attributes.size()+4)!=0)return 0;
        return address_(va);
    }
    bool ImageState(void* image,Layout&l,State&s)const {
        if(!image)return false;
        auto* p=static_cast<std::uint8_t*>(image);auto** vt=*reinterpret_cast<void***>(image);
        if(vt[2]!=reinterpret_cast<void*>(address_(0xA06530)) || vt[3]!=reinterpret_cast<void*>(address_(0xA06510)))return false;
        l={};l.stride=*reinterpret_cast<std::uint16_t*>(p+0x3A);
        const auto n=p[0x38];auto* attrs=*reinterpret_cast<std::uint8_t**>(p+0x34);
        if(!attrs || !n || n>32 || !l.stride || l.stride>512)return false;
        l.attributes.assign(attrs,attrs+n);
        static constexpr unsigned sizes[]={0,4,8,12,16,12,16,8,4};unsigned offset=0;
        for(auto a:l.attributes){if(!a || a>8)return false;if(a==5)l.positionOffset=std::uint16_t(offset);offset+=sizes[a];}
        if(!l.Valid())return false;
        s.word[20]=*reinterpret_cast<std::uint32_t*>(p+0x20);
        s.word[21]=Pair(*reinterpret_cast<std::uint32_t*>(p+0x24),*reinterpret_cast<std::uint32_t*>(p+0x28));
        s.word[22]=Pair(*reinterpret_cast<std::uint32_t*>(p+0x2C),*reinterpret_cast<std::uint32_t*>(p+0x30));
        s.word[23]=FormatDescriptor(unsigned(s.word[20]),l);
        if(!TextureNames(image,s.word[24]))return false;
        s.word[25]=TextureGeneration(std::uint32_t(s.word[24]));
        s.word[26]=TextureGeneration(std::uint32_t(s.word[24]>>32));
        s.word[28]=foreignTextureEpoch_.load();
        return true;
    }
public:
    struct Stats {std::uint64_t stateReads=0,stateQpc=0,describeReject=0,retainReject=0,formatReplay=0,uniformChecks=0,uniformGlQueries=0,uniformQpc=0;};
    Stats stats;
    explicit NativeBackend(ResolveAddress address):address_(address) {
        HMODULE gl=GetModuleHandleA("opengl32.dll");
        if(gl){getInt_=reinterpret_cast<GetInt>(GetProcAddress(gl,"glGetIntegerv"));
               isEnabled_=reinterpret_cast<IsEnabled>(GetProcAddress(gl,"glIsEnabled"));
               isTexture_=reinterpret_cast<IsEnabled>(GetProcAddress(gl,"glIsTexture"));
               getTextureParameter_=reinterpret_cast<GetTextureParameter>(GetProcAddress(gl,"glGetTexParameteriv"));
               context_=reinterpret_cast<CurrentContext>(GetProcAddress(gl,"wglGetCurrentContext"));}
    }
    bool Ready(){if(!getInt_ || !isEnabled_ || !context_ || !context_())return false;knownContext_=context_();return true;}
    void ForeignTextureDelete(){++foreignTextureEpoch_;}
    bool KnownTextureContext()const{return context_ && knownContext_ && context_()==knownContext_;}
    bool ExistingTexture(std::uint32_t name,bool&exists)const {
        if(!name){exists=false;return true;}
        if(!KnownTextureContext() || !isTexture_)return false;
        exists=isTexture_(name)!=0;return true;
    }
    bool BoundTexture2D(unsigned target,unsigned&name)const {
        if(target!=0x0DE1 || !KnownTextureContext() || !isTexture_)return false;
        int unit=0,bound=0;getInt_(0x84E0,&unit);getInt_(0x8069,&bound);
        if(unit<0x84C0 || unit>=0x84C0+192 || !bound || !isTexture_(unsigned(bound)))return false;
        name=unsigned(bound);return true;
    }
    bool SameTextureParameter(unsigned pname,int value,bool&same)const {
        if(!getTextureParameter_)return false;
        int before=0;getTextureParameter_(0x0DE1,pname,&before);same=before==value;return true;
    }
    void TextureDeleted(std::uint32_t name){if(name)++textureGenerations_[name];}
    // Query the real bound FBO as well as native currentRenderTarget. Depth or
    // extra color texture attachments must not be mistaken for unrelated names.
    // Return -1 for unknown state, 0 unrelated, 1 actual attachment dependency.
    int AttachmentDependency(int n,const unsigned*names) {
        if(!KnownTextureContext())return -1;
        std::uint64_t rtNames=0;
        if(!TextureNames(reinterpret_cast<void*>(Read<std::uint32_t>(0xBD56CC)),rtNames))return -1;
        for(int i=0;i<n;++i)if(NamesInclude(rtNames,names[i]))return 1;
        int fbo=0;getInt_(0x8CA6,&fbo);if(!fbo)return 0;
        if(!getAttachment_) {
            auto gl=GetModuleHandleA("opengl32.dll");using GetProc=PROC(WINAPI*)(LPCSTR);
            auto get=gl?reinterpret_cast<GetProc>(GetProcAddress(gl,"wglGetProcAddress")):nullptr;
            if(!get)return -1;auto fn=get("glGetFramebufferAttachmentParameteriv");
            if(reinterpret_cast<std::uintptr_t>(fn)<=3 || reinterpret_cast<std::uintptr_t>(fn)==~std::uintptr_t(0))return -1;
            getAttachment_=reinterpret_cast<GetAttachment>(fn);
        }
        int colors=0;getInt_(0x8CDF,&colors);if(colors<1 || colors>32)return -1;
        for(int i=0;i<colors+2;++i) {
            const unsigned attachment=i<colors?0x8CE0u+unsigned(i):(i==colors?0x8D00u:0x8D20u);
            int type=-1,name=0;getAttachment_(0x8D40,attachment,0x8CD0,&type);
            if(type==0)continue;
            if(type==0x8D41)continue; // Renderbuffer names are a different namespace.
            if(type!=0x1702)return -1;
            getAttachment_(0x8D40,attachment,0x8CD1,&name);
            if(!name)return -1;
            for(int j=0;j<n;++j)if(names[j]==unsigned(name))return 1;
        }
        return 0;
    }
    State ReadState() override {
        LARGE_INTEGER begin;QueryPerformanceCounter(&begin);
        State s;
        s.word[0]=Read<std::uint32_t>(0xBD56CC);s.word[1]=Read<std::uint8_t>(0xBD56D0);
        s.word[2]=Pair(Read<std::uint32_t>(0xC0E944),Read<std::uint32_t>(0xC0E928));
        s.word[3]=Pair(Read<std::uint32_t>(0xC0E71C),Read<std::uint32_t>(0xC0E92C));
        int vp[4]{},sc[4]{};getInt_(0x0BA2,vp);getInt_(0x0C10,sc);
        s.word[4]=Pair(vp[0],vp[1]);s.word[5]=Pair(vp[2],vp[3]);
        s.word[6]=Pair(sc[0],sc[1]);s.word[7]=Pair(sc[2],sc[3]);s.word[8]=isEnabled_(0x0C11);
        s.word[9]=Read<std::uint32_t>(0xBD56C8);StampShader(s);
        int fbo=0;getInt_(0x8CA6,&fbo);
        s.word[27]=Pair(std::uint32_t(reinterpret_cast<std::uintptr_t>(context_?context_():nullptr)),unsigned(fbo));
        s.word[28]=foreignTextureEpoch_.load();
        if(!TextureNames(reinterpret_cast<void*>(std::uintptr_t(s.word[0])),s.word[29])) {
            s.word[29]=s.word[30]=s.word[31]=~std::uint64_t(0);
        } else {
            s.word[30]=TextureGeneration(std::uint32_t(s.word[29]));s.word[31]=TextureGeneration(std::uint32_t(s.word[29]>>32));
        }
        for(unsigned i=0;i<5;++i)s.word[10+i]=Read<std::uint32_t>(0xBD56B4+4*i);
        s.word[15]=Pair(Read<std::uint32_t>(0xBAF5E4),Read<std::uint32_t>(0xBAF7A0));
        s.word[16]=Read<std::uint8_t>(0xBD56D1);s.word[17]=Read<std::uint8_t>(0xBD56D3);
        LARGE_INTEGER end;QueryPerformanceCounter(&end);++stats.stateReads;stats.stateQpc+=end.QuadPart-begin.QuadPart;
        return s;
    }
    bool ContextCompatible(const State&a,const State&b)override {
        for(unsigned i=0;i<9;++i)if(a.word[i]!=b.word[i])return false;
        for(unsigned i=15;i<18;++i)if(a.word[i]!=b.word[i])return false;
        for(unsigned i=27;i<32;++i)if(a.word[i]!=b.word[i])return false;
        return true;
    }
    bool StateRepresentable(const State&s)override {
        if(s.word[28]!=foreignTextureEpoch_.load() || s.word[30]==~std::uint64_t(0))return false;
        if(s.word[30]!=TextureGeneration(std::uint32_t(s.word[29])) || s.word[31]!=TextureGeneration(std::uint32_t(s.word[29]>>32)))return false;
        return ShaderValid(s);
    }
    bool StateUsesTexture(const State&s,std::uint32_t name)const override{return NamesInclude(s.word[29],name);}
    bool CommandUsesTexture(const Command&c,std::uint32_t name)const override{return NamesInclude(c.state.word[24],name) || StateUsesTexture(c.state,name);}
    bool Describe(void* image,Layout&l,State&s)override {
        s=ReadState();if(!ImageState(image,l,s))return false;
        const auto predefined=unsigned(s.word[20]);
        if(predefined<3){s.word[9]=address_(0xBD56F8+0x2C*predefined);StampShader(s);}
        else if(predefined!=3 || !s.word[23]){++stats.describeReject;return false;}
        if(!ShaderValid(s)){++stats.describeReject;return false;}
        if(predefined==3) {
            const int kind=ShaderKind(std::uintptr_t(s.word[9]));
            if(kind<3 || (kind==4?l.stride!=104:l.stride!=88)){++stats.describeReject;return false;}
        }
        return true;
    }
    bool HasResourceState()const override{return true;}
    bool DescribeResource(void* image,Layout&l,State&s)override {
        // Resource commands do not inherit or apply the global shader selection.
        if(!ImageState(image,l,s))return false;
        return s.word[23] || s.word[20]==2;
    }
    bool SameResource(const Command&a,const Command&b)const override {
        if(!(a.layout==b.layout))return false;
        for(unsigned i=20;i<27;++i)if(a.state.word[i]!=b.state.word[i])return false;
        return true;
    }
    std::shared_ptr<void> Retain(void* image)override{auto p=RetainJ374Image(image,address_);stats.retainReject+=!p;return p;}
    const char* ReplayFailure()const override{return replayFailure_;}
    bool CanReplay(const Command&c)override {
        replayFailure_="none";
        Layout nowLayout;State now;
        if(!ImageState(c.quad.image,nowLayout,now)){replayFailure_="resource-layout-invalid";return false;}
        if(now.word[24]!=c.state.word[24] || now.word[25]!=c.state.word[25] || now.word[26]!=c.state.word[26] || now.word[28]!=c.state.word[28]) {
            replayFailure_="texture-name-or-generation-changed";return false;
        }
        if(!now.word[23] && now.word[20]!=2){replayFailure_="resource-current-format-unsupported";return false;}
        const auto descriptor=FormatDescriptor(unsigned(c.state.word[20]),c.layout);
        if(c.state.word[23]!=descriptor){replayFailure_="resource-descriptor-changed";return false;}
        // Enum 2 has no proved setter descriptor; preserve its previous immutable
        // layout support, but never synthesize a descriptor or enum for it.
        if(!descriptor && (c.state.word[20]!=2 || now.word[20]!=2 || !(nowLayout==c.layout))) {
            replayFailure_="resource-format-not-replayable";return false;
        }
        if(descriptor && !J374Hooks::setFormat){replayFailure_="format-hook-unavailable";return false;}
        if(!c.resourceOnly) {
            if(!ContextCompatible(ReadState(),c.state)){replayFailure_="context-changed";return false;}
            if(!ShaderValid(c.state)){replayFailure_="shader-generation-or-uniform-contract";return false;}
            if(c.vertices.size()!=4u*c.layout.stride){replayFailure_="invalid-vertex-snapshot";return false;}
        }
        return true;
    }
    bool PrepareResource(const Command&c)override {
        Layout layout;State now;
        if(!ImageState(c.quad.image,layout,now))return false;
        auto* p=static_cast<std::uint8_t*>(c.quad.image);
        if(!(layout==c.layout) || now.word[20]!=c.state.word[20]) {
            if(!c.state.word[23] || !J374Hooks::setFormat)return false;
            J374Hooks::setFormat(c.quad.image,reinterpret_cast<const void*>(std::uintptr_t(c.state.word[23])));
            ++stats.formatReplay;
            layout={};now={};
            if(!ImageState(c.quad.image,layout,now) || !(layout==c.layout) || now.word[20]!=c.state.word[20])return false;
        }
        // Native A06750/A06770 setters are plain assignments to these four fields.
        // Restore each sampled image's state before native batch selection.
        for(unsigned i=0;i<4;++i)*reinterpret_cast<std::uint32_t*>(p+0x24+4*i)=
            std::uint32_t(c.state.word[21+i/2]>>(32*(i%2)));
        return true;
    }
    void ApplyState(const State&s)override {
        for(unsigned i=0;i<5;++i)*reinterpret_cast<std::uint32_t*>(address_(0xBD56B4+4*i))=std::uint32_t(s.word[10+i]);
        *reinterpret_cast<std::uint32_t*>(address_(0xBD56C8))=std::uint32_t(s.word[9]);
    }
    std::uint8_t* Submit(const Quad&q)override {
        ++J374Hooks::submitDepth;
        auto* result=J374Hooks::submit(q.image,&q.source,&q.destination,&q.color[0],&q.color[1],&q.color[2],&q.color[3]);
        --J374Hooks::submitDepth;return result;
    }
};
class Runtime {
    friend struct RuntimeTestAccess;
    Host host_;NativeBackend backend_;Cache cache_;
    highfps::RenderOrdinalIndex ordinals_;
    std::uint64_t epoch_=~std::uint64_t(0),canonicalFrame_=0;unsigned externalPass_=0;
    std::array<unsigned,64> detailedMisses_{};unsigned totalDetailedMisses_=0,windowDetailedMisses_=0,bloodOriginTraces_=0;
    struct Patch{std::uintptr_t va;std::vector<std::uint8_t> bytes;};
    std::vector<Patch> patches_;std::vector<void*> executable_;
    struct GlGuard {const char*name;std::uintptr_t slot,lazy;void*gate;};
    std::vector<GlGuard> glGuards_;
    bool installed_=false,frameUnsafe_=false;
    std::array<std::uint64_t,512> reasons_{};
    std::array<std::uint64_t,64> canonicalCounts_{},hitCounts_{},missCounts_{},outsideCounts_{};
    std::array<std::uint64_t,64> absentCanonicalCounts_{},absentPureCounts_{};
    ULONGLONG nextReport_=0;
    inline static Runtime* active_=nullptr;
    using LocationKind=highfps::shader_locations::Kind;
    highfps::shader_locations::Cache locations_;
    // Separate from generic observer gates: no gate may wrap these hooks again.
    struct LocationSlot {const char*name;std::uintptr_t slot,lazy;};
    inline static constexpr LocationSlot locationSpecs_[]={
        {"glGetAttribLocation",0xBCEC50,0xA2A950},{"glGetUniformLocation",0xBCECA8,0xA2E0E0},
        {"glLinkProgram",0xBCECD8,0xA30370},{"glDeleteProgram",0xBCECE8,0xA26F50},
        {"glProgramBinary",0xBCC204,0xA353B0},{"glLinkProgramARB",0xBCC4F8,0xA303A0},
        {"glProgramBinaryOES",0xBCDDB0,0xA353E0},
        {"wglMakeCurrent",0xAD0368,0},{"wglDeleteContext",0xAD036C,0},{"wglShareLists",0xAD0374,0},
        {"glDeleteObjectARB",0xBCCFD0,0xA26EB0}
    };
    static constexpr unsigned locationSlotCount_=sizeof(locationSpecs_)/sizeof(locationSpecs_[0]);
    inline static std::atomic<void*> locationTargets_[locationSlotCount_]{};
    inline static std::atomic<std::uint32_t> locationMutationSerial_{0},locationMutationsActive_{0};
    // Deleting a bound GL program is deferred until unbind. Never re-admit a
    // deleted numeric name, even after Link/BeginFrame: a positive query between
    // DeleteProgram and final unbind must not become a stale post-delete hit.
    // Process-lifetime tombstones deliberately trade later name-reuse hits for
    // correctness; overflow disables only this optional optimization.
    inline static std::atomic<unsigned> locationDeleted_[128]{};
    inline static std::atomic<unsigned> locationDeletedCount_{0};
    inline static std::atomic<bool> locationDeletedFull_{false};
    std::atomic<bool> locationHealthy_{false};
    void** locationSlots_[locationSlotCount_]{};
    std::uintptr_t locationBuiltinObjects_[7]{},locationKnownCallers_[5]{},locationLazyAddresses_[locationSlotCount_]{};
    void* locationHookAddresses_[locationSlotCount_]{};
    const std::uint8_t* locationLastBuiltin_=nullptr;
    using LocationContextFn=HGLRC(WINAPI*)();
    LocationContextFn locationContext_=nullptr;
    std::uint32_t locationSerialSeen_=0;
    std::uintptr_t locationContextSeen_=0;
    unsigned locationUpdateDepth_=0;
    std::uint64_t locationProfile_[4][7]{};
    std::atomic<std::uint32_t> locationProfileForeign_{0};
    void ProfileLocation(unsigned kind,std::uintptr_t caller){
        const unsigned phase=locationUpdateDepth_?0u:(host_.insideRender&&host_.insideRender()? (J374Hooks::canonical?1u:2u):3u);
        unsigned index=kind?6u:5u;
        for(unsigned i=0;i<5;++i)if(caller==locationKnownCallers_[i]){index=i;break;}
        ++locationProfile_[phase][index];
    }
    void ReportLocationProfile(){
        if(!host_.profileLocations)return;
        const char*phaseNames[]={"update","canonical-render","pure-render","outside"};
        for(unsigned phase=0;phase<4;++phase){
            auto*count=locationProfile_[phase];std::uint64_t sum=0;for(unsigned i=0;i<7;++i)sum+=count[i];
            if(sum){char line[384];std::snprintf(line,sizeof line,
                "[shader-query-source] phase=%s thread=main a15cff=%llu a15e19=%llu a0ccec=%llu a15e8b=%llu a15ea9=%llu unknownAttr=%llu unknownUniform=%llu",
                phaseNames[phase],static_cast<unsigned long long>(count[0]),static_cast<unsigned long long>(count[1]),
                static_cast<unsigned long long>(count[2]),static_cast<unsigned long long>(count[3]),static_cast<unsigned long long>(count[4]),
                static_cast<unsigned long long>(count[5]),static_cast<unsigned long long>(count[6]));Message(line);}
            for(unsigned i=0;i<7;++i)count[i]=0;
        }
        const auto foreign=locationProfileForeign_.exchange(0);
        if(foreign){char line[96];std::snprintf(line,sizeof line,"[shader-query-source] thread=foreign calls=%u",foreign);Message(line);}
    }
    std::uint64_t locationCalls_=0,locationHits_=0,locationForwards_=0,locationBypass_=0,locationLazy_=0,locationNegative_=0;
    using LocationFn=int(WINAPI*)(unsigned,const char*);
    static bool LocationName(const char*input,char(&copy)[highfps::shader_locations::Cache::NameBytes],std::size_t&length) {
        length=0;if(!input)return false;
#if defined(_MSC_VER)
        __try {
            for(std::size_t i=0;i<sizeof(copy);++i){copy[i]=input[i];if(!copy[i]){length=i;return i!=0;}}
        } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#else
        for(std::size_t i=0;i<sizeof(copy);++i){SIZE_T n=0;if(!ReadProcessMemory(GetCurrentProcess(),input+i,copy+i,1,&n)||n!=1)return false;if(!copy[i]){length=i;return i!=0;}}
#endif
        return false;
    }
    // Resolve immutable image addresses once. Only the object address is MRU:
    // initialized and program ID are read live, even on the hottest repeat hit.
    void PrepareLocationAddresses(){
        const std::uintptr_t builtins[]={0xBD56F8,0xBD5724,0xBD5750,0xBD296C,0xBD2A74,0xBD2890,0xBD2788};
        const std::uintptr_t callers[]={0xA15D05,0xA15E1F,0xA0CCF2,0xA15E91,0xA15EAF};
        for(unsigned i=0;i<7;++i)locationBuiltinObjects_[i]=host_.address(builtins[i]);
        for(unsigned i=0;i<5;++i)locationKnownCallers_[i]=host_.address(callers[i]);
        for(unsigned i=0;i<locationSlotCount_;++i){locationLazyAddresses_[i]=locationSpecs_[i].lazy?host_.address(locationSpecs_[i].lazy):0;locationHookAddresses_[i]=LocationHook(i);}
        locationLastBuiltin_=nullptr;
    }
    bool LocationBuiltin(unsigned program) {
        if(!program)return false;
#if defined(_MSC_VER)
        __try {
#endif
            const auto*last=locationLastBuiltin_;
            if(last && *reinterpret_cast<const volatile std::uint8_t*>(last+4)==1 &&
               *reinterpret_cast<const volatile std::uint32_t*>(last+0x28)==program)return true;
            for(auto address:locationBuiltinObjects_){
                const auto*p=reinterpret_cast<const std::uint8_t*>(address);
                if(p && p!=last && *reinterpret_cast<const volatile std::uint8_t*>(p+4)==1 &&
                   *reinterpret_cast<const volatile std::uint32_t*>(p+0x28)==program){locationLastBuiltin_=p;return true;}
            }
#if defined(_MSC_VER)
        } __except(EXCEPTION_EXECUTE_HANDLER){locationLastBuiltin_=nullptr;return false;}
#endif
        return false;
    }
    bool LocationCaller(unsigned kind,std::uintptr_t caller)const {
        if(kind==0)return caller==locationKnownCallers_[0]||caller==locationKnownCallers_[1];
        return caller==locationKnownCallers_[2]||caller==locationKnownCallers_[3]||caller==locationKnownCallers_[4];
    }
    static void* LocationHook(unsigned index) {
        switch(index){
        case 0:return reinterpret_cast<void*>(&AttributeLocation);case 1:return reinterpret_cast<void*>(&UniformLocation);
        case 2:return reinterpret_cast<void*>(&LinkProgram);case 3:return reinterpret_cast<void*>(&DeleteProgram);
        case 4:return reinterpret_cast<void*>(&ProgramBinary);case 5:return reinterpret_cast<void*>(&LinkProgramARB);
        case 6:return reinterpret_cast<void*>(&ProgramBinaryOES);case 7:return reinterpret_cast<void*>(&MakeCurrent);
        case 8:return reinterpret_cast<void*>(&DeleteContext);case 9:return reinterpret_cast<void*>(&ShareLists);
        case 10:return reinterpret_cast<void*>(&DeleteObjectARB);
        default:return nullptr;}
    }
    bool LocationSlotsIntact() {
        for(unsigned i=0;i<locationSlotCount_;++i)
            if(!locationSlots_[i]||*locationSlots_[i]!=locationHookAddresses_[i]){locationHealthy_.store(false);return false;}
        return true;
    }
    static void LocationRearm(Runtime*r,unsigned index,void*called) {
        if(!r||!locationSpecs_[index].lazy||reinterpret_cast<std::uintptr_t>(called)!=r->locationLazyAddresses_[index])return;
        auto**slot=r->locationSlots_[index];if(!slot){r->locationHealthy_.store(false);return;}
        auto*next=*slot;if(next==LocationHook(index))return;
        const auto value=reinterpret_cast<std::uintptr_t>(next);
        if(value<=3||value==~std::uintptr_t(0)||next==called){r->locationHealthy_.store(false);return;}
        // Only a known native resolver may replace its own slot. Never adopt an
        // arbitrary later hook chain (which might call us and recurse).
        locationTargets_[index].store(next,std::memory_order_release);
        if(InterlockedCompareExchangePointer(slot,LocationHook(index),next)!=next)r->locationHealthy_.store(false);
    }
    int LocationQuery(unsigned kind,unsigned program,const char*name,std::uintptr_t caller) {
        auto*target=locationTargets_[kind].load(std::memory_order_acquire);
        auto original=reinterpret_cast<LocationFn>(target);
        const bool main=GetCurrentThreadId()==J374Hooks::gameThreadId;
        if(main){++locationCalls_;if(host_.profileLocations)ProfileLocation(kind,caller);}
        else if(host_.profileLocations)locationProfileForeign_.fetch_add(1,std::memory_order_relaxed);
        const bool lazy=reinterpret_cast<std::uintptr_t>(target)==locationLazyAddresses_[kind];
        if(main&&lazy)++locationLazy_;
        char stableName[highfps::shader_locations::Cache::NameBytes];std::size_t nameLength=0;
        std::uintptr_t context=0;std::uint32_t serial=0;bool eligible=false;
        if(main&&locationHealthy_.load(std::memory_order_acquire)&&
           (locationUpdateDepth_ || (host_.insideRender&&host_.insideRender()))&&
           LocationSlotsIntact()&&LocationCaller(kind,caller)&&LocationBuiltin(program)&&LocationName(name,stableName,nameLength)&&locationContext_) {
            serial=locationMutationSerial_.load(std::memory_order_acquire);
            context=reinterpret_cast<std::uintptr_t>(locationContext_());
            if(serial!=locationSerialSeen_||context!=locationContextSeen_){locations_.Clear();locationSerialSeen_=serial;locationContextSeen_=context;}
            eligible=context && !lazy && !locationMutationsActive_.load(std::memory_order_acquire) && !LocationWasDeleted(program);
            int result=-1;
            if(eligible&&locations_.FindValidated(context,program,LocationKind(kind),stableName,nameLength,result)&&
               !locationMutationsActive_.load(std::memory_order_acquire)&&serial==locationMutationSerial_.load(std::memory_order_acquire)){
                ++locationHits_;return result;
            }
        }
        if(main){++locationForwards_;if(!eligible)++locationBypass_;}
        const int result=original(program,name); // Invalid/negative queries retain native GL errors.
        if(main&&result<0)++locationNegative_;
        LocationRearm(this,kind,target);
        if(eligible && result>=0 && !LocationWasDeleted(program) && locationHealthy_.load(std::memory_order_acquire)&&LocationSlotsIntact()&&
           !locationMutationsActive_.load(std::memory_order_acquire)&&serial==locationMutationSerial_.load(std::memory_order_acquire)&&
           context==reinterpret_cast<std::uintptr_t>(locationContext_()))locations_.StoreValidated(context,program,LocationKind(kind),stableName,nameLength,result);
        return result;
    }
    static int WINAPI AttributeLocation(unsigned program,const char*name){auto*r=active_;if(r)return r->LocationQuery(0,program,name,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));return reinterpret_cast<LocationFn>(locationTargets_[0].load())(program,name);}
    static int WINAPI UniformLocation(unsigned program,const char*name){auto*r=active_;if(r)return r->LocationQuery(1,program,name,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));return reinterpret_cast<LocationFn>(locationTargets_[1].load())(program,name);}
    static unsigned DeletedHash(unsigned program){program^=program>>16;program*=0x7feb352du;program^=program>>15;return program&127u;}
    static bool LocationWasDeleted(unsigned program){
        for(unsigned i=DeletedHash(program),n=0;n<128;++n,i=(i+1)&127u){
            const auto value=locationDeleted_[i].load(std::memory_order_acquire);
            if(value==program)return true;if(!value)return false;
        }return true;
    }
    static void LocationRememberDeleted(unsigned program){
        if(!program)return;
        for(unsigned i=DeletedHash(program),n=0;n<128;++n,i=(i+1)&127u){
            unsigned value=locationDeleted_[i].load(std::memory_order_acquire);
            if(value==program)return;
            if(!value){
                if(locationDeleted_[i].compare_exchange_strong(value,program,std::memory_order_acq_rel)){locationDeletedCount_.fetch_add(1);return;}
                if(value==program)return;
            }
        }
        locationDeletedFull_.store(true);if(active_)active_->locationHealthy_.store(false);
    }
    static void LocationMutationBegin(){
        locationMutationsActive_.fetch_add(1,std::memory_order_acq_rel);
        locationMutationSerial_.fetch_add(1,std::memory_order_acq_rel);
        if(active_&&active_->locationHealthy_.load(std::memory_order_acquire)&&GetCurrentThreadId()==J374Hooks::gameThreadId)active_->cache_.Unsafe(508);
    }
    static void LocationMutationEnd(){locationMutationSerial_.fetch_add(1,std::memory_order_acq_rel);locationMutationsActive_.fetch_sub(1,std::memory_order_release);}
    static void LocationUnary(unsigned index,unsigned program){
        auto*r=active_;auto*target=locationTargets_[index].load();LocationMutationBegin();
        if(index==3||index==10)LocationRememberDeleted(program);
        // OpenGL dispatch is C ABI and nonthrowing; no game/Lua function is called.
        reinterpret_cast<void(WINAPI*)(unsigned)>(target)(program);
        LocationRearm(r,index,target);LocationMutationEnd();
    }
    static void WINAPI LinkProgram(unsigned p){LocationUnary(2,p);}
    static void WINAPI DeleteProgram(unsigned p){LocationUnary(3,p);}
    static void WINAPI DeleteObjectARB(unsigned object){LocationUnary(10,object);}
    static void WINAPI LinkProgramARB(unsigned p){LocationUnary(5,p);}
    static void LocationBinary(unsigned index,unsigned p,unsigned format,const void*bytes,int length){
        auto*r=active_;auto*target=locationTargets_[index].load();LocationMutationBegin();
        reinterpret_cast<void(WINAPI*)(unsigned,unsigned,const void*,int)>(target)(p,format,bytes,length);
        LocationRearm(r,index,target);LocationMutationEnd();
    }
    static void WINAPI ProgramBinary(unsigned p,unsigned f,const void*b,int n){LocationBinary(4,p,f,b,n);}
    static void WINAPI ProgramBinaryOES(unsigned p,unsigned f,const void*b,int n){LocationBinary(6,p,f,b,n);}
    static BOOL WINAPI MakeCurrent(HDC dc,HGLRC context){LocationMutationBegin();const BOOL result=reinterpret_cast<BOOL(WINAPI*)(HDC,HGLRC)>(locationTargets_[7].load())(dc,context);LocationMutationEnd();return result;}
    static BOOL WINAPI DeleteContext(HGLRC context){LocationMutationBegin();const BOOL result=reinterpret_cast<BOOL(WINAPI*)(HGLRC)>(locationTargets_[8].load())(context);LocationMutationEnd();return result;}
    static BOOL WINAPI ShareLists(HGLRC a,HGLRC b){LocationMutationBegin();const BOOL result=reinterpret_cast<BOOL(WINAPI*)(HGLRC,HGLRC)>(locationTargets_[9].load())(a,b);LocationMutationEnd();return result;}
    bool ValidateLocationCache();
    bool InstallLocationCache() {
        if(!host_.cacheShaderLocations||locationDeletedFull_.load())return false;
        if(!ValidateLocationCache())return false;
        auto gl=GetModuleHandleA("opengl32.dll");
        locationContext_=gl?reinterpret_cast<LocationContextFn>(GetProcAddress(gl,"wglGetCurrentContext")):nullptr;
        if(!locationContext_)return false;
        PrepareLocationAddresses();
        const auto checkpoint=patches_.size();bool ok=true;
        for(unsigned i=0;i<locationSlotCount_;++i){
            auto**slot=reinterpret_cast<void**>(host_.address(locationSpecs_[i].slot));const auto value=reinterpret_cast<std::uintptr_t>(*slot);
            if(value<=3||value==~std::uintptr_t(0)||*slot==LocationHook(i)){ok=false;break;}
            locationSlots_[i]=slot;locationTargets_[i].store(*slot,std::memory_order_release);
            const auto replacement=reinterpret_cast<std::uintptr_t>(LocationHook(i));
            if(!Write(locationSpecs_[i].slot,reinterpret_cast<const std::uint8_t*>(&replacement),4)){ok=false;break;}
        }
        if(!ok){
            // Optional failure must not take down the existing rendering pipeline.
            // Any hook whose rollback fails remains a transparent forwarder.
            while(patches_.size()>checkpoint){auto&patch=patches_.back();if(!host_.poke(patch.va,patch.bytes.data(),patch.bytes.size()))break;patches_.pop_back();}
            return false;
        }
        locationHealthy_.store(true,std::memory_order_release);return true;
    }
    using DeleteTexturesFn=void(WINAPI*)(int,const unsigned*);
    inline static std::atomic<DeleteTexturesFn> deleteTarget_{nullptr};
    void** deleteSlot_=nullptr;
    std::uint64_t deleteCalls_=0,deleteUnrelated_=0,deleteDependent_=0,deleteUnknown_=0;
    unsigned deleteTraces_=0;
    static bool ReadableNames(int n,const unsigned*names) {
        if(n<0 || n>4096 || (n && !names))return false;
        auto p=reinterpret_cast<std::uintptr_t>(names),end=p+std::size_t(n)*4;
        if(end<p)return false;
        while(p<end){MEMORY_BASIC_INFORMATION region{};
            if(!VirtualQuery(reinterpret_cast<void*>(p),&region,sizeof region) || region.State!=MEM_COMMIT ||
               region.Protect&(PAGE_NOACCESS|PAGE_GUARD))return false;
            const auto next=reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
            if(next<=p)return false;p=next<end?next:end;
        }
        return true;
    }
    static void WINAPI DeleteTextures(int n,const unsigned*names) {
        const auto original=deleteTarget_.load();auto*r=active_;
        // Installation publishes the original dispatch chain before the hook.
        if(!r){if(original)original(n,names);return;}
        const bool lazy=reinterpret_cast<std::uintptr_t>(original)==r->host_.address(0xA27220);
        if(GetCurrentThreadId()!=J374Hooks::gameThreadId) {
            // Other GL contexts may share objects. Do not inspect game-owned
            // vectors or mutate the per-name map from a loading thread.
            r->backend_.ForeignTextureDelete();
        } else {
            ++r->deleteCalls_;r->cache_.SealPending();
            bool unknown=!ReadableNames(n,names),dependent=false;
            int attachment=0;std::vector<unsigned> liveNames;
            if(!unknown && n){
                for(int i=0;i<n;++i){
                    bool exists=false;
                    if(!r->backend_.ExistingTexture(names[i],exists)){unknown=true;break;}
                    if(exists)liveNames.push_back(names[i]);
                }
                if(!unknown && !liveNames.empty()){
                    attachment=r->backend_.AttachmentDependency(int(liveNames.size()),liveNames.data());unknown=attachment<0;dependent=attachment>0;
                }
            }
            if(unknown){r->backend_.ForeignTextureDelete();r->cache_.Unsafe(145);++r->deleteUnknown_;}
            else {
                for(auto name:liveNames){dependent=r->cache_.TextureDeleted(name) || dependent;r->backend_.TextureDeleted(name);}
                if(dependent){r->cache_.Unsafe(145);++r->deleteDependent_;}else ++r->deleteUnrelated_;
            }
            if(r->cache_.Capturing() && (unknown||dependent))++r->reasons_[145];
            if(r->cache_.Capturing() && r->deleteTraces_<16 && r->host_.log){
                ++r->deleteTraces_;char line[224];
                std::snprintf(line,sizeof line,"[entity-cache-texture] delete n=%d first=%u second=%u dependency=%u attachment=%d unknown=%u",n,
                    !unknown&&n?names[0]:0,!unknown&&n>1?names[1]:0,unsigned(dependent),attachment,unsigned(unknown));r->Message(line);
            }
        }
        if(original)original(n,names);
        // J374's first call resolves and overwrites its dispatch slot. Re-arm
        // immediately so the second delete in Image::Unload is observed too.
        // Preserve the newly resolved target, never substitute a system export.
        if(lazy && r->deleteSlot_) {
            auto* next=*r->deleteSlot_;const auto value=reinterpret_cast<std::uintptr_t>(next);
            if(next!=reinterpret_cast<void*>(&DeleteTextures) && value>3 && value!=~std::uintptr_t(0)) {
                deleteTarget_.store(reinterpret_cast<DeleteTexturesFn>(next));
                if(InterlockedCompareExchangePointer(r->deleteSlot_,reinterpret_cast<void*>(&DeleteTextures),next)!=next)
                    r->backend_.ForeignTextureDelete();
            }
        }
    }
    using TexParameteriFn=void(WINAPI*)(unsigned,unsigned,int);
    using TexImage2DFn=void(WINAPI*)(unsigned,int,int,int,int,int,unsigned,unsigned,const void*);
    inline static std::atomic<TexParameteriFn> parameterTarget_{nullptr};
    inline static std::atomic<TexImage2DFn> imageTarget_{nullptr};
    void** parameterSlot_=nullptr;void** imageSlot_=nullptr;
    std::uint64_t mutationAllowed_=0,mutationNoop_=0,mutationDependent_=0,mutationUnknown_=0;
    unsigned mutationTraces_=0;
    bool ProvenTextureCall(unsigned reason,std::uintptr_t caller)const {
        if(reason==241){for(auto va:{0xA7131Du,0xA71332u,0xA71347u,0xA7135Cu,0xA71456u,0xA71476u,0xA714CFu,0xA714EDu})if(caller==host_.address(va))return true;}
        if(reason==225)return caller==host_.address(0xA71399) || caller==host_.address(0xA71419);
        return false;
    }
    static bool SupportedParameter(unsigned pname,int value){
        if(pname==0x2800 || pname==0x2801)return value==0x2600 || value==0x2601;
        if(pname==0x2802 || pname==0x2803)return value==0x2901 || value==0x812F;
        return false;
    }
    void BeforeTextureMutation(unsigned reason,bool proven,unsigned target,unsigned pname=0,int value=0){
        if(GetCurrentThreadId()!=J374Hooks::gameThreadId){backend_.ForeignTextureDelete();return;}
        cache_.SealPending();unsigned name=0;bool same=false,dependent=false;
        bool unknown=!proven || !backend_.BoundTexture2D(target,name);
        if(!unknown && reason==241)unknown=!backend_.SameTextureParameter(pname,value,same);
        if(!unknown && !same && cache_.Capturing()){
            const int attachment=backend_.AttachmentDependency(1,&name);
            unknown=attachment<0;dependent=attachment>0;
        }
        if(unknown){backend_.ForeignTextureDelete();cache_.Unsafe(reason);++mutationUnknown_;}
        else if(same)++mutationNoop_;
        else {
            dependent=cache_.TextureDeleted(name,reason) || dependent;
            backend_.TextureDeleted(name); // Content/parameter mutation advances the same lifetime generation.
            if(dependent){cache_.Unsafe(reason);++mutationDependent_;}else ++mutationAllowed_;
        }
        if(cache_.Capturing() && (unknown || dependent))++reasons_[reason];
        if(cache_.Capturing() && mutationTraces_<24 && host_.log){
            ++mutationTraces_;char line[240];
            std::snprintf(line,sizeof line,"[entity-cache-texture] mutation reason=%u texture=%u pname=0x%X value=%d proven=%u noop=%u dependency=%u unknown=%u",reason,name,pname,value,unsigned(proven),unsigned(same),unsigned(dependent),unsigned(unknown));Message(line);
        }
    }
    template<class Fn> static void RearmTextureResolver(Runtime*r,void**slot,Fn original,Fn hook,std::uintptr_t lazy,std::atomic<Fn>&destination){
        if(!r || !slot || reinterpret_cast<std::uintptr_t>(original)!=r->host_.address(lazy))return;
        auto* next=*slot;const auto value=reinterpret_cast<std::uintptr_t>(next);
        if(next==reinterpret_cast<void*>(hook))return;
        if(value<=3 || value==~std::uintptr_t(0)){r->backend_.ForeignTextureDelete();return;}
        destination.store(reinterpret_cast<Fn>(next));
        if(InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(hook),next)!=next)r->backend_.ForeignTextureDelete();
    }
    static void WINAPI TexParameteri(unsigned target,unsigned pname,int value){
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());auto*r=active_;const auto original=parameterTarget_.load();
        if(r)r->BeforeTextureMutation(241,r->ProvenTextureCall(241,caller)&&SupportedParameter(pname,value),target,pname,value);
        if(original)original(target,pname,value);
        RearmTextureResolver(r,r?r->parameterSlot_:nullptr,original,&TexParameteri,0xA3B740,parameterTarget_);
    }
    static void WINAPI TexImage2D(unsigned target,int level,int internal,int width,int height,int border,unsigned format,unsigned type,const void*pixels){
        const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());auto*r=active_;const auto original=imageTarget_.load();
        const bool formatKnown=format==0x1903 || format==0x1907 || format==0x1908;
        const bool internalKnown=internal==0x1903 || internal==0x1907 || internal==0x1908 || internal==0x8C40 || internal==0x8C42;
        if(r)r->BeforeTextureMutation(225,r->ProvenTextureCall(225,caller)&&level==0&&border==0&&width>0&&height>0&&formatKnown&&internalKnown&&type==0x1401,target);
        if(original)original(target,level,internal,width,height,border,format,type,pixels);
        RearmTextureResolver(r,r?r->imageSlot_:nullptr,original,&TexImage2D,0xA3B3D0,imageTarget_);
    }
    static void __cdecl Notify(std::uint32_t code){
        if(!active_ || GetCurrentThreadId()!=J374Hooks::gameThreadId)return;
        if((code&0x40000000u) && J374Hooks::submitDepth)return;
        auto& r=*active_;
        if(code&0x80000000u)r.frameUnsafe_=true;
        if(J374Hooks::cache && J374Hooks::cache->Capturing())++r.reasons_[(code&0xffffu)%512];
        J374Hooks::Unsupported(code&0xffffu);
    }
    static void OutsideEvent(unsigned cb){if(active_ && cb<64)++active_->outsideCounts_[cb];}
    static void NoSubscribers(unsigned cb,bool canonical){
        if(active_ && cb<64)++(canonical?active_->absentCanonicalCounts_:active_->absentPureCounts_)[cb];
    }
    static void Event(unsigned cb,bool canonical,bool hit){
        if(!active_ || cb>=64)return;
        if(canonical)++active_->canonicalCounts_[cb];
        else if(hit)++active_->hitCounts_[cb];else ++active_->missCounts_[cb];
    }
    void TraceBloodOrigin(const Key&key,std::uintptr_t game){
        if(key.callback!=56 || key.type!=1000 || key.variant!=7 || bloodOriginTraces_>=6)return;
        ++bloodOriginTraces_;
        const auto room=game>=0x10000?*reinterpret_cast<std::uintptr_t*>(game+0x18300):0;
        unsigned membership=0,readable=0;
        const unsigned listOffsets[]={0x20,0x40,0x50,0x60,0x70};
        if(room>=0x10000)for(unsigned i=0;i<5;++i){
            const auto list=room+0x1218+listOffsets[i];
            const auto data=*reinterpret_cast<std::uintptr_t*>(list+4);
            const auto count=*reinterpret_cast<std::uint32_t*>(list+12);
            if(count>8192 || (count && data<0x10000))continue;
            MEMORY_BASIC_INFORMATION region{};
            if(count && (!VirtualQuery(reinterpret_cast<void*>(data),&region,sizeof region) || region.State!=MEM_COMMIT ||
                region.Protect&(PAGE_NOACCESS|PAGE_GUARD) || data+count*4>reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize))continue;
            readable|=1u<<i;
            for(unsigned n=0;n<count;++n)if(reinterpret_cast<std::uintptr_t*>(data)[n]==key.entity){membership|=1u<<i;break;}
        }
        void* frames[24]{};const auto count=CaptureStackBackTrace(0,24,frames,nullptr);
        char sites[320]{};std::size_t used=0;
        const auto base=host_.address(0x400000);
        for(unsigned i=0;i<count && used+12<sizeof sites;++i){
            const auto address=reinterpret_cast<std::uintptr_t>(frames[i]);
            if(address<base || address>=base+0x6D0000)continue;
            const int n=std::snprintf(sites+used,sizeof sites-used,"%08X ",unsigned(address-base+0x400000));if(n>0)used+=unsigned(n);
        }
        const auto flags=*reinterpret_cast<std::uint64_t*>(key.entity+0x168);
        char line[640];std::snprintf(line,sizeof line,"[entity-blood-origin] entity=0x%08X seed=%u room=0x%08X flags=0x%llX listMask=%u readableMask=%u (main/update/render/effect/temp bits0..4) nativeReturnSites=%s",unsigned(key.entity),key.seed,unsigned(room),static_cast<unsigned long long>(flags),membership,readable,sites);Message(line);
    }
    static void Miss(const Key&key,const Cache::ReplayInfo&info){
        if(!active_ || !active_->host_.log || key.callback>=64)return;
        auto&r=*active_;
        if(r.totalDetailedMisses_>=256 || r.detailedMisses_[key.callback]>=64 || r.windowDetailedMisses_>=16)return;
        ++r.totalDetailedMisses_;++r.detailedMisses_[key.callback];++r.windowDetailedMisses_;
        const auto game=*reinterpret_cast<std::uintptr_t*>(r.host_.address(0xC0C074));
        const unsigned gf=game>=0x10000?*reinterpret_cast<unsigned*>(game+0x264F8):0;
        float x=0,y=0,bx=0,by=0;
        if(key.entity>=0x10000){const auto*p=reinterpret_cast<const float*>(key.entity+0x33C);x=p[0];y=p[1];bx=p[2];by=p[3];}
        const long long age=info.entitySeen?static_cast<long long>(r.canonicalFrame_-info.lastFrame):-1;
        char line[768];std::snprintf(line,sizeof line,"[entity-cache-miss] cb=%u entity=0x%08X type=%u variant=%u seed=%u epoch=%llu canonicalFrame=%llu gf=%u pass=0x%08X ordinal=%u reason=%s capture=%s unsafe=%u seen=%u lastCanonical=%llu age=%lld priorPass=0x%08X priorOrdinal=%u pos=(%.3f,%.3f) backup=(%.3f,%.3f)",
            key.callback,unsigned(key.entity),key.type,key.variant,key.seed,static_cast<unsigned long long>(key.epoch),static_cast<unsigned long long>(r.canonicalFrame_),gf,key.pass,key.ordinal,
            info.reason,info.captureReject,info.unsafeCode,unsigned(info.entitySeen),static_cast<unsigned long long>(info.lastFrame),age,info.priorPass,info.priorOrdinal,x,y,bx,by);r.Message(line);
        r.TraceBloodOrigin(key,game);
    }
    static bool KeyFor(void* entity,unsigned callback,Key& key){
        if(!active_ || !entity)return false;
        auto& r=*active_;auto* p=static_cast<std::uint8_t*>(entity);
        const auto type=*reinterpret_cast<std::uint32_t*>(p+0x28);
        const auto variant=*reinterpret_cast<std::uint32_t*>(p+0x2C);
        const auto seed=*reinterpret_cast<std::uint32_t*>(p+0x3EC);
        key.epoch=r.epoch_;key.entity=reinterpret_cast<std::uintptr_t>(entity);
        key.generation=(std::uint64_t(seed)<<32) ^ (std::uint64_t(type)*0x9e3779b1u) ^ variant;
        key.callback=callback;key.seed=seed;key.type=type;key.variant=variant;
        key.pass=*reinterpret_cast<std::uint32_t*>(r.host_.address(0xBD56CC)) ^ r.externalPass_;
        return !r.frameUnsafe_ && r.NextOrdinal(key);
    }
    bool NextOrdinal(Key& key){
        return ordinals_.Next(key.entity,key.callback,key.pass,key.ordinal);
    }

    void Message(const char*s){if(host_.log)host_.log(s);}
    std::uint8_t* Allocate(){
        auto*p=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,128,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        if(p)executable_.push_back(p);return p;
    }
    bool Write(std::uintptr_t va,const std::uint8_t* p,std::size_t n){
        Patch save{va,{}};auto* old=reinterpret_cast<std::uint8_t*>(host_.address(va));save.bytes.assign(old,old+n);
        if(!host_.poke(va,p,n))return false;patches_.push_back(std::move(save));return true;
    }
    bool Redirect(std::uintptr_t va,void* target,std::size_t n){
        std::vector<std::uint8_t> patch(n,0x90);
        // Encode against the actual patch site, not this temporary vector.
        patch[0]=0xE9;*reinterpret_cast<std::int32_t*>(&patch[1])=std::int32_t(reinterpret_cast<std::uintptr_t>(target)-(host_.address(va)+5));
        return Write(va,patch.data(),patch.size());
    }
    void* Detour(std::uintptr_t va,void* hook,std::size_t n,void** destination){
        auto* tramp=Allocate();if(!tramp)return nullptr;
        std::memcpy(tramp,reinterpret_cast<void*>(host_.address(va)),n);
        host_.writeJump(tramp+n,reinterpret_cast<void*>(host_.address(va)+n));
        *destination=tramp;
        if(!Redirect(va,hook,n))return nullptr;return tramp;
    }
    // ABI-independent observer: saves flags, all GPRs and x87/SSE state before C++.
    // EBP is a temporary stack anchor only while PUSHAD owns the original value.
    std::uint8_t* ObserverGate(void* target,std::uint32_t code,const std::uint8_t* displaced=nullptr,std::size_t n=0){
        auto*p=Allocate();if(!p)return nullptr;
        std::size_t i=0,fastJump=0;
        if(!(code&0x80000000u)) {
            // Non-lazy Notify has no effect outside capture. A separate stable
            // allocation remains readable even after Shutdown/Cache destruction.
            auto*flag=cache_.ObserverFlag();
            if(!flag){auto*storage=Allocate();if(!storage)return nullptr;flag=new(storage) Cache::ObserverCaptureFlag(0);cache_.BindObserverFlag(flag);}
            // PUSHFD; CMP byte [flag],0; JNE slow; POPFD; JMP original;
            // slow: POPFD, then the complete original observer. No GPR, x87 or
            // SSE register is used by the fast check, and both paths restore the
            // incoming flags before executing any displaced native instruction.
            p[i++]=0x9c;p[i++]=0x80;p[i++]=0x3d;
            *reinterpret_cast<std::uint32_t*>(p+i)=std::uint32_t(reinterpret_cast<std::uintptr_t>(flag));i+=4;
            p[i++]=0;p[i++]=0x75;p[i++]=6;p[i++]=0x9d;
            fastJump=i;i+=5;p[i++]=0x9d;
        }
        // Lazy resolver calls always Notify: frameUnsafe must be set even when
        // no callback is being captured. Specialized texture hooks are separate.
        const std::uint8_t prefix[]={0x9c,0x60,0x89,0xe5,0x81,0xec,0x10,0x02,0,0,0x83,0xe4,0xf0,0x0f,0xae,0x04,0x24};
        std::memcpy(p+i,prefix,sizeof prefix);i+=sizeof prefix;
        p[i++]=0x68;*reinterpret_cast<std::uint32_t*>(p+i)=code;i+=4;
        p[i++]=0xE8;*reinterpret_cast<std::int32_t*>(p+i)=std::int32_t(reinterpret_cast<std::uintptr_t>(&Notify)-(reinterpret_cast<std::uintptr_t>(p+i)+4));i+=4;
        const std::uint8_t suffix[]={0x83,0xc4,0x04,0x0f,0xae,0x0c,0x24,0x89,0xec,0x61,0x9d};
        std::memcpy(p+i,suffix,sizeof suffix);i+=sizeof suffix;
        if(fastJump)host_.writeJump(p+fastJump,p+i);
        if(n){std::memcpy(p+i,displaced,n);i+=n;}host_.writeJump(p+i,target);
        FlushInstructionCache(GetCurrentProcess(),p,i+5);return p;
    }
    bool Observe(std::uintptr_t va,std::size_t n,unsigned reason,bool allocator=false){
        auto* site=reinterpret_cast<std::uint8_t*>(host_.address(va));
        auto* gate=ObserverGate(site+n,reason|(allocator?0x40000000u:0),site,n);
        return gate && Redirect(va,gate,n);
    }
    bool RearmGl(GlGuard& g,unsigned reason){
        auto* slot=reinterpret_cast<void**>(host_.address(g.slot));
        if(*slot==g.gate)return true;
        const auto value=reinterpret_cast<std::uintptr_t>(*slot);
        if(std::strcmp(g.name,"glTexParameteri")==0 || std::strcmp(g.name,"glTexImage2D")==0) {
            if(value<=3 || value==~std::uintptr_t(0))return false;
            if(g.gate){backend_.ForeignTextureDelete();return false;}
            std::uintptr_t ptr=0;
            if(std::strcmp(g.name,"glTexParameteri")==0){parameterTarget_.store(reinterpret_cast<TexParameteriFn>(*slot));parameterSlot_=slot;ptr=reinterpret_cast<std::uintptr_t>(&TexParameteri);}
            else {imageTarget_.store(reinterpret_cast<TexImage2DFn>(*slot));imageSlot_=slot;ptr=reinterpret_cast<std::uintptr_t>(&TexImage2D);}
            if(!Write(g.slot,reinterpret_cast<const std::uint8_t*>(&ptr),4))return false;
            g.gate=reinterpret_cast<void*>(ptr);return true;
        }
        if(std::strcmp(g.name,"glDeleteTextures")==0) {
            if(value<=3 || value==~std::uintptr_t(0))return false;
            // A later third-party replacement may chain to our hook; wrapping it
            // again would create recursion. Reject that unknown change instead.
            if(g.gate){backend_.ForeignTextureDelete();return false;}
            deleteTarget_.store(reinterpret_cast<DeleteTexturesFn>(*slot));deleteSlot_=slot;
            const auto ptr=reinterpret_cast<std::uintptr_t>(&DeleteTextures);
            if(!Write(g.slot,reinterpret_cast<const std::uint8_t*>(&ptr),4))return false;
            g.gate=reinterpret_cast<void*>(ptr);return true;
        }
        if(value<=3 || value==~std::uintptr_t(0))return true;
        const bool lazy=value==host_.address(g.lazy);
        auto* gate=ObserverGate(*slot,reason|(lazy?0x80000000u:0));if(!gate)return false;
        const auto ptr=reinterpret_cast<std::uintptr_t>(gate);
        if(!Write(g.slot,reinterpret_cast<const std::uint8_t*>(&ptr),4))return false;
        g.gate=gate;return true;
    }
    bool ObserveGl(const char*name,std::uintptr_t slot,std::uintptr_t lazy){
        glGuards_.push_back({name,slot,lazy,nullptr});
        return RearmGl(glGuards_.back(),128+unsigned(glGuards_.size()-1));
    }
    void Report(){
        const auto now=GetTickCount64();if(now<nextReport_)return;nextReport_=now+1000;windowDetailedMisses_=0;
        if(!host_.log)return;
        ReportLocationProfile();
        {char line[512];const auto stats=locations_.GetStats();
         std::snprintf(line,sizeof line,"[shader-locations] enabled=%u calls=%llu hit=%llu forward=%llu bypass=%llu lazy=%llu entries=%u negative=%llu full=%llu mutationSerial=%u outstanding=%u deletedNames=%u frontHit=%llu",
            unsigned(locationHealthy_.load()),static_cast<unsigned long long>(locationCalls_),static_cast<unsigned long long>(locationHits_),
            static_cast<unsigned long long>(locationForwards_),static_cast<unsigned long long>(locationBypass_),static_cast<unsigned long long>(locationLazy_),unsigned(locations_.Size()),
            static_cast<unsigned long long>(locationNegative_),static_cast<unsigned long long>(stats.rejectedFull),locationMutationSerial_.load(),locationMutationsActive_.load(),locationDeletedCount_.load(),static_cast<unsigned long long>(stats.frontHits));Message(line);
         locationCalls_=locationHits_=locationForwards_=locationBypass_=locationLazy_=locationNegative_=0;locations_.ResetStats();}
        const auto counts=cache_.GetCounts();LARGE_INTEGER freq;QueryPerformanceFrequency(&freq);
        const auto operations=cache_.GetOperations();const auto limits=cache_.GetLimits();
        char line[512];std::snprintf(line,sizeof line,"[entity-cache] entries=%u valid=%u commands=%u refs=%u glReads=%llu glReadMs=%.3f shaderReject=%llu retainReject=%llu",
            unsigned(counts.entries),unsigned(counts.valid),unsigned(counts.commands),unsigned(counts.refs),
            static_cast<unsigned long long>(backend_.stats.stateReads),1000.0*backend_.stats.stateQpc/freq.QuadPart,
            static_cast<unsigned long long>(backend_.stats.describeReject),static_cast<unsigned long long>(backend_.stats.retainReject));Message(line);
        std::snprintf(line,sizeof line,"[cache-index] noOps=%u bytes=%u entryLimit=%u byteLimit=%u exact=%llu lineage=%llu comparisons=%llu evictions=%llu rejected=%llu",
            unsigned(counts.noOps),unsigned(counts.bytes),unsigned(limits.entries),unsigned(limits.bytes),
            static_cast<unsigned long long>(operations.exactLookups),static_cast<unsigned long long>(operations.lineageLookups),
            static_cast<unsigned long long>(operations.comparisons),static_cast<unsigned long long>(operations.evictions),
            static_cast<unsigned long long>(operations.rejected));Message(line);cache_.ResetOperations();
        std::snprintf(line,sizeof line,"[entity-cache-format] noop=%llu init=%llu changed=%llu replay=%llu",
            static_cast<unsigned long long>(J374Hooks::formatNoop),static_cast<unsigned long long>(J374Hooks::formatInit),
            static_cast<unsigned long long>(J374Hooks::formatChange),static_cast<unsigned long long>(backend_.stats.formatReplay));Message(line);
        J374Hooks::formatNoop=J374Hooks::formatInit=J374Hooks::formatChange=0;
        std::snprintf(line,sizeof line,"[entity-cache-shader] uniformChecks=%llu glQueries=%llu uniformMs=%.3f",
            static_cast<unsigned long long>(backend_.stats.uniformChecks),static_cast<unsigned long long>(backend_.stats.uniformGlQueries),
            1000.0*backend_.stats.uniformQpc/freq.QuadPart);Message(line);
        std::snprintf(line,sizeof line,"[entity-cache-texture] calls=%llu unrelated=%llu dependent=%llu unknown=%llu",
            static_cast<unsigned long long>(deleteCalls_),static_cast<unsigned long long>(deleteUnrelated_),
            static_cast<unsigned long long>(deleteDependent_),static_cast<unsigned long long>(deleteUnknown_));Message(line);
        deleteCalls_=deleteUnrelated_=deleteDependent_=deleteUnknown_=0;
        std::snprintf(line,sizeof line,"[entity-cache-texture] initAllowed=%llu parameterNoop=%llu mutationDependent=%llu mutationUnknown=%llu",
            static_cast<unsigned long long>(mutationAllowed_),static_cast<unsigned long long>(mutationNoop_),
            static_cast<unsigned long long>(mutationDependent_),static_cast<unsigned long long>(mutationUnknown_));Message(line);
        mutationAllowed_=mutationNoop_=mutationDependent_=mutationUnknown_=0;
        for(unsigned cb:{2u,25u,28u,32u,36u,41u,45u,49u,52u,56u,59u}){
            if(absentCanonicalCounts_[cb]||absentPureCounts_[cb]){
                std::snprintf(line,sizeof line,"[entity-unregistered] callback=%u canonical=%llu presentation=%llu",cb,
                    static_cast<unsigned long long>(absentCanonicalCounts_[cb]),static_cast<unsigned long long>(absentPureCounts_[cb]));Message(line);
            }
            if(!canonicalCounts_[cb]&&!hitCounts_[cb]&&!missCounts_[cb]&&!outsideCounts_[cb])continue;
            std::snprintf(line,sizeof line,"[entity-cache] callback=%u canonical=%llu replayHit=%llu replayMiss=%llu",cb,
               static_cast<unsigned long long>(canonicalCounts_[cb]),static_cast<unsigned long long>(hitCounts_[cb]),static_cast<unsigned long long>(missCounts_[cb]));Message(line);
            if(outsideCounts_[cb]){std::snprintf(line,sizeof line,"[entity-cache-outside] callback=%u nativeCalls=%llu",cb,static_cast<unsigned long long>(outsideCounts_[cb]));Message(line);}
        }
        for(unsigned shown=0;shown<6;++shown){
            auto it=std::max_element(reasons_.begin(),reasons_.end());if(!*it)break;
            const unsigned i=unsigned(it-reasons_.begin());
            static const char* nativeNames[]={"none","SetRT@A0C6E0","Clear@A0C880","Flush@A09720","FrameImages@A140C0","TransparentBatches@A14060","Triangle@A078C0","VertexFormat@A065D0","VertexAllocator@A05D80"};
            const char*name=i>=128 && i-128<glGuards_.size()?glGuards_[i-128].name:(i<9?nativeNames[i]:"unknown");
            std::snprintf(line,sizeof line,"[entity-cache] unsafe reason=%u name=%s count=%llu",i,name,static_cast<unsigned long long>(*it));Message(line);*it=0;
        }
        reasons_.fill(0);canonicalCounts_.fill(0);hitCounts_.fill(0);missCounts_.fill(0);outsideCounts_.fill(0);backend_.stats={};
        absentCanonicalCounts_.fill(0);absentPureCounts_.fill(0);
    }

public:
    explicit Runtime(Host host,Cache::Limits limits=Cache::Limits{}):host_(host),backend_(host.address),cache_(backend_,limits){}
    // Optional location queries only: does not change Lua/draw-cache ownership.
    // Host must pair these around native Update using its exception-safe finally.
    void BeginUpdateQueries(){if(GetCurrentThreadId()==J374Hooks::gameThreadId){++locationUpdateDepth_;locations_.BeginFrame();}}
    void EndUpdateQueries(){if(GetCurrentThreadId()==J374Hooks::gameThreadId&&locationUpdateDepth_){--locationUpdateDepth_;locations_.Clear();}}
    // Allocate Runtime for process lifetime or call Shutdown while engine is live.
    // Do not destroy it after the native image manager has been torn down.
    void BeginRender(std::uint64_t epoch,bool canonical,unsigned passTag=0){
        if(GetCurrentThreadId()!=J374Hooks::gameThreadId)return;
        if(epoch_!=epoch || frameUnsafe_){cache_.Clear();epoch_=epoch;frameUnsafe_=false;}
        for(unsigned i=0;i<glGuards_.size();++i)if(!RearmGl(glGuards_[i],128+i))frameUnsafe_=true;
        Report();
        if(canonical)++canonicalFrame_;cache_.SetCanonicalFrame(canonicalFrame_);
        locations_.BeginFrame();
        if(locationHealthy_.load())LocationSlotsIntact();
        externalPass_=passTag;ordinals_.Clear();J374Hooks::canonical=canonical;
    }
    // Use at the ORIGINAL native POST_RENDER point. No FBO target switch, no
    // fake entity pointer and no Lua execution on a pure presentation.
    int MainPostRender(int(__cdecl* original)()){
        if(GetCurrentThreadId()!=J374Hooks::gameThreadId)return original();
        if(host_.insideRender && !host_.insideRender()){OutsideEvent(2);return original();}
        Key key;key.epoch=epoch_;key.callback=2;
        key.pass=*reinterpret_cast<std::uint32_t*>(host_.address(0xBD56CC)) ^ externalPass_;
        if(!installed_ || frameUnsafe_ || !NextOrdinal(key)){
            const int result=J374Hooks::canonical?original():0;
            Event(2,J374Hooks::canonical,false);return result;
        }
        if(J374Hooks::canonical){
            const bool owns=cache_.Begin(key);
            const int result=original();
            if(owns)cache_.End();
            Event(2,true,owns);return result;
        }
        const bool hit=cache_.Replay(key);if(!hit)Miss(key,cache_.LastReplay());Event(2,false,hit);return 0;
    }
    bool Install();
    void Shutdown(){
        locationHealthy_.store(false);locationUpdateDepth_=0;locations_.Clear();
        J374Hooks::Subscribers=nullptr;J374Hooks::OnNoSubscribers=nullptr;subscribers19715::Disable();
        cache_.Clear();J374Hooks::cache=nullptr;J374Hooks::MakeKey=nullptr;J374Hooks::OnEvent=nullptr;J374Hooks::OnMiss=nullptr;J374Hooks::InsideRender=nullptr;J374Hooks::OnOutsideRender=nullptr;
        for(auto it=patches_.rbegin();it!=patches_.rend();++it)host_.poke(it->va,it->bytes.data(),it->bytes.size());
        patches_.clear();installed_=false;active_=nullptr;
        // Gates deliberately stay allocated: a patched caller may still be unwinding.
    }
};



inline bool Runtime::ValidateLocationCache(){
    // Optional guards intentionally avoid Host::expect's fatal-build diagnostic.
    // Main pipeline validation is independent; failure here only disables caching.
    static constexpr std::uint8_t resolver[7][34]={
{0x55,0x8b,0xec,0x68,0x60,0xec,0xae,0x00,0xba,0xf0,0xa1,0xad,0x00,0xb9,0xe9,0x22,0xae,0x00,0xe8,0x79,0x3a,0xff,0xff,0x83,0xc4,0x04,0xa3,0x50,0xec,0xbc,0x00,0x5d,0xff,0xe0},
{0x55,0x8b,0xec,0x68,0xac,0xfe,0xae,0x00,0xba,0x40,0xf4,0xae,0x00,0xb9,0x66,0x45,0xae,0x00,0xe8,0xe9,0x02,0xff,0xff,0x83,0xc4,0x04,0xa3,0xa8,0xec,0xbc,0x00,0x5d,0xff,0xe0},
{0x55,0x8b,0xec,0x68,0x78,0xf5,0xae,0x00,0xba,0xb0,0xac,0xad,0x00,0xb9,0x43,0x56,0xae,0x00,0xe8,0x59,0xe0,0xfe,0xff,0x83,0xc4,0x04,0xa3,0xd8,0xec,0xbc,0x00,0x5d,0xff,0xe0},
{0x55,0x8b,0xec,0x68,0x84,0xda,0xad,0x00,0xba,0x20,0xbb,0xad,0x00,0xb9,0x74,0x05,0xae,0x00,0xe8,0x79,0x74,0xff,0xff,0x83,0xc4,0x04,0xa3,0xe8,0xec,0xbc,0x00,0x5d,0xff,0xe0},
{0x55,0x8b,0xec,0x68,0x0c,0xbd,0xad,0x00,0xba,0x50,0xd7,0xad,0x00,0xb9,0x2f,0x7c,0xae,0x00,0xe8,0x19,0x90,0xfe,0xff,0x83,0xc4,0x04,0xa3,0x04,0xc2,0xbc,0x00,0x5d,0xff,0xe0},
{0x55,0x8b,0xec,0x68,0xb0,0x98,0xad,0x00,0xba,0xb0,0x94,0xad,0x00,0xb9,0x51,0x56,0xae,0x00,0xe8,0x29,0xe0,0xfe,0xff,0x83,0xc4,0x04,0xa3,0xf8,0xc4,0xbc,0x00,0x5d,0xff,0xe0},
{0x55,0x8b,0xec,0x68,0x14,0xff,0xae,0x00,0xba,0x70,0x95,0xad,0x00,0xb9,0x3f,0x7c,0xae,0x00,0xe8,0xe9,0x8f,0xfe,0xff,0x83,0xc4,0x04,0xa3,0xb0,0xdd,0xbc,0x00,0x5d,0xff,0xe0}
    };
#if defined(_MSC_VER)
    __try {
#endif
        for(unsigned i=0;i<7;++i){
            std::uint8_t expected[34];std::memcpy(expected,resolver[i],34);
            for(unsigned offset:{4u,9u,14u,27u}){std::uint32_t value;std::memcpy(&value,expected+offset,4);value=std::uint32_t(host_.address(value));std::memcpy(expected+offset,&value,4);}
            if(std::memcmp(reinterpret_cast<const void*>(host_.address(locationSpecs_[i].lazy)),expected,34)!=0)return false;
        }
        // ARB deletion uses a compact resolver: name ADE128 + uint16(23E0),
        // not an absolute string pointer. Its GLhandleARB is a 32-bit Win32 name.
        {std::uint8_t stub[]={0x55,0x8b,0xec,0xba,0xe0,0x23,0x0,0x0,0xb9,0x62,0x0,0x0,0x0,0xe8,0x6e,0xb4,0xff,0xff,0xa3,0xd0,0xcf,0xbc,0x0,0x5d,0xff};
         std::uint8_t helper[]={0x55,0x8b,0xec,0x83,0xec,0x10,0x89,0x4d,0xf4,0x8d,0x45,0xfc,0xf,0xb7,0xca,0x66,0x89,0x55,0xfc,0x81,0xc1,0x28,0xe1,0xad,0x0,0x50,0x8d,0x55,0xf4,0xc7,0x45,0xf8,0x0,0x0,0x0,0x0,0xe8,0x87,0xc0,0xff,0xff,0x83,0xc4,0x4,0x8b,0xe5,0x5d,0xc3};
         const auto slot=std::uint32_t(host_.address(0xBCCFD0)),names=std::uint32_t(host_.address(0xADE128));
         std::memcpy(stub+19,&slot,4);std::memcpy(helper+21,&names,4);
         if(std::memcmp(reinterpret_cast<const void*>(host_.address(0xA26EB0)),stub,sizeof stub)!=0 ||
            std::memcmp(reinterpret_cast<const void*>(host_.address(0xA22330)),helper,sizeof helper)!=0 ||
            std::memcmp(reinterpret_cast<const void*>(host_.address(0xAE0508)),"glDeleteObjectARB",sizeof("glDeleteObjectARB"))!=0)return false;}
        // Successful native link marks Shader+4 initialized; Shutdown clears it
        // while leaving +28's numeric name stale. Never cache that stale object.
        {const std::uint8_t init[]={0xc6,0x47,0x04,0x01};const std::uint8_t clear[]={0xc6,0x43,0x04,0x00};
         if(std::memcmp(reinterpret_cast<const void*>(host_.address(0xA15BAB)),init,4)!=0 ||
            std::memcmp(reinterpret_cast<const void*>(host_.address(0xA16307)),clear,4)!=0 ||
            std::memcmp(reinterpret_cast<const void*>(host_.address(0xA16315)),clear,4)!=0)return false;}
        for(auto call:{0xA15CFFu,0xA15E19u,0xA0CCECu,0xA15E8Bu,0xA15EA9u}){
            std::uint8_t expected[]={0xff,0x15,0,0,0,0};const auto slot=std::uint32_t(host_.address(call==0xA15CFF||call==0xA15E19?0xBCEC50:0xBCECA8));
            std::memcpy(expected+2,&slot,4);if(std::memcmp(reinterpret_cast<const void*>(host_.address(call)),expected,6)!=0)return false;
        }
        // Prove the three IAT locations by their import names, never their current
        // provider addresses; another legitimate prior import hook is preserved.
        const auto base=host_.address(0x400000);const auto*dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>0x10000)return false;
        const auto*nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;
        const auto&r=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!r.VirtualAddress)return false;
        const auto*desc=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+r.VirtualAddress);unsigned found=0;
        for(;desc->Name;++desc){
            if(_stricmp(reinterpret_cast<const char*>(base+desc->Name),"opengl32.dll")!=0)continue;
            if(!desc->OriginalFirstThunk)return false;
            const auto*names=reinterpret_cast<const IMAGE_THUNK_DATA32*>(base+desc->OriginalFirstThunk);
            const auto*iat=reinterpret_cast<const IMAGE_THUNK_DATA32*>(base+desc->FirstThunk);
            for(;names->u1.AddressOfData;++names,++iat){
                if(names->u1.Ordinal&IMAGE_ORDINAL_FLAG32)continue;
                const auto*name=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
                for(unsigned i=7;i<10;++i)if(std::strcmp(name->Name,locationSpecs_[i].name)==0){
                    if(reinterpret_cast<std::uintptr_t>(&iat->u1.Function)!=host_.address(locationSpecs_[i].slot))return false;
                    found|=1u<<(i-7);
                }
            }
        }
        return found==7;
#if defined(_MSC_VER)
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}
inline bool Runtime::Install(){
    if(installed_)return true;
    if(active_ || !host_.address || !host_.expect || !host_.poke || !host_.writeJump || !host_.insideRender || !backend_.Ready())return false;
    { const std::uint8_t want[]={0x55,0x8b,0xec,0x6a,0xff}; if(!host_.expect(0x860ab0,want,sizeof want,"entity familiar render"))return false;}
    {const std::uint8_t want[]={0xC2,8,0};if(!host_.expect(0x860c5b,want,3,"entity familiar stdcall RET8"))return false;}
    { const std::uint8_t want[]={0x55,0x8b,0xec,0x6a,0xff}; if(!host_.expect(0x860660,want,sizeof want,"entity npc render"))return false;}
    {const std::uint8_t want[]={0xC2,8,0};if(!host_.expect(0x8606f3,want,3,"entity npc stdcall RET8"))return false;}
    { const std::uint8_t want[]={0x55,0x8b,0xec,0x6a,0xff}; if(!host_.expect(0x863f90,want,sizeof want,"entity player render"))return false;}
    {const std::uint8_t want[]={0xC2,8,0};if(!host_.expect(0x864022,want,3,"entity player stdcall RET8"))return false;}
    { const std::uint8_t want[]={0x55,0x8b,0xec,0x6a,0xff}; if(!host_.expect(0x862cd0,want,sizeof want,"entity pickup render"))return false;}
    {const std::uint8_t want[]={0xC2,8,0};if(!host_.expect(0x862e79,want,3,"entity pickup stdcall RET8"))return false;}
    { const std::uint8_t want[]={0x55,0x8b,0xec,0x6a,0xff}; if(!host_.expect(0x8627b0,want,sizeof want,"entity tear render"))return false;}
    {const std::uint8_t want[]={0xC2,8,0};if(!host_.expect(0x86295c,want,3,"entity tear stdcall RET8"))return false;}
    { const std::uint8_t want[]={0x55,0x8b,0xec,0x6a,0xff}; if(!host_.expect(0x862290,want,sizeof want,"entity projectile render"))return false;}
    {const std::uint8_t want[]={0xC2,8,0};if(!host_.expect(0x862324,want,3,"entity projectile stdcall RET8"))return false;}
    { const std::uint8_t want[]={0x55,0x8b,0xec,0x6a,0xff}; if(!host_.expect(0x8613a0,want,sizeof want,"entity laser render"))return false;}
    {const std::uint8_t want[]={0xC2,8,0};if(!host_.expect(0x861434,want,3,"entity laser stdcall RET8"))return false;}
    { const std::uint8_t want[]={0x55,0x8b,0xec,0x6a,0xff}; if(!host_.expect(0x861670,want,sizeof want,"entity knife render"))return false;}
    {const std::uint8_t want[]={0xC2,8,0};if(!host_.expect(0x86181c,want,3,"entity knife stdcall RET8"))return false;}
    { const std::uint8_t want[]={0x55,0x8b,0xec,0x6a,0xff}; if(!host_.expect(0x8610d0,want,sizeof want,"entity effect render"))return false;}
    {const std::uint8_t want[]={0xC2,8,0};if(!host_.expect(0x861164,want,3,"entity effect stdcall RET8"))return false;}
    { const std::uint8_t want[]={0x55,0x8b,0xec,0x6a,0xff}; if(!host_.expect(0x861c80,want,sizeof want,"entity bomb render"))return false;}
    {const std::uint8_t want[]={0xC2,8,0};if(!host_.expect(0x861e2c,want,3,"entity bomb stdcall RET8"))return false;}
    {const std::uint8_t want[]={0x55,0x8b,0xec,0x83,0xe4,0xf0}; if(!host_.expect(0xA06820,want,sizeof want,"entity quad submit"))return false;}
    {const std::uint8_t want[]={0x55,0x8b,0xec,0x51,0x8a,0x45,0xc}; if(!host_.expect(0xa0c6e0,want,sizeof want,"entity unsafe SetRT"))return false;}
    {const std::uint8_t want[]={0x56,0x8b,0xf1,0x57,0x83,0x7e,0x1c,0x0}; if(!host_.expect(0xa0c880,want,sizeof want,"entity unsafe Clear"))return false;}
    {const std::uint8_t want[]={0x56,0x8b,0xf1,0xc7,0x46,0x18,0x0,0x0,0x0,0x0}; if(!host_.expect(0xa09720,want,sizeof want,"entity unsafe Flush"))return false;}
    {const std::uint8_t want[]={0x53,0x8b,0xdc,0x83,0xec,0x8}; if(!host_.expect(0xa140c0,want,sizeof want,"entity unsafe ApplyFrameImages"))return false;}
    {std::uint8_t want[]={0x8b,0x15,0xb4,0x5a,0xbf,0x0}; *reinterpret_cast<std::uint32_t*>(want+2)=std::uint32_t(host_.address(0xBF5AB4)); if(!host_.expect(0xa14060,want,sizeof want,"entity unsafe ApplyTransparentBatches"))return false;}
    {const std::uint8_t want[]={0x55,0x8b,0xec,0x8b,0x1}; if(!host_.expect(0xa078c0,want,sizeof want,"entity unsafe Triangle"))return false;}
    {const std::uint8_t want[]={0x55,0x8b,0xec,0x83,0xe4,0xf8}; if(!host_.expect(0xa065d0,want,sizeof want,"entity VertexFormat"))return false;}
    {const std::uint8_t want[]={0xc2,4,0}; if(!host_.expect(0xa06702,want,sizeof want,"entity VertexFormat no-op RET4") || !host_.expect(0xa0672d,want,sizeof want,"entity VertexFormat changed RET4"))return false;}
    {const std::uint8_t want[]={0x55,0x8b,0xec,0x83,0xe4,0xf8}; if(!host_.expect(0xa05d80,want,sizeof want,"entity unsafe VertexAllocator"))return false;}
    {std::uint8_t want[]={0xb9,0xea,0xe5,0xad,0x0,0xe8,0x89,0xb6,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xade5ea));if(!host_.expect(0xa22d4d,want,sizeof want,"glBindFramebuffer resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x14,0xbd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbd14));if(!host_.expect(0xa22d5a,want,sizeof want,"glBindFramebuffer dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xfc,0xe5,0xad,0x0,0xe8,0x59,0xb6,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xade5fc));if(!host_.expect(0xa22d7d,want,sizeof want,"glBindFramebufferEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x24,0xc5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc524));if(!host_.expect(0xa22d8a,want,sizeof want,"glBindFramebufferEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa2,0xee,0xad,0x0,0xe8,0xc9,0xa3,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadeea2));if(!host_.expect(0xa2400d,want,sizeof want,"glClear resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x18,0xbd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbd18));if(!host_.expect(0xa2401a,want,sizeof want,"glClear dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf,0xfe,0xad,0x0,0xe8,0x89,0x82,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfe0f));if(!host_.expect(0xa2614d,want,sizeof want,"glCopyTexImage1D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa0,0xdd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdda0));if(!host_.expect(0xa2615a,want,sizeof want,"glCopyTexImage1D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x20,0xfe,0xad,0x0,0xe8,0x59,0x82,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfe20));if(!host_.expect(0xa2617d,want,sizeof want,"glCopyTexImage1DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x30,0xe7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce730));if(!host_.expect(0xa2618a,want,sizeof want,"glCopyTexImage1DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x34,0xfe,0xad,0x0,0xe8,0x29,0x82,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfe34));if(!host_.expect(0xa261ad,want,sizeof want,"glCopyTexImage2D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa4,0xda,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdaa4));if(!host_.expect(0xa261ba,want,sizeof want,"glCopyTexImage2D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x45,0xfe,0xad,0x0,0xe8,0xf9,0x81,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfe45));if(!host_.expect(0xa261dd,want,sizeof want,"glCopyTexImage2DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x44,0xeb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbceb44));if(!host_.expect(0xa261ea,want,sizeof want,"glCopyTexImage2DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x59,0xfe,0xad,0x0,0xe8,0xc9,0x81,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfe59));if(!host_.expect(0xa2620d,want,sizeof want,"glCopyTexSubImage1D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x14,0xc7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc714));if(!host_.expect(0xa2621a,want,sizeof want,"glCopyTexSubImage1D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x6d,0xfe,0xad,0x0,0xe8,0x99,0x81,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfe6d));if(!host_.expect(0xa2623d,want,sizeof want,"glCopyTexSubImage1DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x74,0xbd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbd74));if(!host_.expect(0xa2624a,want,sizeof want,"glCopyTexSubImage1DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x84,0xfe,0xad,0x0,0xe8,0x69,0x81,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfe84));if(!host_.expect(0xa2626d,want,sizeof want,"glCopyTexSubImage2D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xcc,0xea,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbceacc));if(!host_.expect(0xa2627a,want,sizeof want,"glCopyTexSubImage2D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x98,0xfe,0xad,0x0,0xe8,0x39,0x81,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfe98));if(!host_.expect(0xa2629d,want,sizeof want,"glCopyTexSubImage2DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x94,0xd6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd694));if(!host_.expect(0xa262aa,want,sizeof want,"glCopyTexSubImage2DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xaf,0xfe,0xad,0x0,0xe8,0x9,0x81,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfeaf));if(!host_.expect(0xa262cd,want,sizeof want,"glCopyTexSubImage3D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd8,0xd4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd4d8));if(!host_.expect(0xa262da,want,sizeof want,"glCopyTexSubImage3D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xc3,0xfe,0xad,0x0,0xe8,0xd9,0x80,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfec3));if(!host_.expect(0xa262fd,want,sizeof want,"glCopyTexSubImage3DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xac,0xe2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce2ac));if(!host_.expect(0xa2630a,want,sizeof want,"glCopyTexSubImage3DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xda,0xfe,0xad,0x0,0xe8,0xa9,0x80,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadfeda));if(!host_.expect(0xa2632d,want,sizeof want,"glCopyTexSubImage3DOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x88,0xc3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc388));if(!host_.expect(0xa2633a,want,sizeof want,"glCopyTexSubImage3DOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x3a,0xff,0xad,0x0,0xe8,0x19,0x80,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadff3a));if(!host_.expect(0xa263bd,want,sizeof want,"glCopyTextureSubImage1D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x1c,0xe7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce71c));if(!host_.expect(0xa263ca,want,sizeof want,"glCopyTextureSubImage1D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x6d,0xff,0xad,0x0,0xe8,0xc9,0x7f,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadff6d));if(!host_.expect(0xa2640d,want,sizeof want,"glCopyTextureSubImage2D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x2c,0xd6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd62c));if(!host_.expect(0xa2641a,want,sizeof want,"glCopyTextureSubImage2D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa0,0xff,0xad,0x0,0xe8,0x79,0x7f,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xadffa0));if(!host_.expect(0xa2645d,want,sizeof want,"glCopyTextureSubImage3D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x38,0xcc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccc38));if(!host_.expect(0xa2646a,want,sizeof want,"glCopyTextureSubImage3D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xae,0x6,0xae,0x0,0xe8,0xa9,0x71,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae06ae));if(!host_.expect(0xa2722d,want,sizeof want,"glDeleteTextures resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x90,0xe0,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce090));if(!host_.expect(0xa2723a,want,sizeof want,"glDeleteTextures dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x88,0x8,0xae,0x0,0xe8,0xc9,0x6c,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0888));if(!host_.expect(0xa2770d,want,sizeof want,"glDisable resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x7c,0xcb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccb7c));if(!host_.expect(0xa2771a,want,sizeof want,"glDisable dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x68,0xa,0xae,0x0,0xe8,0x69,0x69,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0a68));if(!host_.expect(0xa27a6d,want,sizeof want,"glDrawArrays resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x84,0xda,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcda84));if(!host_.expect(0xa27a7a,want,sizeof want,"glDrawArrays dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x75,0xa,0xae,0x0,0xe8,0x39,0x69,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0a75));if(!host_.expect(0xa27a9d,want,sizeof want,"glDrawArraysEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x20,0xd8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd820));if(!host_.expect(0xa27aaa,want,sizeof want,"glDrawArraysEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x85,0xa,0xae,0x0,0xe8,0x9,0x69,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0a85));if(!host_.expect(0xa27acd,want,sizeof want,"glDrawArraysIndirect resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd0,0xde,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcded0));if(!host_.expect(0xa27ada,want,sizeof want,"glDrawArraysIndirect dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x9a,0xa,0xae,0x0,0xe8,0xd9,0x68,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0a9a));if(!host_.expect(0xa27afd,want,sizeof want,"glDrawArraysInstanced resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x1c,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec1c));if(!host_.expect(0xa27b0a,want,sizeof want,"glDrawArraysInstanced dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb0,0xa,0xae,0x0,0xe8,0xa9,0x68,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0ab0));if(!host_.expect(0xa27b2d,want,sizeof want,"glDrawArraysInstancedANGLE resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x14,0xdb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdb14));if(!host_.expect(0xa27b3a,want,sizeof want,"glDrawArraysInstancedANGLE dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xcb,0xa,0xae,0x0,0xe8,0x79,0x68,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0acb));if(!host_.expect(0xa27b5d,want,sizeof want,"glDrawArraysInstancedARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc0,0xd5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd5c0));if(!host_.expect(0xa27b6a,want,sizeof want,"glDrawArraysInstancedARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe4,0xa,0xae,0x0,0xe8,0x49,0x68,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0ae4));if(!host_.expect(0xa27b8d,want,sizeof want,"glDrawArraysInstancedBaseInstance resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xdc,0xc6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc6dc));if(!host_.expect(0xa27b9a,want,sizeof want,"glDrawArraysInstancedBaseInstance dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x6,0xb,0xae,0x0,0xe8,0x19,0x68,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0b06));if(!host_.expect(0xa27bbd,want,sizeof want,"glDrawArraysInstancedBaseInstanceEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x5c,0xc5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc55c));if(!host_.expect(0xa27bca,want,sizeof want,"glDrawArraysInstancedBaseInstanceEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x2b,0xb,0xae,0x0,0xe8,0xe9,0x67,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0b2b));if(!host_.expect(0xa27bed,want,sizeof want,"glDrawArraysInstancedEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x80,0xe6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce680));if(!host_.expect(0xa27bfa,want,sizeof want,"glDrawArraysInstancedEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x44,0xb,0xae,0x0,0xe8,0xb9,0x67,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0b44));if(!host_.expect(0xa27c1d,want,sizeof want,"glDrawArraysInstancedNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x8c,0xe2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce28c));if(!host_.expect(0xa27c2a,want,sizeof want,"glDrawArraysInstancedNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x69,0xb,0xae,0x0,0xe8,0x69,0x67,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0b69));if(!host_.expect(0xa27c6d,want,sizeof want,"glDrawBuffers resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x6c,0xda,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcda6c));if(!host_.expect(0xa27c7a,want,sizeof want,"glDrawBuffers dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x77,0xb,0xae,0x0,0xe8,0x39,0x67,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0b77));if(!host_.expect(0xa27c9d,want,sizeof want,"glDrawBuffersARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x58,0xd8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd858));if(!host_.expect(0xa27caa,want,sizeof want,"glDrawBuffersARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x88,0xb,0xae,0x0,0xe8,0x9,0x67,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0b88));if(!host_.expect(0xa27ccd,want,sizeof want,"glDrawBuffersATI resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe8,0xe0,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce0e8));if(!host_.expect(0xa27cda,want,sizeof want,"glDrawBuffersATI dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x99,0xb,0xae,0x0,0xe8,0xd9,0x66,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0b99));if(!host_.expect(0xa27cfd,want,sizeof want,"glDrawBuffersEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x8c,0xe7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce78c));if(!host_.expect(0xa27d0a,want,sizeof want,"glDrawBuffersEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x5e,0xc,0xae,0x0,0xe8,0xa9,0x65,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0c5e));if(!host_.expect(0xa27e2d,want,sizeof want,"glDrawElements resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x1c,0xed,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbced1c));if(!host_.expect(0xa27e3a,want,sizeof want,"glDrawElements dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x6d,0xc,0xae,0x0,0xe8,0x79,0x65,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0c6d));if(!host_.expect(0xa27e5d,want,sizeof want,"glDrawElementsBaseVertex resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb8,0xe9,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce9b8));if(!host_.expect(0xa27e6a,want,sizeof want,"glDrawElementsBaseVertex dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x86,0xc,0xae,0x0,0xe8,0x49,0x65,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0c86));if(!host_.expect(0xa27e8d,want,sizeof want,"glDrawElementsBaseVertexEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xdc,0xc5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc5dc));if(!host_.expect(0xa27e9a,want,sizeof want,"glDrawElementsBaseVertexEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa2,0xc,0xae,0x0,0xe8,0x19,0x65,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0ca2));if(!host_.expect(0xa27ebd,want,sizeof want,"glDrawElementsBaseVertexOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x4,0xcc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccc04));if(!host_.expect(0xa27eca,want,sizeof want,"glDrawElementsBaseVertexOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xbe,0xc,0xae,0x0,0xe8,0xe9,0x64,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0cbe));if(!host_.expect(0xa27eed,want,sizeof want,"glDrawElementsIndirect resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xdc,0xbe,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbedc));if(!host_.expect(0xa27efa,want,sizeof want,"glDrawElementsIndirect dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xd5,0xc,0xae,0x0,0xe8,0xb9,0x64,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0cd5));if(!host_.expect(0xa27f1d,want,sizeof want,"glDrawElementsInstanced resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x24,0xce,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcce24));if(!host_.expect(0xa27f2a,want,sizeof want,"glDrawElementsInstanced dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xed,0xc,0xae,0x0,0xe8,0x89,0x64,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0ced));if(!host_.expect(0xa27f4d,want,sizeof want,"glDrawElementsInstancedANGLE resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd8,0xd9,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd9d8));if(!host_.expect(0xa27f5a,want,sizeof want,"glDrawElementsInstancedANGLE dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa,0xd,0xae,0x0,0xe8,0x59,0x64,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0d0a));if(!host_.expect(0xa27f7d,want,sizeof want,"glDrawElementsInstancedARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x54,0xd5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd554));if(!host_.expect(0xa27f8a,want,sizeof want,"glDrawElementsInstancedARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x25,0xd,0xae,0x0,0xe8,0x29,0x64,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0d25));if(!host_.expect(0xa27fad,want,sizeof want,"glDrawElementsInstancedBaseInstance resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa0,0xd7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd7a0));if(!host_.expect(0xa27fba,want,sizeof want,"glDrawElementsInstancedBaseInstance dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x49,0xd,0xae,0x0,0xe8,0xf9,0x63,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0d49));if(!host_.expect(0xa27fdd,want,sizeof want,"glDrawElementsInstancedBaseInstanceEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xdc,0xde,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdedc));if(!host_.expect(0xa27fea,want,sizeof want,"glDrawElementsInstancedBaseInstanceEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x70,0xd,0xae,0x0,0xe8,0xc9,0x63,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0d70));if(!host_.expect(0xa2800d,want,sizeof want,"glDrawElementsInstancedBaseVertex resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe8,0xd2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd2e8));if(!host_.expect(0xa2801a,want,sizeof want,"glDrawElementsInstancedBaseVertex dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x92,0xd,0xae,0x0,0xe8,0x99,0x63,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0d92));if(!host_.expect(0xa2803d,want,sizeof want,"glDrawElementsInstancedBaseVertexBaseInstance resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x2c,0xc2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc22c));if(!host_.expect(0xa2804a,want,sizeof want,"glDrawElementsInstancedBaseVertexBaseInstance dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xc0,0xd,0xae,0x0,0xe8,0x69,0x63,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0dc0));if(!host_.expect(0xa2806d,want,sizeof want,"glDrawElementsInstancedBaseVertexBaseInstanceEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xbc,0xc7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc7bc));if(!host_.expect(0xa2807a,want,sizeof want,"glDrawElementsInstancedBaseVertexBaseInstanceEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf1,0xd,0xae,0x0,0xe8,0x39,0x63,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0df1));if(!host_.expect(0xa2809d,want,sizeof want,"glDrawElementsInstancedBaseVertexEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd8,0xbe,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbed8));if(!host_.expect(0xa280aa,want,sizeof want,"glDrawElementsInstancedBaseVertexEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x16,0xe,0xae,0x0,0xe8,0x9,0x63,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0e16));if(!host_.expect(0xa280cd,want,sizeof want,"glDrawElementsInstancedBaseVertexOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x14,0xe3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce314));if(!host_.expect(0xa280da,want,sizeof want,"glDrawElementsInstancedBaseVertexOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x3b,0xe,0xae,0x0,0xe8,0xd9,0x62,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0e3b));if(!host_.expect(0xa280fd,want,sizeof want,"glDrawElementsInstancedEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x4c,0xdc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdc4c));if(!host_.expect(0xa2810a,want,sizeof want,"glDrawElementsInstancedEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x56,0xe,0xae,0x0,0xe8,0xa9,0x62,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0e56));if(!host_.expect(0xa2812d,want,sizeof want,"glDrawElementsInstancedNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x70,0xe5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce570));if(!host_.expect(0xa2813a,want,sizeof want,"glDrawElementsInstancedNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xc9,0xe,0xae,0x0,0xe8,0xf9,0x61,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0ec9));if(!host_.expect(0xa281dd,want,sizeof want,"glDrawRangeElements resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xdc,0xdb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdbdc));if(!host_.expect(0xa281ea,want,sizeof want,"glDrawRangeElements dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xdd,0xe,0xae,0x0,0xe8,0xc9,0x61,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0edd));if(!host_.expect(0xa2820d,want,sizeof want,"glDrawRangeElementsBaseVertex resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x38,0xe4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce438));if(!host_.expect(0xa2821a,want,sizeof want,"glDrawRangeElementsBaseVertex dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xfb,0xe,0xae,0x0,0xe8,0x99,0x61,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0efb));if(!host_.expect(0xa2823d,want,sizeof want,"glDrawRangeElementsBaseVertexEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe4,0xc1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc1e4));if(!host_.expect(0xa2824a,want,sizeof want,"glDrawRangeElementsBaseVertexEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x1c,0xf,0xae,0x0,0xe8,0x69,0x61,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0f1c));if(!host_.expect(0xa2826d,want,sizeof want,"glDrawRangeElementsBaseVertexOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x40,0xd3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd340));if(!host_.expect(0xa2827a,want,sizeof want,"glDrawRangeElementsBaseVertexOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x3d,0xf,0xae,0x0,0xe8,0x39,0x61,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0f3d));if(!host_.expect(0xa2829d,want,sizeof want,"glDrawRangeElementsEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x24,0xd1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd124));if(!host_.expect(0xa282aa,want,sizeof want,"glDrawRangeElementsEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xd8,0xf,0xae,0x0,0xe8,0x39,0x5f,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0fd8));if(!host_.expect(0xa2849d,want,sizeof want,"glDrawTransformFeedback resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe4,0xdf,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdfe4));if(!host_.expect(0xa284aa,want,sizeof want,"glDrawTransformFeedback dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf0,0xf,0xae,0x0,0xe8,0x9,0x5f,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae0ff0));if(!host_.expect(0xa284cd,want,sizeof want,"glDrawTransformFeedbackInstanced resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x58,0xd9,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd958));if(!host_.expect(0xa284da,want,sizeof want,"glDrawTransformFeedbackInstanced dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x11,0x10,0xae,0x0,0xe8,0xd9,0x5e,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae1011));if(!host_.expect(0xa284fd,want,sizeof want,"glDrawTransformFeedbackNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf0,0xc1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc1f0));if(!host_.expect(0xa2850a,want,sizeof want,"glDrawTransformFeedbackNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x2b,0x10,0xae,0x0,0xe8,0xa9,0x5e,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae102b));if(!host_.expect(0xa2852d,want,sizeof want,"glDrawTransformFeedbackStream resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd8,0xd7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd7d8));if(!host_.expect(0xa2853a,want,sizeof want,"glDrawTransformFeedbackStream dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x49,0x10,0xae,0x0,0xe8,0x79,0x5e,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae1049));if(!host_.expect(0xa2855d,want,sizeof want,"glDrawTransformFeedbackStreamInstanced resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x40,0xc8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc840));if(!host_.expect(0xa2856a,want,sizeof want,"glDrawTransformFeedbackStreamInstanced dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x48,0x11,0xae,0x0,0xe8,0x9,0x5d,0xff,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae1148));if(!host_.expect(0xa286cd,want,sizeof want,"glEnable resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x38,0xc9,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc938));if(!host_.expect(0xa286da,want,sizeof want,"glEnable dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa1,0x5f,0xae,0x0,0xe8,0x9,0xca,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae5fa1));if(!host_.expect(0xa319cd,want,sizeof want,"glMultiDrawArrays resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb8,0xc3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc3b8));if(!host_.expect(0xa319da,want,sizeof want,"glMultiDrawArrays dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb3,0x5f,0xae,0x0,0xe8,0xd9,0xc9,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae5fb3));if(!host_.expect(0xa319fd,want,sizeof want,"glMultiDrawArraysEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x8c,0xd0,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd08c));if(!host_.expect(0xa31a0a,want,sizeof want,"glMultiDrawArraysEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xc8,0x5f,0xae,0x0,0xe8,0xa9,0xc9,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae5fc8));if(!host_.expect(0xa31a2d,want,sizeof want,"glMultiDrawArraysIndirect resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa4,0xde,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdea4));if(!host_.expect(0xa31a3a,want,sizeof want,"glMultiDrawArraysIndirect dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe2,0x5f,0xae,0x0,0xe8,0x79,0xc9,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae5fe2));if(!host_.expect(0xa31a5d,want,sizeof want,"glMultiDrawArraysIndirectAMD resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb8,0xd1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd1b8));if(!host_.expect(0xa31a6a,want,sizeof want,"glMultiDrawArraysIndirectAMD dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x6e,0x60,0xae,0x0,0xe8,0xe9,0xc8,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae606e));if(!host_.expect(0xa31aed,want,sizeof want,"glMultiDrawArraysIndirectEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x10,0xd1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd110));if(!host_.expect(0xa31afa,want,sizeof want,"glMultiDrawArraysIndirectEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa8,0x60,0xae,0x0,0xe8,0x99,0xc8,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae60a8));if(!host_.expect(0xa31b3d,want,sizeof want,"glMultiDrawElements resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x6c,0xd8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd86c));if(!host_.expect(0xa31b4a,want,sizeof want,"glMultiDrawElements dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xbc,0x60,0xae,0x0,0xe8,0x69,0xc8,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae60bc));if(!host_.expect(0xa31b6d,want,sizeof want,"glMultiDrawElementsBaseVertex resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf8,0xca,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccaf8));if(!host_.expect(0xa31b7a,want,sizeof want,"glMultiDrawElementsBaseVertex dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xda,0x60,0xae,0x0,0xe8,0x39,0xc8,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae60da));if(!host_.expect(0xa31b9d,want,sizeof want,"glMultiDrawElementsBaseVertexEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb8,0xe8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce8b8));if(!host_.expect(0xa31baa,want,sizeof want,"glMultiDrawElementsBaseVertexEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xfb,0x60,0xae,0x0,0xe8,0x9,0xc8,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae60fb));if(!host_.expect(0xa31bcd,want,sizeof want,"glMultiDrawElementsBaseVertexOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb8,0xca,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccab8));if(!host_.expect(0xa31bda,want,sizeof want,"glMultiDrawElementsBaseVertexOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x1c,0x61,0xae,0x0,0xe8,0xd9,0xc7,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae611c));if(!host_.expect(0xa31bfd,want,sizeof want,"glMultiDrawElementsEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x40,0xcc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccc40));if(!host_.expect(0xa31c0a,want,sizeof want,"glMultiDrawElementsEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x33,0x61,0xae,0x0,0xe8,0xa9,0xc7,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae6133));if(!host_.expect(0xa31c2d,want,sizeof want,"glMultiDrawElementsIndirect resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf4,0xd9,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd9f4));if(!host_.expect(0xa31c3a,want,sizeof want,"glMultiDrawElementsIndirect dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x4f,0x61,0xae,0x0,0xe8,0x79,0xc7,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae614f));if(!host_.expect(0xa31c5d,want,sizeof want,"glMultiDrawElementsIndirectAMD resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x98,0xd8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd898));if(!host_.expect(0xa31c6a,want,sizeof want,"glMultiDrawElementsIndirectAMD dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe3,0x61,0xae,0x0,0xe8,0xe9,0xc6,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae61e3));if(!host_.expect(0xa31ced,want,sizeof want,"glMultiDrawElementsIndirectEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x44,0xc4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc444));if(!host_.expect(0xa31cfa,want,sizeof want,"glMultiDrawElementsIndirectEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf6,0x97,0xae,0x0,0xe8,0xb9,0x56,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xae97f6));if(!host_.expect(0xa38d1d,want,sizeof want,"glScissor resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x3c,0xc2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc23c));if(!host_.expect(0xa38d2a,want,sizeof want,"glScissor dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x2c,0xa1,0xae,0x0,0xe8,0xe9,0x43,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea12c));if(!host_.expect(0xa39fed,want,sizeof want,"glTexBuffer resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc8,0xe0,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce0c8));if(!host_.expect(0xa39ffa,want,sizeof want,"glTexBuffer dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x38,0xa1,0xae,0x0,0xe8,0xb9,0x43,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea138));if(!host_.expect(0xa3a01d,want,sizeof want,"glTexBufferARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x20,0xde,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcde20));if(!host_.expect(0xa3a02a,want,sizeof want,"glTexBufferARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x47,0xa1,0xae,0x0,0xe8,0x89,0x43,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea147));if(!host_.expect(0xa3a04d,want,sizeof want,"glTexBufferEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x60,0xd7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd760));if(!host_.expect(0xa3a05a,want,sizeof want,"glTexBufferEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x56,0xa1,0xae,0x0,0xe8,0x59,0x43,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea156));if(!host_.expect(0xa3a07d,want,sizeof want,"glTexBufferOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x40,0xd1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd140));if(!host_.expect(0xa3a08a,want,sizeof want,"glTexBufferOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x65,0xa1,0xae,0x0,0xe8,0x29,0x43,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea165));if(!host_.expect(0xa3a0ad,want,sizeof want,"glTexBufferRange resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x7c,0xda,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcda7c));if(!host_.expect(0xa3a0ba,want,sizeof want,"glTexBufferRange dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x76,0xa1,0xae,0x0,0xe8,0xf9,0x42,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea176));if(!host_.expect(0xa3a0dd,want,sizeof want,"glTexBufferRangeEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf0,0xd1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd1f0));if(!host_.expect(0xa3a0ea,want,sizeof want,"glTexBufferRangeEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x8a,0xa1,0xae,0x0,0xe8,0xc9,0x42,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea18a));if(!host_.expect(0xa3a10d,want,sizeof want,"glTexBufferRangeOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb0,0xc4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc4b0));if(!host_.expect(0xa3a11a,want,sizeof want,"glTexBufferRangeOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xd6,0xa6,0xae,0x0,0xe8,0x79,0x35,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea6d6));if(!host_.expect(0xa3ae5d,want,sizeof want,"glTexCoordP1ui resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe0,0xde,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdee0));if(!host_.expect(0xa3ae6a,want,sizeof want,"glTexCoordP1ui dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe5,0xa6,0xae,0x0,0xe8,0x49,0x35,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea6e5));if(!host_.expect(0xa3ae8d,want,sizeof want,"glTexCoordP1uiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x8,0xc5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc508));if(!host_.expect(0xa3ae9a,want,sizeof want,"glTexCoordP1uiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf5,0xa6,0xae,0x0,0xe8,0x19,0x35,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea6f5));if(!host_.expect(0xa3aebd,want,sizeof want,"glTexCoordP2ui resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x34,0xcd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccd34));if(!host_.expect(0xa3aeca,want,sizeof want,"glTexCoordP2ui dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x4,0xa7,0xae,0x0,0xe8,0xe9,0x34,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea704));if(!host_.expect(0xa3aeed,want,sizeof want,"glTexCoordP2uiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x30,0xdb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdb30));if(!host_.expect(0xa3aefa,want,sizeof want,"glTexCoordP2uiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x14,0xa7,0xae,0x0,0xe8,0xb9,0x34,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea714));if(!host_.expect(0xa3af1d,want,sizeof want,"glTexCoordP3ui resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa0,0xd3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd3a0));if(!host_.expect(0xa3af2a,want,sizeof want,"glTexCoordP3ui dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x23,0xa7,0xae,0x0,0xe8,0x89,0x34,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea723));if(!host_.expect(0xa3af4d,want,sizeof want,"glTexCoordP3uiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x40,0xc3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc340));if(!host_.expect(0xa3af5a,want,sizeof want,"glTexCoordP3uiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x33,0xa7,0xae,0x0,0xe8,0x59,0x34,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea733));if(!host_.expect(0xa3af7d,want,sizeof want,"glTexCoordP4ui resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x8c,0xe4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce48c));if(!host_.expect(0xa3af8a,want,sizeof want,"glTexCoordP4ui dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x42,0xa7,0xae,0x0,0xe8,0x29,0x34,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea742));if(!host_.expect(0xa3afad,want,sizeof want,"glTexCoordP4uiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x70,0xe7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce770));if(!host_.expect(0xa3afba,want,sizeof want,"glTexCoordP4uiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x52,0xa7,0xae,0x0,0xe8,0xf9,0x33,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea752));if(!host_.expect(0xa3afdd,want,sizeof want,"glTexCoordPointer resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x4c,0xc9,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc94c));if(!host_.expect(0xa3afea,want,sizeof want,"glTexCoordPointer dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xaa,0xa7,0xae,0x0,0xe8,0x69,0x33,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea7aa));if(!host_.expect(0xa3b06d,want,sizeof want,"glTexEnvf resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc,0xeb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbceb0c));if(!host_.expect(0xa3b084,want,sizeof want,"glTexEnvf dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb4,0xa7,0xae,0x0,0xe8,0x29,0x33,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea7b4));if(!host_.expect(0xa3b0ad,want,sizeof want,"glTexEnvfv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc0,0xca,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccac0));if(!host_.expect(0xa3b0ba,want,sizeof want,"glTexEnvfv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xbf,0xa7,0xae,0x0,0xe8,0xf9,0x32,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea7bf));if(!host_.expect(0xa3b0dd,want,sizeof want,"glTexEnvi resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc0,0xe8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce8c0));if(!host_.expect(0xa3b0ea,want,sizeof want,"glTexEnvi dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xc9,0xa7,0xae,0x0,0xe8,0xc9,0x32,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea7c9));if(!host_.expect(0xa3b10d,want,sizeof want,"glTexEnviv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x50,0xdb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdb50));if(!host_.expect(0xa3b11a,want,sizeof want,"glTexEnviv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x8d,0xa8,0xae,0x0,0xe8,0x79,0x30,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea88d));if(!host_.expect(0xa3b35d,want,sizeof want,"glTexGenxOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x18,0xdd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdd18));if(!host_.expect(0xa3b36a,want,sizeof want,"glTexGenxOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x9a,0xa8,0xae,0x0,0xe8,0x49,0x30,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea89a));if(!host_.expect(0xa3b38d,want,sizeof want,"glTexGenxvOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x70,0xdc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdc70));if(!host_.expect(0xa3b39a,want,sizeof want,"glTexGenxvOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb5,0xa8,0xae,0x0,0xe8,0xf9,0x2f,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea8b5));if(!host_.expect(0xa3b3dd,want,sizeof want,"glTexImage2D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xfc,0xe1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce1fc));if(!host_.expect(0xa3b3ea,want,sizeof want,"glTexImage2D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xc2,0xa8,0xae,0x0,0xe8,0xc9,0x2f,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea8c2));if(!host_.expect(0xa3b40d,want,sizeof want,"glTexImage2DMultisample resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf8,0xe3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce3f8));if(!host_.expect(0xa3b41a,want,sizeof want,"glTexImage2DMultisample dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xfc,0xa8,0xae,0x0,0xe8,0x79,0x2f,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea8fc));if(!host_.expect(0xa3b45d,want,sizeof want,"glTexImage3D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x40,0xdb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdb40));if(!host_.expect(0xa3b46a,want,sizeof want,"glTexImage3D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x9,0xa9,0xae,0x0,0xe8,0x49,0x2f,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea909));if(!host_.expect(0xa3b48d,want,sizeof want,"glTexImage3DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x50,0xd8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd850));if(!host_.expect(0xa3b49a,want,sizeof want,"glTexImage3DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x19,0xa9,0xae,0x0,0xe8,0x19,0x2f,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea919));if(!host_.expect(0xa3b4bd,want,sizeof want,"glTexImage3DMultisample resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa8,0xc4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc4a8));if(!host_.expect(0xa3b4ca,want,sizeof want,"glTexImage3DMultisample dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x53,0xa9,0xae,0x0,0xe8,0xc9,0x2e,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea953));if(!host_.expect(0xa3b50d,want,sizeof want,"glTexImage3DOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x38,0xc8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc838));if(!host_.expect(0xa3b51a,want,sizeof want,"glTexImage3DOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x74,0xa9,0xae,0x0,0xe8,0x79,0x2e,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea974));if(!host_.expect(0xa3b55d,want,sizeof want,"glTexPageCommitmentARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb4,0xc7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc7b4));if(!host_.expect(0xa3b56a,want,sizeof want,"glTexPageCommitmentARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x8b,0xa9,0xae,0x0,0xe8,0x49,0x2e,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea98b));if(!host_.expect(0xa3b58d,want,sizeof want,"glTexPageCommitmentEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x94,0xc7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc794));if(!host_.expect(0xa3b59a,want,sizeof want,"glTexPageCommitmentEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa2,0xa9,0xae,0x0,0xe8,0x19,0x2e,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea9a2));if(!host_.expect(0xa3b5bd,want,sizeof want,"glTexParameterIiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xcc,0xe6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce6cc));if(!host_.expect(0xa3b5ca,want,sizeof want,"glTexParameterIiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb4,0xa9,0xae,0x0,0xe8,0xe9,0x2d,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea9b4));if(!host_.expect(0xa3b5ed,want,sizeof want,"glTexParameterIivEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x34,0xd4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd434));if(!host_.expect(0xa3b5fa,want,sizeof want,"glTexParameterIivEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xc9,0xa9,0xae,0x0,0xe8,0xb9,0x2d,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea9c9));if(!host_.expect(0xa3b61d,want,sizeof want,"glTexParameterIivOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xac,0xd6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd6ac));if(!host_.expect(0xa3b62a,want,sizeof want,"glTexParameterIivOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xde,0xa9,0xae,0x0,0xe8,0x89,0x2d,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea9de));if(!host_.expect(0xa3b64d,want,sizeof want,"glTexParameterIuiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x74,0xcf,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccf74));if(!host_.expect(0xa3b65a,want,sizeof want,"glTexParameterIuiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf1,0xa9,0xae,0x0,0xe8,0x59,0x2d,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaea9f1));if(!host_.expect(0xa3b67d,want,sizeof want,"glTexParameterIuivEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x60,0xcc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccc60));if(!host_.expect(0xa3b68a,want,sizeof want,"glTexParameterIuivEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x7,0xaa,0xae,0x0,0xe8,0x29,0x2d,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaa07));if(!host_.expect(0xa3b6ad,want,sizeof want,"glTexParameterIuivOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x80,0xca,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcca80));if(!host_.expect(0xa3b6ba,want,sizeof want,"glTexParameterIuivOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x1d,0xaa,0xae,0x0,0xe8,0xf9,0x2c,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaa1d));if(!host_.expect(0xa3b6dd,want,sizeof want,"glTexParameterf resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe0,0xe2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce2e0));if(!host_.expect(0xa3b6f4,want,sizeof want,"glTexParameterf dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x2d,0xaa,0xae,0x0,0xe8,0xb9,0x2c,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaa2d));if(!host_.expect(0xa3b71d,want,sizeof want,"glTexParameterfv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb8,0xe3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce3b8));if(!host_.expect(0xa3b72a,want,sizeof want,"glTexParameterfv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x3e,0xaa,0xae,0x0,0xe8,0x89,0x2c,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaa3e));if(!host_.expect(0xa3b74d,want,sizeof want,"glTexParameteri resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x5c,0xc6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc65c));if(!host_.expect(0xa3b75a,want,sizeof want,"glTexParameteri dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x4e,0xaa,0xae,0x0,0xe8,0x59,0x2c,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaa4e));if(!host_.expect(0xa3b77d,want,sizeof want,"glTexParameteriv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x24,0xeb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbceb24));if(!host_.expect(0xa3b78a,want,sizeof want,"glTexParameteriv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xbb,0xaa,0xae,0x0,0xe8,0x89,0x2b,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaabb));if(!host_.expect(0xa3b84d,want,sizeof want,"glTexStorage1D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x94,0xcf,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccf94));if(!host_.expect(0xa3b85a,want,sizeof want,"glTexStorage1D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xca,0xaa,0xae,0x0,0xe8,0x59,0x2b,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaaca));if(!host_.expect(0xa3b87d,want,sizeof want,"glTexStorage1DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd4,0xdf,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdfd4));if(!host_.expect(0xa3b88a,want,sizeof want,"glTexStorage1DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xdc,0xaa,0xae,0x0,0xe8,0x29,0x2b,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaadc));if(!host_.expect(0xa3b8ad,want,sizeof want,"glTexStorage2D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xfc,0xdd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcddfc));if(!host_.expect(0xa3b8ba,want,sizeof want,"glTexStorage2D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xeb,0xaa,0xae,0x0,0xe8,0xf9,0x2a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaaeb));if(!host_.expect(0xa3b8dd,want,sizeof want,"glTexStorage2DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x44,0xbd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbd44));if(!host_.expect(0xa3b8ea,want,sizeof want,"glTexStorage2DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xfd,0xaa,0xae,0x0,0xe8,0xc9,0x2a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaafd));if(!host_.expect(0xa3b90d,want,sizeof want,"glTexStorage2DMultisample resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x68,0xc4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc468));if(!host_.expect(0xa3b91a,want,sizeof want,"glTexStorage2DMultisample dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x17,0xab,0xae,0x0,0xe8,0x99,0x2a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeab17));if(!host_.expect(0xa3b93d,want,sizeof want,"glTexStorage3D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x6c,0xc8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc86c));if(!host_.expect(0xa3b94a,want,sizeof want,"glTexStorage3D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x26,0xab,0xae,0x0,0xe8,0x69,0x2a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeab26));if(!host_.expect(0xa3b96d,want,sizeof want,"glTexStorage3DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x1c,0xda,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcda1c));if(!host_.expect(0xa3b97a,want,sizeof want,"glTexStorage3DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x38,0xab,0xae,0x0,0xe8,0x39,0x2a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeab38));if(!host_.expect(0xa3b99d,want,sizeof want,"glTexStorage3DMultisample resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x64,0xcd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccd64));if(!host_.expect(0xa3b9aa,want,sizeof want,"glTexStorage3DMultisample dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x52,0xab,0xae,0x0,0xe8,0x9,0x2a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeab52));if(!host_.expect(0xa3b9cd,want,sizeof want,"glTexStorage3DMultisampleOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa4,0xd7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd7a4));if(!host_.expect(0xa3b9da,want,sizeof want,"glTexStorage3DMultisampleOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x85,0xab,0xae,0x0,0xe8,0xb9,0x29,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeab85));if(!host_.expect(0xa3ba1d,want,sizeof want,"glTexSubImage1D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x38,0xdb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdb38));if(!host_.expect(0xa3ba2a,want,sizeof want,"glTexSubImage1D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x95,0xab,0xae,0x0,0xe8,0x89,0x29,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeab95));if(!host_.expect(0xa3ba4d,want,sizeof want,"glTexSubImage1DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa4,0xe9,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce9a4));if(!host_.expect(0xa3ba5a,want,sizeof want,"glTexSubImage1DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa8,0xab,0xae,0x0,0xe8,0x59,0x29,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaba8));if(!host_.expect(0xa3ba7d,want,sizeof want,"glTexSubImage2D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x98,0xe5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce598));if(!host_.expect(0xa3ba8a,want,sizeof want,"glTexSubImage2D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb8,0xab,0xae,0x0,0xe8,0x29,0x29,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeabb8));if(!host_.expect(0xa3baad,want,sizeof want,"glTexSubImage2DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x8,0xe7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce708));if(!host_.expect(0xa3baba,want,sizeof want,"glTexSubImage2DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xcb,0xab,0xae,0x0,0xe8,0xf9,0x28,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeabcb));if(!host_.expect(0xa3badd,want,sizeof want,"glTexSubImage3D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd4,0xda,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdad4));if(!host_.expect(0xa3baea,want,sizeof want,"glTexSubImage3D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xdb,0xab,0xae,0x0,0xe8,0xc9,0x28,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeabdb));if(!host_.expect(0xa3bb0d,want,sizeof want,"glTexSubImage3DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb0,0xd2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd2b0));if(!host_.expect(0xa3bb1a,want,sizeof want,"glTexSubImage3DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xee,0xab,0xae,0x0,0xe8,0x99,0x28,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeabee));if(!host_.expect(0xa3bb3d,want,sizeof want,"glTexSubImage3DOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa4,0xc6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc6a4));if(!host_.expect(0xa3bb4a,want,sizeof want,"glTexSubImage3DOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x15,0xac,0xae,0x0,0xe8,0x4c,0x28,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeac15));if(!host_.expect(0xa3bb8a,want,sizeof want,"glTextureBarrier resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf8,0xbd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbdf8));if(!host_.expect(0xa3bb97,want,sizeof want,"glTextureBarrier dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x39,0xac,0xae,0x0,0xe8,0x9,0x28,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeac39));if(!host_.expect(0xa3bbcd,want,sizeof want,"glTextureBuffer resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe0,0xd8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd8e0));if(!host_.expect(0xa3bbda,want,sizeof want,"glTextureBuffer dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x5c,0xac,0xae,0x0,0xe8,0xb9,0x27,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeac5c));if(!host_.expect(0xa3bc1d,want,sizeof want,"glTextureBufferRange resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe0,0xca,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccae0));if(!host_.expect(0xa3bc2a,want,sizeof want,"glTextureBufferRange dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb9,0xad,0xae,0x0,0xe8,0xe9,0x25,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeadb9));if(!host_.expect(0xa3bded,want,sizeof want,"glTextureParameterIiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x5c,0xcb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccb5c));if(!host_.expect(0xa3bdfa,want,sizeof want,"glTextureParameterIiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe8,0xad,0xae,0x0,0xe8,0x99,0x25,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeade8));if(!host_.expect(0xa3be3d,want,sizeof want,"glTextureParameterIuiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd8,0xe6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce6d8));if(!host_.expect(0xa3be4a,want,sizeof want,"glTextureParameterIuiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x19,0xae,0xae,0x0,0xe8,0x49,0x25,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeae19));if(!host_.expect(0xa3be8d,want,sizeof want,"glTextureParameterf resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x94,0xc4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc494));if(!host_.expect(0xa3bea4,want,sizeof want,"glTextureParameterf dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x44,0xae,0xae,0x0,0xe8,0xc9,0x24,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeae44));if(!host_.expect(0xa3bf0d,want,sizeof want,"glTextureParameterfv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x18,0xe2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce218));if(!host_.expect(0xa3bf1a,want,sizeof want,"glTextureParameterfv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x71,0xae,0xae,0x0,0xe8,0x79,0x24,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeae71));if(!host_.expect(0xa3bf5d,want,sizeof want,"glTextureParameteri resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x94,0xd4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd494));if(!host_.expect(0xa3bf6a,want,sizeof want,"glTextureParameteri dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x9c,0xae,0xae,0x0,0xe8,0x29,0x24,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeae9c));if(!host_.expect(0xa3bfad,want,sizeof want,"glTextureParameteriv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc4,0xeb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcebc4));if(!host_.expect(0xa3bfba,want,sizeof want,"glTextureParameteriv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf6,0xae,0xae,0x0,0xe8,0x99,0x23,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaef6));if(!host_.expect(0xa3c03d,want,sizeof want,"glTextureStorage1D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xcc,0xc6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc6cc));if(!host_.expect(0xa3c04a,want,sizeof want,"glTextureStorage1D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x9,0xaf,0xae,0x0,0xe8,0x69,0x23,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaf09));if(!host_.expect(0xa3c06d,want,sizeof want,"glTextureStorage1DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x4,0xc7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc704));if(!host_.expect(0xa3c07a,want,sizeof want,"glTextureStorage1DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x1f,0xaf,0xae,0x0,0xe8,0x39,0x23,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaf1f));if(!host_.expect(0xa3c09d,want,sizeof want,"glTextureStorage2D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x10,0xe1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce110));if(!host_.expect(0xa3c0aa,want,sizeof want,"glTextureStorage2D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x32,0xaf,0xae,0x0,0xe8,0x9,0x23,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaf32));if(!host_.expect(0xa3c0cd,want,sizeof want,"glTextureStorage2DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb0,0xe1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce1b0));if(!host_.expect(0xa3c0da,want,sizeof want,"glTextureStorage2DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x48,0xaf,0xae,0x0,0xe8,0xd9,0x22,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaf48));if(!host_.expect(0xa3c0fd,want,sizeof want,"glTextureStorage2DMultisample resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x14,0xdc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdc14));if(!host_.expect(0xa3c10a,want,sizeof want,"glTextureStorage2DMultisample dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x87,0xaf,0xae,0x0,0xe8,0x89,0x22,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaf87));if(!host_.expect(0xa3c14d,want,sizeof want,"glTextureStorage3D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa8,0xc2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc2a8));if(!host_.expect(0xa3c15a,want,sizeof want,"glTextureStorage3D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x9a,0xaf,0xae,0x0,0xe8,0x59,0x22,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeaf9a));if(!host_.expect(0xa3c17d,want,sizeof want,"glTextureStorage3DEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb4,0xea,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbceab4));if(!host_.expect(0xa3c18a,want,sizeof want,"glTextureStorage3DEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb0,0xaf,0xae,0x0,0xe8,0x29,0x22,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeafb0));if(!host_.expect(0xa3c1ad,want,sizeof want,"glTextureStorage3DMultisample resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc4,0xd4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd4c4));if(!host_.expect(0xa3c1ba,want,sizeof want,"glTextureStorage3DMultisample dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x9,0xb0,0xae,0x0,0xe8,0xb9,0x21,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb009));if(!host_.expect(0xa3c21d,want,sizeof want,"glTextureSubImage1D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xac,0xcd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccdac));if(!host_.expect(0xa3c22a,want,sizeof want,"glTextureSubImage1D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x34,0xb0,0xae,0x0,0xe8,0x69,0x21,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb034));if(!host_.expect(0xa3c26d,want,sizeof want,"glTextureSubImage2D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x2c,0xd3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd32c));if(!host_.expect(0xa3c27a,want,sizeof want,"glTextureSubImage2D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x5f,0xb0,0xae,0x0,0xe8,0x19,0x21,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb05f));if(!host_.expect(0xa3c2bd,want,sizeof want,"glTextureSubImage3D resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x3c,0xce,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcce3c));if(!host_.expect(0xa3c2ca,want,sizeof want,"glTextureSubImage3D dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x8a,0xb0,0xae,0x0,0xe8,0xc9,0x20,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb08a));if(!host_.expect(0xa3c30d,want,sizeof want,"glTextureView resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x4c,0xe0,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce04c));if(!host_.expect(0xa3c31a,want,sizeof want,"glTextureView dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x98,0xb0,0xae,0x0,0xe8,0x99,0x20,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb098));if(!host_.expect(0xa3c33d,want,sizeof want,"glTextureViewEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb4,0xe7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce7b4));if(!host_.expect(0xa3c34a,want,sizeof want,"glTextureViewEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa9,0xb0,0xae,0x0,0xe8,0x69,0x20,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb0a9));if(!host_.expect(0xa3c36d,want,sizeof want,"glTextureViewOES resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe8,0xc6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc6e8));if(!host_.expect(0xa3c37a,want,sizeof want,"glTextureViewOES dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe9,0xb1,0xae,0x0,0xe8,0xf6,0x1d,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb1e9));if(!host_.expect(0xa3c5e0,want,sizeof want,"glUniform1d resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf4,0xc6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc6f4));if(!host_.expect(0xa3c5f8,want,sizeof want,"glUniform1d dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf5,0xb1,0xae,0x0,0xe8,0xb9,0x1d,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb1f5));if(!host_.expect(0xa3c61d,want,sizeof want,"glUniform1dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x6c,0xbd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbd6c));if(!host_.expect(0xa3c62a,want,sizeof want,"glUniform1dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x2,0xb2,0xae,0x0,0xe8,0x89,0x1d,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb202));if(!host_.expect(0xa3c64d,want,sizeof want,"glUniform1f resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x3c,0xe6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce63c));if(!host_.expect(0xa3c664,want,sizeof want,"glUniform1f dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe,0xb2,0xae,0x0,0xe8,0x59,0x1d,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb20e));if(!host_.expect(0xa3c67d,want,sizeof want,"glUniform1fARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd0,0xd6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd6d0));if(!host_.expect(0xa3c694,want,sizeof want,"glUniform1fARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x1d,0xb2,0xae,0x0,0xe8,0x29,0x1d,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb21d));if(!host_.expect(0xa3c6ad,want,sizeof want,"glUniform1fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x90,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec90));if(!host_.expect(0xa3c6ba,want,sizeof want,"glUniform1fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x2a,0xb2,0xae,0x0,0xe8,0xf9,0x1c,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb22a));if(!host_.expect(0xa3c6dd,want,sizeof want,"glUniform1fvARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x48,0xc5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc548));if(!host_.expect(0xa3c6ea,want,sizeof want,"glUniform1fvARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x3a,0xb2,0xae,0x0,0xe8,0xc9,0x1c,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb23a));if(!host_.expect(0xa3c70d,want,sizeof want,"glUniform1i resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd0,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcecd0));if(!host_.expect(0xa3c71a,want,sizeof want,"glUniform1i dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x46,0xb2,0xae,0x0,0xe8,0x96,0x1c,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb246));if(!host_.expect(0xa3c740,want,sizeof want,"glUniform1i64NV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xdc,0xd5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd5dc));if(!host_.expect(0xa3c74d,want,sizeof want,"glUniform1i64NV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x56,0xb2,0xae,0x0,0xe8,0x59,0x1c,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb256));if(!host_.expect(0xa3c77d,want,sizeof want,"glUniform1i64vNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf8,0xc7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc7f8));if(!host_.expect(0xa3c78a,want,sizeof want,"glUniform1i64vNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x67,0xb2,0xae,0x0,0xe8,0x29,0x1c,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb267));if(!host_.expect(0xa3c7ad,want,sizeof want,"glUniform1iARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x44,0xca,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcca44));if(!host_.expect(0xa3c7ba,want,sizeof want,"glUniform1iARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x76,0xb2,0xae,0x0,0xe8,0xf9,0x1b,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb276));if(!host_.expect(0xa3c7dd,want,sizeof want,"glUniform1iv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x70,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec70));if(!host_.expect(0xa3c7ea,want,sizeof want,"glUniform1iv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x83,0xb2,0xae,0x0,0xe8,0xc9,0x1b,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb283));if(!host_.expect(0xa3c80d,want,sizeof want,"glUniform1ivARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x80,0xe2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce280));if(!host_.expect(0xa3c81a,want,sizeof want,"glUniform1ivARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x93,0xb2,0xae,0x0,0xe8,0x99,0x1b,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb293));if(!host_.expect(0xa3c83d,want,sizeof want,"glUniform1ui resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf4,0xe3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce3f4));if(!host_.expect(0xa3c84a,want,sizeof want,"glUniform1ui dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa0,0xb2,0xae,0x0,0xe8,0x66,0x1b,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb2a0));if(!host_.expect(0xa3c870,want,sizeof want,"glUniform1ui64NV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa0,0xcc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccca0));if(!host_.expect(0xa3c87d,want,sizeof want,"glUniform1ui64NV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb1,0xb2,0xae,0x0,0xe8,0x29,0x1b,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb2b1));if(!host_.expect(0xa3c8ad,want,sizeof want,"glUniform1ui64vNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa8,0xdc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdca8));if(!host_.expect(0xa3c8ba,want,sizeof want,"glUniform1ui64vNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xc3,0xb2,0xae,0x0,0xe8,0xf9,0x1a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb2c3));if(!host_.expect(0xa3c8dd,want,sizeof want,"glUniform1uiEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x20,0xbe,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbe20));if(!host_.expect(0xa3c8ea,want,sizeof want,"glUniform1uiEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xd3,0xb2,0xae,0x0,0xe8,0xc9,0x1a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb2d3));if(!host_.expect(0xa3c90d,want,sizeof want,"glUniform1uiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x64,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec64));if(!host_.expect(0xa3c91a,want,sizeof want,"glUniform1uiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe1,0xb2,0xae,0x0,0xe8,0x99,0x1a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb2e1));if(!host_.expect(0xa3c93d,want,sizeof want,"glUniform1uivEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd4,0xe1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce1d4));if(!host_.expect(0xa3c94a,want,sizeof want,"glUniform1uivEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf2,0xb2,0xae,0x0,0xe8,0x66,0x1a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb2f2));if(!host_.expect(0xa3c970,want,sizeof want,"glUniform2d resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x3c,0xd2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd23c));if(!host_.expect(0xa3c982,want,sizeof want,"glUniform2d dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xfe,0xb2,0xae,0x0,0xe8,0x19,0x1a,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb2fe));if(!host_.expect(0xa3c9bd,want,sizeof want,"glUniform2dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xac,0xdc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdcac));if(!host_.expect(0xa3c9ca,want,sizeof want,"glUniform2dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb,0xb3,0xae,0x0,0xe8,0xe9,0x19,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb30b));if(!host_.expect(0xa3c9ed,want,sizeof want,"glUniform2f resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa4,0xe1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce1a4));if(!host_.expect(0xa3ca0f,want,sizeof want,"glUniform2f dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x17,0xb3,0xae,0x0,0xe8,0xa9,0x19,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb317));if(!host_.expect(0xa3ca2d,want,sizeof want,"glUniform2fARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xdc,0xda,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdadc));if(!host_.expect(0xa3ca4f,want,sizeof want,"glUniform2fARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x26,0xb3,0xae,0x0,0xe8,0x69,0x19,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb326));if(!host_.expect(0xa3ca6d,want,sizeof want,"glUniform2fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x68,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec68));if(!host_.expect(0xa3ca7a,want,sizeof want,"glUniform2fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x33,0xb3,0xae,0x0,0xe8,0x39,0x19,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb333));if(!host_.expect(0xa3ca9d,want,sizeof want,"glUniform2fvARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x44,0xc7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc744));if(!host_.expect(0xa3caaa,want,sizeof want,"glUniform2fvARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x43,0xb3,0xae,0x0,0xe8,0x9,0x19,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb343));if(!host_.expect(0xa3cacd,want,sizeof want,"glUniform2i resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc8,0xd5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd5c8));if(!host_.expect(0xa3cada,want,sizeof want,"glUniform2i dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x4f,0xb3,0xae,0x0,0xe8,0xd6,0x18,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb34f));if(!host_.expect(0xa3cb00,want,sizeof want,"glUniform2i64NV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa8,0xe0,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce0a8));if(!host_.expect(0xa3cb0d,want,sizeof want,"glUniform2i64NV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x5f,0xb3,0xae,0x0,0xe8,0x99,0x18,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb35f));if(!host_.expect(0xa3cb3d,want,sizeof want,"glUniform2i64vNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x60,0xda,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcda60));if(!host_.expect(0xa3cb4a,want,sizeof want,"glUniform2i64vNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x70,0xb3,0xae,0x0,0xe8,0x69,0x18,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb370));if(!host_.expect(0xa3cb6d,want,sizeof want,"glUniform2iARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xdc,0xd9,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd9dc));if(!host_.expect(0xa3cb7a,want,sizeof want,"glUniform2iARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x7f,0xb3,0xae,0x0,0xe8,0x39,0x18,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb37f));if(!host_.expect(0xa3cb9d,want,sizeof want,"glUniform2iv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xec,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcecec));if(!host_.expect(0xa3cbaa,want,sizeof want,"glUniform2iv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x8c,0xb3,0xae,0x0,0xe8,0x9,0x18,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb38c));if(!host_.expect(0xa3cbcd,want,sizeof want,"glUniform2ivARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x20,0xd2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd220));if(!host_.expect(0xa3cbda,want,sizeof want,"glUniform2ivARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x9c,0xb3,0xae,0x0,0xe8,0xd9,0x17,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb39c));if(!host_.expect(0xa3cbfd,want,sizeof want,"glUniform2ui resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd4,0xbd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbdd4));if(!host_.expect(0xa3cc0a,want,sizeof want,"glUniform2ui dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa9,0xb3,0xae,0x0,0xe8,0xa6,0x17,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb3a9));if(!host_.expect(0xa3cc30,want,sizeof want,"glUniform2ui64NV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb4,0xe6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce6b4));if(!host_.expect(0xa3cc3d,want,sizeof want,"glUniform2ui64NV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xba,0xb3,0xae,0x0,0xe8,0x69,0x17,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb3ba));if(!host_.expect(0xa3cc6d,want,sizeof want,"glUniform2ui64vNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc,0xcc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccc0c));if(!host_.expect(0xa3cc7a,want,sizeof want,"glUniform2ui64vNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xcc,0xb3,0xae,0x0,0xe8,0x39,0x17,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb3cc));if(!host_.expect(0xa3cc9d,want,sizeof want,"glUniform2uiEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc0,0xce,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccec0));if(!host_.expect(0xa3ccaa,want,sizeof want,"glUniform2uiEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xdc,0xb3,0xae,0x0,0xe8,0x9,0x17,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb3dc));if(!host_.expect(0xa3cccd,want,sizeof want,"glUniform2uiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf0,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcecf0));if(!host_.expect(0xa3ccda,want,sizeof want,"glUniform2uiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xea,0xb3,0xae,0x0,0xe8,0xd9,0x16,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb3ea));if(!host_.expect(0xa3ccfd,want,sizeof want,"glUniform2uivEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x28,0xc8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc828));if(!host_.expect(0xa3cd0a,want,sizeof want,"glUniform2uivEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xfb,0xb3,0xae,0x0,0xe8,0xa6,0x16,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb3fb));if(!host_.expect(0xa3cd30,want,sizeof want,"glUniform3d resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x5c,0xcd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccd5c));if(!host_.expect(0xa3cd42,want,sizeof want,"glUniform3d dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x7,0xb4,0xae,0x0,0xe8,0x59,0x16,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb407));if(!host_.expect(0xa3cd7d,want,sizeof want,"glUniform3dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x24,0xdb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdb24));if(!host_.expect(0xa3cd8a,want,sizeof want,"glUniform3dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x14,0xb4,0xae,0x0,0xe8,0x29,0x16,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb414));if(!host_.expect(0xa3cdad,want,sizeof want,"glUniform3f resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x10,0xc6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc610));if(!host_.expect(0xa3cdc9,want,sizeof want,"glUniform3f dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x20,0xb4,0xae,0x0,0xe8,0xd9,0x15,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb420));if(!host_.expect(0xa3cdfd,want,sizeof want,"glUniform3fARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd4,0xc1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc1d4));if(!host_.expect(0xa3ce19,want,sizeof want,"glUniform3fARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x2f,0xb4,0xae,0x0,0xe8,0x89,0x15,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb42f));if(!host_.expect(0xa3ce4d,want,sizeof want,"glUniform3fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x5c,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec5c));if(!host_.expect(0xa3ce5a,want,sizeof want,"glUniform3fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x3c,0xb4,0xae,0x0,0xe8,0x59,0x15,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb43c));if(!host_.expect(0xa3ce7d,want,sizeof want,"glUniform3fvARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x98,0xc4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc498));if(!host_.expect(0xa3ce8a,want,sizeof want,"glUniform3fvARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x4c,0xb4,0xae,0x0,0xe8,0x29,0x15,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb44c));if(!host_.expect(0xa3cead,want,sizeof want,"glUniform3i resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x74,0xdf,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdf74));if(!host_.expect(0xa3ceba,want,sizeof want,"glUniform3i dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x58,0xb4,0xae,0x0,0xe8,0xf6,0x14,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb458));if(!host_.expect(0xa3cee0,want,sizeof want,"glUniform3i64NV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x78,0xbf,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbf78));if(!host_.expect(0xa3ceed,want,sizeof want,"glUniform3i64NV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x68,0xb4,0xae,0x0,0xe8,0xb9,0x14,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb468));if(!host_.expect(0xa3cf1d,want,sizeof want,"glUniform3i64vNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x94,0xd8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd894));if(!host_.expect(0xa3cf2a,want,sizeof want,"glUniform3i64vNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x79,0xb4,0xae,0x0,0xe8,0x89,0x14,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb479));if(!host_.expect(0xa3cf4d,want,sizeof want,"glUniform3iARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc8,0xd6,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd6c8));if(!host_.expect(0xa3cf5a,want,sizeof want,"glUniform3iARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x88,0xb4,0xae,0x0,0xe8,0x59,0x14,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb488));if(!host_.expect(0xa3cf7d,want,sizeof want,"glUniform3iv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x80,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec80));if(!host_.expect(0xa3cf8a,want,sizeof want,"glUniform3iv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x95,0xb4,0xae,0x0,0xe8,0x29,0x14,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb495));if(!host_.expect(0xa3cfad,want,sizeof want,"glUniform3ivARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x8,0xc3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc308));if(!host_.expect(0xa3cfba,want,sizeof want,"glUniform3ivARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa5,0xb4,0xae,0x0,0xe8,0xf9,0x13,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb4a5));if(!host_.expect(0xa3cfdd,want,sizeof want,"glUniform3ui resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xbc,0xe0,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce0bc));if(!host_.expect(0xa3cfea,want,sizeof want,"glUniform3ui dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb2,0xb4,0xae,0x0,0xe8,0xc6,0x13,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb4b2));if(!host_.expect(0xa3d010,want,sizeof want,"glUniform3ui64NV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x10,0xd9,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd910));if(!host_.expect(0xa3d01d,want,sizeof want,"glUniform3ui64NV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xc3,0xb4,0xae,0x0,0xe8,0x89,0x13,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb4c3));if(!host_.expect(0xa3d04d,want,sizeof want,"glUniform3ui64vNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x5c,0xdc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdc5c));if(!host_.expect(0xa3d05a,want,sizeof want,"glUniform3ui64vNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xd5,0xb4,0xae,0x0,0xe8,0x59,0x13,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb4d5));if(!host_.expect(0xa3d07d,want,sizeof want,"glUniform3uiEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x20,0xdc,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdc20));if(!host_.expect(0xa3d08a,want,sizeof want,"glUniform3uiEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe5,0xb4,0xae,0x0,0xe8,0x29,0x13,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb4e5));if(!host_.expect(0xa3d0ad,want,sizeof want,"glUniform3uiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x60,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec60));if(!host_.expect(0xa3d0ba,want,sizeof want,"glUniform3uiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf3,0xb4,0xae,0x0,0xe8,0xf9,0x12,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb4f3));if(!host_.expect(0xa3d0dd,want,sizeof want,"glUniform3uivEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x80,0xe1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce180));if(!host_.expect(0xa3d0ea,want,sizeof want,"glUniform3uivEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x4,0xb5,0xae,0x0,0xe8,0xc6,0x12,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb504));if(!host_.expect(0xa3d110,want,sizeof want,"glUniform4d resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa0,0xe0,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce0a0));if(!host_.expect(0xa3d122,want,sizeof want,"glUniform4d dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x10,0xb5,0xae,0x0,0xe8,0x69,0x12,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb510));if(!host_.expect(0xa3d16d,want,sizeof want,"glUniform4dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x5c,0xc8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc85c));if(!host_.expect(0xa3d17a,want,sizeof want,"glUniform4dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x1d,0xb5,0xae,0x0,0xe8,0x39,0x12,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb51d));if(!host_.expect(0xa3d19d,want,sizeof want,"glUniform4f resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x2c,0xca,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcca2c));if(!host_.expect(0xa3d1b9,want,sizeof want,"glUniform4f dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x29,0xb5,0xae,0x0,0xe8,0xd9,0x11,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb529));if(!host_.expect(0xa3d1fd,want,sizeof want,"glUniform4fARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x7c,0xde,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcde7c));if(!host_.expect(0xa3d219,want,sizeof want,"glUniform4fARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x38,0xb5,0xae,0x0,0xe8,0x79,0x11,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb538));if(!host_.expect(0xa3d25d,want,sizeof want,"glUniform4fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x0,0xed,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbced00));if(!host_.expect(0xa3d26a,want,sizeof want,"glUniform4fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x45,0xb5,0xae,0x0,0xe8,0x49,0x11,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb545));if(!host_.expect(0xa3d28d,want,sizeof want,"glUniform4fvARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc4,0xcf,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccfc4));if(!host_.expect(0xa3d29a,want,sizeof want,"glUniform4fvARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x55,0xb5,0xae,0x0,0xe8,0x19,0x11,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb555));if(!host_.expect(0xa3d2bd,want,sizeof want,"glUniform4i resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe0,0xbd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbde0));if(!host_.expect(0xa3d2ca,want,sizeof want,"glUniform4i dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x61,0xb5,0xae,0x0,0xe8,0xe6,0x10,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb561));if(!host_.expect(0xa3d2f0,want,sizeof want,"glUniform4i64NV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x3c,0xcd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccd3c));if(!host_.expect(0xa3d2fd,want,sizeof want,"glUniform4i64NV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x71,0xb5,0xae,0x0,0xe8,0x99,0x10,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb571));if(!host_.expect(0xa3d33d,want,sizeof want,"glUniform4i64vNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x6c,0xea,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcea6c));if(!host_.expect(0xa3d34a,want,sizeof want,"glUniform4i64vNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x82,0xb5,0xae,0x0,0xe8,0x69,0x10,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb582));if(!host_.expect(0xa3d36d,want,sizeof want,"glUniform4iARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x5c,0xe7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce75c));if(!host_.expect(0xa3d37a,want,sizeof want,"glUniform4iARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x91,0xb5,0xae,0x0,0xe8,0x39,0x10,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb591));if(!host_.expect(0xa3d39d,want,sizeof want,"glUniform4iv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x10,0xed,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbced10));if(!host_.expect(0xa3d3aa,want,sizeof want,"glUniform4iv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x9e,0xb5,0xae,0x0,0xe8,0x9,0x10,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb59e));if(!host_.expect(0xa3d3cd,want,sizeof want,"glUniform4ivARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x4,0xc3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc304));if(!host_.expect(0xa3d3da,want,sizeof want,"glUniform4ivARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xae,0xb5,0xae,0x0,0xe8,0xd9,0xf,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb5ae));if(!host_.expect(0xa3d3fd,want,sizeof want,"glUniform4ui resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x68,0xd7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd768));if(!host_.expect(0xa3d40a,want,sizeof want,"glUniform4ui dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xbb,0xb5,0xae,0x0,0xe8,0xa6,0xf,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb5bb));if(!host_.expect(0xa3d430,want,sizeof want,"glUniform4ui64NV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x50,0xe3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce350));if(!host_.expect(0xa3d43d,want,sizeof want,"glUniform4ui64NV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xcc,0xb5,0xae,0x0,0xe8,0x59,0xf,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb5cc));if(!host_.expect(0xa3d47d,want,sizeof want,"glUniform4ui64vNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x20,0xeb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbceb20));if(!host_.expect(0xa3d48a,want,sizeof want,"glUniform4ui64vNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xde,0xb5,0xae,0x0,0xe8,0x29,0xf,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb5de));if(!host_.expect(0xa3d4ad,want,sizeof want,"glUniform4uiEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x28,0xc5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc528));if(!host_.expect(0xa3d4ba,want,sizeof want,"glUniform4uiEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xee,0xb5,0xae,0x0,0xe8,0xf9,0xe,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb5ee));if(!host_.expect(0xa3d4dd,want,sizeof want,"glUniform4uiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xd4,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcecd4));if(!host_.expect(0xa3d4ea,want,sizeof want,"glUniform4uiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xfc,0xb5,0xae,0x0,0xe8,0xc9,0xe,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb5fc));if(!host_.expect(0xa3d50d,want,sizeof want,"glUniform4uivEXT resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x3c,0xd5,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcd53c));if(!host_.expect(0xa3d51a,want,sizeof want,"glUniform4uivEXT dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xd,0xb6,0xae,0x0,0xe8,0x99,0xe,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb60d));if(!host_.expect(0xa3d53d,want,sizeof want,"glUniformBlockBinding resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x18,0xbe,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbe18));if(!host_.expect(0xa3d54a,want,sizeof want,"glUniformBlockBinding dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x92,0xb6,0xae,0x0,0xe8,0xa9,0xd,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb692));if(!host_.expect(0xa3d62d,want,sizeof want,"glUniformMatrix2dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x1c,0xc3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc31c));if(!host_.expect(0xa3d63a,want,sizeof want,"glUniformMatrix2dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa5,0xb6,0xae,0x0,0xe8,0x79,0xd,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb6a5));if(!host_.expect(0xa3d65d,want,sizeof want,"glUniformMatrix2fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x7c,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec7c));if(!host_.expect(0xa3d66a,want,sizeof want,"glUniformMatrix2fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb8,0xb6,0xae,0x0,0xe8,0x49,0xd,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb6b8));if(!host_.expect(0xa3d68d,want,sizeof want,"glUniformMatrix2fvARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x70,0xbf,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbf70));if(!host_.expect(0xa3d69a,want,sizeof want,"glUniformMatrix2fvARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xce,0xb6,0xae,0x0,0xe8,0x19,0xd,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb6ce));if(!host_.expect(0xa3d6bd,want,sizeof want,"glUniformMatrix2x3dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x44,0xdf,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdf44));if(!host_.expect(0xa3d6ca,want,sizeof want,"glUniformMatrix2x3dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe3,0xb6,0xae,0x0,0xe8,0xe9,0xc,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb6e3));if(!host_.expect(0xa3d6ed,want,sizeof want,"glUniformMatrix2x3fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xdc,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcecdc));if(!host_.expect(0xa3d6fa,want,sizeof want,"glUniformMatrix2x3fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf8,0xb6,0xae,0x0,0xe8,0xb9,0xc,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb6f8));if(!host_.expect(0xa3d71d,want,sizeof want,"glUniformMatrix2x3fvNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x7c,0xe1,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce17c));if(!host_.expect(0xa3d72a,want,sizeof want,"glUniformMatrix2x3fvNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf,0xb7,0xae,0x0,0xe8,0x89,0xc,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb70f));if(!host_.expect(0xa3d74d,want,sizeof want,"glUniformMatrix2x4dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xcc,0xc2,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc2cc));if(!host_.expect(0xa3d75a,want,sizeof want,"glUniformMatrix2x4dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x24,0xb7,0xae,0x0,0xe8,0x59,0xc,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb724));if(!host_.expect(0xa3d77d,want,sizeof want,"glUniformMatrix2x4fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xa0,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbceca0));if(!host_.expect(0xa3d78a,want,sizeof want,"glUniformMatrix2x4fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x39,0xb7,0xae,0x0,0xe8,0x29,0xc,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb739));if(!host_.expect(0xa3d7ad,want,sizeof want,"glUniformMatrix2x4fvNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xb8,0xe4,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce4b8));if(!host_.expect(0xa3d7ba,want,sizeof want,"glUniformMatrix2x4fvNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x50,0xb7,0xae,0x0,0xe8,0xf9,0xb,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb750));if(!host_.expect(0xa3d7dd,want,sizeof want,"glUniformMatrix3dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x78,0xe3,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce378));if(!host_.expect(0xa3d7ea,want,sizeof want,"glUniformMatrix3dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x63,0xb7,0xae,0x0,0xe8,0xc9,0xb,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb763));if(!host_.expect(0xa3d80d,want,sizeof want,"glUniformMatrix3fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xfc,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcecfc));if(!host_.expect(0xa3d81a,want,sizeof want,"glUniformMatrix3fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x76,0xb7,0xae,0x0,0xe8,0x99,0xb,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb776));if(!host_.expect(0xa3d83d,want,sizeof want,"glUniformMatrix3fvARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf8,0xdd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcddf8));if(!host_.expect(0xa3d84a,want,sizeof want,"glUniformMatrix3fvARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x8c,0xb7,0xae,0x0,0xe8,0x69,0xb,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb78c));if(!host_.expect(0xa3d86d,want,sizeof want,"glUniformMatrix3x2dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x24,0xc8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc824));if(!host_.expect(0xa3d87a,want,sizeof want,"glUniformMatrix3x2dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa1,0xb7,0xae,0x0,0xe8,0x39,0xb,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb7a1));if(!host_.expect(0xa3d89d,want,sizeof want,"glUniformMatrix3x2fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xac,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcecac));if(!host_.expect(0xa3d8aa,want,sizeof want,"glUniformMatrix3x2fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb6,0xb7,0xae,0x0,0xe8,0x9,0xb,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb7b6));if(!host_.expect(0xa3d8cd,want,sizeof want,"glUniformMatrix3x2fvNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x0,0xe7,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce700));if(!host_.expect(0xa3d8da,want,sizeof want,"glUniformMatrix3x2fvNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xcd,0xb7,0xae,0x0,0xe8,0xd9,0xa,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb7cd));if(!host_.expect(0xa3d8fd,want,sizeof want,"glUniformMatrix3x4dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xec,0xca,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccaec));if(!host_.expect(0xa3d90a,want,sizeof want,"glUniformMatrix3x4dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe2,0xb7,0xae,0x0,0xe8,0xa9,0xa,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb7e2));if(!host_.expect(0xa3d92d,want,sizeof want,"glUniformMatrix3x4fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x98,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec98));if(!host_.expect(0xa3d93a,want,sizeof want,"glUniformMatrix3x4fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xf7,0xb7,0xae,0x0,0xe8,0x79,0xa,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb7f7));if(!host_.expect(0xa3d95d,want,sizeof want,"glUniformMatrix3x4fvNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x1c,0xde,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcde1c));if(!host_.expect(0xa3d96a,want,sizeof want,"glUniformMatrix3x4fvNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xe,0xb8,0xae,0x0,0xe8,0x49,0xa,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb80e));if(!host_.expect(0xa3d98d,want,sizeof want,"glUniformMatrix4dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe8,0xe8,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbce8e8));if(!host_.expect(0xa3d99a,want,sizeof want,"glUniformMatrix4dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x21,0xb8,0xae,0x0,0xe8,0x19,0xa,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb821));if(!host_.expect(0xa3d9bd,want,sizeof want,"glUniformMatrix4fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xbc,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcecbc));if(!host_.expect(0xa3d9ca,want,sizeof want,"glUniformMatrix4fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x34,0xb8,0xae,0x0,0xe8,0xe9,0x9,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb834));if(!host_.expect(0xa3d9ed,want,sizeof want,"glUniformMatrix4fvARB resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xf0,0xdb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcdbf0));if(!host_.expect(0xa3d9fa,want,sizeof want,"glUniformMatrix4fvARB dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x4a,0xb8,0xae,0x0,0xe8,0xb9,0x9,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb84a));if(!host_.expect(0xa3da1d,want,sizeof want,"glUniformMatrix4x2dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xbc,0xcb,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbccbbc));if(!host_.expect(0xa3da2a,want,sizeof want,"glUniformMatrix4x2dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x5f,0xb8,0xae,0x0,0xe8,0x89,0x9,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb85f));if(!host_.expect(0xa3da4d,want,sizeof want,"glUniformMatrix4x2fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe0,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcece0));if(!host_.expect(0xa3da5a,want,sizeof want,"glUniformMatrix4x2fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x74,0xb8,0xae,0x0,0xe8,0x59,0x9,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb874));if(!host_.expect(0xa3da7d,want,sizeof want,"glUniformMatrix4x2fvNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x4,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcec04));if(!host_.expect(0xa3da8a,want,sizeof want,"glUniformMatrix4x2fvNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x8b,0xb8,0xae,0x0,0xe8,0x29,0x9,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb88b));if(!host_.expect(0xa3daad,want,sizeof want,"glUniformMatrix4x3dv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x48,0xc0,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcc048));if(!host_.expect(0xa3daba,want,sizeof want,"glUniformMatrix4x3dv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xa0,0xb8,0xae,0x0,0xe8,0xf9,0x8,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb8a0));if(!host_.expect(0xa3dadd,want,sizeof want,"glUniformMatrix4x3fv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x4,0xed,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbced04));if(!host_.expect(0xa3daea,want,sizeof want,"glUniformMatrix4x3fv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb5,0xb8,0xae,0x0,0xe8,0xc9,0x8,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb8b5));if(!host_.expect(0xa3db0d,want,sizeof want,"glUniformMatrix4x3fvNV resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x2c,0xde,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcde2c));if(!host_.expect(0xa3db1a,want,sizeof want,"glUniformMatrix4x3fvNV dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xcc,0xb8,0xae,0x0,0xe8,0x99,0x8,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb8cc));if(!host_.expect(0xa3db3d,want,sizeof want,"glUniformSubroutinesuiv resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0x70,0xbe,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbe70));if(!host_.expect(0xa3db4a,want,sizeof want,"glUniformSubroutinesuiv dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0xb5,0xb9,0xae,0x0,0xe8,0xb9,0x6,0xfe,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaeb9b5));if(!host_.expect(0xa3dd1d,want,sizeof want,"glUseProgram resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xe4,0xec,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcece4));if(!host_.expect(0xa3dd2a,want,sizeof want,"glUseProgram dispatch slot"))return false;}
    {std::uint8_t want[]={0xb9,0x49,0xd8,0xae,0x0,0xe8,0x89,0xc3,0xfd,0xff};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xaed849));if(!host_.expect(0xa4204d,want,sizeof want,"glViewport resolver name"))return false;}
    {std::uint8_t want[]={0xa3,0xc,0xbd,0xbc,0x0};*reinterpret_cast<std::uint32_t*>(want+1)=std::uint32_t(host_.address(0xbcbd0c));if(!host_.expect(0xa4205a,want,sizeof want,"glViewport dispatch slot"))return false;}
    {const std::uint8_t want[]={0x55,0x8B,0xEC,0x6A,0xFF};if(!host_.expect(0x408330,want,5,"entity native SmartImage release"))return false;}
    {const std::uint8_t want[]={0x56,0x8b,0xf1,0x8d,0x46,0x78,0x50,0x6a,0x01};if(!host_.expect(0xA711E0,want,sizeof want,"ImagePlatform texture0 unload field"))return false;}
    {const std::uint8_t want[]={0x8d,0x46,0x7c,0x50,0x6a,0x01};if(!host_.expect(0xA711EF,want,sizeof want,"ImagePlatform palette unload field"))return false;}
    {const std::uint8_t want[]={0xff,0x76,0x78,0x68,0xe1,0x0d,0,0};if(!host_.expect(0xA15E76,want,sizeof want,"ImagePlatform texture bind field"))return false;}
    {const std::uint8_t want[]={0xff,0x76,0x78,0x68,0xe1,0x0d,0,0};if(!host_.expect(0xA0C70F,want,sizeof want,"ImagePlatform render target texture field"))return false;}
    for(auto va:{0xA71317u,0xA7132Cu,0xA71341u,0xA71356u,0xA71450u,0xA71470u,0xA714C9u,0xA714E7u}) {
        std::uint8_t want[]={0xff,0x15,0x5c,0xc6,0xbc,0};*reinterpret_cast<std::uint32_t*>(want+2)=std::uint32_t(host_.address(0xBCC65C));
        if(!host_.expect(va,want,sizeof want,"native texture parameter callsite"))return false;
    }
    for(auto va:{0xA71393u,0xA71413u}) {
        std::uint8_t want[]={0xff,0x15,0xfc,0xe1,0xbc,0};*reinterpret_cast<std::uint32_t*>(want+2)=std::uint32_t(host_.address(0xBCE1FC));
        if(!host_.expect(va,want,sizeof want,"native texture upload callsite"))return false;
    }
    active_=this;J374Hooks::gameThreadId=GetCurrentThreadId();J374Hooks::cache=&cache_;J374Hooks::MakeKey=KeyFor;J374Hooks::OnEvent=Event;J374Hooks::OnMiss=Miss;J374Hooks::InsideRender=host_.insideRender;J374Hooks::OnOutsideRender=OutsideEvent;
    auto fail=[this](){Message("[entity-cache] install failed; reverting all cache hooks");Shutdown();return false;};
    if(!Detour(0x860ab0,reinterpret_cast<void*>(&J374Hooks::Familiar),5,reinterpret_cast<void**>(&J374Hooks::familiar)))return fail();
    if(!Detour(0x860660,reinterpret_cast<void*>(&J374Hooks::Npc),5,reinterpret_cast<void**>(&J374Hooks::npc)))return fail();
    if(!Detour(0x863f90,reinterpret_cast<void*>(&J374Hooks::Player),5,reinterpret_cast<void**>(&J374Hooks::player)))return fail();
    if(!Detour(0x862cd0,reinterpret_cast<void*>(&J374Hooks::Pickup),5,reinterpret_cast<void**>(&J374Hooks::pickup)))return fail();
    if(!Detour(0x8627b0,reinterpret_cast<void*>(&J374Hooks::Tear),5,reinterpret_cast<void**>(&J374Hooks::tear)))return fail();
    if(!Detour(0x862290,reinterpret_cast<void*>(&J374Hooks::Projectile),5,reinterpret_cast<void**>(&J374Hooks::projectile)))return fail();
    if(!Detour(0x8613a0,reinterpret_cast<void*>(&J374Hooks::Laser),5,reinterpret_cast<void**>(&J374Hooks::laser)))return fail();
    if(!Detour(0x861670,reinterpret_cast<void*>(&J374Hooks::Knife),5,reinterpret_cast<void**>(&J374Hooks::knife)))return fail();
    if(!Detour(0x8610d0,reinterpret_cast<void*>(&J374Hooks::Effect),5,reinterpret_cast<void**>(&J374Hooks::effect)))return fail();
    if(!Detour(0x861c80,reinterpret_cast<void*>(&J374Hooks::Bomb),5,reinterpret_cast<void**>(&J374Hooks::bomb)))return fail();
    if(!Detour(0xA06820,reinterpret_cast<void*>(&J374Hooks::Submit),6,reinterpret_cast<void**>(&J374Hooks::submit)))return fail();
    if(!Observe(0xa0c6e0,7,1,false))return fail(); // SetRT
    if(!Observe(0xa0c880,8,2,false))return fail(); // Clear
    if(!Observe(0xa09720,10,3,false))return fail(); // Flush
    if(!Observe(0xa140c0,6,4,false))return fail(); // ApplyFrameImages
    if(!Observe(0xa14060,6,5,false))return fail(); // ApplyTransparentBatches
    if(!Observe(0xa078c0,5,6,false))return fail(); // Triangle
    if(!Detour(0xa065d0,reinterpret_cast<void*>(&J374Hooks::SetFormat),6,reinterpret_cast<void**>(&J374Hooks::setFormat)))return fail(); // VertexFormat, thiscall RET4
    if(!Observe(0xa05d80,6,8,true))return fail(); // VertexAllocator
    if(!ObserveGl("glBindFramebuffer",0xbcbd14,0xa22d40))return fail();
    if(!ObserveGl("glBindFramebufferEXT",0xbcc524,0xa22d70))return fail();
    if(!ObserveGl("glClear",0xbcbd18,0xa24000))return fail();
    if(!ObserveGl("glCopyTexImage1D",0xbcdda0,0xa26140))return fail();
    if(!ObserveGl("glCopyTexImage1DEXT",0xbce730,0xa26170))return fail();
    if(!ObserveGl("glCopyTexImage2D",0xbcdaa4,0xa261a0))return fail();
    if(!ObserveGl("glCopyTexImage2DEXT",0xbceb44,0xa261d0))return fail();
    if(!ObserveGl("glCopyTexSubImage1D",0xbcc714,0xa26200))return fail();
    if(!ObserveGl("glCopyTexSubImage1DEXT",0xbcbd74,0xa26230))return fail();
    if(!ObserveGl("glCopyTexSubImage2D",0xbceacc,0xa26260))return fail();
    if(!ObserveGl("glCopyTexSubImage2DEXT",0xbcd694,0xa26290))return fail();
    if(!ObserveGl("glCopyTexSubImage3D",0xbcd4d8,0xa262c0))return fail();
    if(!ObserveGl("glCopyTexSubImage3DEXT",0xbce2ac,0xa262f0))return fail();
    if(!ObserveGl("glCopyTexSubImage3DOES",0xbcc388,0xa26320))return fail();
    if(!ObserveGl("glCopyTextureSubImage1D",0xbce71c,0xa263b0))return fail();
    if(!ObserveGl("glCopyTextureSubImage2D",0xbcd62c,0xa26400))return fail();
    if(!ObserveGl("glCopyTextureSubImage3D",0xbccc38,0xa26450))return fail();
    if(!ObserveGl("glDeleteTextures",0xbce090,0xa27220))return fail();
    if(!ObserveGl("glDisable",0xbccb7c,0xa27700))return fail();
    if(!ObserveGl("glDrawArrays",0xbcda84,0xa27a60))return fail();
    if(!ObserveGl("glDrawArraysEXT",0xbcd820,0xa27a90))return fail();
    if(!ObserveGl("glDrawArraysIndirect",0xbcded0,0xa27ac0))return fail();
    if(!ObserveGl("glDrawArraysInstanced",0xbcec1c,0xa27af0))return fail();
    if(!ObserveGl("glDrawArraysInstancedANGLE",0xbcdb14,0xa27b20))return fail();
    if(!ObserveGl("glDrawArraysInstancedARB",0xbcd5c0,0xa27b50))return fail();
    if(!ObserveGl("glDrawArraysInstancedBaseInstance",0xbcc6dc,0xa27b80))return fail();
    if(!ObserveGl("glDrawArraysInstancedBaseInstanceEXT",0xbcc55c,0xa27bb0))return fail();
    if(!ObserveGl("glDrawArraysInstancedEXT",0xbce680,0xa27be0))return fail();
    if(!ObserveGl("glDrawArraysInstancedNV",0xbce28c,0xa27c10))return fail();
    if(!ObserveGl("glDrawBuffers",0xbcda6c,0xa27c60))return fail();
    if(!ObserveGl("glDrawBuffersARB",0xbcd858,0xa27c90))return fail();
    if(!ObserveGl("glDrawBuffersATI",0xbce0e8,0xa27cc0))return fail();
    if(!ObserveGl("glDrawBuffersEXT",0xbce78c,0xa27cf0))return fail();
    if(!ObserveGl("glDrawElements",0xbced1c,0xa27e20))return fail();
    if(!ObserveGl("glDrawElementsBaseVertex",0xbce9b8,0xa27e50))return fail();
    if(!ObserveGl("glDrawElementsBaseVertexEXT",0xbcc5dc,0xa27e80))return fail();
    if(!ObserveGl("glDrawElementsBaseVertexOES",0xbccc04,0xa27eb0))return fail();
    if(!ObserveGl("glDrawElementsIndirect",0xbcbedc,0xa27ee0))return fail();
    if(!ObserveGl("glDrawElementsInstanced",0xbcce24,0xa27f10))return fail();
    if(!ObserveGl("glDrawElementsInstancedANGLE",0xbcd9d8,0xa27f40))return fail();
    if(!ObserveGl("glDrawElementsInstancedARB",0xbcd554,0xa27f70))return fail();
    if(!ObserveGl("glDrawElementsInstancedBaseInstance",0xbcd7a0,0xa27fa0))return fail();
    if(!ObserveGl("glDrawElementsInstancedBaseInstanceEXT",0xbcdedc,0xa27fd0))return fail();
    if(!ObserveGl("glDrawElementsInstancedBaseVertex",0xbcd2e8,0xa28000))return fail();
    if(!ObserveGl("glDrawElementsInstancedBaseVertexBaseInstance",0xbcc22c,0xa28030))return fail();
    if(!ObserveGl("glDrawElementsInstancedBaseVertexBaseInstanceEXT",0xbcc7bc,0xa28060))return fail();
    if(!ObserveGl("glDrawElementsInstancedBaseVertexEXT",0xbcbed8,0xa28090))return fail();
    if(!ObserveGl("glDrawElementsInstancedBaseVertexOES",0xbce314,0xa280c0))return fail();
    if(!ObserveGl("glDrawElementsInstancedEXT",0xbcdc4c,0xa280f0))return fail();
    if(!ObserveGl("glDrawElementsInstancedNV",0xbce570,0xa28120))return fail();
    if(!ObserveGl("glDrawRangeElements",0xbcdbdc,0xa281d0))return fail();
    if(!ObserveGl("glDrawRangeElementsBaseVertex",0xbce438,0xa28200))return fail();
    if(!ObserveGl("glDrawRangeElementsBaseVertexEXT",0xbcc1e4,0xa28230))return fail();
    if(!ObserveGl("glDrawRangeElementsBaseVertexOES",0xbcd340,0xa28260))return fail();
    if(!ObserveGl("glDrawRangeElementsEXT",0xbcd124,0xa28290))return fail();
    if(!ObserveGl("glDrawTransformFeedback",0xbcdfe4,0xa28490))return fail();
    if(!ObserveGl("glDrawTransformFeedbackInstanced",0xbcd958,0xa284c0))return fail();
    if(!ObserveGl("glDrawTransformFeedbackNV",0xbcc1f0,0xa284f0))return fail();
    if(!ObserveGl("glDrawTransformFeedbackStream",0xbcd7d8,0xa28520))return fail();
    if(!ObserveGl("glDrawTransformFeedbackStreamInstanced",0xbcc840,0xa28550))return fail();
    if(!ObserveGl("glEnable",0xbcc938,0xa286c0))return fail();
    if(!ObserveGl("glMultiDrawArrays",0xbcc3b8,0xa319c0))return fail();
    if(!ObserveGl("glMultiDrawArraysEXT",0xbcd08c,0xa319f0))return fail();
    if(!ObserveGl("glMultiDrawArraysIndirect",0xbcdea4,0xa31a20))return fail();
    if(!ObserveGl("glMultiDrawArraysIndirectAMD",0xbcd1b8,0xa31a50))return fail();
    if(!ObserveGl("glMultiDrawArraysIndirectEXT",0xbcd110,0xa31ae0))return fail();
    if(!ObserveGl("glMultiDrawElements",0xbcd86c,0xa31b30))return fail();
    if(!ObserveGl("glMultiDrawElementsBaseVertex",0xbccaf8,0xa31b60))return fail();
    if(!ObserveGl("glMultiDrawElementsBaseVertexEXT",0xbce8b8,0xa31b90))return fail();
    if(!ObserveGl("glMultiDrawElementsBaseVertexOES",0xbccab8,0xa31bc0))return fail();
    if(!ObserveGl("glMultiDrawElementsEXT",0xbccc40,0xa31bf0))return fail();
    if(!ObserveGl("glMultiDrawElementsIndirect",0xbcd9f4,0xa31c20))return fail();
    if(!ObserveGl("glMultiDrawElementsIndirectAMD",0xbcd898,0xa31c50))return fail();
    if(!ObserveGl("glMultiDrawElementsIndirectEXT",0xbcc444,0xa31ce0))return fail();
    if(!ObserveGl("glScissor",0xbcc23c,0xa38d10))return fail();
    if(!ObserveGl("glTexBuffer",0xbce0c8,0xa39fe0))return fail();
    if(!ObserveGl("glTexBufferARB",0xbcde20,0xa3a010))return fail();
    if(!ObserveGl("glTexBufferEXT",0xbcd760,0xa3a040))return fail();
    if(!ObserveGl("glTexBufferOES",0xbcd140,0xa3a070))return fail();
    if(!ObserveGl("glTexBufferRange",0xbcda7c,0xa3a0a0))return fail();
    if(!ObserveGl("glTexBufferRangeEXT",0xbcd1f0,0xa3a0d0))return fail();
    if(!ObserveGl("glTexBufferRangeOES",0xbcc4b0,0xa3a100))return fail();
    if(!ObserveGl("glTexCoordP1ui",0xbcdee0,0xa3ae50))return fail();
    if(!ObserveGl("glTexCoordP1uiv",0xbcc508,0xa3ae80))return fail();
    if(!ObserveGl("glTexCoordP2ui",0xbccd34,0xa3aeb0))return fail();
    if(!ObserveGl("glTexCoordP2uiv",0xbcdb30,0xa3aee0))return fail();
    if(!ObserveGl("glTexCoordP3ui",0xbcd3a0,0xa3af10))return fail();
    if(!ObserveGl("glTexCoordP3uiv",0xbcc340,0xa3af40))return fail();
    if(!ObserveGl("glTexCoordP4ui",0xbce48c,0xa3af70))return fail();
    if(!ObserveGl("glTexCoordP4uiv",0xbce770,0xa3afa0))return fail();
    if(!ObserveGl("glTexCoordPointer",0xbcc94c,0xa3afd0))return fail();
    if(!ObserveGl("glTexEnvf",0xbceb0c,0xa3b060))return fail();
    if(!ObserveGl("glTexEnvfv",0xbccac0,0xa3b0a0))return fail();
    if(!ObserveGl("glTexEnvi",0xbce8c0,0xa3b0d0))return fail();
    if(!ObserveGl("glTexEnviv",0xbcdb50,0xa3b100))return fail();
    if(!ObserveGl("glTexGenxOES",0xbcdd18,0xa3b350))return fail();
    if(!ObserveGl("glTexGenxvOES",0xbcdc70,0xa3b380))return fail();
    if(!ObserveGl("glTexImage2D",0xbce1fc,0xa3b3d0))return fail();
    if(!ObserveGl("glTexImage2DMultisample",0xbce3f8,0xa3b400))return fail();
    if(!ObserveGl("glTexImage3D",0xbcdb40,0xa3b450))return fail();
    if(!ObserveGl("glTexImage3DEXT",0xbcd850,0xa3b480))return fail();
    if(!ObserveGl("glTexImage3DMultisample",0xbcc4a8,0xa3b4b0))return fail();
    if(!ObserveGl("glTexImage3DOES",0xbcc838,0xa3b500))return fail();
    if(!ObserveGl("glTexPageCommitmentARB",0xbcc7b4,0xa3b550))return fail();
    if(!ObserveGl("glTexPageCommitmentEXT",0xbcc794,0xa3b580))return fail();
    if(!ObserveGl("glTexParameterIiv",0xbce6cc,0xa3b5b0))return fail();
    if(!ObserveGl("glTexParameterIivEXT",0xbcd434,0xa3b5e0))return fail();
    if(!ObserveGl("glTexParameterIivOES",0xbcd6ac,0xa3b610))return fail();
    if(!ObserveGl("glTexParameterIuiv",0xbccf74,0xa3b640))return fail();
    if(!ObserveGl("glTexParameterIuivEXT",0xbccc60,0xa3b670))return fail();
    if(!ObserveGl("glTexParameterIuivOES",0xbcca80,0xa3b6a0))return fail();
    if(!ObserveGl("glTexParameterf",0xbce2e0,0xa3b6d0))return fail();
    if(!ObserveGl("glTexParameterfv",0xbce3b8,0xa3b710))return fail();
    if(!ObserveGl("glTexParameteri",0xbcc65c,0xa3b740))return fail();
    if(!ObserveGl("glTexParameteriv",0xbceb24,0xa3b770))return fail();
    if(!ObserveGl("glTexStorage1D",0xbccf94,0xa3b840))return fail();
    if(!ObserveGl("glTexStorage1DEXT",0xbcdfd4,0xa3b870))return fail();
    if(!ObserveGl("glTexStorage2D",0xbcddfc,0xa3b8a0))return fail();
    if(!ObserveGl("glTexStorage2DEXT",0xbcbd44,0xa3b8d0))return fail();
    if(!ObserveGl("glTexStorage2DMultisample",0xbcc468,0xa3b900))return fail();
    if(!ObserveGl("glTexStorage3D",0xbcc86c,0xa3b930))return fail();
    if(!ObserveGl("glTexStorage3DEXT",0xbcda1c,0xa3b960))return fail();
    if(!ObserveGl("glTexStorage3DMultisample",0xbccd64,0xa3b990))return fail();
    if(!ObserveGl("glTexStorage3DMultisampleOES",0xbcd7a4,0xa3b9c0))return fail();
    if(!ObserveGl("glTexSubImage1D",0xbcdb38,0xa3ba10))return fail();
    if(!ObserveGl("glTexSubImage1DEXT",0xbce9a4,0xa3ba40))return fail();
    if(!ObserveGl("glTexSubImage2D",0xbce598,0xa3ba70))return fail();
    if(!ObserveGl("glTexSubImage2DEXT",0xbce708,0xa3baa0))return fail();
    if(!ObserveGl("glTexSubImage3D",0xbcdad4,0xa3bad0))return fail();
    if(!ObserveGl("glTexSubImage3DEXT",0xbcd2b0,0xa3bb00))return fail();
    if(!ObserveGl("glTexSubImage3DOES",0xbcc6a4,0xa3bb30))return fail();
    if(!ObserveGl("glTextureBarrier",0xbcbdf8,0xa3bb80))return fail();
    if(!ObserveGl("glTextureBuffer",0xbcd8e0,0xa3bbc0))return fail();
    if(!ObserveGl("glTextureBufferRange",0xbccae0,0xa3bc10))return fail();
    if(!ObserveGl("glTextureParameterIiv",0xbccb5c,0xa3bde0))return fail();
    if(!ObserveGl("glTextureParameterIuiv",0xbce6d8,0xa3be30))return fail();
    if(!ObserveGl("glTextureParameterf",0xbcc494,0xa3be80))return fail();
    if(!ObserveGl("glTextureParameterfv",0xbce218,0xa3bf00))return fail();
    if(!ObserveGl("glTextureParameteri",0xbcd494,0xa3bf50))return fail();
    if(!ObserveGl("glTextureParameteriv",0xbcebc4,0xa3bfa0))return fail();
    if(!ObserveGl("glTextureStorage1D",0xbcc6cc,0xa3c030))return fail();
    if(!ObserveGl("glTextureStorage1DEXT",0xbcc704,0xa3c060))return fail();
    if(!ObserveGl("glTextureStorage2D",0xbce110,0xa3c090))return fail();
    if(!ObserveGl("glTextureStorage2DEXT",0xbce1b0,0xa3c0c0))return fail();
    if(!ObserveGl("glTextureStorage2DMultisample",0xbcdc14,0xa3c0f0))return fail();
    if(!ObserveGl("glTextureStorage3D",0xbcc2a8,0xa3c140))return fail();
    if(!ObserveGl("glTextureStorage3DEXT",0xbceab4,0xa3c170))return fail();
    if(!ObserveGl("glTextureStorage3DMultisample",0xbcd4c4,0xa3c1a0))return fail();
    if(!ObserveGl("glTextureSubImage1D",0xbccdac,0xa3c210))return fail();
    if(!ObserveGl("glTextureSubImage2D",0xbcd32c,0xa3c260))return fail();
    if(!ObserveGl("glTextureSubImage3D",0xbcce3c,0xa3c2b0))return fail();
    if(!ObserveGl("glTextureView",0xbce04c,0xa3c300))return fail();
    if(!ObserveGl("glTextureViewEXT",0xbce7b4,0xa3c330))return fail();
    if(!ObserveGl("glTextureViewOES",0xbcc6e8,0xa3c360))return fail();
    if(!ObserveGl("glUniform1d",0xbcc6f4,0xa3c5d0))return fail();
    if(!ObserveGl("glUniform1dv",0xbcbd6c,0xa3c610))return fail();
    if(!ObserveGl("glUniform1f",0xbce63c,0xa3c640))return fail();
    if(!ObserveGl("glUniform1fARB",0xbcd6d0,0xa3c670))return fail();
    if(!ObserveGl("glUniform1fv",0xbcec90,0xa3c6a0))return fail();
    if(!ObserveGl("glUniform1fvARB",0xbcc548,0xa3c6d0))return fail();
    if(!ObserveGl("glUniform1i",0xbcecd0,0xa3c700))return fail();
    if(!ObserveGl("glUniform1i64NV",0xbcd5dc,0xa3c730))return fail();
    if(!ObserveGl("glUniform1i64vNV",0xbcc7f8,0xa3c770))return fail();
    if(!ObserveGl("glUniform1iARB",0xbcca44,0xa3c7a0))return fail();
    if(!ObserveGl("glUniform1iv",0xbcec70,0xa3c7d0))return fail();
    if(!ObserveGl("glUniform1ivARB",0xbce280,0xa3c800))return fail();
    if(!ObserveGl("glUniform1ui",0xbce3f4,0xa3c830))return fail();
    if(!ObserveGl("glUniform1ui64NV",0xbccca0,0xa3c860))return fail();
    if(!ObserveGl("glUniform1ui64vNV",0xbcdca8,0xa3c8a0))return fail();
    if(!ObserveGl("glUniform1uiEXT",0xbcbe20,0xa3c8d0))return fail();
    if(!ObserveGl("glUniform1uiv",0xbcec64,0xa3c900))return fail();
    if(!ObserveGl("glUniform1uivEXT",0xbce1d4,0xa3c930))return fail();
    if(!ObserveGl("glUniform2d",0xbcd23c,0xa3c960))return fail();
    if(!ObserveGl("glUniform2dv",0xbcdcac,0xa3c9b0))return fail();
    if(!ObserveGl("glUniform2f",0xbce1a4,0xa3c9e0))return fail();
    if(!ObserveGl("glUniform2fARB",0xbcdadc,0xa3ca20))return fail();
    if(!ObserveGl("glUniform2fv",0xbcec68,0xa3ca60))return fail();
    if(!ObserveGl("glUniform2fvARB",0xbcc744,0xa3ca90))return fail();
    if(!ObserveGl("glUniform2i",0xbcd5c8,0xa3cac0))return fail();
    if(!ObserveGl("glUniform2i64NV",0xbce0a8,0xa3caf0))return fail();
    if(!ObserveGl("glUniform2i64vNV",0xbcda60,0xa3cb30))return fail();
    if(!ObserveGl("glUniform2iARB",0xbcd9dc,0xa3cb60))return fail();
    if(!ObserveGl("glUniform2iv",0xbcecec,0xa3cb90))return fail();
    if(!ObserveGl("glUniform2ivARB",0xbcd220,0xa3cbc0))return fail();
    if(!ObserveGl("glUniform2ui",0xbcbdd4,0xa3cbf0))return fail();
    if(!ObserveGl("glUniform2ui64NV",0xbce6b4,0xa3cc20))return fail();
    if(!ObserveGl("glUniform2ui64vNV",0xbccc0c,0xa3cc60))return fail();
    if(!ObserveGl("glUniform2uiEXT",0xbccec0,0xa3cc90))return fail();
    if(!ObserveGl("glUniform2uiv",0xbcecf0,0xa3ccc0))return fail();
    if(!ObserveGl("glUniform2uivEXT",0xbcc828,0xa3ccf0))return fail();
    if(!ObserveGl("glUniform3d",0xbccd5c,0xa3cd20))return fail();
    if(!ObserveGl("glUniform3dv",0xbcdb24,0xa3cd70))return fail();
    if(!ObserveGl("glUniform3f",0xbcc610,0xa3cda0))return fail();
    if(!ObserveGl("glUniform3fARB",0xbcc1d4,0xa3cdf0))return fail();
    if(!ObserveGl("glUniform3fv",0xbcec5c,0xa3ce40))return fail();
    if(!ObserveGl("glUniform3fvARB",0xbcc498,0xa3ce70))return fail();
    if(!ObserveGl("glUniform3i",0xbcdf74,0xa3cea0))return fail();
    if(!ObserveGl("glUniform3i64NV",0xbcbf78,0xa3ced0))return fail();
    if(!ObserveGl("glUniform3i64vNV",0xbcd894,0xa3cf10))return fail();
    if(!ObserveGl("glUniform3iARB",0xbcd6c8,0xa3cf40))return fail();
    if(!ObserveGl("glUniform3iv",0xbcec80,0xa3cf70))return fail();
    if(!ObserveGl("glUniform3ivARB",0xbcc308,0xa3cfa0))return fail();
    if(!ObserveGl("glUniform3ui",0xbce0bc,0xa3cfd0))return fail();
    if(!ObserveGl("glUniform3ui64NV",0xbcd910,0xa3d000))return fail();
    if(!ObserveGl("glUniform3ui64vNV",0xbcdc5c,0xa3d040))return fail();
    if(!ObserveGl("glUniform3uiEXT",0xbcdc20,0xa3d070))return fail();
    if(!ObserveGl("glUniform3uiv",0xbcec60,0xa3d0a0))return fail();
    if(!ObserveGl("glUniform3uivEXT",0xbce180,0xa3d0d0))return fail();
    if(!ObserveGl("glUniform4d",0xbce0a0,0xa3d100))return fail();
    if(!ObserveGl("glUniform4dv",0xbcc85c,0xa3d160))return fail();
    if(!ObserveGl("glUniform4f",0xbcca2c,0xa3d190))return fail();
    if(!ObserveGl("glUniform4fARB",0xbcde7c,0xa3d1f0))return fail();
    if(!ObserveGl("glUniform4fv",0xbced00,0xa3d250))return fail();
    if(!ObserveGl("glUniform4fvARB",0xbccfc4,0xa3d280))return fail();
    if(!ObserveGl("glUniform4i",0xbcbde0,0xa3d2b0))return fail();
    if(!ObserveGl("glUniform4i64NV",0xbccd3c,0xa3d2e0))return fail();
    if(!ObserveGl("glUniform4i64vNV",0xbcea6c,0xa3d330))return fail();
    if(!ObserveGl("glUniform4iARB",0xbce75c,0xa3d360))return fail();
    if(!ObserveGl("glUniform4iv",0xbced10,0xa3d390))return fail();
    if(!ObserveGl("glUniform4ivARB",0xbcc304,0xa3d3c0))return fail();
    if(!ObserveGl("glUniform4ui",0xbcd768,0xa3d3f0))return fail();
    if(!ObserveGl("glUniform4ui64NV",0xbce350,0xa3d420))return fail();
    if(!ObserveGl("glUniform4ui64vNV",0xbceb20,0xa3d470))return fail();
    if(!ObserveGl("glUniform4uiEXT",0xbcc528,0xa3d4a0))return fail();
    if(!ObserveGl("glUniform4uiv",0xbcecd4,0xa3d4d0))return fail();
    if(!ObserveGl("glUniform4uivEXT",0xbcd53c,0xa3d500))return fail();
    if(!ObserveGl("glUniformBlockBinding",0xbcbe18,0xa3d530))return fail();
    if(!ObserveGl("glUniformMatrix2dv",0xbcc31c,0xa3d620))return fail();
    if(!ObserveGl("glUniformMatrix2fv",0xbcec7c,0xa3d650))return fail();
    if(!ObserveGl("glUniformMatrix2fvARB",0xbcbf70,0xa3d680))return fail();
    if(!ObserveGl("glUniformMatrix2x3dv",0xbcdf44,0xa3d6b0))return fail();
    if(!ObserveGl("glUniformMatrix2x3fv",0xbcecdc,0xa3d6e0))return fail();
    if(!ObserveGl("glUniformMatrix2x3fvNV",0xbce17c,0xa3d710))return fail();
    if(!ObserveGl("glUniformMatrix2x4dv",0xbcc2cc,0xa3d740))return fail();
    if(!ObserveGl("glUniformMatrix2x4fv",0xbceca0,0xa3d770))return fail();
    if(!ObserveGl("glUniformMatrix2x4fvNV",0xbce4b8,0xa3d7a0))return fail();
    if(!ObserveGl("glUniformMatrix3dv",0xbce378,0xa3d7d0))return fail();
    if(!ObserveGl("glUniformMatrix3fv",0xbcecfc,0xa3d800))return fail();
    if(!ObserveGl("glUniformMatrix3fvARB",0xbcddf8,0xa3d830))return fail();
    if(!ObserveGl("glUniformMatrix3x2dv",0xbcc824,0xa3d860))return fail();
    if(!ObserveGl("glUniformMatrix3x2fv",0xbcecac,0xa3d890))return fail();
    if(!ObserveGl("glUniformMatrix3x2fvNV",0xbce700,0xa3d8c0))return fail();
    if(!ObserveGl("glUniformMatrix3x4dv",0xbccaec,0xa3d8f0))return fail();
    if(!ObserveGl("glUniformMatrix3x4fv",0xbcec98,0xa3d920))return fail();
    if(!ObserveGl("glUniformMatrix3x4fvNV",0xbcde1c,0xa3d950))return fail();
    if(!ObserveGl("glUniformMatrix4dv",0xbce8e8,0xa3d980))return fail();
    if(!ObserveGl("glUniformMatrix4fv",0xbcecbc,0xa3d9b0))return fail();
    if(!ObserveGl("glUniformMatrix4fvARB",0xbcdbf0,0xa3d9e0))return fail();
    if(!ObserveGl("glUniformMatrix4x2dv",0xbccbbc,0xa3da10))return fail();
    if(!ObserveGl("glUniformMatrix4x2fv",0xbcece0,0xa3da40))return fail();
    if(!ObserveGl("glUniformMatrix4x2fvNV",0xbcec04,0xa3da70))return fail();
    if(!ObserveGl("glUniformMatrix4x3dv",0xbcc048,0xa3daa0))return fail();
    if(!ObserveGl("glUniformMatrix4x3fv",0xbced04,0xa3dad0))return fail();
    if(!ObserveGl("glUniformMatrix4x3fvNV",0xbcde2c,0xa3db00))return fail();
    if(!ObserveGl("glUniformSubroutinesuiv",0xbcbe70,0xa3db30))return fail();
    if(!ObserveGl("glUseProgram",0xbcece4,0xa3dd10))return fail();
    if(!ObserveGl("glViewport",0xbcbd0c,0xa42040))return fail();
    if(subscribers19715::Enable(host_.address,host_.expect)) {
        J374Hooks::Subscribers=&subscribers19715::Query;J374Hooks::OnNoSubscribers=&NoSubscribers;
        Message("[entity-cache] live native subscription gates enabled");
    } else Message("[entity-cache] subscription gate unavailable; full capture retained");
    Message(InstallLocationCache()?"[shader-locations] native positive query cache enabled":"[shader-locations] optional query cache off; original queries retained");
    installed_=true;Message("[entity-cache] 10 fixed-cadence entity callbacks and guarded native quad replay installed");return true;
}
} // namespace highfps_entity_probe
#endif
