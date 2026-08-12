// IVI reader (QM side). Opens the SAME shared region but maps it READ-ONLY:
// the MMU enforces PROT_READ, so this process physically cannot write ADAS's
// memory -- the same page-permission mechanism a hypervisor uses across VMs.
//
// It treats the channel as a BLACK CHANNEL and validates every message with
// E2E (CRC + freshness + sequence). Any failure -> reject -> SAFE STATE
// (hold last-known-good). Reader never makes the writer wait.

#include "protocol.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <ctime>

using namespace xpc;

static uint64_t now_ns() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}

int main() {
    int fd = -1;
    for (int tries = 0; tries < 50 && fd < 0; ++tries) {   // wait for writer
        fd = shm_open(SHM_NAME, O_RDONLY, 0666);
        if (fd < 0) usleep(20 * 1000);
    }
    if (fd < 0) { perror("shm_open (is the writer running?)"); return 1; }

    void* addr = mmap(nullptr, sizeof(SharedRegion), PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) { perror("mmap"); return 1; }
    auto* region = static_cast<const SharedRegion*>(addr);

    printf("[IVI reader]  mapped READ-ONLY. Validating E2E = CRC + freshness + sequence.\n");

    uint64_t last_accepted  = 0;
    uint64_t last_change_ns = now_ns();
    int  good = 0, rejected = 0, dropped = 0;
    bool safe_state = false;
    int  last_good_level = 0;

    const uint64_t run_ns = 6ull * 1000000000ull;
    uint64_t start = now_ns();

    while (now_ns() - start < run_ns) {
        uint64_t h = region->head.load(std::memory_order_acquire);

        // E2E: TIMEOUT -- no fresh data -> SAFE STATE.
        if (h == last_accepted) {
            if (!safe_state &&
                (now_ns() - last_change_ns) / 1000000ull > TIMEOUT_MS) {
                safe_state = true;
                printf("  [E2E] TIMEOUT   no data > %ums -> SAFE STATE (hold warn=%d)\n",
                       TIMEOUT_MS, last_good_level);
            }
            usleep(5 * 1000);
            continue;
        }

        uint32_t slot = uint32_t(h) & (CAPACITY - 1);
        Message  m    = region->slots[slot];      // copy out (freshness-first)
        if (m.seq != h) continue;                 // torn read: writer lapped us

        last_change_ns = now_ns();

        // E2E check 1 -- CRC (corruption).
        if (message_crc(m) != m.crc) {
            rejected++; safe_state = true; last_accepted = h;
            printf("  [E2E] CRC FAIL  seq=%llu -> REJECT, SAFE STATE (hold warn=%d)\n",
                   (unsigned long long)m.seq, last_good_level);
            continue;
        }
        // E2E check 2 -- freshness (staleness).
        uint64_t age_ms = (now_ns() - m.timestamp_ns) / 1000000ull;
        if (age_ms > FRESH_MS) {
            rejected++; safe_state = true; last_accepted = h;
            printf("  [E2E] STALE     seq=%llu age=%llums -> REJECT, SAFE STATE\n",
                   (unsigned long long)m.seq, (unsigned long long)age_ms);
            continue;
        }
        // E2E check 3 -- sequence (dup / reorder / gap).
        if (m.seq <= last_accepted) {
            rejected++;
            printf("  [E2E] REORDER   seq=%llu (last=%llu) -> REJECT\n",
                   (unsigned long long)m.seq, (unsigned long long)last_accepted);
            continue;
        }
        if (last_accepted != 0 && m.seq > last_accepted + 1) {
            uint64_t miss = m.seq - last_accepted - 1;   // overwritten before read
            dropped += int(miss);
            printf("  [info] dropped %llu stale sample(s) (freshness > completeness)\n",
                   (unsigned long long)miss);
        }

        // Accept: valid, fresh, in order.
        good++; safe_state = false;
        last_accepted   = m.seq;
        last_good_level = m.warning_level;
        printf("  [OK]  seq=%llu  TTC=%dms  warn=%d\n",
               (unsigned long long)m.seq, m.ttc_ms, m.warning_level);

        usleep(5 * 1000);
    }

    printf("[IVI reader]  summary: accepted=%d  rejected=%d  dropped=%d  safe_state=%s\n",
           good, rejected, dropped, safe_state ? "YES" : "no");
    munmap(addr, sizeof(SharedRegion));
    close(fd);
    return 0;
}
