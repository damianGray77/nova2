// 9.4's memory-taker (build-plan-gate2.md step 9): taker_world.h.
#include "taker_world.h"

#include <cstring>

namespace nova2 {

// A thread starts with A = SPAWN's starting value and B, C, D and IP at its
// start (A.3). The first thread starts with A = 0; the threads it SPAWNs
// start with A = the start address, so they go straight to sleep.
//    0  JNZ #30     a thread it spawned: sleep
//    2  MOV B       A = the start: SPAWN a thread of its own (9.4), B = the start
//    3  SYS #SPAWN
//    5  JNZ #-3     made one: again
//    7  MOV #0      its device is full: B = 0 (data), C = 0 (any device)
//    9  STO B
//   10  STO C
//   11  MOV #1
//   13  SHL #12     A = 4,096, the largest allocation
//   15  PUSH A      the size, on the stack
//   16  POP A
//   17  PUSH A
//   18  JEZ #12     size 0: done
//   20  SYS #ALLOC
//   22  JNZ #-6     got one: the same size again
//   24  POP A
//   25  SUB #1      none left that size: one byte smaller
//   27  PUSH A
//   28  JMP #-12
//   30  MOV #0      sleep until an interrupt, for ever
//   32  SYS #SLEEP
//   34  JMP #-4
const u8 kTaker[kTakerBytes] = {encode(kJnz, kImm), 30,
                                encode(kMov, kB),
                                encode(kSys, kImm), kSpawn,
                                encode(kJnz, kImm), static_cast<u8>(-3),
                                encode(kMov, kImm), 0,
                                encode(kSto, kB),
                                encode(kSto, kC),
                                encode(kMov, kImm), 1,
                                encode(kShl, kImm), 12,
                                encode(kPush, kA),
                                encode(kPop, kA),
                                encode(kPush, kA),
                                encode(kJez, kImm), 12,
                                encode(kSys, kImm), kAlloc,
                                encode(kJnz, kImm), static_cast<u8>(-6),
                                encode(kPop, kA),
                                encode(kSub, kImm), 1,
                                encode(kPush, kA),
                                encode(kJmp, kImm), static_cast<u8>(-12),
                                encode(kMov, kImm), 0,
                                encode(kSys, kImm), kSleep,
                                encode(kJmp, kImm), static_cast<u8>(-4)};

u32 held_by(const Memory& m, u32 holder) {
    u32 n = 0;
    for (u32 id = m.first_of(holder); id; id = m.record(id).next)
        n += m.record(id).extent;
    return n;
}

namespace {

// Puts thread t to sleep until an interrupt, with the registers its code
// leaves when it gets there (offsets 30 and 32): it goes through core 0, as
// the scheduler would put it on and the SLEEP take it off.
void sleep_by_hand(Place& pl, u32 t, u32 at, u32 b, u32 c, u32 d, u32 sp) {
    pl.os.put_on(t, 0);
    Regs& r = pl.cores[0].x.r;
    r.a = 0;
    r.b = b;
    r.c = c;
    r.d = d;
    r.sp = sp;
    r.ip = at + 34; // past the SLEEP
    pl.os.take_off(0, ThreadState::waiting_interrupt);
}

void note_filled(Place& pl, TakerPlaced* out) {
    out->threads = pl.os.program(out->holder).threads;
    out->held = held_by(pl.mem, out->holder);
    out->free = pl.mem.free_bytes();
}

} // namespace

bool place_taker_code(Place& pl, TakerPlaced* out) {
    if (pl.cores[0].thread || pl.cores[0].idle_run)
        return false; // core 0 is place_taker's stage, and must be free
    const u32 at = pl.mem.alloc(1, kTakerBytes, 1, 0); // executable, held for now by the first run
    if (!at)
        return false;
    memcpy(pl.mem.bytes() + at, kTaker, kTakerBytes);
    if (!pl.os.place_by_hand(1, at, at, 0))
        return false;
    out->code = at;
    out->holder = pl.mem.record(pl.mem.alloc_at(at)).holder;
    out->uid = pl.os.program(out->holder).uid;
    out->device = pl.mem.device_of(at);
    note_filled(pl, out);
    return true;
}

bool place_taker(Place& pl, TakerPlaced* out) {
    if (!place_taker_code(pl, out))
        return false;
    const u32 at = out->code, holder = out->holder;
    const u32 first = pl.os.program(holder).first_thread - 1;
    const u32 stack = pl.os.settings().stack_size;
    const u32 top = pl.os.thread(first).stack + stack;
    // Offsets 2 to 5: SPAWN a thread of its own, A = B = the start, until its
    // device has no room for another stack.
    pl.os.put_on(first, 0);
    while (pl.os.spawn(0, at, at, at + 3)) {
    }
    pl.os.take_off(0, ThreadState::ready);
    // Offsets 7 to 28: ALLOC whatever is left, data on any device, from the
    // largest allocation down a byte at a time. The last size, 0, stays
    // pushed on the first thread's stack.
    for (u32 size = pl.mem.max_alloc(); size; --size)
        while (pl.mem.alloc(holder, size, 0, 0)) {
        }
    memset(pl.mem.bytes() + top - 4, 0, 4);
    pl.mem.note_write(top - 4, 4, holder); // as the interpreter's PUSH notes it
    // Every thread asleep: the ones it spawned went from offset 0 to 30 with
    // B, C and D at the start; the first got there with B and C 0, D the last
    // failed ALLOC's 0, and the size pushed.
    for (u32 k = pl.os.program(holder).first_thread; k; k = pl.os.thread(k - 1).next) {
        const u32 t = k - 1;
        if (t == first)
            sleep_by_hand(pl, t, at, 0, 0, 0, top - 4);
        else
            sleep_by_hand(pl, t, at, at, at, at, pl.os.thread(t).stack + stack);
    }
    note_filled(pl, out);
    return true;
}

} // namespace nova2
