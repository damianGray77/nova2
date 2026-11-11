# Nova v2 Design Review

**Prepared for:** Damian and the Nova v2 design reviewers  
**Prepared by:** Codex  
**Date:** November 11, 2026  
**Source:** E:/Projects/nova2/design.md, the 2,002-line revision reviewed on November 11, 2026

This report preserves the independent review's 25 main findings, nine smaller corrections, and recommended next steps. References identify sections and original line numbers so the findings can be compared with other reviews. The source design and its history have not been edited.

**Assessment:** Remove the claim that the design is "complete apart from numbers." Several unresolved choices change what programs can do, how they reproduce, and whether the intended ecology follows from the rules. The findings below distinguish concrete contradictions from missing specifications and experimental risks.

The most consequential concern is finding 2: whether living populations actually create the resource pressure the ecology depends on. Resolve that alongside the execution and ownership rules before investing further in environmental detail.

1. **The power model is nonlinear, but the specified solver is linear.**

   The graph's resistances are linear; the devices are not. Working power scales with voltage squared times speed, and speed scales with voltage minus threshold. Consequently, current depends nonlinearly on voltage. Leakage, cut-outs, and cooling add further dependencies.

   A single linear solve needs an explicit approximation: previous-tick loads, frozen conductances, or iterations toward a consistent solution. Those alternatives produce different brownouts and cascades. Specify the equations, update order, convergence/failure behavior, and treatment of disconnected components without sources. Fixed-point arithmetic alone does not settle this.

   References: [Section 4.1, line 289](E:/Projects/nova2/design.md:289); [device draw, line 632](E:/Projects/nova2/design.md:632); [speed, line 748](E:/Projects/nova2/design.md:748).

2. **The expected relationship between population, power consumption, and heat is missing a key link.**

   Every idle CPU core already executes the reset soup. Ownerless code cannot SLEEP. Memory draws power regardless of ownership. Therefore, an empty place can already have busy cores and fully powered memory.

   Replacing soup execution with living programs does not necessarily increase CPU activity, and adding threads to occupied cores mostly redistributes existing activity. This weakens the stated mechanisms by which crowds cause sag and the world heats as life grows.

   Before accepting those ecological predictions, work through three cases: an empty place, one replicator, and a crowded place. Identify exactly which device draws increase. Overclocking and accelerator use remain mechanisms, but ordinary population growth currently lacks a clear one.

   References: [idle execution, line 1373](E:/Projects/nova2/design.md:1373); [ownerless calls, line 1078](E:/Projects/nova2/design.md:1078); [power sharing, line 397](E:/Projects/nova2/design.md:397).

3. **Time needs one authoritative definition.**

   Tick length is described in instructions per core, execution costs are cycles, scheduling uses cycle quanta, and clock changes alter speed. Meanwhile, heat, wear, bandwidth, mutation, and external events advance per tick.

   Specify a fixed simulated duration per tick and how voltage and SETCLOCK translate that duration into work. Also specify whether scheduler fairness measures cycles or elapsed simulated time.

   Otherwise changing tick length or quantum size can accidentally change ecology, not merely simulation performance. Rowhammer's per-tick window makes this especially visible.

   References: [engine timing, line 129](E:/Projects/nova2/design.md:129); [costs, line 971](E:/Projects/nova2/design.md:971); [rounds, line 1211](E:/Projects/nova2/design.md:1211).

4. **The instruction encoding does not preserve alignment under mutation.**

   The claim that a flipped bit changes one instruction without changing how the rest decode is false when # consumes the next byte. A mutation from a one-byte operand to # can consume what was previously the following instruction.

   Either accept variable-length decoding and revise the claim, or change the encoding. This matters to the argument about mutation robustness.

   Reference: [Section 8.1, line 916](E:/Projects/nova2/design.md:916).

5. **GPU lanes currently have no successful way to exit.**

   SYS on a GPU faults and ends the launch. EXIT exists only as an OS call. Yet GLAUNCH completes successfully when every lane executes EXIT.

   Make EXIT an explicit GPU exception, provide another termination mechanism, or revise the GPU execution model. As written, these rules contradict one another.

   References: [GPU fault, line 958](E:/Projects/nova2/design.md:958); [EXIT call, line 1056](E:/Projects/nova2/design.md:1056); [lane termination, line 1248](E:/Projects/nova2/design.md:1248).

