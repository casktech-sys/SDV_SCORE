# Cross-Partition Comms POC — ADAS → IVI (shared memory + E2E)

A runnable proof of the locked communication design: **single-writer, overwriting
ring buffer** in shared memory, **read-only consumer**, and **black-channel + E2E**
(CRC + freshness + sequence) with **safe-state fallback**. Two Linux processes stand
in for the two partitions; the logic is identical whether the ends are two processes
or two VMs.

## Build & run (WSL2 / any Linux)

```bash
./build.sh
./demo.sh none     # healthy stream
./demo.sh crc      # E2E catches corrupted messages
./demo.sh stale    # E2E catches stale (delayed) messages
./demo.sh freeze   # writer stops -> reader times out -> SAFE STATE
```

Requires only `g++` (C++17). No libraries, no root, no hypervisor.

## What each principle looks like in the code

| Design principle (from the decision record) | Where it lives |
|---|---|
| Single-writer, **ADAS never blocks** | `adas_writer.cpp` — writes and advances `head`, never waits |
| **Overwriting** buffer (freshness > completeness) | slot = `seq & (CAPACITY-1)`; old slots overwritten |
| **Read-only consumer**, MMU-enforced | `ivi_reader.cpp` — `mmap(..., PROT_READ, ...)` |
| **Black channel + E2E** | `message_crc` + freshness + sequence checks in reader |
| **Safe state** on any failure | reader holds last-known-good, prints `SAFE STATE` |
| Torn-read guard | reader re-checks `m.seq == head` after copy |

## Is this Eclipse S-CORE / LoLa?

**No — and that's deliberate.** This is a standalone POSIX-shared-memory build of
the *same pattern* LoLa uses. It is **not** LoLa/S-CORE itself, because:

1. LoLa is **intra-partition** — S-CORE has no cross-partition shared-memory
   transport today (that gap is the whole reason this design exists).
2. Standing up S-CORE + Bazel + LoLa just to move data between two processes would
   burn the time budget on build tooling, not on the concepts being tested.
3. The concepts — ring buffer, read-only consumer, E2E, safe state — are
   **transport-agnostic**: identical under LoLa, raw shm, or vsomeip.

So this proves the **pattern**, and it drops behind the binding-interface
abstraction without touching application logic. Building S-CORE on WSL2 (which you
can already do) is a separate, parallel proof point — not coupled to this demo.

## One line for the interview

> "I built the cross-partition data-plane pattern — single-writer overwriting ring
> buffer, MMU-enforced read-only consumer, and E2E with fault injection and
> safe-state fallback — on WSL2. Two processes stand in for two partitions; the
> logic is identical, and it swaps into LoLa or vsomeip behind the binding
> interface without changing app code."
