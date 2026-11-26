// The settings registry's table (design.md 2): one entry for every setting the
// engine reads. nova2/keys.h explains the macros, how to add a setting and how
// to read it; keys.cpp includes this file once per macro set. Keep entries in
// their group's section, in the order the page shows them. Settings arrive
// with the build step that reads them (build-plan-gate1.md 5).
//
//   NOVA2_FLAG  (name, default,                 flags, group, label, needs, help)
//   NOVA2_WHOLE (name, default, min, max,       flags, group, label, needs, help)
//   NOVA2_NUMBER(name, default, min, max,       flags, group, label, needs, help)
//   NOVA2_CHOICE(name, default, "opt1 opt2 ..", flags, group, label, needs, help)
//   NOVA2_TEXT  (name, default,                 flags, group, label, needs, help)
//   NOVA2_MEMBER(set, member, label, group)     a family's member (names with <set>)
//   NOVA2_DEFAULT(name, default)                one family member's own default
//
// A computed default (kKeyComputed) is written as it comes out with every
// other setting at its own default; the call site supplies the real one.

// ---------------------------------------------------------------------------
// Run and output
// ---------------------------------------------------------------------------
NOVA2_WHOLE("run.ticks", 1000, 0, kKeyNoMax, 0, "Run and output", "Ticks", "",
            "Stop after this many ticks (0 = no tick limit).")
NOVA2_WHOLE("run.threads", 0, 0, 1024, 0, "Run and output", "Threads", "",
            "Host threads that run places in parallel (0 = one per hardware thread). Never changes the world.")
NOVA2_CHOICE("bench.part", "all", "all workloads gate2 gate3 gate4 gate5", 0, "Run and output",
             "Benchmark's part", "",
             "Which part of nova2 bench to run: all of it, or only the three workloads, gate 2's parts of a tick, "
             "gate 3's network and routes, gate 4's solve and heat, or gate 5's wear, rowhammer, respawns and "
             "rerouting. With run.threads set, gate 4's and gate 5's parts run on that many host threads at "
             "most.")

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
NOVA2_WHOLE("world.seed", 1, 0, kKeyNoMax, 0, "World", "Seed", "",
            "The world's seed: everything random in the run follows from it (principle 20).")
NOVA2_WHOLE("world.places", 48, 1, 1000000, 0, "World", "Places", "",
            "How many places the world has (6.1). The runs of gates 1 to 3 use 48 (14.1).")
NOVA2_CHOICE("world.map", "landscape", "landscape none grid ring", 0, "World", "Map", "",
             "Where the places sit and how data links join them (6.1): a landscape of land and water, with places "
             "by density, data links within each landmass and bridges across the water; none, places with no "
             "links, as at gates 1 and 2; or a grid or a ring, for test worlds.")

// ---------------------------------------------------------------------------
// Map (6.1, 6.2, 7.3; build-plan-gate3.md step 2). Lengths are in the map's
// height.
// ---------------------------------------------------------------------------
NOVA2_NUMBER("map.land.aspect", 1.6, 0.25, 8, 0, "Map", "Aspect", "world.map=landscape",
             "The map's width over its height.")
NOVA2_NUMBER("map.land.share", 0.3, 0.01, 1, 0, "Map", "Land", "world.map=landscape",
             "The share of the map that's land; the rest is water.")
NOVA2_NUMBER("map.land.scale", 3, 0.1, 64, 0, "Map", "Land scale", "world.map=landscape",
             "The terrain noise's frequency across the map's height: higher gives more, smaller landmasses.")
NOVA2_NUMBER("map.land.warp", 0.3, 0, 4, 0, "Map", "Land warp", "world.map=landscape",
             "How far two more noises push the terrain about, so coastlines wind.")
NOVA2_NUMBER("map.land.border", 0.3, 0, 4, 0, "Map", "Land border", "world.map=landscape",
             "How far the terrain sinks towards the map's edges, so the map is bounded by sea.")
NOVA2_WHOLE("map.land.resolution", 256, 16, 4096, 0, "Map", "Land resolution", "world.map=landscape",
            "Raster cells across the map's height, for land and water. Places may sit no closer than a quarter of "
            "a cell.")
NOVA2_NUMBER("map.density", 3, 1, 100, 0, "Map", "Town density", "world.map=landscape",
             "How much denser places are where the density field is highest than where it's lowest: towns and "
             "countryside (6.1). Places are scattered as tight as fits.")
NOVA2_WHOLE("map.bridges", 2, 0, 64, 0, "Map", "Bridges a pair", "world.map=landscape",
            "Bridges (data links across water) between each pair of neighbouring landmasses: the shortest "
            "crossings, with no place at the end of two.")
NOVA2_NUMBER("map.bridge.longest", 0.25, 0, 8, 0, "Map", "Longest bridge", "world.map=landscape",
             "The longest of those bridges. A landmass left apart by them is still joined to the rest by its "
             "shortest crossing, whatever its length.")
NOVA2_NUMBER("map.unbridged", 0, 0, 1, 0, "Map", "Unbridged share", "world.map=landscape",
             "Each landmass's chance of no bridges at all.")
NOVA2_WHOLE("map.bandwidth.min", 16, 1, 1000000, 0, "Map", "Least bandwidth", "",
            "The narrowest link on land, in bytes a tick (11.3). Links take the bandwidth field at their middle, "
            "spread evenly on a log scale from the least to the most.")
NOVA2_WHOLE("map.bandwidth.max", 256, 1, 1000000, 0, "Map", "Most bandwidth", "",
            "The widest link on land, in bytes a tick.")
NOVA2_WHOLE("map.bridge.bandwidth", 256, 1, 1000000, 0, "Map", "Bridge bandwidth", "",
            "A bridge's bandwidth, in bytes a tick (11.3): bridges carry most traffic between landmasses.")
NOVA2_WHOLE("map.delay.hop", 1, 0, 1000000, 0, "Map", "Delay a hop", "",
            "The ticks every hop takes (11.3). With the per-length part, rounded up, and never under a tick.")
NOVA2_NUMBER("map.delay.length", 1, 0, 1000000, 0, "Map", "Delay by length", "",
             "The ticks a hop takes for each average link length it covers (11.3). 0 gives a hop-only network.")
NOVA2_FLAG("map.fields", true, 0, "Map", "Fields", "",
           "The hardware and OS values from the map's fields (7.3): drawn for all the places as before, then dealt "
           "out by each value's field, so neighbours are alike, with barriers between. Off gives gates 1 and 2's "
           "independent draws. A world with no map has no fields.")
NOVA2_NUMBER("map.field.scale", 3, 0.1, 64, 0, "Map", "Field scale", "",
             "The fields' noise frequency across the map's height (6.2).")
NOVA2_NUMBER("map.field.scatter", 0.05, 0, 1, 0, "Map", "Field scatter", "",
             "How far each place's field value is nudged before values are dealt, either way, as a share of the "
             "field's range at the places (7.3).")
NOVA2_NUMBER("map.<field>.edge", 1, 0, 100, 0, "", "<field> edge strength", "",
             "<field>: how strong the field's drop-offs are beside its gradients (6.2). 0 gives gradients only.")
NOVA2_NUMBER("map.<field>.sharpness", 8, 0, 1000, 0, "", "<field> sharpness", "",
             "<field>: how steep the field's drop-offs are (6.2).")
NOVA2_MEMBER("field", "cores", "Core count", "Map")
NOVA2_MEMBER("field", "ecores", "E cores", "Map")
NOVA2_MEMBER("field", "devices", "Memory devices", "Map")
NOVA2_MEMBER("field", "latency", "Memory latency", "Map")
NOVA2_MEMBER("field", "page", "Page size", "Map")
NOVA2_MEMBER("field", "strict", "Strict or lax", "Map")
NOVA2_MEMBER("field", "bandwidth", "Bandwidth", "Map")
NOVA2_MEMBER("field", "density", "Density", "Map")
NOVA2_MEMBER("field", "resistance", "Link resistance", "Map")
NOVA2_MEMBER("field", "cooling", "Cooling", "Map")
NOVA2_MEMBER("field", "coolers", "Cooling devices", "Map")
NOVA2_MEMBER("field", "threads", "Thread limit", "Map")
// The power map (6.1, 6.2; build-plan-gate4.md step 2). Lengths here are in
// average link lengths.
NOVA2_NUMBER("map.resistance.min", 0.5, 0.001, 1000, 0, "Map", "Least resistance", "",
             "A proximity link's resistance a length where the resistance field is lowest, over the usual (6.1).")
NOVA2_NUMBER("map.resistance.max", 2, 0.001, 1000, 0, "Map", "Most resistance", "",
             "A proximity link's resistance a length where the resistance field is highest, over the usual. Links "
             "are ranked by the field at their middles and spread evenly on a log scale between the two.")
NOVA2_NUMBER("map.water.reach", 1, 0.01, 1000, 0, "Map", "Water's reach", "world.map=landscape",
             "How far power reaches across water, in average link lengths: a proximity link's resistance is its "
             "resistance on land x e^(its width of water / the reach) (6.1).")
NOVA2_NUMBER("map.border.strength", 10, 0, 100000, 0, "Map", "Grid border strength", "",
             "What crossing a grid border adds to a proximity link's resistance, in average links (6.2). A link "
             "crosses one when its ends lie on opposite sides of a step of the resistance field. 0 is no borders.")
NOVA2_NUMBER("map.cooling.min", 0.5, 0.001, 1000, 0, "Map", "Least cooling", "",
             "A place's cooling where the cooling field is lowest, over the usual (5).")
NOVA2_NUMBER("map.cooling.max", 2, 0.001, 1000, 0, "Map", "Most cooling", "",
             "A place's cooling where the cooling field is highest, over the usual. Places are ranked by the field "
             "and spread evenly on a log scale between the two.")

// ---------------------------------------------------------------------------
// Hardware (7.2, 7.3; at gate 1 drawn from ranges, with no map fields yet)
// ---------------------------------------------------------------------------
NOVA2_WHOLE("hw.cores.min", 2, 1, 64, 0, "Hardware", "Fewest cores", "", "The fewest cores a place has.")
NOVA2_WHOLE("hw.cores.max", 8, 1, 64, 0, "Hardware", "Most cores", "", "The most cores a place has.")
NOVA2_NUMBER("hw.e_share", 0.5, 0, 1, 0, "Hardware", "E core share", "",
             "The chance that each core is an E core rather than a P core.")
NOVA2_WHOLE("hw.<kind>.slope", 166667, 1, 1000000, 0, "", "<kind> speed slope", "",
            "A <kind>'s speed for each volt above its threshold, in cycles per tick (7.5). P's 166,667 and E's "
            "72,000 give gate 1's stock speeds at 1,000 mV, 25,000 and 18,000 cycles a tick.")
NOVA2_WHOLE("hw.<kind>.threshold", 850, 1, 10000, 0, "", "<kind> threshold", "",
            "A <kind>'s nominal threshold, its lowest working voltage, in millivolts (4.4).")
NOVA2_WHOLE("hw.<kind>.maximum", 1300, 1, 10000, 0, "", "<kind> maximum", "",
            "A <kind>'s maximum voltage, in millivolts: above it its speed rises no more (4.4).")
