// Arrivals at port 0s and the network's messages, for gate 3's reports
// (arrivals.h).
#include "arrivals.h"

#include <cstdio>

namespace nova2 {

namespace {
typedef unsigned long long ull;
} // namespace

u64 landed(const Place& p) { return p.region.arrival_joined + p.region.arrival_dropped; }

u64 delivered_to(const Place& p) {
    return landed(p) + p.os.recv_direct + p.os.inbox_joined + p.os.inbox_dropped;
}

void Arrivals::add(const Place& p) {
    const Region& g = p.region;
    landed += ::nova2::landed(p);
    dropped += g.arrival_dropped;
    cleared += g.arrival_cleared;
    waiting += g.arrivals_waiting();
    runs += g.arrival_runs;
    made += g.arrival_made;
    travellers += g.travellers;
    neither += g.arrival_neither;
    preempted += g.arrival_preempted;
    overwritten += g.arrival_overwritten;
    alloc_elsewhere += g.arrival_alloc_elsewhere;
    alloc_nowhere += g.arrival_alloc_nowhere;
    alloc_refused += g.arrival_alloc_refused;
    free_failed += g.arrival_free_failed;
    protect_failed += g.arrival_protect_failed;
    spawn_not_held += g.arrival_spawn_not_held;
    spawn_data += g.arrival_spawn_data;
    spawn_no_record += g.arrival_spawn_no_record;
    no_failure += g.arrival_no_failure;
    faulted += g.arrival_faulted;
    capped += g.arrival_capped;
    cut += g.arrival_cut;
    landed_runs += g.landed_runs;
    landed_made += g.landed_made;
}

void Arrivals::add(const Arrivals& b) {
    landed += b.landed;
    dropped += b.dropped;
    cleared += b.cleared;
    waiting += b.waiting;
    runs += b.runs;
    made += b.made;
    travellers += b.travellers;
    neither += b.neither;
    preempted += b.preempted;
    overwritten += b.overwritten;
    alloc_elsewhere += b.alloc_elsewhere;
    alloc_nowhere += b.alloc_nowhere;
    alloc_refused += b.alloc_refused;
    free_failed += b.free_failed;
    protect_failed += b.protect_failed;
    spawn_not_held += b.spawn_not_held;
    spawn_data += b.spawn_data;
    spawn_no_record += b.spawn_no_record;
    no_failure += b.no_failure;
    faulted += b.faulted;
    capped += b.capped;
    cut += b.cut;
    landed_runs += b.landed_runs;
    landed_made += b.landed_made;
}

Arrivals arrivals(World& w) {
    Arrivals a;
    for (u32 i = 0; i < w.place_count(); ++i)
        a.add(w.place(i));
    return a;
}

void print_rest(const char* lead, const Arrivals& a) {
    const u64 alloc = a.alloc_elsewhere + a.alloc_nowhere + a.alloc_refused;
    const u64 spawn = a.spawn_not_held + a.spawn_data + a.spawn_no_record;
    printf("  %s %llu kept their core and did neither. Their first call to fail: ALLOC %llu (%llu with room on "
           "another memory device but not the one asked for, %llu with room on none, %llu refused as asked), FREE "
           "%llu, PROTECT %llu, SPAWN %llu (%llu not at an allocation of its own, %llu on a data page, %llu with no "
           "record free); none failed %llu. They ended by a fault %llu, the cap %llu, a cut-out %llu\n",
           lead, static_cast<ull>(a.neither - a.preempted), static_cast<ull>(alloc),
           static_cast<ull>(a.alloc_elsewhere), static_cast<ull>(a.alloc_nowhere), static_cast<ull>(a.alloc_refused),
           static_cast<ull>(a.free_failed), static_cast<ull>(a.protect_failed), static_cast<ull>(spawn),
           static_cast<ull>(a.spawn_not_held), static_cast<ull>(a.spawn_data), static_cast<ull>(a.spawn_no_record),
           static_cast<ull>(a.no_failure), static_cast<ull>(a.faulted), static_cast<ull>(a.capped),
           static_cast<ull>(a.cut));
}

void print_arrivals(const Arrivals& a) {
    const u64 ended = a.made + a.travellers + a.neither;
    printf("  arrivals at port 0s: %llu landed, %llu dropped there for want of room, %llu cleared by a cold start "
           "before they ran, %llu still waiting; %llu ran: %llu made a program, %llu only SENT, %llu "
           "did neither (%llu of them because a ready thread took their core, 10.2; %llu with their bytes written "
           "over before they ended), %llu still running\n",
           static_cast<ull>(a.landed), static_cast<ull>(a.dropped), static_cast<ull>(a.cleared),
           static_cast<ull>(a.waiting), static_cast<ull>(a.runs), static_cast<ull>(a.made),
           static_cast<ull>(a.travellers), static_cast<ull>(a.neither), static_cast<ull>(a.preempted),
           static_cast<ull>(a.overwritten), static_cast<ull>(a.runs > ended ? a.runs - ended : 0));
    print_rest("of the arrivals that ran,", a);
    printf("  static runs that started in an arrival's landed bytes, all as they landed: %llu, and %llu of them made "
           "a program, its sender's lineage (13.2)\n",
           static_cast<ull>(a.landed_runs), static_cast<ull>(a.landed_made));
}

void add_counts(NetCounts& a, const NetCounts& b) {
    a.sent += b.sent;
    a.sent_bytes += b.sent_bytes;
    a.delivered += b.delivered;
    a.delivered_bytes += b.delivered_bytes;
    a.hops += b.hops;
    a.full += b.full;
    a.stranded += b.stranded;
    a.lost += b.lost;
    a.cut += b.cut;
    a.flips += b.flips;
    a.corrupted += b.corrupted;
    a.looped += b.looped;
    a.pings += b.pings;
    a.echoes += b.echoes;
}

void print_network(const NetCounts& n, u32 in_flight) {
    printf("  messages: %llu sent, %llu delivered; dropped %llu for no room, %llu stranded, %llu lost at a dark "
           "place, %llu cut at a dark device; %u on their way\n",
           static_cast<ull>(n.sent), static_cast<ull>(n.delivered), static_cast<ull>(n.full),
           static_cast<ull>(n.stranded), static_cast<ull>(n.lost), static_cast<ull>(n.cut), in_flight);
}

} // namespace nova2