6. **The 14-byte replicator is an unverified sketch, not yet a sound design anchor.**

   With arguments assigned in A-D in table order, ALLOC expects size in A, permission in B, and device in C. The sketch uses B as the source-code address and does not deliberately establish permission or device. Whether it works therefore depends on unspecified argument normalization and initial register values.

   The "two more bytes for PROTECT" claim has the same problem: its required arguments must actually be present. IP's precise value and CPY's register effects also matter.

   Give the sketch a register-by-register trace, including execution by its child at a different address. Until then, page sizes and discoverability arguments should not rely on 14 bytes.

   References: [sketch, line 964](E:/Projects/nova2/design.md:964); [call arguments, line 1052](E:/Projects/nova2/design.md:1052).

7. **The transition from idle code to program needs a complete lifecycle.**

   First ALLOC creates an owner, but the idle run is not a thread. Its SPAWN into the new allocation creates another program. What then keeps the original owner alive, and exactly when are its remaining allocations reclaimed?

   The text explicitly covers an idle run ending before it starts a thread, but its child's thread belongs to a different owner. It also leaves unclear whether failed ALLOC creates an owner, and what changes if the first successful allocation is disk.

   A small state-transition table would resolve this more reliably than prose distributed across three sections.

   References: [ownership, line 1151](E:/Projects/nova2/design.md:1151); [idle-run cleanup, line 1166](E:/Projects/nova2/design.md:1166); [birth sequence, line 1397](E:/Projects/nova2/design.md:1397).

8. **SPAWN's identity rule becomes ambiguous when programs execute from multiple allocations.**

   "Inside the caller's own running code's allocation" could mean the allocation containing the current IP or a persistent main-image allocation.

   If it means current IP, calling a helper in another allocation can change whether the same SPAWN creates a thread or an offspring. More seriously, an allocation can be gifted while another parent thread still executes in it.

   Define the reference allocation, whether active allocations may transfer, and what happens to existing threads and outstanding operations when ownership changes.

   References: [SPAWN, line 1055](E:/Projects/nova2/design.md:1055); [program identity, line 1155](E:/Projects/nova2/design.md:1155).

9. **Page access rights and allocation ownership need separate definitions.**

   Allocations from multiple programs share a page, and any resident program can access its neighbors within that page. Elsewhere, touching memory "the thread doesn't own" faults, and pages are described as having an owner.

   A shared page cannot have one ordinary program owner. The likely intended model is allocation ownership plus a set of programs allowed to access the page, but that needs to be explicit.

   Include what happens after a program frees its last allocation in a page, whether executable permission also permits reading, and how multi-page operations stop or fault. These rules define the actual possibilities for parasitism.

   References: [faults, line 958](E:/Projects/nova2/design.md:958); [shared-page protection, line 1104](E:/Projects/nova2/design.md:1104).

10. **Disk persistence does not yet provide a reliable recovery path beyond the boot block.**

    When a program dies, its disk allocations are freed. A cold-start loader is a new owner and cannot DREAD the previous owner's blocks without obtaining rights to them. ALLOC cannot request a particular old range.

    Additionally, ownerless boot code cannot DREAD until its first ALLOC. Consequently, the proposed second-stage loader needs more than a disk address: it needs a defined way to regain access to the saved data.

    Choose the intended persistent-storage contract. Keeping bytes is different from keeping a usable reference and permission to read them.

    References: [disk access, line 1130](E:/Projects/nova2/design.md:1130); [cleanup, line 1166](E:/Projects/nova2/design.md:1166); [second-stage boot, line 1444](E:/Projects/nova2/design.md:1444).

11. **The physical storage of OS state is unspecified, despite being essential to survival.**

    Queued threads survive because their saved state is in memory. That requires decisions about where registers, stacks, handler frames, and pending-call state live, which device failure affects them, and what corruption does to them.

    Allocation metadata, ownership records, and scheduler metadata also need an explicit status: physically vulnerable storage or protected infrastructure.

    Gradual memory fading creates another gap: what exact condition means the reset region has "lost its contents" and triggers a cold start? A continuously increasing error rate does not automatically provide that discrete event.

    References: [cut-out survival, line 813](E:/Projects/nova2/design.md:813); [cold start, line 1440](E:/Projects/nova2/design.md:1440).

