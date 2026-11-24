#include "nova2/pool.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <intrin.h> // __rdtsc, _mm_pause

#include <cstdlib> // calloc, free

namespace nova2 {

double clock_seconds() {
    static LARGE_INTEGER freq = {};
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(freq.QuadPart);
}

namespace {

struct PoolImpl {
    SRWLOCK lock;
    CONDITION_VARIABLE wake; // workers wait here for a new generation (or quit)
    CONDITION_VARIABLE done; // the caller waits here for busy == 0

    // Written only by the caller, with the lock held exclusively. The workers
    // read them with it held shared, so they don't queue for it one by one
    // at every run's start. A thread that spins (below) reads generation or
    // quit with no lock, only to see when to stop spinning; it then takes
    // the lock and looks again before it acts on what it saw.
    volatile u64 generation;
    volatile bool quit;

    // The current job. Written by the caller before the generation bump;
    // read by workers after they see the bump.
    ThreadPool::TaskFn fn;
    void* ctx;
    u32 count;
    const u32* order;
    bool reversed;

    HANDLE* handles;
    u32 workers;

    // What every thread writes while a run is on, each on a cache line of
    // its own, away from what the threads only read.
    char pad0[64];
    volatile LONG next; // the shared queue head (interlocked)
    char pad1[64];
    volatile LONG busy; // workers that have not finished the current generation (interlocked)
    char pad2[64];
};

// A tick's tasks take a few hundred microseconds between them, and the
// caller's own work between two runs is shorter still. Sleeping in the
// kernel and being woken costs a good part of that, every tick. So a thread
// with nothing to do first spins this long, by the time-stamp counter,
// watching for what it waits for, and only then sleeps
// (optimization-plan.md 4.6). About 7 microseconds on this machine: on a
// thread for every core, pass 12 measured 50 microseconds slower, and
// longer spins slower still. The figure only trades idle processor time
// against wake-ups, and no result depends on it.
const u64 kSpinCycles = 25000;


// Pull indices until the range is exhausted. Shared by workers and the caller.
void drain(PoolImpl* p, ThreadPool::TaskFn fn, void* ctx, u32 count, const u32* order, bool reversed) {
    for (;;) {
        LONG slot = InterlockedIncrement(&p->next) - 1;
        if (slot < 0 || static_cast<u32>(slot) >= count)
            return;
        u32 k = static_cast<u32>(slot);
        fn(ctx, order ? order[k] : reversed ? count - 1 - k : k);
    }
}

DWORD WINAPI worker_main(LPVOID arg) {
    PoolImpl* p = static_cast<PoolImpl*>(arg);
    u64 seen = 0;
    for (;;) {
        for (const u64 t0 = __rdtsc(); p->generation == seen && !p->quit && __rdtsc() - t0 <= kSpinCycles;)
            _mm_pause();
        AcquireSRWLockShared(&p->lock);
        while (p->generation == seen && !p->quit)
            SleepConditionVariableSRW(&p->wake, &p->lock, INFINITE, CONDITION_VARIABLE_LOCKMODE_SHARED);
        if (p->quit) {
            ReleaseSRWLockShared(&p->lock);
            return 0;
        }
        seen = p->generation;
        ThreadPool::TaskFn fn = p->fn;
        void* ctx = p->ctx;
        u32 count = p->count;
        const u32* order = p->order;
        const bool reversed = p->reversed;
        ReleaseSRWLockShared(&p->lock);

        drain(p, fn, ctx, count, order, reversed);

        // The last worker to finish wakes the caller. It takes the lock first
        // (and lets it go at once): the caller looks at busy with the lock
        // held before it sleeps, so by then it is either asleep, and this
        // wakes it, or it has yet to look, and will see 0.
        if (InterlockedDecrement(&p->busy) == 0) {
            AcquireSRWLockExclusive(&p->lock);
            ReleaseSRWLockExclusive(&p->lock);
            WakeConditionVariable(&p->done);
        }
    }
}

} // namespace

u32 ThreadPool::hardware_threads() {
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return n ? static_cast<u32>(n) : 1;
}

void ThreadPool::start(u32 threads) {
    stop();
    if (threads == 0)
        threads = hardware_threads();
    if (threads > 1024)
        threads = 1024;
    threads_ = threads;
    u32 workers = threads - 1;
    if (workers == 0)
        return; // inline mode: no impl, no threads

    PoolImpl* p = static_cast<PoolImpl*>(calloc(1, sizeof(PoolImpl)));
    if (!p)
        NOVA2_FATAL("ThreadPool: out of memory");
    InitializeSRWLock(&p->lock);
    InitializeConditionVariable(&p->wake);
    InitializeConditionVariable(&p->done);
    p->handles = static_cast<HANDLE*>(calloc(workers, sizeof(HANDLE)));
    if (!p->handles)
        NOVA2_FATAL("ThreadPool: out of memory");
    for (u32 i = 0; i < workers; ++i) {
        p->handles[i] = CreateThread(nullptr, 0, worker_main, p, 0, nullptr);
        if (!p->handles[i])
            NOVA2_FATAL("ThreadPool: CreateThread failed");
        ++p->workers;
    }
    impl_ = p;
}

void ThreadPool::stop() {
    PoolImpl* p = static_cast<PoolImpl*>(impl_);
    if (p) {
        AcquireSRWLockExclusive(&p->lock);
        p->quit = true;
        ReleaseSRWLockExclusive(&p->lock);
        WakeAllConditionVariable(&p->wake);
        for (u32 i = 0; i < p->workers; ++i) {
            WaitForSingleObject(p->handles[i], INFINITE);
            CloseHandle(p->handles[i]);
        }
        free(p->handles);
        free(p);
        impl_ = nullptr;
    }
    threads_ = 0;
}

void ThreadPool::run(u32 count, TaskFn fn, void* ctx) {
    NOVA2_ASSERT(threads_ > 0); // start() first
    if (count == 0)
        return;
    NOVA2_ASSERT(count <= 0x7FFFFFFFu);
    // The test order is a permutation of [0, order_count_): runs of another
    // length go in order, or in reverse.
    const u32* order = count == order_count_ ? order_ : nullptr;
    const bool reversed = !order && reversed_;
    PoolImpl* p = static_cast<PoolImpl*>(impl_);
    if (!p) {
        for (u32 k = 0; k < count; ++k)
            fn(ctx, order ? order[k] : reversed ? count - 1 - k : k);
        return;
    }

    AcquireSRWLockExclusive(&p->lock);
    p->fn = fn;
    p->ctx = ctx;
    p->count = count;
    p->order = order;
    p->reversed = reversed;
    p->next = 0;
    p->busy = static_cast<LONG>(p->workers);
    ++p->generation;
    ReleaseSRWLockExclusive(&p->lock);
    WakeAllConditionVariable(&p->wake);

    drain(p, fn, ctx, count, order, reversed); // the caller is one of the threads

    // Barrier: wait until every worker has finished this generation. A
    // worker's last act is an interlocked write to busy, which also makes
    // all its task's writes visible to the caller.
    for (const u64 t0 = __rdtsc(); p->busy != 0 && __rdtsc() - t0 <= kSpinCycles;)
        _mm_pause();
    AcquireSRWLockExclusive(&p->lock);
    while (p->busy != 0)
        SleepConditionVariableSRW(&p->done, &p->lock, INFINITE, 0);
    ReleaseSRWLockExclusive(&p->lock);
}

} // namespace nova2
