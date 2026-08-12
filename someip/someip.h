#pragma once
// -----------------------------------------------------------------------------
//  Minimal SOME/IP framing (COVESA/AUTOSAR wire format) over UDP.
//
//  This is the REAL SOME/IP header layout, big-endian on the wire, so what
//  travels between partitions is genuine SOME/IP -- not "some UDP blob".
//  It is NOT the full vsomeip stack: no Service Discovery (SD), no routing
//  daemon, no eventgroups. It demonstrates the CONTROL PLANE concretely and
//  runs on the same 192.168.100.x virtual link SOME/IP would bind to.
//
//  SOME/IP header (16 bytes) then payload:
//    Message ID   : Service ID (16) | Method ID (16)
//    Length       : bytes from Request ID to end of payload  (= 8 + payload)
//    Request ID   : Client ID (16) | Session ID (16)
//    Proto Ver(8) | Iface Ver(8) | Message Type(8) | Return Code(8)
// -----------------------------------------------------------------------------
#include <cstdint>
#include <cstring>
#include <vector>

namespace someip {

constexpr uint16_t SERVICE_ID = 0x1234;   // "ADAS safety service"
constexpr uint16_t METHOD_ID  = 0x0421;   // "GetSafetyStatus"
constexpr uint16_t UDP_PORT   = 30509;    // typical SOME/IP dynamic port

constexpr uint8_t  PROTO_VER  = 0x01;
constexpr uint8_t  IFACE_VER  = 0x01;
enum MsgType : uint8_t { REQUEST = 0x00, RESPONSE = 0x80, ERROR = 0x81 };
enum RetCode : uint8_t { E_OK = 0x00, E_NOT_OK = 0x01 };

struct Header {                 // decoded form (host order)
    uint16_t service_id, method_id;
    uint32_t length;
    uint16_t client_id, session_id;
    uint8_t  proto_ver, iface_ver, msg_type, return_code;
};

// ---- big-endian pack/unpack (SOME/IP is network byte order) ----
inline void put16(std::vector<uint8_t>& b, uint16_t v){ b.push_back(v>>8); b.push_back(v&0xFF); }
inline void put32(std::vector<uint8_t>& b, uint32_t v){ b.push_back(v>>24); b.push_back((v>>16)&0xFF); b.push_back((v>>8)&0xFF); b.push_back(v&0xFF); }
inline uint16_t get16(const uint8_t* p){ return (uint16_t(p[0])<<8) | p[1]; }
inline uint32_t get32(const uint8_t* p){ return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3]; }

// Build a full SOME/IP message (header + payload) ready to sendto().
inline std::vector<uint8_t> build(uint16_t svc, uint16_t mth,
                                  uint16_t client, uint16_t session,
                                  uint8_t type, uint8_t rc,
                                  const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> b;
    put16(b, svc); put16(b, mth);
    put32(b, uint32_t(8 + payload.size()));      // Length: RequestID(4)+4 flag bytes+payload
    put16(b, client); put16(b, session);
    b.push_back(PROTO_VER); b.push_back(IFACE_VER); b.push_back(type); b.push_back(rc);
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

// Parse a received datagram. Returns false if too short / malformed.
inline bool parse(const uint8_t* buf, size_t n, Header& h, std::vector<uint8_t>& payload) {
    if (n < 16) return false;
    h.service_id = get16(buf+0);  h.method_id = get16(buf+2);
    h.length     = get32(buf+4);
    h.client_id  = get16(buf+8);  h.session_id = get16(buf+10);
    h.proto_ver  = buf[12]; h.iface_ver = buf[13]; h.msg_type = buf[14]; h.return_code = buf[15];
    size_t plen = (h.length >= 8) ? (h.length - 8) : 0;
    if (16 + plen > n) plen = n - 16;             // be lenient on trailing bytes
    payload.assign(buf+16, buf+16+plen);
    return true;
}

} // namespace someip