12. **Replacing memory with a differently sized device can invalidate unrelated live addresses.**

    Memory devices occupy one flat address space, laid end to end. Failed devices receive fresh hardware draws, potentially with different sizes.

    If replacing the first device shifts the base of the second, programs on the second lose valid pointers despite its never failing. Disks have a related addressing problem.

    Reserve stable address ranges, constrain replacement sizes, or specify another mapping. Also define how the reset region moves to a surviving memory device without overwriting allocations already there.

    References: [address layout, line 580](E:/Projects/nova2/design.md:580); [replacement, line 856](E:/Projects/nova2/design.md:856); [reset-region relocation, line 1391](E:/Projects/nova2/design.md:1391).

13. **The instruction and OS tables need an operational appendix.**

    Several omissions affect behavior rather than numerical tuning: arithmetic overflow, signed division, negative and oversized shifts, negative modulo, exact IP semantics, stack word width, CPY overlap and partial completion, and initial registers and stack state for each launch.

    The call interface also needs to say where RECV returns the sender ID when A already returns the byte count; how a timer is armed when SETHANDLER only takes kind and address; how a fault handler changes the interrupted context so HRET does not simply repeat the fault; and how NMATMUL's addresses, dimensions, output, and numeric format fit its calling convention.

    These details determine whether C++ and HLSL can produce identical results.

    References: [ISA, line 914](E:/Projects/nova2/design.md:914); [calls, line 1040](E:/Projects/nova2/design.md:1040); [interrupts, line 1233](E:/Projects/nova2/design.md:1233).

14. **Long operations and asynchronous devices need explicit interruption and scheduling rules.**

    CPY can exceed a quantum. Disk operations and accelerators can outlive their caller's current turn. Another thread can modify, free, or transfer their buffers while they run.

    Specify when inputs are read, when outputs become visible, whether permissions are rechecked, and what partial work survives failure. Define accelerator fairness so a nonterminating kernel cannot monopolize a device unless that is an intentional property.

    Host-batched disk operations on the proposed GPU backend must use the same simulated completion semantics as the CPU backend; host timing cannot decide the world.

    References: [backend disk handling, line 157](E:/Projects/nova2/design.md:157); [CPY, line 938](E:/Projects/nova2/design.md:938); [blocking operations, line 1292](E:/Projects/nova2/design.md:1292).

15. **Migration requires spare CPU time, not just spare memory, and can be completely excluded.**

    A place with every core occupied by runnable threads never executes arrivals. That remains true even if it has abundant free memory and an arriving program would be much better adapted.

    This is stronger than "colonisation goes to open hardware": CPU saturation creates an absolute establishment barrier. It may be desirable, but it deserves a named hypothesis and test, especially for competitive replacement and recovery from resident junk loops.

    Separately, "port 0 never refuses" needs finite-buffer semantics: payload placement, oversized payloads, overlapping arrivals, and whether queued entry points remain valid after subsequent overwrites.

    Reference: [Section 10.4, line 1413](E:/Projects/nova2/design.md:1413).

16. **The network mixes several models that need reconciliation.**

    "Least delay" includes message size and queueing, but route weights are fixed. Define whether routing minimizes only propagation delay or uses some other static estimate.

    The bandwidth term size / bottleneck bandwidth suggests pipelining. If forwarding waits for a complete message at each hop, serialization delay accumulates differently. Choose the forwarding model explicitly.

    Rerouting also moves old messages ahead of new arrivals into bounded queues: what happens when they do not fit? Where are stranded messages stored? Those decisions are necessary before the no-allocation engine and memory bounds can be specified.

    References: [delay and routing, line 1536](E:/Projects/nova2/design.md:1536); [queues, line 1576](E:/Projects/nova2/design.md:1576); [rerouting, line 1609](E:/Projects/nova2/design.md:1609).

17. **Respawning a place is a much larger state transition than the document acknowledges.**

    Moving a place and reusing its ID raises unanswered questions. Do existing sources stay at their original geographic positions or follow the ID? What happens to bridges declared fixed for the run? Can old in-flight messages arrive at the new location? Which link wear, queues, and maintenance schedules survive? Are minimum spacing and source catchment areas recomputed? How do observers distinguish successive incarnations of the same place?

    Source relocation is especially consequential because it could make a supposedly external supply move in response to program-induced hardware failures. Define this transition before calling its cost small.

    References: [fixed bridges, line 499](E:/Projects/nova2/design.md:499); [respawning, line 890](E:/Projects/nova2/design.md:890).

