#pragma once
#include "protocol.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

// ============================================================================
//  Transport shim -- the single place that decides HOW the two ends obtain
//  the SharedRegion pointer. Everything downstream (ring buffer, release/
//  acquire, E2E = CRC+freshness+sequence, safe-state) is identical either way.
//
//     default build            -> POSIX shared memory  (two processes, WSL2)
//     build with -DXPC_IVSHMEM  -> ivshmem PCI BAR      (two VMs, one host)
//
//  This file is the ONLY thing that differs between the two topologies.
// ============================================================================

namespace xpc {

#ifdef XPC_IVSHMEM
// ---- ivshmem-plain: the shared region IS the device's BAR2 -----------------
//  One region of host RAM, mapped into both guests as PCI memory. sysfs
//  exposes BAR2 as resource2 (adjust 0000:00:05.0 if lspci shows another slot).
constexpr char   XPC_TRANSPORT[] = "ivshmem PCI BAR (cross-VM, zero-copy)";
constexpr char   IVSHMEM_BAR[]   =
    "/sys/bus/pci/devices/0000:00:05.0/resource2";
constexpr size_t IVSHMEM_BYTES   = 4u * 1024 * 1024;   // must match QEMU size=4M

inline SharedRegion* map_region_rw() {
    int fd = open(IVSHMEM_BAR, O_RDWR);
    if (fd < 0) { perror("open ivshmem BAR (run with sudo)"); exit(1); }
    void* a = mmap(nullptr, IVSHMEM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (a == MAP_FAILED) { perror("mmap BAR"); exit(1); }
    return static_cast<SharedRegion*>(a);
}

inline const SharedRegion* map_region_ro() {
    // Same MMU-enforced read-only guarantee as the shm build: PROT_READ means
    // the QM side physically cannot write the ASIL region -- across VMs now.
    int fd = open(IVSHMEM_BAR, O_RDONLY);
    if (fd < 0) { perror("open ivshmem BAR (run with sudo)"); exit(1); }
    void* a = mmap(nullptr, IVSHMEM_BYTES, PROT_READ, MAP_SHARED, fd, 0);
    if (a == MAP_FAILED) { perror("mmap BAR"); exit(1); }
    return static_cast<const SharedRegion*>(a);
}

#else
// ---- POSIX shared memory: the original two-process transport ---------------
constexpr char XPC_TRANSPORT[] = "POSIX shm /adas_ivi_channel";

inline SharedRegion* map_region_rw() {
    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { perror("shm_open"); exit(1); }
    if (ftruncate(fd, sizeof(SharedRegion)) != 0) { perror("ftruncate"); exit(1); }
    void* a = mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (a == MAP_FAILED) { perror("mmap"); exit(1); }
    return static_cast<SharedRegion*>(a);
}

inline const SharedRegion* map_region_ro() {
    int fd = -1;
    for (int t = 0; t < 50 && fd < 0; ++t) {          // wait for writer to create it
        fd = shm_open(SHM_NAME, O_RDONLY, 0666);
        if (fd < 0) usleep(20 * 1000);
    }
    if (fd < 0) { perror("shm_open (is the writer running?)"); exit(1); }
    void* a = mmap(nullptr, sizeof(SharedRegion), PROT_READ, MAP_SHARED, fd, 0);
    if (a == MAP_FAILED) { perror("mmap"); exit(1); }
    return static_cast<const SharedRegion*>(a);
}
#endif

} // namespace xpc
