#include "carousel_presentation.h"
#include <cstdio>
#include <limits>
#include <type_traits>
using namespace highfps::carousel_presentation;
static int failures=0;
#define CHECK(x) do{if(!(x)){++failures;std::printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
int main(){
 static_assert(std::is_trivially_destructible<Scope>::value,"SEH scope must not require C++ unwinding");
 float xy[6]={1,2,3,4,5,6},scale[3]={.8f,1,1.2f};
 Context c;c.owner=0x123400;c.managerState=1;c.menuPage=5;c.status=0;c.count=3;c.width=640;c.height=480;
 c.positions={uintptr_t(xy),uintptr_t(xy+6),uintptr_t(xy+6)};c.scales={uintptr_t(scale),uintptr_t(scale+3),uintptr_t(scale+3)};
 History h;CHECK(h.Sample(c,xy,scale));CHECK(Equal(h.previous[0],h.current[0]));
 for(int i=0;i<3;++i){xy[i*2]+=4;xy[i*2+1]-=2;scale[i]+=.2f;}
 CHECK(h.Sample(c,xy,scale));
 Scope s;CHECK(!s.Begin(h,c,xy,scale,1.0,1.0,false));
 CHECK(s.Begin(h,c,xy,scale,1+1.0/120,1,true));CHECK(std::abs(xy[0]-3)<1e-5);CHECK(std::abs(scale[0]-.9f)<1e-5);
 CHECK(!s.Begin(h,c,xy,scale,1,1,true));CHECK(s.Restore(c));CHECK(xy[0]==5&&xy[1]==0&&scale[0]==1);
 // Native unexpected writes and lifetime changes are never overwritten.
 CHECK(s.Begin(h,c,xy,scale,1,1,true));xy[2]=987;CHECK(!s.Restore(c));CHECK(xy[2]==987);CHECK(!h.valid);
 CHECK(h.Sample(c,xy,scale));CHECK(s.Begin(h,c,xy,scale,1,1,true));Context changed=c;changed.epoch++;
 CHECK(!s.Restore(changed));CHECK(!h.valid);
 CHECK(h.Sample(c,xy,scale));xy[0]+=1;CHECK(!s.Begin(h,c,xy,scale,1,1,true));CHECK(!h.valid);
 for(int identity=0;identity<6;++identity){
  CHECK(h.Sample(c,xy,scale));changed=c;
  if(identity==0)changed.rosterPage=1;if(identity==1)changed.rosterHash++;
  if(identity==2)changed.width=800;if(identity==3)changed.epoch++;
  if(identity==4)changed.positions.capacity+=8;if(identity==5)changed.owner+=4;
  CHECK(h.Sample(changed,xy,scale));CHECK(Equal(h.previous[0],h.current[0]));
 }
 changed=c;changed.scales={uintptr_t(xy),uintptr_t(xy+3),uintptr_t(xy+3)};CHECK(!Active(changed));
 changed=c;changed.positions.begin++;changed.positions.end++;changed.positions.capacity++;CHECK(!Active(changed));
 changed=c;changed.count=0;CHECK(!Active(changed));
 changed=c;changed.count=MaxEntries+1;CHECK(!h.Sample(changed,xy,scale));
 changed=c;changed.positions.end-=4;CHECK(!h.Sample(changed,xy,scale));
 changed=c;changed.status=3;CHECK(!h.Sample(changed,xy,scale));
 xy[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!h.Sample(c,xy,scale));xy[0]=1;
 CHECK(h.Sample(c,xy,scale));CHECK(s.Begin(h,c,xy,scale,std::numeric_limits<double>::quiet_NaN(),1,true));CHECK(s.Restore(c));
 // Wrap/reversal use stable entry positions, not a naive 359->1 angle lerp.
 for(int hz:{45,60,80,90,100,144}){
  History curve;double angle=350;unsigned tick=0;
  auto sample=[&](){double radians=angle*3.141592653589793/180;xy[0]=float(100*std::cos(radians));xy[1]=float(50*std::sin(radians));scale[0]=float(1+.2*std::sin(radians));CHECK(curve.Sample(c,xy,scale));};
  sample();
  for(int frame=1;frame<=hz;++frame){
   double now=double(frame)/hz;
   while(double(tick+1)/60<=now+1e-9){++tick;angle+=tick<30?3:-3;sample();}
   Pose before=Read(xy,scale,0);Scope draw;CHECK(draw.Begin(curve,c,xy,scale,now,double(tick)/60,true));
   Pose rendered=Read(xy,scale,0),a=curve.previous[0],b=curve.current[0];
   CHECK(rendered.x>=std::fmin(a.x,b.x)-1e-5&&rendered.x<=std::fmax(a.x,b.x)+1e-5);
   CHECK(rendered.y>=std::fmin(a.y,b.y)-1e-5&&rendered.y<=std::fmax(a.y,b.y)+1e-5);
   CHECK(rendered.scale>=std::fmin(a.scale,b.scale)-1e-5&&rendered.scale<=std::fmax(a.scale,b.scale)+1e-5);
   CHECK(draw.Restore(c));CHECK(Equal(Read(xy,scale,0),before));
  }
 }
 std::printf("carousel_presentation_test: %s (%d failures)\n",failures?"FAIL":"PASS",failures);return failures?1:0;
}
