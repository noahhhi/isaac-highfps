#define HIGHFPS_THREADING_TEST_ACCESS
#include "threading_support.h"
#include <cstdlib>
#include <string>
#include <vector>
#include <cstdio>
using namespace highfps::threading;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"CHECK failed line%d: %s (Win32=%lu)\n",__LINE__,#x,GetLastError());std::exit(1);}}while(0)
struct Sink {
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    HANDLE release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::vector<std::string> rows;
    DWORD writeThread=0,flushThread=0;
    static void Write(void* context,const char* text,std::size_t n){
        auto& s=*static_cast<Sink*>(context);s.writeThread=GetCurrentThreadId();
        SetEvent(s.entered);WaitForSingleObject(s.release,INFINITE);s.rows.emplace_back(text,n);
    }
    static void Flush(void* context){static_cast<Sink*>(context)->flushThread=GetCurrentThreadId();}
    TextSink Interface(){return {this,&Write,&Flush};}
};
namespace highfps { namespace threading {
struct AsyncTextLogTestAccess {
    template<std::size_t C,std::size_t B> static void SetOrigin(AsyncTextLog<C,B>& q,DWORD origin){
        q.enqueue_.store(origin);q.dequeue_=origin;q.flushedTicket_.store(origin);
        for(DWORD i=0;i<C;++i)q.queue_[(origin+i)&(C-1)].sequence.store(origin+i);
    }
    template<std::size_t C,std::size_t B> static void ReserveTwo(AsyncTextLog<C,B>& q){q.enqueue_.store(2);InterlockedExchange(&q.activeProducers_,2);}
    template<std::size_t C,std::size_t B> static void Publish(AsyncTextLog<C,B>& q,DWORD pos,const char* text){
        auto& slot=q.queue_[pos&(C-1)];slot.length=std::strlen(text);std::memcpy(slot.text,text,slot.length);
        slot.sequence.store(pos+1,std::memory_order_release);InterlockedIncrement(&q.accepted_);InterlockedDecrement(&q.activeProducers_);SetEvent(q.wake_);
    }
};
}}
struct Producer {
    AsyncTextLog<4096,64>* log=nullptr;int id=0;bool accepted[500]={};
};
DWORD WINAPI Produce(void* arg){auto& p=*static_cast<Producer*>(arg);for(int i=0;i<500;++i){char line[64];std::sprintf(line,"p%d-%d",p.id,i);p.accepted[i]=p.log->TryWrite(line);}return 0;}
struct Sentinel {HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);};
DWORD WINAPI SentinelThread(void* p){WaitForSingleObject(static_cast<Sentinel*>(p)->stop,INFINITE);return 0;}
DWORD WINAPI WrongRestore(void* p){return static_cast<MainThreadAffinity*>(p)->Restore()?1:0;}
int main(){
    MainThreadAffinity affinity;GROUP_AFFINITY before={},after={},otherBefore={},otherAfter={};DWORD_PTR processBefore=0,processAfter=0,system=0;
    CHECK(GetThreadGroupAffinity(GetCurrentThread(),&before));CHECK(GetProcessAffinityMask(GetCurrentProcess(),&processBefore,&system));
    Sentinel sentinel;HANDLE other=CreateThread(nullptr,0,SentinelThread,&sentinel,0,nullptr);CHECK(other);CHECK(GetThreadGroupAffinity(other,&otherBefore));
    CHECK(!affinity.Apply(-3));CHECK(!affinity.Apply(int(sizeof(DWORD_PTR)*8)));CHECK(affinity.Apply(-1));
    CHECK(affinity.Apply(-2));CHECK(affinity.PinnedMask()!=0);CHECK((affinity.PinnedMask()&(affinity.PinnedMask()-1))==0);
    CHECK(affinity.OriginalMask()==before.Mask);
    for(unsigned cpu=0;cpu<sizeof(DWORD_PTR)*8;++cpu){const DWORD_PTR bit=DWORD_PTR(1)<<cpu;if(!(processBefore&bit)){MainThreadAffinity invalid;CHECK(!invalid.Apply(int(cpu)));break;}}
    for(unsigned cpu=0;cpu<sizeof(DWORD_PTR)*8;++cpu){const DWORD_PTR bit=DWORD_PTR(1)<<cpu;if((processBefore&bit) && bit!=affinity.PinnedMask()){CHECK(!affinity.Apply(int(cpu)));break;}}CHECK(GetThreadGroupAffinity(GetCurrentThread(),&after));CHECK(after.Mask==affinity.PinnedMask());
    CHECK(GetThreadGroupAffinity(other,&otherAfter));CHECK(otherBefore.Mask==otherAfter.Mask);CHECK(GetProcessAffinityMask(GetCurrentProcess(),&processAfter,&system));CHECK(processBefore==processAfter);
    HANDLE wrong=CreateThread(nullptr,0,WrongRestore,&affinity,0,nullptr);CHECK(wrong);CHECK(WaitForSingleObject(wrong,5000)==WAIT_OBJECT_0);DWORD ret=1;CHECK(GetExitCodeThread(wrong,&ret));CHECK(ret==0);CloseHandle(wrong);
    const DWORD_PTR workerMask=affinity.WorkerMask();
    Sink sink;auto* log=new AsyncTextLog<4,64>(sink.Interface());CHECK(log->Start(workerMask));
    CHECK(log->TryWrite("first"));CHECK(WaitForSingleObject(sink.entered,5000)==WAIT_OBJECT_0);
    for(int i=0;i<4;++i){char text[32];std::sprintf(text,"queued-%d",i);CHECK(log->TryWrite(text));}
    ULONGLONG start=GetTickCount64();for(int i=0;i<100000;++i)CHECK(!log->TryWrite("full"));
    const ULONGLONG saturationMs=GetTickCount64()-start;CHECK(saturationMs<1000);CHECK(log->Dropped()==100000);CHECK(!log->Flush(0));CHECK(!log->Stop(0));
    CHECK(!log->TryWrite("after-stop"));CHECK(log->Dropped()==100001);
    SetEvent(sink.release);CHECK(log->Stop(5000));CHECK(log->Flush(0));CHECK(log->Accepted()==5);CHECK(log->Written()==5);CHECK(sink.rows.size()==5);
    CHECK(sink.rows[0]=="first\n");for(int i=0;i<4;++i)CHECK(sink.rows[i+1]=="queued-"+std::to_string(i)+"\n");
    CHECK(sink.writeThread==log->WorkerThreadId());CHECK(sink.flushThread==log->WorkerThreadId());CHECK(sink.writeThread!=GetCurrentThreadId());
    Sink longSink;SetEvent(longSink.release);auto* longLog=new AsyncTextLog<4,64>(longSink.Interface());CHECK(longLog->Start(workerMask));CHECK(longLog->TryWrite(std::string(200,'x').c_str()));CHECK(longLog->Flush(5000));CHECK(longLog->Truncated()==1);CHECK(longSink.rows[0].size()==63);CHECK(longSink.rows[0].find(" [truncated]\n")!=std::string::npos);
    HANDLE logThread=OpenThread(THREAD_QUERY_INFORMATION,FALSE,longLog->WorkerThreadId());CHECK(logThread);CHECK(GetThreadPriority(logThread)==THREAD_PRIORITY_BELOW_NORMAL);GROUP_AFFINITY workerAffinity={};CHECK(GetThreadGroupAffinity(logThread,&workerAffinity));CHECK(workerAffinity.Mask==workerMask);CloseHandle(logThread);CHECK(longLog->Stop(5000));
    // Consumer continuously copies full production-size lines while the single
    // producer stays below capacity. No consumer-induced contention loss allowed.
    Sink sustained;SetEvent(sustained.release);auto* sustainedLog=new AsyncTextLog<>(sustained.Interface());CHECK(sustainedLog->Start(workerMask));
    const std::string payload(3900,'x');
    for(int batch=0;batch<64;++batch){for(int i=0;i<64;++i){const std::string line=std::to_string(batch*64+i)+":"+payload;CHECK(sustainedLog->TryWrite(line.c_str()));}CHECK(sustainedLog->Flush(5000));}
    CHECK(sustainedLog->Stop(5000));CHECK(sustainedLog->Dropped()==0);CHECK(sustained.rows.size()==4096);
    for(int i=0;i<4096;++i)CHECK(sustained.rows[i]==std::to_string(i)+":"+payload+"\n");
    // Tickets cross 32-bit wrap with FIFO and Flush intact.
    Sink wrapped;SetEvent(wrapped.release);auto* wrapLog=new AsyncTextLog<4,64>(wrapped.Interface());
    AsyncTextLogTestAccess::SetOrigin(*wrapLog,0xfffffffeu);CHECK(wrapLog->Start(workerMask));
    for(int i=0;i<12;++i){CHECK(wrapLog->TryWrite(std::to_string(i).c_str()));CHECK(wrapLog->Flush(5000));}
    CHECK(wrapLog->Stop(5000));CHECK(wrapped.rows.size()==12);CHECK(wrapLog->Dropped()==0);
    for(int i=0;i<12;++i)CHECK(wrapped.rows[i]==std::to_string(i)+"\n");
    // Producer2 publishes before producer1. Stop cannot abandon a reservation,
    // and Flush cannot mistake the published count for the ordered prefix.
    Sink reversed;SetEvent(reversed.release);auto* reverseLog=new AsyncTextLog<4,64>(reversed.Interface());
    AsyncTextLogTestAccess::ReserveTwo(*reverseLog);CHECK(reverseLog->Start(workerMask));
    AsyncTextLogTestAccess::Publish(*reverseLog,1,"second\n");CHECK(!reverseLog->Flush(0));CHECK(!reverseLog->Stop(0));
    AsyncTextLogTestAccess::Publish(*reverseLog,0,"first\n");CHECK(reverseLog->Stop(5000));CHECK(reverseLog->Flush(0));
    CHECK(reversed.rows.size()==2);CHECK(reversed.rows[0]=="first\n");CHECK(reversed.rows[1]=="second\n");
    // Actual concurrent producers may exhaust their bounded CAS retry budget;
    // all accepted records must remain complete/FIFO within each producer.
    Sink concurrent;SetEvent(concurrent.release);auto* multiLog=new AsyncTextLog<4096,64>(concurrent.Interface());CHECK(multiLog->Start(workerMask));
    Producer producers[4];HANDLE producersThreads[4];
    for(int i=0;i<4;++i){producers[i].id=i;producers[i].log=multiLog;producersThreads[i]=CreateThread(nullptr,0,Produce,&producers[i],0,nullptr);CHECK(producersThreads[i]);}
    CHECK(WaitForMultipleObjects(4,producersThreads,TRUE,5000)==WAIT_OBJECT_0);for(auto h:producersThreads)CloseHandle(h);CHECK(multiLog->Stop(5000));
    int last[4]={-1,-1,-1,-1};bool seen[4][500]={};int accepted=0;
    for(const auto& row:concurrent.rows){int id=-1,n=-1;CHECK(std::sscanf(row.c_str(),"p%d-%d",&id,&n)==2);CHECK(id>=0&&id<4&&n>=0&&n<500);CHECK(producers[id].accepted[n]);CHECK(!seen[id][n]);CHECK(n>last[id]);last[id]=n;seen[id][n]=true;++accepted;}
    for(int i=0;i<4;++i)for(int n=0;n<500;++n)CHECK(seen[i][n]==producers[i].accepted[n]);
    CHECK(multiLog->Accepted()==accepted);CHECK(multiLog->Dropped()+accepted==2000);
    CHECK(affinity.Restore());CHECK(GetThreadGroupAffinity(GetCurrentThread(),&after));CHECK(after.Mask==before.Mask);CHECK(affinity.Apply(-1));
    // External changes are retained rather than silently overwritten on teardown.
    CHECK(affinity.Apply(-2));
    if(before.Mask!=affinity.PinnedMask()){
        CHECK(SetThreadAffinityMask(GetCurrentThread(),before.Mask)!=0);CHECK(!affinity.Restore());
        CHECK(GetThreadGroupAffinity(GetCurrentThread(),&after));CHECK(after.Mask==before.Mask);
        CHECK(SetThreadAffinityMask(GetCurrentThread(),affinity.PinnedMask())!=0);CHECK(affinity.Restore());
    }else CHECK(affinity.Restore());
    SetEvent(sentinel.stop);CHECK(WaitForSingleObject(other,5000)==WAIT_OBJECT_0);CloseHandle(other);
    std::printf("PASS main-thread-only affinity/restore; FIFO, saturation100000drops in%llums, explicit drain, timeout lifetime, truncation, worker-only IO, low priority/mask; sustained4096 no-loss,32bit wrap,out-of-order publish/stop and4 producers\n",static_cast<unsigned long long>(saturationMs));return 0;
}