18. **The parasite detector misses the document's main example of hijacking.**

    If an attacker overwrites a victim's code in place, the victim's thread and allocation still have the same owner. The proposed owner-mismatch detector therefore misses the hijack.

    Conversely, a thread intentionally executing another allocation's code need not be parasitic. Shared pages also make "the owner of the page" ambiguous.

    The proposed signal can be called cross-owner execution. Detecting hijacking requires additional evidence about code changes or provenance.

    References: [hijacking model, line 1171](E:/Projects/nova2/design.md:1171); [detector, line 1875](E:/Projects/nova2/design.md:1875).

19. **Genome equals birth allocation is useful bookkeeping, but an unreliable measure of hereditary identity.**

    Reset-region founders have no owned starting allocation. Strict-node programs must allocate whole pages, so otherwise identical code may carry different dirty padding and receive different hashes. Programs can also modify their code or depend on data in other allocations.

    Keep the exact birth image as a factual record, but distinguish it from the representation used for similarity, replication detection, and novelty. Otherwise allocator residue can look like evolution, and real evolution after birth can go unseen.

    References: [genome definition, line 1845](E:/Projects/nova2/design.md:1845); [replicator detector, line 1882](E:/Projects/nova2/design.md:1882).

20. **The promised observer analyses require evidence that is not currently archived.**

    Birth/death records, aggregate message counts, and occasional state samples are insufficient to establish that behavior followed received data, that code came from a corpse, or that GPU use improved fitness.

    Likewise, arbitrary new detectors cannot necessarily be rerun over an archive without rerunning the simulation. They can only use information that was retained.

    For each detector, list its required observations, storage cost, and whether its output is an observation, heuristic, or causal claim. This will expose expensive instrumentation before implementation.

    References: [facts and archive, line 1803](E:/Projects/nova2/design.md:1803); [detectors, line 1873](E:/Projects/nova2/design.md:1873).

