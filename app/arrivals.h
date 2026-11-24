// Arrivals at port 0s from other places, and the network's messages, as gate
// 3's reports give them: the test worlds' (worlds.cpp, build-plan-gate3.md
// step 7) and the gate 3 runs' (origin.cpp, step 8). Counts for reports only:
// the region's and the network's, none of them saved or hashed.
#pragma once

#include "nova2/world.h"

namespace nova2 {

// Arrivals that reached place p's port 0: they joined its queue, or were
// dropped there for want of room in it.
u64 landed(const Place& p);

// Messages delivered to place p's ports: its arrivals at port 0, and those
// to its other ports, taken by a waiting RECV, joined to an inbox or dropped
// at a full one.
u64 delivered_to(const Place& p);

struct Arrivals {
    u64 landed = 0, dropped = 0, cleared = 0, waiting = 0;
    u64 runs = 0, made = 0, travellers = 0, neither = 0, preempted = 0, overwritten = 0;
    // Of those that did neither and kept their core: the first call that
    // failed, and how they ended (Region's counts).
    u64 alloc_elsewhere = 0, alloc_nowhere = 0, alloc_refused = 0, free_failed = 0, protect_failed = 0;
    u64 spawn_not_held = 0, spawn_data = 0, spawn_no_record = 0, no_failure = 0, faulted = 0, capped = 0, cut = 0;
    // Static runs that started in an arrival's landed bytes, all as they
    // landed, and those that made a program (13.2: the sender's lineage).
    u64 landed_runs = 0, landed_made = 0;

    void add(const Place& p);
    void add(const Arrivals& b);
};

// The world's arrivals, summed over its places.
Arrivals arrivals(World& w);

// Three lines: the arrivals, what became of those that ran, and the static
// runs in their landed bytes.
void print_arrivals(const Arrivals& a);
// The arrivals that did neither and kept their core: which call failed
// first, and how they ended. `lead` begins the line.
void print_rest(const char* lead, const Arrivals& a);

void add_counts(NetCounts& a, const NetCounts& b);
// One line: the messages sent, delivered, dropped and on their way.
void print_network(const NetCounts& n, u32 in_flight);

} // namespace nova2