NOVA2_WHOLE("hw.<kind>.draw", 400000, 0, 10000000, 0, "", "<kind> working draw", "",
            "k in a <kind>'s working draw, k x V^2 x speed (appendix C), in nanowatts per volt squared per cycle a "
            "tick. P's 400,000 is 10 W at 1,000 mV and 25,000 cycles a tick; E's 138,889 is 2.5 W at 18,000.")
NOVA2_WHOLE("hw.<kind>.idle_draw", 1000, 0, 1000000, 0, "", "<kind> idle draw", "",
            "A <kind>'s draw while it's halted, in milliwatts (4.5).")
NOVA2_MEMBER("kind", "p", "P core", "Hardware")
NOVA2_MEMBER("kind", "e", "E core", "Hardware")
NOVA2_DEFAULT("hw.e.slope", 72000)
NOVA2_DEFAULT("hw.e.threshold", 750)
NOVA2_DEFAULT("hw.e.maximum", 1200)
NOVA2_DEFAULT("hw.e.draw", 138889)
NOVA2_DEFAULT("hw.e.idle_draw", 300)
NOVA2_WHOLE("hw.network.draw", 1000, 0, 1000000, 0, "Hardware", "Network device draw", "",
            "The network device's draw, in milliwatts: constant at gate 2, with nothing to send yet (7.2).")
NOVA2_WHOLE("hw.network.threshold", 600, 1, 10000, 0, "Hardware", "Network device threshold", "",
            "The network device's lowest working voltage, in millivolts (7.1): below it, it's off and draws nothing. "
            "A placeholder, as the disk's.")
NOVA2_WHOLE("hw.network.maximum", 1300, 1, 10000, 0, "Hardware", "Network device maximum", "",
            "The network device's maximum voltage, in millivolts.")
NOVA2_WHOLE("hw.network.byte_draw", 1, 0, 1000000, 0, "Hardware", "Network draw a byte", "",
            "The network device's extra draw for each byte it sends or receives in a tick, in microwatts (11.4). "
            "Forwarding is free.")
NOVA2_WHOLE("hw.network.latency", 1, 1, 1000000, 0, "Hardware", "Network device latency", "",
            "The network device's latency, in ticks (11.4): a SEND's sleep starts with it, and a message to its own "
            "place arrives after it.")
NOVA2_WHOLE("hw.temperature", 250, -2730, 100000, 0, "Hardware", "Temperature", "",
            "The place's temperature at gate 1, in tenths of a degree C (SENSE reads it).")

// ---------------------------------------------------------------------------
// Memory and disk (6.3, 9.3)
// ---------------------------------------------------------------------------
NOVA2_WHOLE("memory.size", 163840, 4096, 1073741824, 0, "Memory", "Memory per place", "",
            "A place's memory in bytes, shared out among its memory devices in whole kilobytes.")
NOVA2_WHOLE("memory.devices.min", 2, 1, 16, 0, "Memory", "Fewest memory devices", "",
            "The fewest memory devices a place has.")
NOVA2_WHOLE("memory.devices.max", 4, 1, 16, 0, "Memory", "Most memory devices", "",
            "The most memory devices a place has.")
NOVA2_WHOLE("memory.latency.min", 4, 0, 1000, 0, "Memory", "Fastest latency", "",
            "The lowest latency a memory device can have, in cycles (8.4).")
NOVA2_WHOLE("memory.latency.max", 12, 0, 1000, 0, "Memory", "Slowest latency", "",
            "The highest latency a memory device can have, in cycles.")
NOVA2_WHOLE("memory.threshold.fast", 700, 1, 10000, 0, "Memory", "Fast memory's threshold", "",
            "The threshold of the fastest memory, in millivolts: fast memory fades first (6.3).")
NOVA2_WHOLE("memory.threshold.slow", 600, 1, 10000, 0, "Memory", "Slow memory's threshold", "",
            "The threshold of the slowest memory, in millivolts. The reset region goes on the lowest.")
NOVA2_WHOLE("memory.maximum", 1300, 1, 10000, 0, "Memory", "Memory's maximum", "",
            "Every memory device's maximum voltage, in millivolts (4.4).")
NOVA2_NUMBER("memory.fade.fastest", 1, 0.001, 1e9, kKeyLog, "Memory", "Leakiest cell's retention", "",
             "How many ticks the leakiest bit of memory holds at 0 V, cut out (4.4, appendix C). The rest spread "
             "evenly on a log scale up to memory.fade.slowest.")
NOVA2_NUMBER("memory.fade.slowest", 1000, 0.001, 1e9, kKeyLog, "Memory", "Most enduring cell's retention", "",
             "How many ticks the most enduring bit of memory holds at 0 V.")
NOVA2_NUMBER("memory.fade.at_threshold", 1000, 1, 1e9, kKeyLog, "Memory", "Retention at the threshold", "",
             "How many times longer a cell holds just below its device's threshold than at 0 V, on a log scale with "
             "the voltage between.")
NOVA2_WHOLE("memory.hold_draw", 500, 0, 1000000, 0, "Memory", "Holding draw", "",
            "Each memory device's draw just to hold its contents, in milliwatts (4.5).")
NOVA2_WHOLE("memory.access_draw", 500, 0, 1000000, 0, "Memory", "Draw per access", "",
            "Memory's extra draw for each row activation in a tick, in microwatts: a data access to a page other "
            "than the one its core touched last, or each page a CPY touches (4.5, cpu.h). 500: a core touching a "
            "new row with every instruction, one to three thousand a tick, adds about 1 W, twice a device's "
            "holding draw; copying programs make only a few a tick.")
NOVA2_WHOLE("memory.class_steps", 2, 1, 2, 0, "Memory", "Size classes per doubling", "",
            "Size classes per doubling below a page: 2 gives 16, 24, 32, 48 and so on; 1 gives powers of two.")
NOVA2_WHOLE("memory.max_alloc", 4096, 8, 1073741824, 0, "Memory", "Largest allocation", "",
            "The largest allocation in bytes, at least a page (ALLOC's largest size class).")
NOVA2_NUMBER("hammer.strength", 0.01, 0, 1, 0, "Memory", "Rowhammer's strength", "",
             "Rowhammer (9.8): the chance that a row activation of a page past its memory device's threshold "
             "flips a bit in the page before it, and the same for the page after, in the same device. 0: off.")
NOVA2_WHOLE("hammer.window", 8000, 1, 100000000, 0, "Memory", "Rowhammer's window", "hammer.strength",
            "The refresh window, in ticks: each page's row activations are counted over it and start again at "
            "its end, the same ticks for the whole world. 8,000 ticks stand for a real 64 ms.")
NOVA2_WHOLE("hammer.threshold.fast", 10000, 1, 1000000000, 0, "Memory", "Fast memory's rowhammer threshold",
            "hammer.strength",
            "The row activations of a page in a window before it disturbs its neighbours, for the fastest "
            "memory. A device's own follows its latency, evenly on a log scale up to the slowest's.")
NOVA2_WHOLE("hammer.threshold.slow", 140000, 1, 1000000000, 0, "Memory", "Slow memory's rowhammer threshold",
            "hammer.strength", "The same for the slowest memory: older, less dense chips flip later.")
NOVA2_WHOLE("disk.size", 1048576, 0, 1073741824, 0, "Memory", "Disk per place", "",
            "A place's raw disk space in bytes (9.3).")
NOVA2_WHOLE("disk.latency", 20000, 0, 100000000, 0, "Memory", "Disk latency", "",
            "Cycles a DREAD or DWRITE costs after the barrier, on top of waiting for it (9.7).")
NOVA2_WHOLE("disk.cycles_per_byte", 1, 0, 100000, 0, "Memory", "Disk cycles per byte", "",
            "Cycles a DREAD or DWRITE costs per byte moved.")
NOVA2_WHOLE("disk.threshold", 600, 1, 10000, 0, "Memory", "Disk threshold", "",
            "The disk's lowest working voltage, in millivolts (4.4).")
NOVA2_WHOLE("disk.maximum", 1300, 1, 10000, 0, "Memory", "Disk maximum", "",
            "The disk's maximum voltage, in millivolts.")
NOVA2_WHOLE("disk.draw", 1000, 0, 1000000, 0, "Memory", "Disk draw", "",
            "The disk's draw, in milliwatts: constant at gate 2.")
NOVA2_WHOLE("disk.boot_block", 512, 1, 1073741824, 0, "Memory", "Boot block", "",
            "The boot block's size in bytes: the disk space's first bytes, which a cold start runs (10.5).")

// ---------------------------------------------------------------------------
// OS and calls (9.2, 9.6)
// ---------------------------------------------------------------------------
NOVA2_TEXT("os.page_sizes", "64 256 1024", 0, "OS and calls", "Page sizes", "",
           "The page sizes a place's OS may have, powers of two, drawn equally often (9.6).")
NOVA2_NUMBER("os.strict_share", 0.5, 0, 1, 0, "OS and calls", "Strict share", "",
             "The chance that a place's OS is strict, with no page writable and executable at once (9.3).")
NOVA2_TEXT("os.thread_limits", "0", 0, "OS and calls", "Thread limits", "",
           "The limits on the threads one program may have, one drawn for each place; 0 is no limit.")
NOVA2_TEXT("os.slots","0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 0 3", 0, "OS and calls", "Call numbering", "",
           "SYS s picks slot s mod the slot count, and each number here names a slot's call (9.2).")
NOVA2_WHOLE("os.stack", 64, 28, 1048576, 0, "OS and calls", "Stack per thread", "",
            "Bytes of stack a thread gets at SPAWN. The first 24 hold its saved registers (A.3).")
NOVA2_WHOLE("os.ports", 16, 1, 65536, 0, "OS and calls", "Ports", "",
            "Ports per place. Port 0 is the reset region's (11.2).")
NOVA2_WHOLE("os.clock.min", 5, 1, 255, 0, "OS and calls", "Lowest clock", "",
            "SETCLOCK's lowest level, in tenths of stock speed (7.5).")
NOVA2_WHOLE("os.clock.max", 15, 1, 255, 0, "OS and calls", "Highest clock", "",
            "SETCLOCK's highest level, in tenths of stock speed.")
NOVA2_WHOLE("os.ping_timeout", 100, 1, 1000000000, 0, "OS and calls", "PING's default timeout", "",
            "Ticks PING waits when its own timeout is 0 or less (A.4).")
NOVA2_WHOLE("os.send_byte_cycles", 2, 0, 1000000, 0, "OS and calls", "SEND's copy, a byte", "",
            "Cycles SEND pays for each byte it copies into the network device, on top of its call's cost (9.7, "
            "11.2): about a CPY's.")
NOVA2_FLAG("os.cutout_pause", false, 0, "OS and calls", "Pause at a cut-out", "",
           "7.6's fallback: a thread running on a core that cuts out is paused, back in the ready queue as it was, "
           "instead of ended. Off at gate 2, so the origin regression can try it without a rebuild.")
NOVA2_FLAG("os.saved_state_fails", true, 0, "OS and calls", "Saved state fails", "",
           "The saved-state rule (9.4): a thread ends when any bit of its saved state fails to hold, by a "
           "cosmic-ray flip or an edge error as well as by fading. An overwrite only corrupts it. Off, only fading "
           "ends a thread, as in gate 2's first run.")
NOVA2_FLAG("os.idle_send", false, 0, "OS and calls", "Idle runs SEND", "",
           "Whether an idle run may SEND (9.2). Off, SEND fails soft in an idle run, as DWRITE does, so code "
           "travels only in a program's SEND. On, as before 2026-11-17, bytes can send themselves on without "
           "becoming a program (10.4).")
