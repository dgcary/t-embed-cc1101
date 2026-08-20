#include "IcmpProbe.h"
#include "NetToolsUtils.h"

#include <errno.h>
#include <esp_system.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <string.h>

namespace {

constexpr size_t ICMP_PACKET_SIZE = 32;
constexpr uint8_t ICMP_ECHO_REQUEST = 8;
constexpr uint8_t ICMP_ECHO_REPLY = 0;
constexpr uint8_t ICMP_TIME_EXCEEDED = 11;

uint16_t readNet16(const uint8_t *data) {
    return (static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]);
}

void writeNet16(uint8_t *data, uint16_t value) {
    data[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[1] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t internetChecksum(const uint8_t *data, size_t length) {
    uint32_t sum = 0;
    while (length > 1) {
        sum += (static_cast<uint16_t>(data[0]) << 8) | data[1];
        data += 2;
        length -= 2;
    }
    if (length == 1) sum += static_cast<uint16_t>(data[0]) << 8;

    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

IPAddress sourceIpFromPacket(const uint8_t *packet) {
    return IPAddress(packet[12], packet[13], packet[14], packet[15]);
}

bool matchesQuotedProbe(
    const uint8_t *packet,
    size_t packetLength,
    size_t outerIcmpOffset,
    uint16_t identifier,
    uint16_t sequence
) {
    const size_t quotedIpOffset = outerIcmpOffset + 8;
    if (packetLength < quotedIpOffset + 20) return false;

    const uint8_t *quotedIp = packet + quotedIpOffset;
    if ((quotedIp[0] >> 4) != 4 || quotedIp[9] != IPPROTO_ICMP) return false;

    const size_t quotedIhl = static_cast<size_t>(quotedIp[0] & 0x0F) * 4;
    if (quotedIhl < 20 || packetLength < quotedIpOffset + quotedIhl + 8) return false;

    const uint8_t *quotedIcmp = quotedIp + quotedIhl;
    if (quotedIcmp[0] != ICMP_ECHO_REQUEST) return false;

    return readNet16(quotedIcmp + 4) == identifier && readNet16(quotedIcmp + 6) == sequence;
}

} // namespace

IcmpProbeSession::IcmpProbeSession() : socketFd(-1), identifier(0) {}

IcmpProbeSession::~IcmpProbeSession() {
    if (socketFd >= 0) {
        lwip_close(socketFd);
        socketFd = -1;
    }
}

bool IcmpProbeSession::begin() {
    if (socketFd >= 0) return true;

    socketFd = lwip_socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (socketFd < 0) return false;

    identifier = static_cast<uint16_t>(esp_random() & 0xFFFFu);
    if (identifier == 0) identifier = 0xBEEF;
    return true;
}

bool IcmpProbeSession::isReady() const { return socketFd >= 0; }

IcmpProbeResult IcmpProbeSession::probe(
    const IPAddress &target,
    uint8_t ttl,
    uint16_t sequence,
    uint32_t timeoutMs
) {
    IcmpProbeResult result;
    if (socketFd < 0 || timeoutMs == 0 || ttl == 0) return result;

    int ttlValue = ttl;
    if (lwip_setsockopt(socketFd, IPPROTO_IP, IP_TTL, &ttlValue, sizeof(ttlValue)) != 0) return result;

    uint8_t packet[ICMP_PACKET_SIZE] = {0};
    packet[0] = ICMP_ECHO_REQUEST;
    packet[1] = 0;
    writeNet16(packet + 4, identifier);
    writeNet16(packet + 6, sequence);
    for (size_t i = 8; i < sizeof(packet); ++i) {
        packet[i] = static_cast<uint8_t>(0x40u + ((sequence + i) & 0x3Fu));
    }
    const uint16_t checksum = internetChecksum(packet, sizeof(packet));
    writeNet16(packet + 2, checksum);

    sockaddr_in destination = {};
    destination.sin_family = AF_INET;
    destination.sin_port = 0;
    destination.sin_addr.s_addr = lwip_htonl(ipToHostOrder(target));

    const uint32_t startUs = micros();
    const uint32_t startMs = millis();
    const ssize_t sent = lwip_sendto(
        socketFd,
        packet,
        sizeof(packet),
        0,
        reinterpret_cast<const sockaddr *>(&destination),
        sizeof(destination)
    );
    if (sent != static_cast<ssize_t>(sizeof(packet))) return result;

    while (millis() - startMs < timeoutMs) {
        const uint32_t elapsedMs = millis() - startMs;
        const uint32_t remainingMs = timeoutMs > elapsedMs ? timeoutMs - elapsedMs : 0;
        if (remainingMs == 0) break;

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketFd, &readSet);

        timeval timeout = {};
        timeout.tv_sec = remainingMs / 1000;
        timeout.tv_usec = static_cast<suseconds_t>((remainingMs % 1000) * 1000);

        const int ready = lwip_select(socketFd + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready == 0) break;
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }

        uint8_t receiveBuffer[256];
        sockaddr_in source = {};
        socklen_t sourceLength = sizeof(source);
        const int received = lwip_recvfrom(
            socketFd,
            receiveBuffer,
            sizeof(receiveBuffer),
            0,
            reinterpret_cast<sockaddr *>(&source),
            &sourceLength
        );
        if (received < 28) continue;

        const size_t packetLength = static_cast<size_t>(received);
        if ((receiveBuffer[0] >> 4) != 4) continue;

        const size_t ipHeaderLength = static_cast<size_t>(receiveBuffer[0] & 0x0F) * 4;
        if (ipHeaderLength < 20 || packetLength < ipHeaderLength + 8) continue;
        if (receiveBuffer[9] != IPPROTO_ICMP) continue;

        const size_t icmpOffset = ipHeaderLength;
        const uint8_t type = receiveBuffer[icmpOffset];
        const uint8_t code = receiveBuffer[icmpOffset + 1];

        if (type == 0 && code == 0) {
            if (readNet16(receiveBuffer + icmpOffset + 4) != identifier) continue;
            if (readNet16(receiveBuffer + icmpOffset + 6) != sequence) continue;

            result.success = true;
            result.reachedTarget = true;
            result.responder = sourceIpFromPacket(receiveBuffer);
            result.rttUs = micros() - startUs;
            result.replyTtl = receiveBuffer[8];
            return result;
        }

        if (type == 11 && matchesQuotedProbe(receiveBuffer, packetLength, icmpOffset, identifier, sequence)) {
            result.success = true;
            result.timeExceeded = true;
            result.responder = sourceIpFromPacket(receiveBuffer);
            result.rttUs = micros() - startUs;
            result.replyTtl = receiveBuffer[8];
            return result;
        }
    }

    return result;
}
