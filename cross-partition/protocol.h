#pragma once
#include <cstdint>
#include <atomic>
#include <cstddef>

// ============================================================================
//  Cross-partition contract:  ADAS (writer, ASIL) --> IVI (reader, QM)
// ----------------------------------------------------------------------------
//  This is a standalone POSIX-shared-memory implementation of the SAME PATTERN
//  Eclipse S-CORE LoLa uses (zero-copy, single-writer, service-style events).
//  It is deliberately NOT LoLa itself -- see README.md for why. The logic here
//  is identical whether the two ends are two processes (this POC) or two VMs.
// ============================================================================

namespace xpc {  // cross-partition comms

constexpr char     SHM_NAME[] = "/adas_ivi_channel";
constexpr uint32_t CAPACITY   = 16;    // ring-buffer slots (power of two)
constexpr uint32_t FRESH_MS   = 200;   // E2E: reject messages older than this
constexpr uint32_t TIMEOUT_MS = 500;   // E2E: no new data for this long -> SAFE STATE

// One message = one ADAS sample crossing the QM<->ASIL boundary.
// Payload here carries the AEB story: time-to-collision + warning level.
struct Message {
    uint64_t seq;           // E2E: sequence counter  (loss / dup / reorder)
    uint64_t timestamp_ns;  // E2E: freshness         (monotonic clock)
    int32_t  ttc_ms;        // payload: time-to-collision (ms)
    int32_t  warning_level; // payload: 0..3  (Collision_Warning_Level)
    uint32_t crc;           // E2E: CRC32 over every field above
};

// The shared region: a single-writer, overwriting ring buffer.
struct SharedRegion {
    std::atomic<uint64_t> head;          // latest committed seq (writer publishes)
    std::atomic<uint64_t> writer_now_ns; // writer's monotonic clock, refreshed each publish.
                                         // COMMON time reference for freshness: sender and
                                         // receiver kernels have different CLOCK_MONOTONIC
                                         // epochs, so age must be measured in ONE domain.
    uint32_t              capacity;
    uint32_t              _pad;
    Message               slots[CAPACITY];
};

// ---- CRC32 (IEEE 802.3 polynomial), header-only ---------------------------
inline uint32_t crc32(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return ~crc;
}

// CRC covers everything up to (not including) the crc field itself.
inline uint32_t message_crc(const Message& m) {
    return crc32(&m, offsetof(Message, crc));
}

} // namespace xpc