NOVA2_FLAG("os.stack_held", true, 0, "OS and calls", "A live stack is held", "",
           "A live thread's stack counts as in use (9.4): a SPAWN into it starts a thread of the same program, "
           "and a FREE of it fails. Off, as before 2026-11-19, a program can give a sibling thread's stack away "
           "or free it, and the thread's saved registers go on being written there, whoever holds it.")
NOVA2_WHOLE("os.killer", 2000, 0, 1e15, 0, "OS and calls", "Out-of-memory killer", "",
            "The out-of-memory killer (9.1, gate 5): a place whose free memory has been under a tenth (13.3) at "
            "this many barriers in a row, a day by default, loses one program, drawn by the bytes it holds, "
            "stacks included, each byte as likely as any. Every thread of it ends, cause \"out of memory\", its "
            "memory freed with its bytes left, and the place's clock starts again. 0: off.")
NOVA2_WHOLE("os.watchdog", 1000000, 0, 1e15, 0, "OS and calls", "Watchdog", "",
            "The watchdog (9.1, gate 5): a thread that has run this many cycles of its own since its last call, "
            "any call, takes the watchdog fault (code 3) before its next instruction. A fault handler can take "
            "it, and its count starts again; with none the thread ends, cause \"watchdog\". Ten times the "
            "idle-run cap, so a thread that copies never trips it. 0: off.")

// ---------------------------------------------------------------------------
// Scheduler (9.5)
// ---------------------------------------------------------------------------
NOVA2_WHOLE("sched.quanta", 16, 1, 1024, 0, "Scheduler", "Quanta per tick", "",
            "Rounds in a tick. Each core runs one quantum per round (9.5).")
NOVA2_WHOLE("sched.turn", 4, 1, 1024, 0, "Scheduler", "Quanta per turn", "",
            "How many quanta a thread keeps a core before the next waiting thread gets it.")
NOVA2_WHOLE("sched.switch", 1000, 1, 10000000, 0, "Scheduler", "Thread switch", "",
            "Cycles a thread switch costs (8.4: 1,000 to 3,000).")
NOVA2_WHOLE("sched.clock_switch", 500, 0, 10000000, 0, "Scheduler", "Clock switch", "",
            "Extra cycles a switch costs when the clock level changes (7.5).")

// ---------------------------------------------------------------------------
// Reset region (10.2)
// ---------------------------------------------------------------------------
NOVA2_WHOLE("region.size", 1024, 1, 1073741824, 0, "Reset region", "Region size", "",
            "The reset region's size in bytes, rounded up to whole pages (10.2).")
NOVA2_WHOLE("region.step", 13, 1, 1000000, 0, "Reset region", "Start-spot step", "",
            "Bytes each start moves a core's start spot. It must share no factor with the region's size (R2).")
NOVA2_WHOLE("region.cap", 100000, 1, 1000000000, 0, "Reset region", "Idle-run cap", "",
            "An idle run's longest, in cycles, longer than the dearest birth (R3).")
NOVA2_WHOLE("region.retry", 1, 1, 1000000000, 0, "Reset region", "Retry", "",
            "Ticks before a halted core tries again with nothing changed (R1). 1 at gate 1, where nothing draws power.")
NOVA2_WHOLE("region.queue", 4096, 0, 1073741824, 0, "Reset region", "Port 0's queue", "",
            "Port 0's queue in bytes (10.4, R3): arrivals wait there for a core. It must hold the boot block.")
NOVA2_NUMBER("region.arrival_share", 0.5, 0, 1, 0, "Reset region", "Arrivals' share", "",
             "The most of a place's cores that arrivals from other places may hold at once as prompt runs (10.4), "
             "rounded down but at least 1, so static keeps the rest. A boot block isn't held back. 1 lets arrivals "
             "take every core, as before 2026-11-17.")

// ---------------------------------------------------------------------------
// Noise (7.5)
// ---------------------------------------------------------------------------
NOVA2_NUMBER("noise.<nkind>_floor", 1e-5, 0, 0.5, kKeyLog, "", "<nkind> error floor", "",
             "The chance that each result a <nkind> gives is wrong, at stock clock (7.5).")
NOVA2_MEMBER("nkind", "p", "P core", "Noise")
NOVA2_MEMBER("nkind", "e", "E core", "Noise")
NOVA2_WHOLE("noise.clock_doubling", 1, 0, 100, 0, "Noise", "Clock doubling", "",
            "Tenths of clock per doubling of the error rate above stock, halving below (0: no effect).")
NOVA2_NUMBER("noise.cosmic", 1e-9, 0, 0.5, kKeyLog, "Noise", "Cosmic-ray floor", "",
             "The chance that each bit of memory flips in a tick (7.5). With cosmic rays at the start, the rate they "
             "fall to.")
NOVA2_NUMBER("noise.cosmic_start", 0, 0, 0.5, kKeyLog, "Noise", "Cosmic rays at the start", "",
             "The chance that each bit of memory flips in a tick at tick 0, falling from there to the cosmic-ray "
             "floor (7.5). 0: no fall, the floor from the start.")
NOVA2_WHOLE("noise.cosmic_half_life", 1000000, 1, 1e15, 0, "Noise", "Cosmic rays' half-life", "",
            "Ticks over which the cosmic-ray rate's excess over its floor halves, in eight steps (7.5).")
NOVA2_NUMBER("flaky.share", 0.02, 0, 1, 0, "Noise", "Flaky parts' share", "",
             "Flaky parts (7.5): the chance that a core or a memory device is flaky, drawn when it is built and "
             "again when it is replaced. A flaky one's error floor is its kind's times a factor. 0: none are.")
NOVA2_NUMBER("flaky.factor.min", 3, 1, 10000, kKeyLog, "Noise", "A flaky part's least factor", "flaky.share",
             "A flaky device's floor over its kind's, at the least: a core's wrong results, a memory device's "
             "cosmic-ray rate. Factors are drawn evenly on a log scale from here to the most.")
NOVA2_NUMBER("flaky.factor.max", 100, 1, 10000, kKeyLog, "Noise", "A flaky part's most factor", "flaky.share",
             "The same, at the most. Must be at least the least.")
NOVA2_NUMBER("flaky.worn", 10, 1, 1000, kKeyLog, "Noise", "A worn core's floor", "",
             "A core's error floor at the end of its life, over its own when new (7.5): it rises in proportion "
             "to the core's wear. 1: wear doesn't raise it.")
NOVA2_WHOLE("noise.edge_band", 50, 0, 1000, 0, "Noise", "Edge band", "",
            "How close to either edge of its voltage range, in millivolts, a device's error rate starts rising (7.5). "
            "0: no edge errors.")
NOVA2_WHOLE("noise.edge_doubling", 10, 1, 1000, 0, "Noise", "Edge doubling", "",
            "Millivolts closer to an edge per doubling of the error rate, inside the band: at the edge it's 2 to the "
            "band over this times the floor. Memory's floor is the cosmic-ray floor.")

// ---------------------------------------------------------------------------
// Costs (8.4, 9.7), placeholders
// ---------------------------------------------------------------------------
NOVA2_WHOLE("cost.<ckind>.simple", 16, 1, 1000000, 0, "", "<ckind> simple work", "",
            "Sixteenths of a cycle a <ckind> takes for a simple instruction's work (8.4).")
NOVA2_WHOLE("cost.<ckind>.mul", 48, 1, 1000000, 0, "", "<ckind> MUL", "",
            "Sixteenths of a cycle a <ckind> takes for MUL.")
NOVA2_WHOLE("cost.<ckind>.div", 320, 1, 1000000, 0, "", "<ckind> DIV", "",
            "Sixteenths of a cycle a <ckind> takes for DIV.")
NOVA2_WHOLE("cost.<ckind>.cpy_setup", 64, 0, 1000000, 0, "", "<ckind> CPY setup", "",
            "Sixteenths of a cycle a <ckind> takes to start a CPY.")
NOVA2_WHOLE("cost.<ckind>.cpy_byte", 32, 1, 1000000, 0, "", "<ckind> CPY per byte", "",
            "Sixteenths of a cycle a <ckind> takes per byte CPY moves.")
NOVA2_MEMBER("ckind", "p", "P core", "Costs")
NOVA2_MEMBER("ckind", "e", "E core", "Costs")
NOVA2_DEFAULT("cost.e.simple", 18)
NOVA2_DEFAULT("cost.e.mul", 55)
NOVA2_DEFAULT("cost.e.div", 528)
NOVA2_DEFAULT("cost.e.cpy_setup", 74)
NOVA2_DEFAULT("cost.e.cpy_byte", 37)
NOVA2_WHOLE("cost.call.<call>", 300, 1, 100000000, 0, "", "<call> cost", "",
            "Cycles a <call> costs its caller, the crossing included (9.7).")
NOVA2_MEMBER("call", "alloc", "ALLOC", "Costs")
NOVA2_MEMBER("call", "free", "FREE", "Costs")
NOVA2_MEMBER("call", "protect", "PROTECT", "Costs")
NOVA2_MEMBER("call", "spawn", "SPAWN", "Costs")
NOVA2_MEMBER("call", "exit", "EXIT", "Costs")
NOVA2_MEMBER("call", "sleep", "SLEEP", "Costs")
NOVA2_MEMBER("call", "setclock", "SETCLOCK", "Costs")
NOVA2_MEMBER("call", "setcore", "SETCORE", "Costs")
NOVA2_MEMBER("call", "sethandler", "SETHANDLER", "Costs")
NOVA2_MEMBER("call", "hret", "HRET", "Costs")
NOVA2_MEMBER("call", "sense", "SENSE", "Costs")
NOVA2_MEMBER("call", "send", "SEND", "Costs")
NOVA2_MEMBER("call", "recv", "RECV", "Costs")
NOVA2_MEMBER("call", "dread", "DREAD", "Costs")
NOVA2_MEMBER("call", "dwrite", "DWRITE", "Costs")
NOVA2_MEMBER("call", "ping", "PING", "Costs")
NOVA2_DEFAULT("cost.call.alloc", 2000)
NOVA2_DEFAULT("cost.call.protect", 5000)
NOVA2_DEFAULT("cost.call.spawn", 20000)
NOVA2_DEFAULT("cost.call.setclock", 30000)
NOVA2_DEFAULT("cost.call.sense", 150)
NOVA2_DEFAULT("cost.call.send", 150)
NOVA2_DEFAULT("cost.call.recv", 150)
NOVA2_DEFAULT("cost.call.dread", 150)
NOVA2_DEFAULT("cost.call.dwrite", 150)
NOVA2_DEFAULT("cost.call.ping", 150)
NOVA2_WHOLE("cost.trap", 150, 1, 100000000, 0, "Costs", "Fault trap", "",
            "Cycles a fault's trap costs: the crossing (9.7).")
NOVA2_WHOLE("cost.e_calls", 26, 1, 1000, 0, "Costs", "E core calls", "",
            "Sixteenths per cycle an E core pays for calls. 26 is about 60% slower than a P core's 16 (7.2).")


// ---------------------------------------------------------------------------
// Observers (13)
// ---------------------------------------------------------------------------
NOVA2_WHOLE("observe.sample_every", 100, 1, 1000000000, 0, "Observers", "Sample interval", "",
            "Ticks between the observers' samples (13.5).")
