#pragma once
// Win32-only, process-lifetime helpers. No game, Lua, GL or renderer ownership.
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <atomic>

namespace highfps { namespace threading {
class MainThreadAffinity {
    DWORD owner_=0;
    DWORD_PTR original_=0, applied_=0, process_=0;
public:
    // -1 disabled, -2 current logical CPU, >=0 explicit group-0 logical CPU.
    // Call on the actual wrapper thread, never the DLL initialization worker.
    bool Apply(int policy) {
        if(policy==-1) return Restore();
        if(policy < -2) return false;
        if(applied_) {
            if(owner_!=GetCurrentThreadId())return false;
            if(policy==-2)return true; // keep the already chosen CPU
            return policy>=0 && unsigned(policy)<sizeof(DWORD_PTR)*8 && applied_==(DWORD_PTR(1)<<policy);
        }
        GROUP_AFFINITY group={};
        DWORD_PTR system=0;
        if(!GetThreadGroupAffinity(GetCurrentThread(),&group) || group.Group!=0 ||
           !GetProcessAffinityMask(GetCurrentProcess(),&process_,&system))return false;
        const DWORD cpu=policy==-2?GetCurrentProcessorNumber():static_cast<DWORD>(policy);
        if(cpu>=sizeof(DWORD_PTR)*8)return false;
        const DWORD_PTR mask=DWORD_PTR(1)<<cpu;
        if(!(process_&mask))return false;
        const DWORD_PTR previous=SetThreadAffinityMask(GetCurrentThread(),mask);
        if(!previous)return false;
        owner_=GetCurrentThreadId();original_=previous;applied_=mask;return true;
    }
    bool Restore() {
        if(!applied_)return true;
        if(owner_!=GetCurrentThreadId())return false;
        GROUP_AFFINITY current={};
        // Do not overwrite an affinity change made by another actor since Apply.
        if(!GetThreadGroupAffinity(GetCurrentThread(),&current) || current.Group!=0 || current.Mask!=applied_)return false;
        if(!SetThreadAffinityMask(GetCurrentThread(),original_))return false;
        applied_=0;owner_=0;return true;
    }
    DWORD_PTR PinnedMask()const{return applied_;}
    DWORD_PTR OriginalMask()const{return original_;}
    DWORD_PTR ProcessMask()const{return process_;}
    DWORD OwnerThreadId()const{return owner_;}
    DWORD_PTR WorkerMask()const{
        const DWORD_PTR without=process_&~applied_;return without?without:process_;
    }
};

struct TextSink {
    void* context=nullptr;
    void (*write)(void*,const char*,std::size_t)=nullptr;
    void (*flush)(void*)=nullptr;
};

// Allocate once and retain until process exit, or explicitly Stop and leave the
// object allocated. Never destroy/stop/wait under DllMain's loader lock. A worker
// that times out remains alive; its FILE, sink context and object must stay alive.
// Bounded MPSC queue: producers reserve tickets, publish copied bytes with release,
// and one consumer reclaims slots after acquire/copy. Consumer work NEVER causes
// a producer lock-contention drop. Only capacity exhaustion or bounded contention
// with other producers can reject. FIFO follows reservation order.
template<std::size_t Capacity=128,std::size_t LineBytes=4096>
class AsyncTextLog {
    static_assert(Capacity>0 && (Capacity&(Capacity-1))==0 && Capacity<(1u<<30) && LineBytes>=32,"invalid log queue size");
    static_assert(std::atomic<DWORD>::is_always_lock_free,"32-bit atomics must be lock free");
    struct Slot {std::atomic<DWORD> sequence{0};char text[LineBytes];std::size_t length;};
    struct Line {char text[LineBytes];std::size_t length;};
    Slot queue_[Capacity]={};
    std::atomic<DWORD> enqueue_{0},flushedTicket_{0};
    DWORD dequeue_=0; // worker only
    HANDLE wake_=nullptr,worker_=nullptr;
    DWORD workerId_=0;
    DWORD_PTR workerMask_=0;
    TextSink sink_;
    volatile LONG started_=0,stopping_=0,dropped_=0,truncated_=0;
    volatile LONG accepted_=0,written_=0,activeProducers_=0;
#ifdef HIGHFPS_THREADING_TEST_ACCESS
    friend struct AsyncTextLogTestAccess;
#endif
    static LONG Read(volatile LONG* p){return InterlockedCompareExchange(p,0,0);}
    static LONG Distance(DWORD a,DWORD b){return static_cast<LONG>(a-b);}
    void InitializeSlots(){for(DWORD i=0;i<Capacity;++i)queue_[i].sequence.store(i,std::memory_order_relaxed);}
    static void FileWrite(void* p,const char* text,std::size_t size){std::fwrite(text,1,size,static_cast<FILE*>(p));}
    static void FileFlush(void* p){std::fflush(static_cast<FILE*>(p));}
    static DWORD WINAPI Entry(void* p){return static_cast<AsyncTextLog*>(p)->Run();}
    bool Pop(Line& line){
        Slot& slot=queue_[dequeue_&(Capacity-1)];
        if(slot.sequence.load(std::memory_order_acquire)!=dequeue_+1)return false;
        line.length=slot.length;std::memcpy(line.text,slot.text,line.length);
        slot.sequence.store(dequeue_+static_cast<DWORD>(Capacity),std::memory_order_release);
        ++dequeue_;return true;
    }
    DWORD Run(){
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        if(workerMask_)SetThreadAffinityMask(GetCurrentThread(),workerMask_);
        for(;;){
            unsigned batch=0;
            for(;batch<64;++batch){
                Line local;if(!Pop(local))break;
                sink_.write(sink_.context,local.text,local.length);InterlockedIncrement(&written_);
            }
            if(batch){sink_.flush(sink_.context);flushedTicket_.store(dequeue_,std::memory_order_release);}
            if(Read(&stopping_) && !Read(&activeProducers_) && dequeue_==enqueue_.load(std::memory_order_acquire))return 0;
            if(batch<64)WaitForSingleObject(wake_,20);
        }
    }
public:
    explicit AsyncTextLog(FILE* file):sink_{file,&FileWrite,&FileFlush}{InitializeSlots();}
    explicit AsyncTextLog(TextSink sink):sink_(sink){InitializeSlots();}
    AsyncTextLog(const AsyncTextLog&)=delete;
    AsyncTextLog& operator=(const AsyncTextLog&)=delete;
    ~AsyncTextLog()=delete; // process lifetime; see ownership contract above
    bool Start(DWORD_PTR workerAllowedMask=0){
        if(Read(&started_))return !Read(&stopping_);
        if(!sink_.write || !sink_.flush || Read(&stopping_))return false;
        if(!wake_){wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!wake_)return false;}
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Entry),&module))return false;
        workerMask_=workerAllowedMask;
        worker_=CreateThread(nullptr,0,&Entry,this,0,&workerId_);
        if(!worker_)return false;
        InterlockedExchange(&started_,1);return true;
    }
    bool TryWrite(const char* text){
        if(!text || !Read(&started_) || Read(&stopping_)){InterlockedIncrement(&dropped_);return false;}
        // Stop cannot exit with a producer holding an unpublished reservation.
        // A producer entering after Stop must observe stopping before reserving.
        InterlockedIncrement(&activeProducers_);
        if(Read(&stopping_)){InterlockedDecrement(&activeProducers_);InterlockedIncrement(&dropped_);return false;}
        DWORD pos=enqueue_.load(std::memory_order_relaxed);
        Slot* reserved=nullptr;
        for(unsigned attempt=0;attempt<8;++attempt){
            Slot& slot=queue_[pos&(Capacity-1)];
            const LONG difference=Distance(slot.sequence.load(std::memory_order_acquire),pos);
            if(difference<0)break; // all capacity tickets still occupied
            if(difference>0){pos=enqueue_.load(std::memory_order_relaxed);continue;}
            if(enqueue_.compare_exchange_strong(pos,pos+1,std::memory_order_relaxed,std::memory_order_relaxed)){
                reserved=&slot;break;
            }
        }
        if(!reserved){InterlockedDecrement(&activeProducers_);InterlockedIncrement(&dropped_);return false;}
        std::size_t n=0;while(n<LineBytes-2 && text[n]){reserved->text[n]=text[n];++n;}
        if(n==LineBytes-2 && text[n]){
            constexpr char suffix[]=" [truncated]";constexpr std::size_t len=sizeof(suffix)-1;
            std::memcpy(reserved->text+n-len,suffix,len);InterlockedIncrement(&truncated_);
        }
        reserved->text[n++]='\n';reserved->text[n]=0;reserved->length=n;
        reserved->sequence.store(pos+1,std::memory_order_release);
        InterlockedIncrement(&accepted_);InterlockedDecrement(&activeProducers_);
        SetEvent(wake_);return true;
    }
    // Startup/fatal/test boundary only, never a normal frame/hitch path.
    // Snapshot reservation ticket, not number of completed producers: a later
    // producer can publish first, but Flush still waits for the whole FIFO prefix.
    bool Flush(DWORD timeoutMs){
        if(!Read(&started_))return false;
        const DWORD target=enqueue_.load(std::memory_order_acquire);SetEvent(wake_);
        const ULONGLONG begin=GetTickCount64();
        while(Distance(flushedTicket_.load(std::memory_order_acquire),target)<0){
            if(GetTickCount64()-begin>=timeoutMs)return false;
            Sleep(1);
        }
        return true;
    }
    bool Stop(DWORD timeoutMs){
        if(!worker_)return true;
        InterlockedExchange(&stopping_,1);SetEvent(wake_);
        if(GetCurrentThreadId()==workerId_)return false;
        if(WaitForSingleObject(worker_,timeoutMs)!=WAIT_OBJECT_0)return false;
        CloseHandle(worker_);worker_=nullptr;return true;
    }
    LONG Dropped(){return Read(&dropped_);}
    LONG Truncated(){return Read(&truncated_);}
    LONG Accepted(){return Read(&accepted_);}
    LONG Written(){return Read(&written_);}
    DWORD WorkerThreadId()const{return workerId_;}
};
using ProcessTextLog=AsyncTextLog<>;
}} // highfps::threading
