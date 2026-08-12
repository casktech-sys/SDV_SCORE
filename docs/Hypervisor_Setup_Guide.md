# Hypervisor Setup Guide — Cross-Partition POC across real VMs
## QEMU/KVM nested in WSL2, with ivshmem (and QNX as the stretch)

**Goal:** run the ADAS→IVI shared-memory POC across *real virtual-machine partitions*
on one host — the faithful "single HPC, multiple partitions under a hypervisor" picture.
**Status:** Phase 4 — post-interview stretch. The two-process POC is sufficient for the interview.

**Why this path:** WSL2 already runs on Hyper-V. Running QEMU/KVM *inside* WSL2 (nested)
avoids the VirtualBox/VMware-vs-Hyper-V conflict on Windows and keeps everything in Linux.

---

## Is this safe? Will it touch my laptop / other projects?

**No impact, fully removable, non-destructive.** Key points:

- **You are NOT converting your laptop into a hypervisor.** You run a hypervisor as an
  *application* on top of your existing OS. Two flavours:
  - **Type-1 (bare-metal):** hypervisor is the base layer on the hardware — Xen, QNX
    Hypervisor, PikeOS. *This is what a real car uses.*
  - **Type-2 (hosted):** hypervisor runs as an app inside your normal OS — QEMU/KVM.
    *This is what this POC uses.* Windows, your files, and your other projects run
    untouched. (Good interview line: real car = Type-1, my demo = Type-2.)
- **The VMs are just files** — disk images in a folder. They don't see or merge with
  your other projects.
- **The "partitions" are virtual** (guest RAM, virtual disks, the ivshmem region) — they
  exist only while QEMU runs. They are *not* partitions of your real hard drive.
- **The only Windows change** is one line, `nestedVirtualization=true`, in `.wslconfig`.
- **Cost is temporary:** RAM/CPU used only while a VM is running; freed on shutdown.

**To remove everything:** close QEMU (stops the VM) → delete the image files (deletes the
VM) → remove the `.wslconfig` line and `sudo apt remove qemu-system-x86` (undoes the host
change). Nothing sticks.

**Habit:** keep all VM images and scripts in one dedicated folder (e.g. `~/score/hyp-poc/`),
so cleanup is deleting a single folder.

---

## Stage 0 — Enable nested virtualization  (~30 min)

1. On Windows, edit `%UserProfile%\.wslconfig`:
   ```ini
   [wsl2]
   nestedVirtualization=true
   ```
2. `wsl --shutdown` in PowerShell, then reopen WSL2.
3. Inside WSL2, confirm KVM is available and install QEMU:
   ```bash
   ls -l /dev/kvm                 # must exist
   sudo apt update
   sudo apt install -y qemu-system-x86 qemu-utils bridge-utils
   ```
**Proves:** your laptop can host guest VMs with hardware acceleration.

---

## Stage 1 — Two Linux guests over a virtual network  (~half day)

- Grab a small cloud image (e.g. Ubuntu cloud image or a Buildroot rootfs) ×2.
- Boot two guests, each with a virtio-net NIC on a shared bridge/socket network.
- Ping between them, then run any networked exchange.

**Proves:** the "virtual network inside one HPC" concept — no cable, virtio-net over
shared RAM. This is the transport the *control plane* (SOME/IP/vsomeip) would ride on.

---

## Stage 2 — Two Linux guests sharing memory via ivshmem  ★ the key stage  (~1 day)

`ivshmem` = QEMU's inter-VM shared memory. `ivshmem-plain` (no interrupts, polling)
matches this POC's polling reader exactly.

1. Create a host-backed shared region and attach it to **both** guests:
   ```bash
   # representative — adapt paths/sizes
   -object memory-backend-file,id=hostmem,size=4M,mem-path=/dev/shm/ivshmem,share=on
   -device ivshmem-plain,memdev=hostmem
   ```
2. In each guest the region appears as a **PCI BAR**. Map it (Linux) via UIO:
   ```bash
   # the shared memory is the device's BAR2 resource
   /sys/bus/pci/devices/<addr>/resource2   # mmap this instead of shm_open
   ```
3. **The one code change:** in `protocol.h`/both programs, replace the
   `shm_open`+`mmap` of `/adas_ivi_channel` with an `mmap` of the ivshmem BAR.
   Everything else — ring buffer, release/acquire, E2E, safe state — is **unchanged**.

**Proves:** your exact POC runs across two *real* VM partitions. This is the
"same code, now across partitions" claim, demonstrated. This is the money result.

---

## Stage 3 — Replace one guest with QNX (heterogeneous cross-OS)  (days — the hard part)

1. Get **QNX SDP** (non-commercial licence — *verify current terms before relying on it*).
2. Build the QNX **x86_64** target image; boot it as a QEMU guest.
3. Run `adas_writer` on the **QNX** guest, `ivi_reader` on the **Linux** guest.
4. Transport: ivshmem (zero-copy) or virtio-net (SOME/IP via vsomeip, which ships on QNX).

**Proves:** the real automotive picture — QNX-ADAS ↔ Linux-IVI on one host, one API.
**Reality check:** QNX image build + PCI/ivshmem mapping on QNX is where the effort
concentrates. Budget days, not hours. Do it *after* the interview.

---

## Effort vs payoff

| Stage | Effort | What you can claim |
|-------|--------|--------------------|
| 0–1 | ~1 day | "I stood up a nested hypervisor with two guests on a virtual network." |
| 2 | ~1 day | "My POC runs across two real VM partitions over inter-VM shared memory." ★ |
| 3 | days | "Full heterogeneous QNX-ADAS ↔ Linux-IVI, one API across both." |

**Recommended stopping point for a strong result without a QNX rabbit hole: end of Stage 2.**

---

## Gotchas to expect
- Nested virt must be on *and* `/dev/kvm` present, or QEMU falls back to slow emulation.
- `ivshmem-plain` gives shared memory only (polling) — perfect here; `ivshmem-doorbell`
  adds interrupts but needs an ivshmem-server. Don't add complexity you don't need.
- Mapping the PCI BAR in a guest needs a UIO binding (`uio_pci_generic`) — this is the
  step people trip on; expect to spend time here.
- QNX is the long pole. Prove the whole flow on two Linux guests (Stage 2) *first*, so
  QNX becomes "swap one guest," not "debug everything at once."