NOVA2_WHOLE("observe.write_every", 1, 1, 1000000, 0, "Observers", "Samples to a written row", "",
            "A row of the samples' record, and each place's row of the voltages' record, is written at every "
            "this-many-th sample: with 2, at the ticks that are a multiple of twice observe.sample_every, which "
            "halves both records. Sampling itself is not changed, nor anything worked out at a sample (the "
            "stages, the windows, the report). A written row is the sample at its tick as it stands: every "
            "column is a running total or the state of the world at that tick, and none counts or peaks over one "
            "sample's ticks, so nothing is lost between rows but the readings of the samples left out. A place's "
            "voltage row does cover a stretch: its mean and its lowest are over every tick since the row before. "
            "A seed's last sample is always written. The viewer's charts draw the written rows.")
NOVA2_NUMBER("observe.group_threshold", 0.8, 0.01, 1, 0, "Observers", "Group threshold", "",
             "The share of 3-byte runs a genome must share with a group's first to join it (13.2).")
NOVA2_WHOLE("observe.min_genome", 8, 1, 1000000, 0, "Observers", "Shortest faithful genome", "",
            "A birth counts as faithful only if its genome, the parent's, is at least this long (13.2).")
NOVA2_WHOLE("observe.turnover_window", 200000, 0, kKeyNoMax, 0, "Observers", "Turnover window", "",
            "Ticks in each window of the turnover measure: whether a place's programs die and are replaced "
            "(build-plan-gate2.md step 1; 0: off).")
NOVA2_WHOLE("observe.turnover_members", 20, 1, 1000000, 0, "Observers", "Turnover members", "",
            "A window counts for the turnover measure if it starts with at least this many programs alive.")
NOVA2_WHOLE("observe.keep_generations", 3, 1, 4, 0, "Observers", "Generations kept", "",
            "A genome is kept, and counted in the genebank and its groups, once a program carrying it has a "
            "descendant this many generations on, counting itself as the first (13.2; 1: every genome, at its "
            "first birth). The same count decides which finished lineages get a row in their record: one that "
            "died out without ever copying itself gets a row only if it went this many generations, so a lineage "
            "with a row is one a genome could be kept from. The shorter ones, most of them one program that never "
            "bred, are counted in a tally kept with the record, how many and the births in them, so the totals "
            "still add up.")
NOVA2_FLAG("observe.forget", false, 0, "Observers", "Forget the extinct", "",
           "For a run with no end set: a kept genome that no living program carries, or descends from within "
           "the generations kept, leaves memory, and a group with it when its last member does. Both stay in "
           "the genebank's records (origin.records). The sample's group count is then the groups with living "
           "members, and one let go that turns up again counts as new. Off: everything kept stays in memory, as "
           "the gates' runs count it. Needs observe.keep_generations of 2 or more.")
NOVA2_WHOLE("observe.genome_key_every", 16, 1, 255, 0, "Observers", "Whole genome every", "",
            "A kept genome's bytes are recorded (seed-N.genome_bytes) as its difference from the genome it came "
            "from, with a whole copy at least every this-many steps along a line, and a whole copy whenever the "
            "parent's bytes aren't to hand or the difference would be no shorter. 1: every genome whole. A reader "
            "builds a genome from 16 entries at most at the default, and damage to one entry spoils only the "
            "genomes built on it up to the next whole copy.")
NOVA2_NUMBER("observe.flush_seconds", 30, 1, 86400, 0, "Observers", "Records written every", "",
             "Seconds between writes of the finished records buffered for their files (origin.records), which are "
             "written at every save too (13.1; Damian's number).")

// ---------------------------------------------------------------------------
// Origin test (10.6): placeholders, to be agreed before the first run
// ---------------------------------------------------------------------------
NOVA2_WHOLE("origin.seeds", 10, 1, 100000, 0, "Origin test", "Seeds", "",
            "How many seeds the origin test runs, each a world of its own, from world.seed on (10.6).")
NOVA2_WHOLE("origin.ticks", 10000000, 1, kKeyNoMax, 0, "Origin test", "Budget in ticks", "",
            "The ticks each seed runs: the same simulated time for every seed, whatever lives in it (10.6).")
NOVA2_WHOLE("origin.budget", 0, 0, kKeyNoMax, 0, "Origin test", "Instruction cap", "",
            "Stop a seed after this many instructions, idle runs included, even within its ticks (0: no cap).")
NOVA2_WHOLE("origin.parallel", 0, 0, 1024, 0, "Origin test", "Seeds at once", "",
            "How many seeds run side by side, sharing the host threads (0: as many as fit two threads each).")
NOVA2_FLAG("origin.view", false, 0, "Origin test", "Window on the run", "",
           "Opens the viewer's window on the run at its start, in the run's own process. It shows one seed at a "
           "time, read between that seed's ticks, and it only watches: the run's files are the same with it open "
           "or shut. Closing it leaves the run going, and nova2 watch opens it again, on any run, at any time.")
NOVA2_TEXT("origin.picture", "", 0, "Origin test", "Picture of the window", "",
           "A file the run's window writes a picture of itself to, a PNG drawn off screen from what the window "
           "shows, so it comes with the display asleep or the window covered (empty: none). A window that "
           "has been closed writes none.")
NOVA2_NUMBER("origin.picture_minutes", 5, 0, 100000, 0, "Origin test", "Picture every", "",
             "Minutes between the pictures of origin.picture (0: none).")
NOVA2_TEXT("origin.samples", "", 0, "Origin test", "Samples folder", "",
           "A folder for each seed's samples as text, seed-N.csv (empty: none). A run that keeps its records in "
           "memory writes it as it goes. One that writes them to files (origin.records) holds the samples there "
           "once, and nova2 samples makes the same seed-N.csv from them when it is wanted.")
NOVA2_TEXT("origin.genomes", "", 0, "Origin test", "Genomes folder", "",
           "A folder to write each seed's most common genomes to at its end, disassembled, as genomes-seed-N.txt "
           "(empty: none).")
NOVA2_TEXT("origin.snapshots", "", 0, "Origin test", "Snapshots folder", "",
           "A folder for place snapshots, seed-S-tick-T-place-P.snap, read by nova2 trace --snapshot (empty: none). "
           "At a seed's end, each place where a lineage origin.generations deep has half origin.members alive or "
           "more; and a place as its lineage reaches stage 4. And every origin.snapshot_every ticks as at the end, "
           "if that is set.")
NOVA2_TEXT("origin.windows", "", 0, "Origin test", "Turnover windows folder", "",
           "A folder to write each seed's turnover windows to, as seed-N.csv: one row for each place's window that "
           "started with a program alive (empty: none). And its windows by their ground (hot, dim or fair), as "
           "seed-N-ground.csv: one row for every place's window.")
NOVA2_TEXT("origin.records", "", 0, "Origin test", "Records folder", "",
           "A folder for each seed's finished records, written as the run goes so a long run doesn't keep them all "
           "in memory: seed-N.samples, seed-N.volts, seed-N.windows and seed-N.ground (13.1). Empty: kept in memory.")
NOVA2_WHOLE("origin.snapshot_every", 0, 0, kKeyNoMax, 0, "Origin test", "Snapshot interval", "",
            "Ticks between a seed's place snapshots through the run (origin.snapshots). 0: none through the run, "
            "only the one as a lineage reaches stage 4 and those at the seed's end (Damian, 2026-11-20; it was "
            "1,000,000, and the earlier runs' settings still say so).")
NOVA2_TEXT("origin.saves", "", 0, "Origin test", "Saves folder", "",
           "A folder for a seed's saves taken mid-run (empty: the folder the run was started in). A save is two "
           "files: seed-S-tick-T.world.snap, its whole world, which nova2 view opens and nova2 resume carries on; "
           "and seed-S-tick-T.test.snap, the observers' memory and the test's record, with which origin.resume "
           "carries the test itself on. nova2 save asks every running seed for one, the window on a run has a "
           "button for the seed shown, and origin.save_every takes them by the clock.")
NOVA2_WHOLE("origin.save_every", 0, 0, kKeyNoMax, 0, "Origin test", "Save interval", "",
            "Ticks between a seed's saves (origin.saves; 0: only when asked). A seed that saves by the clock also "
            "saves where it stops, so it can be carried on with more ticks.")
NOVA2_NUMBER("origin.save_minutes", 0, 0, 100000, 0, "Origin test", "Save every", "",
             "Minutes on the clock between a seed's saves (origin.saves; 0: not by the clock), for a run whose "
             "speed changes too much to save by ticks. Kept and removed as origin.save_every's are.")
NOVA2_NUMBER("origin.progress_minutes", 0, 0, 100000, 0, "Origin test", "Progress every", "",
             "Minutes on the clock between a seed's progress lines (0: only at each tenth of its ticks or "
             "budget), for a run with no end in sight.")
NOVA2_WHOLE("origin.memory_floor", 0, 0, kKeyNoMax, 0, "Origin test", "Memory floor", "",
            "Megabytes of memory the machine must have left, free and committable. A seed that finds less "
            "saves its world and stops there, to be carried on (origin.resume), rather than run the machine "
            "out of memory (0: not watched).")
NOVA2_WHOLE("origin.saves_kept", 2, 0, 1000000, 0, "Origin test", "Saves kept", "",
            "How many of a seed's saves taken by the clock are kept: as a new one is written the older ones go (0: "
            "all are kept). A seed that runs its ticks or its budget out keeps only the save it ended on, unless "
            "this is 0. A save that was asked for is never removed, nor the one a run was carried on from.")
NOVA2_TEXT("origin.resume", "", 0, "Origin test", "Carry on from", "",
           "A folder of saves (origin.saves) to carry the test on from: each seed goes on from its newest save "
           "there, with its family tree, stages and counts as they stood, and its report covers its whole life. A "
           "seed with no save there starts at tick 0. The settings must be those the saves were made under, but "
           "for the ones that shape the run and its files (the ticks, the budget, the folders).")
NOVA2_WHOLE("origin.generations", 10, 1, 1000000, 0, "Origin test", "Generations", "",
            "Stage 3: the generations a copying lineage must reach (10.6).")
NOVA2_WHOLE("origin.span", 100000, 1, kKeyNoMax, 0, "Origin test", "Establishing span", "",
            "Stage 4: the ticks a lineage must hold its place without a break (build-plan-gate1.md 7).")
NOVA2_NUMBER("origin.share", 0.1, 0, 1, 0, "Origin test", "Establishing share", "",
             "Stage 4: the share of its place's programs it must hold.")
NOVA2_WHOLE("origin.members", 20, 1, 1000000, 0, "Origin test", "Establishing members", "",
            "Stage 4: the living members it must have while it holds its place.")
NOVA2_NUMBER("origin.copies", 0.5, 0, 1, 0, "Origin test", "Establishing copies", "",
             "Stage 4: the share of its living members that must have been born as copies: faithful, or at least "
             "half like the parent's genome.")
NOVA2_WHOLE("origin.groups", 3, 1, 1000000, 0, "Origin test", "New groups", "",
            "Stage 5: new genome groups it must make while its place is crowded, after it reached stage 4.")
NOVA2_NUMBER("origin.pass_share", 0.5, 0, 1, 0, "Origin test", "Pass mark", "",
             "The share of seeds that must reach stage 4 for a pass (10.6).")