21. **The origin test needs acceptance criteria beyond a replicator arising.**

    Distinguish a copying sequence appearing; its producing viable offspring; those offspring reproducing across successive generations; a lineage establishing despite faults and competition; and continued adaptation after resources become crowded.

    Set a compute budget, seed count, and success criterion before tuning. Otherwise an isolated lucky event can validate a world that almost never supports sustained life.

    This distinction has direct research support: a 2026 BFF follow-up separates initial emergence from subsequent spread and shows that preventing takeover need not prevent replicators from appearing. It does not establish that Nova's substrate will work.

    References: [origin test, line 1466](E:/Projects/nova2/design.md:1466); [Knierim et al., BFF: Simple explanations for complex phenomena, 2026](https://arxiv.org/html/2607.01483v1).

22. **Nothing downstream being testable until a replicator arises is unnecessarily restrictive.**

    The no-seeded-ancestor constraint still allows nonreplicating instruction fixtures, allocation tests, disk recovery scenarios, queue tests, and physical conservation checks.

    Those tests are prerequisites for interpreting an origin failure. Otherwise "no life appeared" could mean an inhospitable substrate or a broken SPAWN implementation.

    Explicitly separate tests of mechanics from experiments about spontaneous life. Include snapshot/resume equivalence and thread-count equivalence in the former.

    References: [testing restriction, line 1355](E:/Projects/nova2/design.md:1355); [determinism, line 257](E:/Projects/nova2/design.md:257).

23. **Performance estimates omit several potentially dominant costs.**

    The interpreter is only part of this engine. The complete workload includes electrical solves, topology changes, routing, thermal updates, memory corruption, allocation, observers, and archive output.

    Instructions per second also becomes ambiguous when one CPY instruction performs thousands of byte operations. Report instruction throughput alongside bytes processed, simulated time, and observed reproductive throughput.

    Storage deserves a budget too: at an illustrative 10,000 births plus 10,000 deaths per second, 32 bytes per event produces about 55 GB/day before compression, excluding genomes and samples. Append-only logging does not make that cost negligible.

    References: [performance estimate, line 118](E:/Projects/nova2/design.md:118); [routing cost, line 1622](E:/Projects/nova2/design.md:1622); [log volume, line 1830](E:/Projects/nova2/design.md:1830).

24. **Several expensive mechanisms have not yet earned their place under the document's own guardrail.**

    The strongest candidates for deferral are the NPU, detailed divergent GPU execution, optional wear levelling, and geographic relocation of dead places.

    Each adds substantial semantics and testing. "Selection decides whether it is used" does not justify implementing hardware whose useful workload has not been demonstrated. The NPU is the clearest example: the document already admits that matrix-shaped work may not exist.

    For each candidate, state the experiment it enables or the observed failure it fixes. Keeping a future extension point is much cheaper than implementing its whole behavior now.

    References: [guardrail, line 194](E:/Projects/nova2/design.md:194); [NPU justification, line 1259](E:/Projects/nova2/design.md:1259).

25. **Every number being a setting encourages complexity and needs a boundary.**

    Instruction widths, register counts, ABI layouts, and invariants are different from tunable rates and catalogue values. Making all of them configurable expands the supported state space dramatically.

    Separate structural constants, experimental parameters, and derived values. Define validation constraints between parameters, such as reset-region capacity, memory size, voltage thresholds, and thermal integration limits.

    Likewise, count actual independent parameters and special-case behaviors, not just sliders or catalogue rows. Moving complexity into a table does not eliminate it.

    References: [settings, line 65](E:/Projects/nova2/design.md:65); [common hardware row, line 622](E:/Projects/nova2/design.md:622).

The following smaller corrections should also be addressed.

| Issue | Why it matters |
|---|---|
| **Permanent core failure versus temporary cut-out** | Failure says its threads run on other cores; cut-out kills the running thread. Clarify whether permanent failure also crashes the active thread before survivors migrate. [Section 7.6, line 791](E:/Projects/nova2/design.md:791). |
| **RECV's maximum length versus automatic overrun** | Input longer than the allocated buffer only overruns if the program supplies an excessive maximum, or RECV ignores that maximum. State which. Arbitrarily long incoming data should not silently override the API's bound. [Call, line 1063](E:/Projects/nova2/design.md:1063); [injection, line 1317](E:/Projects/nova2/design.md:1317). |
| **Rowhammer counts CPY once per touched page** | A large copy and a one-byte access can contribute the same count. Decide whether this is an intentional abstraction; otherwise attacks depend strongly on the instruction used to perform equivalent memory traffic. [Rule, line 1328](E:/Projects/nova2/design.md:1328). |
| **Soft sources shrinking the hot zone** | Lower voltage reduces some device heating, but increased current raises the source's internal loss. Treat the net effect as a hypothesis throughout, as Section 14 already partly does. [Claim, line 295](E:/Projects/nova2/design.md:295). |
| **Bounded power does not prove thermal equilibrium** | Settling also requires adequate positive cooling and suitable dynamics. Time-varying sources and cut-outs can produce continuing oscillations. Narrow the claim. [Heat, line 440](E:/Projects/nova2/design.md:440). |
| **Only blackouts losing data** | Queues, inboxes, and expired stranded messages also drop data under the specified rules. [Claim, line 1620](E:/Projects/nova2/design.md:1620). |
| **Cold-start disagreement with history** | The history triggers on crossing the reset memory's voltage threshold; the current text requires lost contents. Gradual fading makes these observably different after a brief dip. [History, line 1380](E:/Projects/nova2/design-history.md:1380); [current, line 1440](E:/Projects/nova2/design.md:1440). |
| **The starting-point label has inconsistent meaning** | It is defined as observer-only, but also marks world-generation noise. Changing that method changes the world. [Definition, line 17](E:/Projects/nova2/design.md:17); [usage, line 552](E:/Projects/nova2/design.md:552). |
| **Direct2D and D3D12 integration needs a named path** | Microsoft's documented shared rendering path uses D3D11On12, wrapped resources, and synchronization. Include that in the engineering plan rather than treating direct device sharing as already specified. [Design, line 109](E:/Projects/nova2/design.md:109); [Microsoft documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/d2d-using-d3d11on12). |

The next revision should prioritize four concrete additions:

- An exact instruction and OS contract, including the traced replicator.
- Lifecycle tables for owners, threads, devices, allocations, and pending operations.
- One tick-order specification with electrical and thermal equations.
- An experiment table separating guaranteed behavior, predictions, and success criteria.
