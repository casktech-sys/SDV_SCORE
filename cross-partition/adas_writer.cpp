// ADAS writer (ASIL side). Creates the shared region, maps it READ-WRITE, and
// publishes samples into a single-writer OVERWRITING ring buffer. It NEVER
// waits for the reader -- if IVI falls behind, ADAS overwrites and moves on.
//
//   Usage: ./adas_writer [--fault=none|crc|stale|freeze] [--count=N]
//
//   Transport selected at build time (xpc_map.h):
//     g++ ...            -> POSIX shm     (two processes, WSL2)
//     g++ -DXPC_IVSHMEM  -> ivshmem BAR   (two VMs, one host)

#include "xpc_map.h"      // was "protocol.h"; xpc_map.h includes it and adds the transport
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

using namespace xpc;

static uint64_t now_ns() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}

int main(int argc, char** argv) {
    std::string fault = "none";
    int count = 60;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a.rfind("--fault=", 0) == 0) fault = a.substr(8);
        else if (a.rfind("--count=", 0) == 0) count = std::atoi(a.c_str() + 8);
    }

    // ---- transport acquisition: the ONLY thing that differs across topologies ----
    auto* region = map_region_rw();

    region->capacity = CAPACITY;
    region->writer_now_ns.store(now_ns(), std::memory_order_relaxed);
    region->head.store(0, std::memory_order_relaxed);   // fresh head every run

    printf("[ADAS writer] transport=%s  slots=%u  fault=%s  count=%d  (writer NEVER blocks)\n",
           XPC_TRANSPORT, CAPACITY, fault.c_str(), count);

    for (int i = 1; i <= count; ++i) {
        uint64_t seq = uint64_t(i);

        // FREEZE fault: stop publishing -> reader must fall to SAFE STATE on timeout.
        if (fault == "freeze" && i > count / 2) {
            printf("[ADAS writer] FROZEN (fault=freeze) after seq=%llu\n",
                   (unsigned long long)(seq - 1));
            break;
        }

        Message m{};
        m.seq           = seq;
        m.timestamp_ns  = now_ns();
        m.ttc_ms        = 3000 - i * 10;                    // closing on impact
        m.warning_level = (m.ttc_ms < 800) ? 3 : (m.ttc_ms < 1500 ? 2 : 1);

        // STALE fault: backdate the timestamp on every 7th message.
        if (fault == "stale" && (i % 7 == 0))
            m.timestamp_ns -= uint64_t(FRESH_MS + 300) * 1000000ull;

        m.crc = message_crc(m);

        // CRC fault: corrupt the checksum on every 7th message.
        if (fault == "crc" && (i % 7 == 0))
            m.crc ^= 0xDEADBEEF;

        // Write the slot, THEN publish head with release ordering so the reader
        // (acquire) can never observe a half-written slot.
        uint32_t slot = uint32_t(seq) & (CAPACITY - 1);
        region->slots[slot] = m;
        // Publish the common time reference, THEN head. Reader reads head first,
        // so any writer_now it then sees is >= the one paired with this message.
        region->writer_now_ns.store(now_ns(), std::memory_order_release);
        region->head.store(seq, std::memory_order_release);

        usleep(50 * 1000);  // ~20 Hz
    }

    printf("[ADAS writer] done.\n");
    return 0;
}