NOVA2_NUMBER("origin.turnover_share", 0.9, 0, 1, 0, "Origin test", "Turnover pass mark", "",
             "Gate 2's pass mark (build-plan-gate2.md 7): the share of counted turnover windows, and of crowded "
             "ones, that must turn over. Windows with a cold start in them count.")
NOVA2_WHOLE("origin.crowded_min", 100, 0, 1000000000, 0, "Origin test", "Fewest crowded windows", "",
            "Gate 2's pass mark: the fewest crowded windows, across all seeds, it can be judged on.")
NOVA2_WHOLE("origin.crowded_seeds", 5, 0, 100000, 0, "Origin test", "Seeds with crowded windows", "",
            "Gate 2's pass mark: the fewest seeds those crowded windows must come from.")
NOVA2_NUMBER("origin.spread_share", 0.25, 0, 1, 0, "Origin test", "Spread pass mark", "",
             "Gate 3's pass mark (build-plan-gate3.md 7; a placeholder until agreed): the share of seeds in which a "
             "lineage settles a place beyond the one it began in (13.3).")
NOVA2_NUMBER("origin.ground_ratio", 2, 1, 1000, 0, "Origin test", "Ground pass mark", "",
             "Gate 4's pass mark (build-plan-gate4.md 7): in the lax places of the seeds that reach stage 4, how many "
             "times the programs of a hot place-window, and of a dim one, a fair place-window must start with.")
NOVA2_WHOLE("origin.ground_min", 100, 0, 1000000000, 0, "Origin test", "Fewest windows of each ground", "",
            "Gate 4's pass mark: the fewest place-windows of each kind of ground (fair, hot, dim) it can be judged "
            "on.")
NOVA2_WHOLE("origin.ground_seeds", 5, 0, 100000, 0, "Origin test", "Seeds with each ground", "",
            "Gate 4's pass mark: the fewest seeds each kind of ground's place-windows must come from.")
NOVA2_WHOLE("origin.era", 2000000, 0, kKeyNoMax, 0, "Origin test", "Era", "",
            "Gate 5's measure (build-plan-gate5.md 7): the ticks of an era. A seed's run from its stage 4 to its end "
            "is cut into eras, and the later half of them are its late eras. An era has change if a genome group "
            "leads a place for the first time in it (0: no eras).")
NOVA2_NUMBER("origin.era_share", 0.75, 0, 1, 0, "Origin test", "Late eras with change", "",
             "Gate 5's pass mark: the share of a seed's late eras that must have a first lead.")
NOVA2_WHOLE("origin.era_late", 4, 1, 100000, 0, "Origin test", "Fewest late eras", "",
            "Gate 5's pass mark: the fewest late eras a seed must have to be counted (4: it ran 8 eras or more past "
            "its stage 4).")
NOVA2_FLAG("origin.calls", false, 0, "Origin test", "Gate 5's sample columns", "",
           "The samples' CSV carries gate 5's columns: the calls programs' threads make and those that succeed, by "
           "kind; SENDs to a port other than 0; reading input, overruns and injections. Counted either way.")
NOVA2_WHOLE("origin.era_seeds", 5, 0, 100000, 0, "Origin test", "Seeds with late change", "",
            "Gate 5's pass mark: the fewest seeds that must have a first lead in enough of their late eras.")
NOVA2_NUMBER("origin.era_seed_share", 0.5, 0, 1, 0, "Origin test", "Share of seeds with late change", "",
             "Gate 5's pass mark: the share of the seeds counted that must have a first lead in enough of their "
             "late eras.")

// ---------------------------------------------------------------------------
// Test worlds (build-plan-gate3.md step 7; nova2 worlds)
// ---------------------------------------------------------------------------
NOVA2_CHOICE("worlds.test", "sense",
             "sense blind busy nightfall flood band heat water sleeper rejection numbers classes scars succession "
             "storms partition respawning hammerer injection bootblocks options flaky islands fanout current",
             0,
             "Test worlds", "Test", "",
             "Gate 3's test worlds at full size (R7): a sensing migrant, a blind one, places kept busy by threads "
             "while migrants arrive, nightfall with traffic, or the world as it is with its port 0 queues read "
             "(flood; worlds.cpp says what each reports). And gate 4's: the band around a stiff source and a soft "
             "one, a full place's heat, the voltage across water, a thread that sleeps on the low-voltage "
             "interrupt beside one that doesn't, and a crowd cut off at once (rejection; power_worlds.cpp). "
             "numbers measures what gate 4's numbers do: the cores' heat, the places' draw, where power reaches. "
             "classes names each place's ground by what a full place gets there, for gate 4's pass mark. And "
             "gate 5's (wear_worlds.cpp): scars under three visit intervals, a lineage's succession at a hot hub, "
             "storms over a town and over countryside, a partition, respawning over a long run, a hammerer, "
             "injection, boot blocks with levelling off and on, the options (replacement and respawn shares), "
             "a flaky place, two islands rejoined, fan-out under the killer and the watchdog, and a current "
             "across a chain of places.")
NOVA2_CHOICE("worlds.migrant_device", "spot", "spot any", 0, "Test worlds", "Migrant's device", "",
             "The memory device a migrant's arrival ALLOCs its copy from: the one its landing spot names (an "
             "address, so the slowest, A.5), or any (asked after build-plan-gate3.md step 7).")
NOVA2_WHOLE("worlds.ticks", 20000, 1, kKeyNoMax, 0, "Test worlds", "Ticks", "",
            "How long a test world runs, in ticks.")
NOVA2_WHOLE("worlds.report_every", 1000, 1, kKeyNoMax, 0, "Test worlds", "Report interval", "",
            "Ticks between a test world's progress lines.")
NOVA2_WHOLE("worlds.from", 0, 0, kKeyNoMax, 0, "Test worlds", "Classing from", "",
            "Gate 4's classing (worlds.test = classes, and numbers' last part): the tick whose sources the "
            "classing's worlds have. Each world starts empty at that tick, its places at the outside's "
            "temperature.")
NOVA2_WHOLE("worlds.class_every", 2000000, 1, kKeyNoMax, 0, "Test worlds", "Classing interval", "",
            "worlds.test = classes: the ticks from one classing to the next. A place-window takes the latest "
            "classing at or before its start.")
NOVA2_WHOLE("worlds.class_to", 0, 0, kKeyNoMax, 0, "Test worlds", "Last classing", "",
            "worlds.test = classes: the classings are at worlds.from and every worlds.class_every ticks after, "
            "none later than this tick (a run's last tick, say). Less than worlds.from: the one at worlds.from.")
NOVA2_TEXT("worlds.classes", "", 0, "Test worlds", "Classes folder", "",
           "worlds.test = classes: the folder for seed-N-classes.csv, a row a place for each classing. Rows of "
           "an earlier call from before worlds.from are kept, so a seed can be classed over several calls. "
           "Empty: no file.")
NOVA2_TEXT("worlds.respawns", "", 0, "Test worlds", "Respawns folder", "",
           "Gate 4's classing: the run's windows folder (origin.windows), whose seed-N-respawns.csv gives the "
           "seed's respawns. Each classing's worlds have those at or before its tick applied, each checked "
           "against the run's record of it, so a respawned place is classed as it stood then; and a place that "
           "respawns after a classing is classed alone at its respawn's tick. Empty: the first map's places "
           "throughout.")

// ---------------------------------------------------------------------------
// Power (4; build-plan-gate2.md)
// ---------------------------------------------------------------------------
NOVA2_WHOLE("power.restart_gap", 50, 1, 10000, 0, "Power", "Restart gap", "",
            "How far above its threshold, in millivolts, the voltage must climb before a device that cut out "
            "restarts (4.4). The same for every device.")
NOVA2_WHOLE("power.off_time", 20, 1, kKeyNoMax, 0, "Power", "Minimum off-time", "",
            "The fewest ticks a device stays off after it cuts out, as real undervoltage lockouts do (4.1).")
NOVA2_WHOLE("power.low_voltage_margin", 30, 0, 10000, 0, "Power", "Low-voltage margin", "",
            "The low-voltage interrupt (9.5): a thread with a handler takes it when it starts a quantum on a core "
            "whose voltage is less than this many millivolts above the core's threshold. Once a dip: it can take "
            "it again only after starting a quantum with the voltage back at or above the margin. 0: never.")

// ---------------------------------------------------------------------------
// Supply (design.md 2, 4.2, 4.3; build-plan-gate2.md 2 and 7): a source for
// each place. The defaults are steady and stiff, gate 1's supply; gate 2's
// runs set section 7's numbers.
// ---------------------------------------------------------------------------
NOVA2_WHOLE("supply.voltage", 1000, 1, 10000, 0, "Supply", "Source voltage", "",
            "Each place's source's voltage, in millivolts: the place's voltage with nothing drawn (4.2).")
NOVA2_WHOLE("supply.sag", 0, 0, 10000, 0, "Supply", "Sag at full draw", "",
            "How far, in millivolts, a place's voltage sags at its full draw with its source at full capacity: "
            "this sets each place's internal resistance (4.2). 0: a stiff source, which never sags. Gate 2: 50.")
NOVA2_WHOLE("supply.day", 2000, 1, 1000000, 0, "Supply", "Day", "",
            "Ticks in a day (appendix E): the supply's day, at each place's own phase, and the reports' rates per day.")
NOVA2_NUMBER("supply.night", 1, 0, 1, 0, "Supply", "Night's capacity", "",
             "A source's capacity at midnight, as a share of noon's (4.3). 1: no day. Gate 2: 0.2.")
NOVA2_NUMBER("supply.swing", 0, 0, 1, 0, "Supply", "Swings", "",
             "How far below full a source's capacity drifts at most, as a share (4.3). 0: no swings. Gate 2: 0.25.")
NOVA2_WHOLE("supply.swing_period", 20000, 1, 1000000000, 0, "Supply", "Swing period", "",
            "Ticks from one swing level to the next: each is drawn evenly between full and the deepest, and "
            "capacity eases from one to the next (about 10 days).")
NOVA2_NUMBER("supply.dropouts", 0, 0, 1000, 0, "Supply", "Drop-outs", "",
             "Drop-outs starting per place per million ticks, on average: a source's capacity falling low or to "
             "nothing for a while (4.3). 0: none. Gate 2: 1.")
NOVA2_WHOLE("supply.dropout.min", 10, 1, 1000000, 0, "Supply", "Shortest drop-out", "",
            "A drop-out's shortest, in ticks. Lengths are drawn evenly on a log scale between the shortest and "
            "the longest.")
NOVA2_WHOLE("supply.dropout.max", 1000, 1, 1000000, 0, "Supply", "Longest drop-out", "",
            "A drop-out's longest, in ticks.")
NOVA2_NUMBER("supply.blackout_share", 0.5, 0, 1, 0, "Supply", "Blackouts", "",
             "The share of drop-outs that leave nothing: a blackout, so a cold start (2.3).")
NOVA2_NUMBER("supply.dropout.keep_min", 0, 0, 1, 0, "Supply", "Drop-out's least left", "",
             "The rest leave a share of capacity drawn evenly between this and supply.dropout.keep_max.")
NOVA2_NUMBER("supply.dropout.keep_max", 0.5, 0, 1, 0, "Supply", "Drop-out's most left", "",
             "The most of capacity a drop-out that isn't a blackout leaves, as a share.")
