#include <core/runtime/SwFiberPool.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void counts(SwFiberPool& pool,int warm,int spill,int idle,int running,int yielded) {
    const auto state=pool.stats();
    require(state.warmCount==warm && state.spilloverCount==spill && state.idleCount==idle &&
            state.runningCount==running && state.yieldedCount==yielded,"fiber accounting differs from expected lifecycle state");
}
void rejected(SwFiberPool& pool,SwFiberLane lane) {
    bool backpressure=false;
    require(!pool.enqueueTask([] {},lane,&backpressure) && backpressure,"occupied/queued slots bypassed admission capacity");
}
void exercise(LPVOID main,int warm) {
    SwFiberPool pool;SwFiberPoolConfig config;
    config.warmFiberCount=warm;config.maxFiberCount=warm;config.emergencySpilloverCount=1;
    pool.setConfig(config);pool.bindMainFiber(main);pool.ensureWarmInitialized();
    counts(pool,warm,0,warm,0,0);
#if defined(__ANDROID__)
    int completed=0;
    for(int i=0;i<warm;++i)require(pool.enqueueTask([&] {++completed;},SwFiberLane::Normal),"fake-fiber queue admission failed");
    rejected(pool,SwFiberLane::Normal);
    for(int i=0;i<warm;++i)require(pool.runNextWorkItem(),"fake-fiber task was not executed");
    counts(pool,warm,0,warm,0,0);require(completed==warm,"fake fibers lost a callback");
#else
    int entered=0,resumed=0,released=0;
    bool failedInside=false;
    for(int i=0;i<warm;++i)require(pool.enqueueTask([&,i] {
        const auto state=pool.stats();
        if(state.runningCount!=1 || state.idleCount!=warm-i-1 || state.yieldedCount!=i)failedInside=true;
        ++entered;pool.yieldCurrent(100+i);++resumed;
        if(pool.stats().runningCount!=1)failedInside=true;
        if(i==0) {pool.releaseCurrent();++released;if(pool.stats().runningCount!=1)failedInside=true;}
    },SwFiberLane::Normal),"warm queue admission failed");
    rejected(pool,SwFiberLane::Normal);
    for(int i=0;i<warm;++i)require(pool.runNextWorkItem(),"warm task was not dispatched");
    require(entered==warm && !failedInside,"running counters were incorrect inside warm callbacks");
    counts(pool,warm,0,0,0,warm);rejected(pool,SwFiberLane::Normal);rejected(pool,SwFiberLane::Background);
    require(pool.enqueueTask([&] {
        const auto state=pool.stats();
        if(state.runningCount!=1 || state.spilloverCount!=1 || state.idleCount!=0)failedInside=true;
        pool.yieldCurrent(1000);
    },SwFiberLane::Input),"emergency input admission failed");
    rejected(pool,SwFiberLane::Control);require(pool.runNextWorkItem(),"spillover task was not dispatched");
    counts(pool,warm,1,0,0,warm+1);rejected(pool,SwFiberLane::Input);

    require(pool.unYield(100,SwFiberLane::Control),"warm wake failed");
    require(!pool.unYield(100,SwFiberLane::Control),"duplicate wake was accepted");
    counts(pool,warm,1,0,0,warm);
    require(pool.runNextWorkItem(),"warm continuation failed");
    counts(pool,warm,1,0,0,warm); // releaseCurrent queues a continuation without freeing capacity.
    rejected(pool,SwFiberLane::Normal);
    require(pool.runNextWorkItem(),"released continuation failed");
    counts(pool,warm,1,1,0,warm);
    require(released==1 && resumed==1,"release/resume duplicated the callback");
    // Spillover still consumes capacity, even while one warm handle is idle.
    rejected(pool,SwFiberLane::Normal);
    require(pool.unYield(1000,SwFiberLane::Input) && pool.runNextWorkItem(),"spillover continuation failed");
    counts(pool,warm,1,2,0,warm-1);
    require(pool.enqueueTask([] {throw std::runtime_error("intentional accounting fixture exception");},SwFiberLane::Normal),
            "freed capacity did not accept another normal task");
    require(pool.runNextWorkItem(),"exception callback was not dispatched");
    counts(pool,warm,1,2,0,warm-1);
    for(int i=1;i<warm;++i)require(pool.unYield(100+i,SwFiberLane::Normal),"remaining warm wake failed");
    for(int i=1;i<warm;++i)require(pool.runNextWorkItem(),"remaining warm resume failed");
    counts(pool,warm,1,warm+1,0,0);
    require(resumed==warm && !failedInside,"fiber running counter changed during continuation");
    require(pool.stats().tasksExecuted==warm+2,"completed task count changed across yield/release/exception");
    const auto reused=pool.stats().tasksReused;
    for(int i=0;i<100;++i) {
        require(pool.enqueueTask([] {},SwFiberLane::Normal) && pool.runNextWorkItem(),"idle handle reuse failed");
        counts(pool,warm,1,warm+1,0,0);
    }
    require(pool.stats().tasksReused==reused+100,"warm handles were recreated instead of reused");
#endif
    pool.shutdown();counts(pool,0,0,0,0,0);
    pool.ensureWarmInitialized();counts(pool,warm,0,warm,0,0);
    pool.shutdown();counts(pool,0,0,0,0,0);
}
#if !defined(__ANDROID__)
void watchdogReturn(LPVOID main,bool spillover) {
    SwFiberPool pool;SwFiberPoolConfig config;config.warmFiberCount=1;config.maxFiberCount=1;config.emergencySpilloverCount=1;
    pool.setConfig(config);pool.bindMainFiber(main);std::atomic<bool> watchdog(false);
    pool.bindRuntimeState(nullptr,nullptr,&watchdog);
    // A late flag must not put a completed/idle handle back in ready while it
    // is still available in the idle queue.
    require(pool.enqueueTask([&] {watchdog.store(true);},SwFiberLane::Normal) && pool.runNextWorkItem(),
            "late watchdog fixture dispatch failed");
    counts(pool,1,0,1,0,0);require(!pool.hasWork(),"late watchdog resurrected a completed callback");
    if(spillover) {
        require(pool.enqueueTask([&] {pool.yieldCurrent(2000);},SwFiberLane::Normal) && pool.runNextWorkItem(),"watchdog fixture warm occupancy failed");
    }
    bool resumed=false;
    require(pool.enqueueTask([&] {
        watchdog.store(true);SwitchToFiber(main);resumed=true;
    },spillover?SwFiberLane::Input:SwFiberLane::Normal) && pool.runNextWorkItem(),"watchdog fixture dispatch failed");
#if defined(_WIN32)
    // Windows retires the preempted handle and replaces only warm capacity.
    counts(pool,1,0,spillover?0:1,0,spillover?1:0);
    require(!resumed,"retired Windows continuation unexpectedly resumed");
    require(pool.enqueueTask([] {},SwFiberLane::Control) && pool.runNextWorkItem(),"replacement handle did not accept work");
    counts(pool,1,spillover?1:0,1,0,spillover?1:0);
#else
    // POSIX retains and requeues the interrupted handle.
    counts(pool,1,spillover?1:0,0,0,spillover?1:0);
    require(pool.runNextWorkItem() && resumed,"POSIX watchdog did not resume its handle");
    counts(pool,1,spillover?1:0,1,0,spillover?1:0);
#endif
    if(spillover)require(pool.unYield(2000,SwFiberLane::Normal) && pool.runNextWorkItem(),"watchdog fixture warm resume failed");
    pool.shutdown();counts(pool,0,0,0,0,0);
}
#endif
}
int main() {try {
    const auto main=ConvertThreadToFiber(nullptr);require(main!=nullptr,"main fiber conversion failed");
    exercise(main,2);exercise(main,17);
#if !defined(__ANDROID__)
    watchdogReturn(main,false);watchdogReturn(main,true);
#endif
    std::cout<<"PASS: configurable capacity, yield/release, bounded spillover, exception/reuse, shutdown and watchdog accounting\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
