# SDV Cross-Partition Communication POC

A working proof-of-concept for **ADAS ↔ IVI communication across hypervisor partitions**
on a single HPC — the "software-defined vehicle" consolidation picture, where a safety
(ASIL) domain and a comfort (QM) domain share one piece of hardware under a hypervisor.

It demonstrates two communication planes between the partitions:

- **Data plane — zero-copy shared memory** (ivshmem): a single-writer overwriting ring
  buffer with end-to-end protection (CRC + freshness + sequence) and a safe-state
  fallback. Sub-microsecond, trap-free in steady state.
- **Control plane — SOME/IP over a virtual network link**: request/response service
  calls, the low-rate path for discovery, configuration, and events.

The same communication logic runs unchanged across three topologies — two processes,
two Linux VMs, or a Linux↔QNX pair — because the transport is isolated behind a single
shim. That portability is the point.

> **Scope (honest):** this is a **mechanism demonstrator**, not a production or certified
> artifact. No MISRA compliance, no ASIL-B coverage metrics, no safety traceability.
> Portions were developed with AI assistance used as engineering tooling. It illustrates
> the *principle* of cross-partition safety communication; it is not a shipping transport.

---

## Why this design

The transport between partitions is treated as a **black channel** — it is uncertified and
guarantees nothing. Safety is therefore enforced at the **application layer**, not in the
transport: every message carries E2E (CRC + freshness + sequence), and the ASIL receiver
falls to a defined **safe state** (hold last-known-good) on any failure or timeout. Because
safety lives above the transport, the *same* safety logic is valid whether the wire is POSIX
shared memory, an ivshmem PCI BAR, or SOME/IP.

See **[docs/Architecture.md](docs/Architecture.md)** for the full design write-up
(two-plane split, release/acquire handoff, the cross-kernel clock finding, Linux vs QNX
transport, and why shared memory is trap-free under a hypervisor).

---

## Repository layout

```
shmpoc/           Base POC — two processes on one host (POSIX shared memory)
cross-partition/  The differentiator — two VM partitions over ivshmem; Linux + QNX transport shims
someip/           Control-plane demo — minimal SOME/IP request/response over UDP
docs/             Architecture write-up + hypervisor setup guide
```

| File | Role |
|------|------|
| `protocol.h` | The contract: 32-byte `Message` + 16-slot ring `SharedRegion`. Identical logic across topologies. |
| `adas_writer.cpp` | ADAS (ASIL) sender — fills a slot, publishes `head` with release ordering. Never blocks. |
| `ivi_reader.cpp` | IVI (QM) receiver — reads with acquire ordering, validates E2E, else safe state. |
| `xpc_map.h` | **Transport shim (Linux):** maps the ivshmem PCI BAR. The one file that changes per OS. |
| `xpc_map_qnx.h` | **Transport shim (QNX):** maps a shared-memory object. Same function pair, different body. |
| `someip.h` / `adas_service.cpp` / `ivi_client.cpp` | Control-plane: real SOME/IP header framing over UDP. |

---

## Quick start — base POC (no VMs needed)

Runs the whole data-plane flow as two processes on one Linux host:

```bash
cd shmpoc
./build.sh
# terminal 1:
./adas_writer --count=200
# terminal 2:
./ivi_reader
```

Try the fault injection to see the E2E checks trip the safe state:

```bash
./adas_writer --fault=crc    --count=200   # -> reader: CRC FAIL  -> SAFE STATE
./adas_writer --fault=stale  --count=200   # -> reader: STALE     -> SAFE STATE
./adas_writer --fault=freeze --count=200   # -> reader: TIMEOUT   -> SAFE STATE
```

## Cross-partition (two real VMs over ivshmem)

The same source with one transport change (`-DXPC_IVSHMEM`), running across two QEMU/KVM
guests that share an ivshmem region. Full reproduction steps — enabling nested
virtualization, booting the guests, wiring the shared region — are in
**[docs/Hypervisor_Setup_Guide.md](docs/Hypervisor_Setup_Guide.md)**.

```bash
cd cross-partition
./build_vm.sh          # g++ -DXPC_IVSHMEM ...  (Linux ivshmem transport)
sudo ./adas_writer --count=1000   # on guest1 (ADAS)
sudo ./ivi_reader  --seconds=60   # on guest2 (IVI)
```

Swapping one guest to **QNX** changes only the transport shim (`xpc_map_qnx.h`,
built with `q++ -DXPC_QNX`); everything above the `mmap` is unchanged. See
`cross-partition/xpc_map_qnx.h`.

## Control plane (SOME/IP)

```bash
cd someip
./build.sh
./adas_service 192.168.100.1     # guest1 offers the service
./ivi_client   192.168.100.1 10  # guest2 calls it
```

---

## Key engineering findings

- **Cross-kernel clock domains.** Each guest's `CLOCK_MONOTONIC` counts from its own boot,
  so a sender timestamp compared against the receiver's clock underflows — every message
  looked "stale." Fixed by carrying the writer's clock in-band as a common reference
  (the software stand-in for gPTP / IEEE 802.1AS). See Architecture.md.
- **Trap-free data path.** After a one-time mapping, shared-memory reads/writes are native
  CPU instructions — no hypervisor trap — which is why the data plane is sub-microsecond
  while the SOME/IP control plane (emulated NIC, traps per packet) is milliseconds.
- **Transport as a thin shim.** WSL2 two-process → two Linux VMs → Linux+QNX changed only
  the ~6 lines that obtain the `SharedRegion*`. The contract and safety logic never changed.

## License

To be added by the author before publishing.
