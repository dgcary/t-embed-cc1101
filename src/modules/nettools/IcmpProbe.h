#ifndef __NETTOOLS_ICMP_PROBE_H__
#define __NETTOOLS_ICMP_PROBE_H__

#include <Arduino.h>
#include <IPAddress.h>
#include <stdint.h>

struct IcmpProbeResult {
    bool success = false;
    bool reachedTarget = false;
    bool timeExceeded = false;
    IPAddress responder;
    uint32_t rttUs = 0;
    uint8_t replyTtl = 0;
};

class IcmpProbeSession {
public:
    IcmpProbeSession();
    ~IcmpProbeSession();

    IcmpProbeSession(const IcmpProbeSession &) = delete;
    IcmpProbeSession &operator=(const IcmpProbeSession &) = delete;

    bool begin();
    bool isReady() const;
    IcmpProbeResult probe(const IPAddress &target, uint8_t ttl, uint16_t sequence, uint32_t timeoutMs);

private:
    int socketFd;
    uint16_t identifier;
};

#endif
