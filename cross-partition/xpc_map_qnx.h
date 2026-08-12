#pragma once
#include "protocol.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

// =============================================================================
//  QNX transport shim  --  same SharedRegion contract, different mapping API.
//
//  Compare with the Linux ivshmem shim (xpc_map.h):
//    Linux:  open("/sys/bus/pci/devices/0000:00:05.0/resource2") + mmap
//    QNX  :  shm_open("/ivshmem")            + mmap        (typed shared object)
//       or:  mmap_device_memory(<phys BAR>)               (raw physical BAR)
//
//  The struct, ring buffer, release/acquire, E2E and safe-state are UNCHANGED.
//  Only these two functions differ from the Linux build. That is the point:
//  the safety/communication logic is portable; the transport is a thin shim.
//
//  Build QNX ADAS writer:  q++ -DXPC_QNX_ADAS -std=c++17 adas_writer.cpp -o adas_writer
// =============================================================================

namespace xpc {

constexpr char XPC_TRANSPORT[] = "QNX shared memory (cross-partition)";

// -- Option A (portable, recommended for the demo): named shared-memory object.
//    Under the QNX Hypervisor the inter-VM region is presented to the guest as a
//    shared-memory object; the guest opens it by name and mmaps it. This mirrors
//    the Linux side closely enough to make the "same mmap, different source" point.
constexpr char QNX_SHM_NAME[] = "/ivshmem";      // name configured in the QNX .vm/hypervisor cfg
constexpr size_t QNX_SHM_BYTES = 4u * 1024 * 1024;

inline SharedRegion* map_region_rw() {
    // O_CREAT so the ADAS side (writer) creates it if it maps first.
    int fd = shm_open(QNX_SHM_NAME, O_RDWR | O_CREAT, 0666);
    if (fd < 0) { perror("shm_open (QNX)"); exit(1); }
    if (ftruncate(fd, QNX_SHM_BYTES) != 0) { perror("ftruncate (QNX)"); exit(1); }
    void* a = mmap(nullptr, QNX_SHM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (a == MAP_FAILED) { perror("mmap (QNX)"); exit(1); }
    return static_cast<SharedRegion*>(a);
}

inline const SharedRegion* map_region_ro() {
    int fd = shm_open(QNX_SHM_NAME, O_RDONLY, 0666);
    if (fd < 0) { perror("shm_open RO (QNX)"); exit(1); }
    void* a = mmap(nullptr, QNX_SHM_BYTES, PROT_READ, MAP_SHARED, fd, 0);
    if (a == MAP_FAILED) { perror("mmap RO (QNX)"); exit(1); }
    return static_cast<const SharedRegion*>(a);
}

/*  -- Option B (raw physical BAR, if the hypervisor exposes ivshmem as a device):
    QNX maps a known physical address range directly. Requires PROCMGR_AID_MEM_PHYS
    ability. Shown for completeness -- this is the closest analogue to the Linux
    "map the PCI BAR" path.

    #include <sys/mman.h>
    inline SharedRegion* map_region_rw_phys(uint64_t bar_phys) {
        void* a = mmap_device_memory(nullptr, QNX_SHM_BYTES,
                                     PROT_READ | PROT_WRITE | PROT_NOCACHE,
                                     0, bar_phys);
        if (a == MAP_FAILED) { perror("mmap_device_memory (QNX)"); exit(1); }
        return static_cast<SharedRegion*>(a);
    }
*/

} // namespace xpc
