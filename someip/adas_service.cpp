// ADAS SOME/IP service (guest1 / ASIL side). Offers Service 0x1234, Method 0x0421
// ("GetSafetyStatus"). Binds the private inter-partition link and answers each
// REQUEST with a RESPONSE carrying {warning_level, ttc_ms}. This is the CONTROL
// PLANE: low-rate request/response, distinct from the shared-memory DATA PLANE.
#include "someip.h"
#include <cstdio>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const char* bind_ip = (argc > 1) ? argv[1] : "0.0.0.0";   // 192.168.100.1 in the guest

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in me{}; me.sin_family = AF_INET; me.sin_port = htons(someip::UDP_PORT);
    inet_pton(AF_INET, bind_ip, &me.sin_addr);
    if (bind(sock, (sockaddr*)&me, sizeof(me)) < 0) { perror("bind"); return 1; }

    printf("[ADAS service] SOME/IP service 0x%04X offered on %s:%u  (method 0x%04X GetSafetyStatus)\n",
           someip::SERVICE_ID, bind_ip, someip::UDP_PORT, someip::METHOD_ID);
    printf("[ADAS service] waiting for requests on the control plane...\n");

    uint8_t buf[1500];
    int warn = 1, ttc = 3000;
    for (;;) {
        sockaddr_in cli{}; socklen_t cl = sizeof(cli);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&cli, &cl);
        if (n <= 0) continue;

        someip::Header h; std::vector<uint8_t> pl;
        if (!someip::parse(buf, n, h, pl)) continue;

        char ip[INET_ADDRSTRLEN]; inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        printf("[<- REQ ] svc=0x%04X mth=0x%04X client=0x%04X session=0x%04X type=0x%02X from %s\n",
               h.service_id, h.method_id, h.client_id, h.session_id, h.msg_type, ip);

        // Only serve our service/method; otherwise return an ERROR.
        uint8_t type = someip::RESPONSE, rc = someip::E_OK;
        std::vector<uint8_t> out;
        if (h.service_id == someip::SERVICE_ID && h.method_id == someip::METHOD_ID) {
            // synthesize a moving status so responses visibly change
            ttc -= 100; if (ttc < -500) { ttc = 3000; }
            warn = (ttc < 800) ? 3 : (ttc < 1500 ? 2 : 1);
            out.push_back(uint8_t(warn));
            someip::put32(out, uint32_t(int32_t(ttc)));
        } else {
            type = someip::ERROR; rc = someip::E_NOT_OK;
        }

        // RESPONSE echoes the same Request ID (client|session) -- SOME/IP rule.
        auto msg = someip::build(h.service_id, h.method_id, h.client_id, h.session_id, type, rc, out);
        sendto(sock, msg.data(), msg.size(), 0, (sockaddr*)&cli, cl);
        printf("[ -> RESP] client=0x%04X session=0x%04X  rc=0x%02X  payload: warn=%d ttc=%dms\n\n",
               h.client_id, h.session_id, rc, warn, ttc);
    }
}
