#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include "menu_presentation.h"

namespace highfps { namespace carousel_presentation {
constexpr unsigned MaxEntries = 128;
struct Span {
    uintptr_t begin=0,end=0,capacity=0;
    bool operator==(const Span& b)const{return begin==b.begin&&end==b.end&&capacity==b.capacity;}
};
struct Context {
    uintptr_t owner=0;
    uint64_t epoch=0,rosterHash=0;
    int managerState=0,menuPage=0,status=0,rosterPage=0;
    unsigned count=0;
    float width=0,height=0;
    Span positions{},scales{},roster{};
};
struct Pose {float x=0,y=0,scale=0;};
inline bool Equal(Pose a,Pose b){return a.x==b.x&&a.y==b.y&&a.scale==b.scale;}
inline bool Finite(Pose p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.scale);}
inline bool ValidSpan(Span s,unsigned count,unsigned stride){
    return s.begin>=0x10000&&(s.begin%4)==0&&s.end>=s.begin&&s.capacity>=s.end&&
        s.end-s.begin==uintptr_t(count)*stride&&(s.capacity-s.begin)%stride==0&&
        s.capacity-s.begin<=1024*1024;
}
inline bool Active(const Context& c){
    return c.owner>=0x10000&&c.managerState==1&&c.menuPage==5&&c.status==0&&
        (c.rosterPage==0||c.rosterPage==1)&&c.count>0&&c.count<=MaxEntries&&
        std::isfinite(c.width)&&std::isfinite(c.height)&&c.width>0&&c.height>0&&
        ValidSpan(c.positions,c.count,8)&&ValidSpan(c.scales,c.count,4)&&
        (c.positions.end<=c.scales.begin||c.scales.end<=c.positions.begin);
}
inline bool Same(const Context&a,const Context&b){
    return a.owner==b.owner&&a.epoch==b.epoch&&a.managerState==b.managerState&&
        a.menuPage==b.menuPage&&a.status==b.status&&a.rosterPage==b.rosterPage&&a.count==b.count&&
        a.width==b.width&&a.height==b.height&&a.positions==b.positions&&a.scales==b.scales&&
        a.roster==b.roster&&a.rosterHash==b.rosterHash;
}
inline Pose Read(const float*xy,const float*scale,unsigned i){return {xy[2*i],xy[2*i+1],scale[i]};}
inline void Write(float*xy,float*scale,unsigned i,Pose p){xy[2*i]=p.x;xy[2*i+1]=p.y;scale[i]=p.scale;}
inline Pose Interpolate(Pose a,Pose b,float t){
    auto xy=menu_presentation::Interpolate({a.x,a.y},{b.x,b.y},t);
    if(!std::isfinite(t))t=0;if(t<0)t=0;if(t>1)t=1;
    return {xy.x,xy.y,float(double(a.scale)+(double(b.scale)-a.scale)*t)};
}
struct History {
    Context context{};
    std::array<Pose,MaxEntries> previous{},current{};
    bool valid=false;
    void Invalidate(){valid=false;}
    bool Matches(const Context&c)const{return valid&&Active(c)&&Same(context,c);}
    // Every canonical 60Hz render supplies the per-entry poses already computed
    // by native UpdateRotations. Its angle advances within the entry loop, so a
    // single angle cannot reconstruct this array. No extra native update runs.
    bool Sample(const Context&c,const float*xy,const float*scale){
        if(!Active(c)||uintptr_t(xy)!=c.positions.begin||uintptr_t(scale)!=c.scales.begin){Invalidate();return false;}
        std::array<Pose,MaxEntries> incoming{};
        for(unsigned i=0;i<c.count;++i){incoming[i]=Read(xy,scale,i);if(!Finite(incoming[i])){Invalidate();return false;}}
        const bool fresh=!Matches(c);
        for(unsigned i=0;i<c.count;++i){previous[i]=fresh?incoming[i]:current[i];current[i]=incoming[i];}
        context=c;valid=true;return true;
    }
};
// Trivially destructible explicit scope for the host's SEH __finally. Neither
// selection nor target/input-ready/angle belongs to this temporary draw state.
class Scope {
    History*history_=nullptr;Context context_{};
    float*xy_=nullptr,*scale_=nullptr;
    std::array<Pose,MaxEntries> saved_{},displayed_{};
    bool applied_=false;
public:
    Scope()=default;
    Scope(const Scope&)=delete;Scope&operator=(const Scope&)=delete;
    bool Applied()const{return applied_;}
    bool Begin(History&h,const Context&c,float*xy,float*scale,double now,double tick,bool pure){
        if(applied_||!pure)return false;
        if(!h.Matches(c)){h.Invalidate();return false;}
        if(uintptr_t(xy)!=c.positions.begin||uintptr_t(scale)!=c.scales.begin)return false;
        for(unsigned i=0;i<c.count;++i){
            saved_[i]=Read(xy,scale,i);
            if(!Finite(saved_[i])||!Equal(saved_[i],h.current[i])){h.Invalidate();return false;}
            displayed_[i]=Interpolate(h.previous[i],h.current[i],menu_presentation::Alpha60(now,tick));
        }
        history_=&h;context_=c;xy_=xy;scale_=scale;applied_=true;
        for(unsigned i=0;i<c.count;++i)Write(xy_,scale_,i,displayed_[i]);
        return true;
    }
    bool Restore(const Context&live){
        if(!applied_)return true;applied_=false;
        if(!Active(live)||!Same(live,context_)){history_->Invalidate();return false;}
        for(unsigned i=0;i<live.count;++i)if(!Equal(Read(xy_,scale_,i),displayed_[i])){history_->Invalidate();return false;}
        for(unsigned i=0;i<live.count;++i)Write(xy_,scale_,i,saved_[i]);
        return true;
    }
    void Abandon(){if(history_)history_->Invalidate();applied_=false;}
};
}}
