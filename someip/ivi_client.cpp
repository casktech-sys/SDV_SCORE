// IVI SOME/IP client (guest2 / QM side). Calls ADAS Service 0x1234 / Method 0x0421
// once per second across the private inter-partition link, and prints the matched
// RESPONSE. Session ID increments each call (SOME/IP session handling), so you can
// see request/response pair up by Request ID -- the control-plane analogue of the
// data plane's seq/slot matching.
#include "someip.h"
#include <cstdio>
#include <cstdlib>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const char* server_ip = (argc > 1) ? argv[1] : "192.168.100.1";
    int         count     = (argc > 2) ? std::atoi(argv[2]) : 10;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    timeval tv{1, 0}; setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); // 1s timeout

    sockaddr_in srv{}; srv.sin_family = AF_INET; srv.sin_port = htons(someip::UDP_PORT);
    inet_pton(AF_INET, server_ip, &srv.sin_addr);

    const uint16_t CLIENT_ID = 0x0002;   // IVI's SOME/IP client id
    printf("[IVI client]  calling SOME/IP service 0x%04X method 0x%04X at %s:%u\n",
           someip::SERVICE_ID, someip::METHOD_ID, server_ip, someip::UDP_PORT);

    for (int i = 1; i <= count; ++i) {
        uint16_t session = uint16_t(i);
        auto req = someip::build(someip::SERVICE_ID, someip::METHOD_ID,
                                 CLIENT_ID, session, someip::REQUEST, someip::E_OK, {});
        sendto(sock, req.data(), req.size(), 0, (sockaddr*)&srv, sizeof(srv));
        printf("[ -> REQ ] client=0x%04X session=0x%04X  GetSafetyStatus\n", CLIENT_ID, session);

        uint8_t buf[1500];
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n <= 0) { printf("[  !!!  ] no response (timeout) -> service unreachable\n\n"); }
        else {
            someip::Header h; std::vector<uint8_t> pl;
            if (someip::parse(buf, n, h, pl) && pl.size() >= 5) {
                int warn = pl[0];
                int32_t ttc = int32_t(someip::get32(pl.data()+1));
                printf("[<- RESP] client=0x%04X session=0x%04X  rc=0x%02X  type=0x%02X | warn=%d ttc=%dms\n\n",
                       h.client_id, h.session_id, h.return_code, h.msg_type, warn, ttc);
            } else {
                printf("[<- RESP] malformed/short payload\n\n");
            }
        }
        sleep(1);
    }
    return 0;
}