// The sources on the map (4.2, 4.3; build-plan-gate4.md step 3 and 7). The
// numbers are section 7's placeholders, to be worked again and agreed before
// the first gate 4 run.
NOVA2_CHOICE("supply.model", "sources", "sources place", 0, "Supply", "Supply model", "",
             "Where power comes from: sources that land on the map and reach places along the proximity links, "
             "through one voltage solve a tick (4.1, 4.2; from gate 4); or a source of its own at each place, "
             "gates 2 and 3's supply.")
NOVA2_NUMBER("sources.start", 25, 0, 10000, 0, "Supply", "Sources at the start", "supply.model=sources",
             "Sources on the map at the start, for each 100 places (4.2). Each is as old as a draw puts it, "
             "evenly within its life.")
NOVA2_NUMBER("sources.chance", 0.924196, 0, 1000000, kKeyComputed, "Supply", "New sources", "supply.model=sources",
             "New sources for each 100 places in a million ticks (4.2). The long-run count is this x the average "
             "lifetime; left unset, it is what keeps sources.start.")
NOVA2_WHOLE("sources.life", 20000000, 40, 1000000000000, 0, "Supply", "Source lifetime", "supply.model=sources",
            "The middle of the sources' lifetimes, in ticks: each lives from a quarter of this to four times it, "
            "evenly on a log scale (1.35 times it on average), growing over its first tenth and fading over its "
            "last (4.2).")
NOVA2_NUMBER("sources.area_power", 1, 0, 8, 0, "Supply", "Source-area power", "supply.model=sources",
             "A place's chance of a new source is its catchment's area raised to this (4.2): 1 is by area, 0 by "
             "place, and above 1 favours the countryside more.")
NOVA2_NUMBER("sources.size.min", 100, 0.001, 1000000, 0, "Supply", "Smallest source", "supply.model=sources",
             "The least load a source is rated for, in watts: the load at which it sags by its type's sag (4.2).")
NOVA2_NUMBER("sources.size.max", 800, 0.001, 1000000, 0, "Supply", "Largest source", "supply.model=sources",
             "The most load a source is rated for, in watts.")
NOVA2_NUMBER("sources.size.skew", 3, 0.01, 100, 0, "Supply", "Source size skew", "supply.model=sources",
             "How skewed the sources' sizes are (4.2): a size is the least x (the most / the least)^(u^skew), u "
             "drawn evenly. 1 is even on a log scale; above 1, most are small and a few large.")
NOVA2_NUMBER("sources.day_span", 1, 0, 1000, 0, "Supply", "Day's span", "supply.model=sources",
             "How much of a day lies between the map's east and west edges (4.3): the day sweeps from east to "
             "west. 1: it is always noon somewhere. 0: the whole map has one time of day.")
NOVA2_NUMBER("sources.swell.min", 1.1, 1, 8, 0, "Supply", "Least swell", "supply.model=sources",
             "A swell multiplies its source's voltage by a factor drawn evenly between this and "
             "sources.swell.max (4.3).")
NOVA2_NUMBER("sources.swell.max", 1.8, 1, 8, 0, "Supply", "Most swell", "supply.model=sources",
             "The most a swell multiplies its source's voltage by.")
NOVA2_WHOLE("sources.swell.shortest", 10, 1, 1000000, 0, "Supply", "Shortest swell", "supply.model=sources",
            "The shortest swell, in ticks. Lengths are drawn evenly on a log scale; swells start and end at once "
            "until gate 5's ramps.")
NOVA2_WHOLE("sources.swell.longest", 1000, 1, 1000000, 0, "Supply", "Longest swell", "supply.model=sources",
            "The longest swell, in ticks.")
NOVA2_TEXT("sources.fixed", "", 0, "Supply", "Sources placed by hand", "supply.model=sources",
           "Sources placed by hand, for test worlds: 'place type watts sag', with commas between, such as "
           "'0 steady 100 30, 17 gusty 250 100'. Each lasts the whole run at its full size, with its type's "
           "voltage, day, swings, drop-outs and swells.")
NOVA2_NUMBER("source.<source>.share", 0.25, 0, 1000, 0, "", "<source> share", "supply.model=sources",
             "<source>: this type's share of the sources drawn, against the other types' shares (4.2).")
NOVA2_WHOLE("source.<source>.voltage", 1050, 1, 5000, 0, "", "<source> voltage", "supply.model=sources",
            "<source>: the type's voltage, in millivolts: its place's voltage with nothing drawn.")
NOVA2_WHOLE("source.<source>.sag.min", 20, 1, 5000, 0, "", "<source> least sag", "supply.model=sources",
            "<source>: the least a source of this type sags at its rated load, in millivolts: the stiffest. A "
            "source's sag is drawn evenly on a log scale between this and the most.")
NOVA2_WHOLE("source.<source>.sag.max", 50, 1, 5000, 0, "", "<source> most sag", "supply.model=sources",
            "<source>: the most a source of this type sags at its rated load, in millivolts: the softest.")
NOVA2_NUMBER("source.<source>.night", 1, 0, 1, 0, "", "<source> night", "supply.model=sources",
             "<source>: its capacity at midnight, as a share of noon's (4.3). 1: it ignores the day.")
NOVA2_NUMBER("source.<source>.swing", 0.05, 0, 1, 0, "", "<source> swings", "supply.model=sources",
             "<source>: how far below full its swings take its capacity, at most (4.3).")
NOVA2_WHOLE("source.<source>.swing_period", 20000, 1, 1000000000, 0, "", "<source> swing period",
            "supply.model=sources", "<source>: ticks from one swing level to the next.")
NOVA2_NUMBER("source.<source>.dropouts", 1, 0, 1000, 0, "", "<source> drop-outs", "supply.model=sources",
             "<source>: drop-outs starting per source in a million ticks (4.3). Their lengths and depths are the "
             "supply.dropout and supply.blackout_share settings.")
NOVA2_NUMBER("source.<source>.swells", 0, 0, 1000, 0, "", "<source> swells", "supply.model=sources",
             "<source>: swells starting per source in a million ticks (4.3).")
NOVA2_MEMBER("source", "steady", "Steady sources", "Supply")
NOVA2_MEMBER("source", "daylight", "Daylight sources", "Supply")
NOVA2_MEMBER("source", "gusty", "Gusty sources", "Supply")
NOVA2_DEFAULT("source.daylight.share", 0.5)
NOVA2_DEFAULT("source.daylight.voltage", 1250)
NOVA2_DEFAULT("source.daylight.sag.min", 50)
NOVA2_DEFAULT("source.daylight.sag.max", 300)
NOVA2_DEFAULT("source.daylight.night", 0.2)
NOVA2_DEFAULT("source.daylight.swing", 0.25)
NOVA2_DEFAULT("source.gusty.voltage", 1350)
NOVA2_DEFAULT("source.gusty.sag.min", 50)
NOVA2_DEFAULT("source.gusty.sag.max", 300)
NOVA2_DEFAULT("source.gusty.swing", 0.5)
NOVA2_DEFAULT("source.gusty.swing_period", 2000)
NOVA2_DEFAULT("source.gusty.swells", 0.5)
// The voltage solve (4.1, 6.1, appendix C 2-3; build-plan-gate4.md step 4).
NOVA2_NUMBER("grid.link_resistance", 2.5, 0.001, 1000000, 0, "Supply", "Link resistance", "supply.model=sources",
             "A usual proximity link's resistance, in milliohms: a link of average length on unvaried ground. "
             "A link's own is this times its length, its field, its water and any border it crosses (6.1).")
NOVA2_NUMBER("grid.drain", 0.3, 0, 1000, 0, "Supply", "Shore drain", "supply.model=sources",
             "What a place wholly on the shore loses to the water at 1,000 mV, in watts (6.1). A place's drain is "
             "this times its share of water, and rises with its voltage squared. 0: no drains.")
NOVA2_NUMBER("grid.tolerance", 0.1, 0, 1000, 0, "Supply", "Solve tolerance", "supply.model=sources",
             "The voltage solve ends at the sweep that moves no place by more than this, in millivolts "
             "(appendix C).")
NOVA2_WHOLE("grid.sweeps", 64, 1, 100000, 0, "Supply", "Solve sweeps", "supply.model=sources",
            "The most sweeps the voltage solve makes in a tick. A solve that reaches it keeps what its last sweep "
            "gave, and is counted.")
// ---------------------------------------------------------------------------
// Heat (design.md 5, 7.7, appendix C 7; build-plan-gate4.md steps 5 and 6)
// ---------------------------------------------------------------------------
NOVA2_FLAG("heat.on", true, 0, "Heat", "Heat", "",
           "Heat (5): each device's temperature and its place's, leakage, heat trips, memory fading faster when "
           "hot, and cooling devices. Off, every temperature stays the outside one (hw.temperature), as before "
           "gate 4.")
NOVA2_NUMBER("heat.<part>.limit", 100, -273, 10000, 0, "", "<part> heat limit", "heat.on",
             "<part>: the temperature it trips at, in degrees C (4.4, 7.1). It's off until it has cooled by "
             "heat.cooldown.")
NOVA2_NUMBER("heat.<part>.sink", 0.1, 0, 10000, 0, "", "<part> heatsink", "heat.on",
             "<part>: its heatsink rate, in watts for each degree it is above its place (5).")
NOVA2_NUMBER("heat.<part>.capacity", 2, 0.000001, 1000000000, 0, "", "<part> heat capacity", "heat.on",
             "<part>: its heat capacity, in watt-ticks a degree. Its temperature follows its draw within about "
             "the capacity divided by the heatsink rate, in ticks.")
NOVA2_MEMBER("part", "p", "P core", "Heat")
NOVA2_MEMBER("part", "e", "E core", "Heat")
NOVA2_MEMBER("part", "memory", "Memory device", "Heat")
NOVA2_MEMBER("part", "disk", "Disk", "Heat")
NOVA2_MEMBER("part", "network", "Network device", "Heat")
NOVA2_MEMBER("part", "cooler", "Cooling device", "Heat")
NOVA2_DEFAULT("heat.p.sink", 0.98)
NOVA2_DEFAULT("heat.p.capacity", 19.6)
NOVA2_DEFAULT("heat.e.sink", 0.124)
NOVA2_DEFAULT("heat.e.capacity", 2.48)
NOVA2_DEFAULT("heat.memory.limit", 85)
NOVA2_DEFAULT("heat.disk.limit", 70)
NOVA2_DEFAULT("heat.network.limit", 85)
NOVA2_DEFAULT("heat.cooler.limit", 85)
NOVA2_DEFAULT("heat.cooler.sink", 1)
NOVA2_DEFAULT("heat.cooler.capacity", 20)
NOVA2_NUMBER("heat.cooldown", 10, 0, 1000, 0, "Heat", "Cool-down", "heat.on",
             "How far below its heat limit, in degrees, a tripped device must cool before it comes back (4.4). It "
             "needs no off-time.")
NOVA2_NUMBER("heat.leakage", 0.01, 0, 1, 0, "Heat", "Leakage", "heat.on",
             "How much more a device draws for each degree it is above the outside temperature, as a share (5). "
             "Cooling devices aside.")
NOVA2_NUMBER("heat.place.cooling", 2.5, 0, 100000, 0, "Heat", "Place cooling", "heat.on",
             "A usual place's cooling rate, in watts for each degree it is above the outside (5). A place's own "
             "is this times its cooling field's value, and more on the coast (heat.coast).")
