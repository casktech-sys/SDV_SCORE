# Architecture — SDV Cross-Partition Communication POC

## The problem
Consolidate an ADAS (ASIL / safety) domain and an IVI (QM / comfort) domain onto **one HPC**
under a hypervisor, and let them communicate. Two isolation domains, one piece of hardware.

## Two planes
- **Data plane — shared memory (ivshmem).** High-rate sensor stream. Zero-copy, trap-free,
  sub-microsecond. This is where the AEB/ADAS samples flow.
- **Control plane — SOME/IP over a virtual NIC.** Low-rate request/response, service discovery,
  configuration, events. Milliseconds; that's fine for control.

Splitting them is deliberate: the data plane cannot afford copies or hypervisor traps; the
control plane values flexibility and standard service semantics over raw latency.

## The black-channel thesis
The transport between partitions is **uncertified and guarantees nothing** — a black channel.
Safety is enforced at the **application layer**, not the transport:

- **CRC** — detects corruption.
- **Freshness** — detects stale data (age check).
- **Sequence** — detects loss, duplication, reordering (monotonic counter; the AUTOSAR E2E
  "Counter" role).
- **Safe state** — any failure, or a receiver-side timeout, holds last-known-good.

Because safety sits above the transport, the identical logic is valid over POSIX shared
memory, an ivshmem BAR, or SOME/IP. Certifying the transport is not required.

## The ring buffer
- **16-slot single-writer overwriting ring** + one atomic `head`. Fixed memory, no `malloc`
  (deterministic), no locks. `slot = seq % 16` is a bit-AND because 16 is a power of two.
- **Shallow on purpose:** for safety data, buffering is the enemy of freshness. The reader
  always takes the newest sample; if it falls behind, old samples are dropped, not queued.
  Freshness over completeness.
- **Writer never blocks:** the ASIL producer must not stall for a slow QM consumer.

## The handoff (lock-free)
Writer: fill `slots[seq % 16]`, then `head.store(seq, release)`.
Reader: `head.load(acquire)`, then read `slots[head % 16]`.
The release/acquire pair is the entire safety of the handoff — if the reader observes the new
`head`, the slot behind it is guaranteed fully written. No locks, no half-written reads.

## The transport shim (portability)
Everything above the `SharedRegion*` pointer is OS-agnostic. Only the function pair that
*obtains* the pointer differs:

- **Linux (`xpc_map.h`):** `open()` the ivshmem PCI BAR at `/sys/bus/pci/.../resource2`, `mmap`.
- **QNX (`xpc_map_qnx.h`):** `shm_open()` a shared-memory object (or `mmap_device_memory()` a
  raw BAR), `mmap`.

Same signature, same everything downstream. Swapping Linux↔QNX is a one-file change, selected
by a compile flag (`-DXPC_IVSHMEM` / `-DXPC_QNX`).

## Cross-kernel clock-domain finding
Running across two real kernels surfaced a latent assumption the single-host POC hid:
`CLOCK_MONOTONIC` counts from **each kernel's own boot**, so the two guests have different
epochs. A sender timestamp compared against the receiver's clock underflows (unsigned wrap),
and every message reads as impossibly stale.

Fix: the writer publishes its own clock **in-band** in the shared region (`writer_now_ns`), and
the reader computes freshness in the writer's clock domain. In a real vehicle the shared time
reference is **gPTP (IEEE 802.1AS)** distributed over the TSN backbone; the in-band writer clock
is the POC stand-in. Note the *channel-liveness* timeout was always correct because it is
measured entirely on the receiver's own clock — only the per-message data-age check needed a
common time base.

## Why shared memory is fast under a hypervisor
A guest runs directly on the physical CPU; the hypervisor intervenes only on a **trap**
(privileged instruction or emulated-device access), and each trap costs a guest exit → context
switch → hypervisor → context switch → guest entrance.

- **Shared memory:** after the one-time mapping, reads/writes are native CPU memory accesses
  (second-stage/EPT translation in hardware). **No trap, trap-free in steady state** → sub-µs.
- **Virtual NIC / SOME/IP:** touching the emulated device **traps** on every packet → ms.

That hardware asymmetry is why the data plane is shared memory and the control plane is SOME/IP.

## Type-1 vs Type-2
A production vehicle uses a **Type-1** hypervisor (e.g. QNX Hypervisor / PikeOS on bare metal).
This POC uses **Type-2** (QEMU/KVM as an application) for convenience. Both run guests natively
via hardware virtualization and both provide inter-partition shared memory, so the communication
and safety behaviour demonstrated here is faithful; only the base layer differs.