NOVA2_NUMBER("heat.place.capacity", 1250, 0.000001, 1000000000, 0, "Heat", "Place heat capacity", "heat.on",
             "A place's heat capacity, in watt-ticks a degree.")
NOVA2_NUMBER("heat.coast", 1, 0, 1000, 0, "Heat", "Coast factor", "heat.on",
             "How much better a place cools by the water: its rate is multiplied by 1 + this times its share of "
             "water (5).")
NOVA2_NUMBER("heat.spread", 0.25, 0, 100000, 0, "Heat", "Heat between places", "heat.on",
             "The heat a proximity link of average length carries over land, in watts for each degree between "
             "its ends (5). A longer link carries less. None crosses water. 0: none.")
NOVA2_NUMBER("heat.fade.from", 45, -273, 10000, 0, "Heat", "Fading from", "heat.on",
             "The memory device temperature, in degrees C, above which its cells fade faster (5).")
NOVA2_NUMBER("heat.fade.halving", 10, 0.01, 10000, 0, "Heat", "Fading halves each", "heat.on",
             "A memory cell's retention halves for each this many degrees its device is above heat.fade.from.")
// Cooling devices (7.7; build-plan-gate4.md step 6).
NOVA2_NUMBER("cooler.share", 0.333333, 0, 1, 0, "Heat", "Places with cooling", "heat.on",
             "The share of places that have a cooling device (7.7): those the coolers' field picks, highest "
             "first. In a world with no map fields each place has this chance of one.")
NOVA2_NUMBER("cooler.start", 40, -273, 10000, 0, "Heat", "Cooling starts at", "heat.on",
             "The place's temperature, in degrees C, at which its cooling device starts.")
NOVA2_NUMBER("cooler.top", 80, -273, 10000, 0, "Heat", "Cooling full at", "heat.on",
             "The place's temperature at which its cooling device is at its full rate and full draw. Both rise "
             "evenly from nothing at cooler.start.")
NOVA2_NUMBER("cooler.rate", 2.5, 0, 100000, 0, "Heat", "Cooling device's rate", "heat.on",
             "A cooling device's cooling at full, in watts for each degree its place is above the outside, added "
             "to the place's own.")
NOVA2_NUMBER("cooler.draw", 3, 0, 100000, 0, "Heat", "Cooling device's draw", "heat.on",
             "A cooling device's draw at full, in watts.")
NOVA2_WHOLE("cooler.threshold", 600, 1, 10000, 0, "Heat", "Cooling device's threshold", "heat.on",
            "A cooling device's lowest working voltage, in millivolts. Below it, it cuts out and cools nothing.")
NOVA2_WHOLE("cooler.maximum", 1300, 1, 10000, 0, "Heat", "Cooling device's maximum", "heat.on",
            "A cooling device's maximum voltage, in millivolts: above it its draw rises as (voltage / maximum) "
            "squared (4.4).")
// ---------------------------------------------------------------------------
// Wear and visits (design.md 7.4, 7.6, 7.8; build-plan-gate5.md). Every
// strength here has an off; with all of them off the world is gate 4's.
// ---------------------------------------------------------------------------
NOVA2_NUMBER("wear.rate", 1, 0, 1000, 0, "Wear and visits", "Wear rate", "",
             "How fast every device wears at the reference conditions, in ticks of wear a tick (7.4, appendix "
             "C's w0). 0: nothing wears and nothing fails.")
NOVA2_NUMBER("wear.heat", 1, 0, 100, 0, "Wear and visits", "Wear with heat", "",
             "Doublings of a device's wear for each 10 degrees it is above wear.reference, and halvings below "
             "it (7.4). 0: heat doesn't change wear.")
NOVA2_NUMBER("wear.reference", 45, -273, 10000, 0, "Wear and visits", "Wear's reference", "",
             "The temperature, in degrees C, at which a device wears a tick a tick at a rate of 1.")
NOVA2_NUMBER("wear.voltage", 1, 0, 100, 0, "Wear and visits", "Wear with overvoltage", "",
             "Doublings of a device's wear for each 100 mV its place's voltage is over the device's maximum "
             "(7.4). A swell wears, and a strike breaks a device at once. 0: overvoltage doesn't wear.")
NOVA2_WHOLE("wear.threshold_rise", 50, 0, 1000, 0, "Wear and visits", "Threshold's rise", "",
            "How far a device's threshold has risen at the end of its life, in millivolts; in proportion to its "
            "wear before (7.4). A worn core is slower and cuts out sooner. SENSE still reads the nominal "
            "threshold.")
NOVA2_WHOLE("wear.<wpart>.endurance", 200000000, 1, 1000000000000, 0, "", "<wpart> endurance", "",
            "<wpart>: its endurance, in ticks at the reference conditions (7.4). Each device's own is drawn from "
            "half to twice this, evenly on a log scale; it fails when its wear reaches it.")
NOVA2_MEMBER("wpart", "p", "P core", "Wear and visits")
NOVA2_MEMBER("wpart", "e", "E core", "Wear and visits")
NOVA2_MEMBER("wpart", "memory", "Memory device", "Wear and visits")
NOVA2_MEMBER("wpart", "disk", "Disk", "Wear and visits")
NOVA2_MEMBER("wpart", "network", "Network device", "Wear and visits")
NOVA2_MEMBER("wpart", "cooler", "Cooling device", "Wear and visits")
NOVA2_NUMBER("wear.weak.share", 0.001, 0, 1, 0, "Wear and visits", "Weak cells", "",
             "The share of a memory device's cells that are weak at the end of its life: its leakiest, by its "
             "leak ranks, their number rising in proportion to its wear (7.4). 0: none.")
NOVA2_NUMBER("wear.weak.chance", 0.000125, 0, 0.5, 0, "Wear and visits", "Weak cell's chance", "",
             "The chance a tick that a weak cell loses its bit, reading back random, while its device is on "
             "(7.4): about once a refresh window.")
NOVA2_WHOLE("disk.block", 512, 16, 1048576, 0, "Wear and visits", "Disk block", "",
            "A disk block, in bytes: the unit of a disk's wear by writes (7.4). The boot block's size by default.")
NOVA2_WHOLE("disk.block_endurance", 10000, 0, 4000000000, 0, "Wear and visits", "Block endurance", "",
            "How many writes a disk block takes (7.4). Each block's own is drawn from half to twice this, evenly "
            "on a log scale. A disk whose first block passes its endurance has failed. 0: blocks don't wear.")
NOVA2_NUMBER("disk.worn_flip", 0.001, 0, 0.5, 0, "Wear and visits", "Worn block's flips", "",
             "Past a block's endurance, the chance that each bit written to it lands wrong, times its writes "
             "past the endurance over disk.block_endurance, and at most one in two (7.4).")
NOVA2_FLAG("os.levelling", false, 0, "Wear and visits", "Wear levelling", "",
           "The OS spreads a disk's writes over its blocks (7.4): a block whose writes pass the least-written "
           "block's by os.levelling_gap swaps contents and places with it, two more writes at no thread's cost. "
           "Off, an offset keeps its block for life.")
NOVA2_WHOLE("os.levelling_gap", 100, 1, 1000000000, 0, "Wear and visits", "Levelling gap", "os.levelling",
            "How far a block's writes may pass the least-written block's before wear levelling swaps the two.")
NOVA2_WHOLE("visit.mean", 1000000, 0, 1000000000000, 0, "Wear and visits", "Visit interval", "",
            "The mean number of ticks between a place's maintenance visits (7.8). A visit replaces every device "
            "that failed since the last; its times are drawn from the seed before the run. 0: never, and the "
            "world wears out.")
NOVA2_NUMBER("visit.regularity", 0, 0, 1, 0, "Wear and visits", "Visit regularity", "visit.mean",
             "0: the gaps between a place's visits are random (exponential). 1: exactly periodic. Between, a "
             "gap is the mean times this plus the rest of it times a random draw.")
NOVA2_NUMBER("visit.density_tie", 0, -8, 8, 0, "Wear and visits", "Visits follow density", "visit.mean",
             "How far towns are visited more often (7.8): a place's mean interval is the world's times (its "
             "density over the average) to the power of minus this. 0: the same everywhere.")
NOVA2_NUMBER("visit.replace_at", 1, 0, 1, 0, "Wear and visits", "Replace when worn", "visit.mean",
             "The share of its life at which a visit replaces a worn device like for like, its contents copied "
             "across, corrupted bits and all (7.8). 1: only failed devices are replaced.")
NOVA2_NUMBER("visit.respawn_share", 1, 0, 2, 0, "Wear and visits", "Respawn share", "visit.mean",
             "The share of a place's devices that must have failed for a visit to tear the place down and "
             "build it again somewhere else (7.8). 1: only wholly dead places move. Above 1: none do.")
NOVA2_WHOLE("wear.link.endurance", 200000000, 0, 1000000000000, 0, "Wear and visits", "Link endurance",
            "supply.model=sources",
            "A link's endurance, in ticks at its reference current (7.4). Each link's own is drawn from half to "
            "twice this, evenly on a log scale. A link that carries current wears by its loss; at its endurance "
            "it fails and carries nothing, power or data, until a visit to either end. 0: links don't wear.")
NOVA2_NUMBER("wear.link.current", 30, 0.001, 1000000, 0, "Wear and visits", "Link's reference current",
             "supply.model=sources",
             "The current, in amps, at which a new link wears a tick a tick (7.4). A tick's wear is the link's "
             "loss over its loss at this current when new: the current's square, times how far its resistance "
             "has risen.")
NOVA2_NUMBER("wear.link.rise", 2, 1, 1000, 0, "Wear and visits", "Link's resistance, worn", "supply.model=sources",
             "A link's resistance at the end of its life, over its resistance when new; in proportion to its "
             "wear before (7.4). So a worn link loses more, and wears faster. 1: its resistance doesn't rise.")
NOVA2_NUMBER("link.cuts", 5e-8, 0, 0.5, 0, "Wear and visits", "Cuts", "",
             "The chance a tick that a link of the average length is cut from outside (11.6); a link's own is "
             "in proportion to its length. Any link can be cut: power, data or both, and bridges. A cut link "
             "carries nothing until a visit to either end. 0: no cuts.")
NOVA2_NUMBER("strait.share", 0.5, 0, 1, 0, "Wear and visits", "Straits that part", "",
             "Straits (11.6): the chance that a strait, all the bridges between one pair of landmasses, parts "
             "and rejoins through the run. A parted strait's bridges carry nothing, and no visit mends them. "
             "Every strait starts joined. 0: none part.")
NOVA2_WHOLE("strait.spell.min", 2000000, 1, 1e15, 0, "Wear and visits", "A strait's shortest spell",
            "strait.share",
            "The shortest a strait stays joined, or parted, in ticks. Each spell's length is drawn evenly on a "
            "log scale from here to the longest, from the seed and the strait.")
NOVA2_WHOLE("strait.spell.max", 20000000, 1, 1e15, 0, "Wear and visits", "A strait's longest spell",
            "strait.share", "The longest, in ticks. Must be at least the shortest.")
// ---------------------------------------------------------------------------
// Network (11; build-plan-gate3.md step 3). The links' bandwidths and delays
// are the map's (map.bandwidth.*, map.delay.*).
// ---------------------------------------------------------------------------
NOVA2_WHOLE("net.queue", 8, 1, 1000000, 0, "Network", "Queue", "",
            "Each link end's queue, in ticks of its link's bandwidth (11.5): about a round trip's worth, and never "
            "less than net.message_max. A message that finds no room is dropped, and those waiting keep their "
            "place.")
NOVA2_NUMBER("net.corruption", 1e-5, 0, 0.5, 0, "Network", "Corruption", "",
             "The chance that each bit of a message flips for each average link length it crosses (11.3). It's "
             "silent: the message arrives as it is.")
NOVA2_WHOLE("net.message_max", 1024, 1, 1048576, 0, "Network", "Message limit", "",
            "The biggest message, in bytes (11.2).")
NOVA2_WHOLE("net.inbox", 1024, 1, 1048576, 0, "Network", "Inbox", "",
            "Each comms port's inbox, in bytes (11.5). A message that finds no room is dropped, and those waiting "
            "keep their place. It's lost at a cold start.")
NOVA2_WHOLE("net.ping_size", 8, 1, 1048576, 0, "Network", "PING size", "",
            "PING's request and echo, in bytes (11.2): small, but enough that a nearly full queue drops one.")
NOVA2_WHOLE("net.lifetime", 1000, 0, 1000000000000, 0, "Network", "Message lifetime", "",
            "How long a message whose way on is gone may wait, in ticks (11.6): its link failed, or its next "
            "place is dark. It takes a new route as soon as there is one, ahead of new arrivals, and is dropped "
            "when it has had no route for this long. 0: it's dropped at once, as before gate 5.")
// ---------------------------------------------------------------------------
// Regional events (12.2 to 12.4; build-plan-gate5.md step 7): ramps, strikes
// and the patch catalogue.
// ---------------------------------------------------------------------------
NOVA2_WHOLE("ramp.lead.min", 20, 1, 1000000, 0, "Regional events", "Shortest lead", "",
            "The shortest lead of an outside event, in ticks (12.4): a drop-out, a swell or a patch rises in a "
            "straight line over its lead, holds, and eases over as long again. Each event's lead is drawn evenly "
            "on a log scale between this and the longest.")
NOVA2_WHOLE("ramp.lead.max", 2000, 0, 1000000, 0, "Regional events", "Longest lead", "",
            "The longest lead, in ticks. 0: nothing ramps and nothing is a false alarm; events start and end at "
            "once, as before gate 5.")
NOVA2_NUMBER("ramp.false_share", 0.25, 0, 1, 0, "Regional events", "False alarms", "ramp.lead.max",
             "The share of events that are false alarms (12.4): one rises over its lead to part of its strength "
             "and eases at once; the event never comes.")
NOVA2_NUMBER("ramp.false.min", 0.1, 0, 1, 0, "Regional events", "False alarm's least peak", "ramp.lead.max",
             "The least a false alarm peaks at, as a share of the event's strength. Drawn evenly up to the most.")
NOVA2_NUMBER("ramp.false.max", 0.5, 0, 1, 0, "Regional events", "False alarm's most peak", "ramp.lead.max",
             "The most a false alarm peaks at, as a share of the event's strength.")
NOVA2_WHOLE("strike.voltage", 20000, 1, 32000, 0, "Regional events", "Strike's voltage", "supply.model=sources",
            "A strike's voltage, in millivolts (12.2). A strike is a source for one tick at the place it lands "
            "on; strikes land only inside storms (patch.storm).")
NOVA2_NUMBER("strike.resistance", 3, 0.001, 1000000, 0, "Regional events", "Strike's resistance",
             "supply.model=sources",
             "A strike's internal resistance, in milliohms: how much charge it carries. 3 gives about ten times "
             "a large source's rated current.")
NOVA2_NUMBER("patch.speed", 2, 0, 1000, 0, "Regional events", "Patches' speed", "",
             "The fastest a patch moves, in links a day (the map's average link length). A moving patch's speed "
             "is drawn evenly up to this.")
NOVA2_NUMBER("patch.still", 0.3333333333333333, 0, 1, 0, "Regional events", "Patches that stay put", "",
             "The share of patches that don't move.")
NOVA2_NUMBER("patch.turn", 0.125, 0, 0.5, 0, "Regional events", "Patches' turn", "",
             "The most a moving patch's heading turns a day, either way, as a share of a full turn. Each patch's "
             "own is drawn evenly, and stays the same through its life.")
NOVA2_WHOLE("patch.life.min", 2000, 1, 100000000, 0, "Regional events", "Patches' shortest life", "",
            "The shortest time a patch is at full strength, in ticks, between its rise and its ease. Each "
            "patch's own is drawn evenly on a log scale up to the longest.")
NOVA2_WHOLE("patch.life.max", 50000, 1, 100000000, 0, "Regional events", "Patches' longest life", "",
            "The longest time a patch is at full strength, in ticks.")
NOVA2_NUMBER("patch.<patch>.rate", 0.5, 0, 1000000, 0, "", "<patch> rate", "",
             "<patch>: how many form in a million ticks for each 100 places (12.3). 0: none.")
NOVA2_NUMBER("patch.<patch>.radius.min", 1, 0.001, 1000, 0, "", "<patch> least radius", "",
             "<patch>: the smallest radius, in links. A patch is at full strength at its centre and falls in a "
             "straight line to nothing at its radius. Drawn evenly up to the largest.")
NOVA2_NUMBER("patch.<patch>.radius.max", 3, 0.001, 1000, 0, "", "<patch> most radius", "",
             "<patch>: the largest radius, in links.")
NOVA2_NUMBER("patch.<patch>.strength.min", 1, 0, 1000000, 0, "", "<patch> least strength", "",
             "<patch>: the least it scales by at its centre. A storm: the chance a tick that a strike lands on a "
             "place. A swell: the factor on sources' voltage. A brownout: the share of sources' capacity left. A "
             "heat wave: degrees added to the outside temperature its places cool toward. An outage: the factor "
             "on a link's chance of a cut. A gust: the factor on the replication current's strength (12.6). "
             "Drawn evenly up to the most.")
NOVA2_NUMBER("patch.<patch>.strength.max", 1, 0, 1000000, 0, "", "<patch> most strength", "",
             "<patch>: the most it scales by at its centre.")
NOVA2_MEMBER("patch", "storm", "Storms", "Regional events")
NOVA2_MEMBER("patch", "swell", "Swell patches", "Regional events")
NOVA2_MEMBER("patch", "brownout", "Brownouts", "Regional events")
NOVA2_MEMBER("patch", "heatwave", "Heat waves", "Regional events")
NOVA2_MEMBER("patch", "outage", "Network outages", "Regional events")
NOVA2_MEMBER("patch", "gust", "Gusts", "Regional events")
NOVA2_DEFAULT("patch.storm.strength.min", 0.001)
NOVA2_DEFAULT("patch.storm.strength.max", 0.001)
NOVA2_DEFAULT("patch.swell.strength.min", 1.1)
NOVA2_DEFAULT("patch.swell.strength.max", 1.4)
NOVA2_DEFAULT("patch.brownout.strength.min", 0.1)
NOVA2_DEFAULT("patch.brownout.strength.max", 0.5)
NOVA2_DEFAULT("patch.heatwave.strength.min", 5)
NOVA2_DEFAULT("patch.heatwave.strength.max", 15)
NOVA2_DEFAULT("patch.outage.strength.min", 1000)
NOVA2_DEFAULT("patch.outage.strength.max", 1000)
NOVA2_DEFAULT("patch.gust.rate", 20)
NOVA2_DEFAULT("patch.gust.strength.min", 3)
NOVA2_DEFAULT("patch.gust.strength.max", 10)
// ---------------------------------------------------------------------------
// The replication current (12.6; build-plan-gate5.md step 9).
// ---------------------------------------------------------------------------
NOVA2_NUMBER("current.strength", 1, 0, 1000000, 0, "Regional events", "Current's strength", "",
             "The replication current (12.6): background copying that no program asks for. How many blocks a "
             "place copies down its links in a day, at its noon and at a season's mean strength. Each block is "
             "copied from a random address in the place's memory, goes down one data link that points along the "
             "current's heading, and is written into the next place's reset region. It never waits: a full "
             "queue, a dark place or a failed link drops it. 0: no current.")
NOVA2_WHOLE("current.block", 256, 1, 1048576, 0, "Regional events", "Current's block", "current.strength",
            "The size of a block the current copies, in bytes. A block is a message of its size, so at most "
            "net.message_max.")
NOVA2_WHOLE("current.season", 500000, 1, 1e15, 0, "Regional events", "Current's season", "current.strength",
            "How long the current takes to ease from one heading and strength to the next, in ticks. Both are "
            "drawn from the seed for each season.")
NOVA2_NUMBER("current.turn", 0.125, 0, 0.5, 0, "Regional events", "Current's turn", "current.strength",
             "The most the current's heading turns from one season to the next, either way, as a share of a "
             "full turn. The first season's heading is drawn evenly over the full turn.")
NOVA2_NUMBER("current.strength.min", 0.5, 0.000001, 1000000, 0, "Regional events", "Season's least strength",
             "current.strength",
             "The least a season's strength is, as a share of the mean. Drawn evenly on a log scale up to the "
             "most.")
NOVA2_NUMBER("current.strength.max", 2, 0.000001, 1000000, 0, "Regional events", "Season's most strength",
             "current.strength", "The most a season's strength is, as a share of the mean.")
NOVA2_NUMBER("current.night", 0.2, 0, 1, 0, "Regional events", "Current's night", "current.strength",
             "The current's strength at a place's midnight, as a share of its noon's: it follows each place's "
             "day as the supply does. 1: no day.")
// ---------------------------------------------------------------------------
// Test only (hidden): one entry of every kind and flag, so the keys suite can
// exercise the registry before the engine has settings of each sort.
// ---------------------------------------------------------------------------
NOVA2_FLAG("test.switch", false, kKeyHidden, "Run and output", "Test switch", "", "A test-only switch.")
NOVA2_WHOLE("test.limit", 64, 1, 1024, kKeyHidden | kKeyMaxComputed, "Run and output", "Test limit", "test.switch",
            "A test-only whole number whose maximum the call site supplies.")
NOVA2_WHOLE("test.count", 8, 0, 1000, kKeyHidden | kKeyComputed, "Run and output", "Test count", "",
            "A test-only whole number whose default the call site supplies.")
NOVA2_WHOLE("test.offset", -1, -1, 100, kKeyHidden, "Run and output", "Test offset", "",
            "A test-only whole number that can be negative.")
NOVA2_NUMBER("test.rate", 0.25, 0, 1, kKeyHidden | kKeyOpenMin | kKeyLog, "Run and output", "Test rate", "",
             "A test-only number above 0 and at most 1.")
NOVA2_CHOICE("test.mode", "plain", "plain fancy", kKeyHidden, "Run and output", "Test mode", "",
             "A test-only choice.")
NOVA2_TEXT("test.label", "", kKeyHidden, "Run and output", "Test label", "", "A test-only text.")
NOVA2_NUMBER("test.<side>.weight", 1, 0, 10, kKeyHidden, "", "<side> weight", "",
             "A test-only family: the <side> weight.")
NOVA2_MEMBER("side", "left", "Left", "Run and output")
NOVA2_MEMBER("side", "right", "Right", "Run and output")
NOVA2_DEFAULT("test.right.weight", 3)
