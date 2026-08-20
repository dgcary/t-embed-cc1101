#include "PortScanner.h"
#include "NetToolsUtils.h"

#include <algorithm>
#include <array>
#include <errno.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <sys/ioctl.h>

namespace {

static constexpr size_t MAX_CONCURRENT_SOCKETS = 8;

constexpr uint16_t COMMON_TCP_PORTS[] = {
    21, 22, 23, 25, 53, 80, 110, 135, 139, 143, 179, 389, 443, 445, 465, 514,
    515, 554, 587, 631, 636, 873, 902, 993, 995, 1433, 1521, 1723, 1883, 2049,
    2375, 2376, 3306, 3389, 5432, 5900, 5985, 5986, 6379, 6443, 8000, 8080,
    8443, 9000, 9100, 9200, 10000, 27017,
};

struct SocketProbe {
    int fd = -1;
    uint16_t port = 0;
    bool pending = false;
    bool open = false;
};

void closeProbe(SocketProbe &probe) {
    if (probe.fd >= 0) {
        lwip_close(probe.fd);
        probe.fd = -1;
    }
}

template <typename PortGetter>
std::vector<PortScanResult> scanGeneratedPorts(
    const IPAddress &target,
    uint32_t totalPorts,
    uint32_t timeoutMs,
    PortScanProgress progress,
    PortGetter getPort
) {
    std::vector<PortScanResult> results;
    if (totalPorts == 0) return results;
    if (timeoutMs == 0) timeoutMs = 1;

    const uint32_t targetAddress = lwip_htonl(ipToHostOrder(target));
    uint32_t scanned = 0;

    while (scanned < totalPorts) {
        const size_t batchSize = static_cast<size_t>(
            std::min<uint32_t>(MAX_CONCURRENT_SOCKETS, totalPorts - scanned)
        );
        std::array<SocketProbe, MAX_CONCURRENT_SOCKETS> probes;

        fd_set writeSet;
        fd_set errorSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        int maxFd = -1;

        for (size_t index = 0; index < batchSize; ++index) {
            SocketProbe &probe = probes[index];
            probe.port = getPort(scanned + index);
            probe.fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (probe.fd < 0) continue;

            int nonBlocking = 1;
            if (ioctl(probe.fd, FIONBIO, &nonBlocking) < 0) {
                closeProbe(probe);
                continue;
            }

            sockaddr_in destination = {};
            destination.sin_family = AF_INET;
            destination.sin_port = lwip_htons(probe.port);
            destination.sin_addr.s_addr = targetAddress;

            const int connectResult = lwip_connect(
                probe.fd,
                reinterpret_cast<const sockaddr *>(&destination),
                sizeof(destination)
            );
            if (connectResult == 0) {
                probe.open = true;
                continue;
            }

            if (errno == EINPROGRESS || errno == EALREADY || errno == EWOULDBLOCK) {
                probe.pending = true;
                FD_SET(probe.fd, &writeSet);
                FD_SET(probe.fd, &errorSet);
                if (probe.fd > maxFd) maxFd = probe.fd;
            } else {
                closeProbe(probe);
            }
        }

        if (maxFd >= 0) {
            timeval timeout = {};
            timeout.tv_sec = timeoutMs / 1000;
            timeout.tv_usec = static_cast<suseconds_t>((timeoutMs % 1000) * 1000);

            const int ready = lwip_select(maxFd + 1, nullptr, &writeSet, &errorSet, &timeout);
            if (ready > 0) {
                for (size_t index = 0; index < batchSize; ++index) {
                    SocketProbe &probe = probes[index];
                    if (!probe.pending || probe.fd < 0) continue;
                    if (!FD_ISSET(probe.fd, &writeSet) && !FD_ISSET(probe.fd, &errorSet)) continue;

                    int socketError = -1;
                    socklen_t errorLength = sizeof(socketError);
                    if (lwip_getsockopt(
                            probe.fd,
                            SOL_SOCKET,
                            SO_ERROR,
                            &socketError,
                            &errorLength
                        ) == 0 &&
                        socketError == 0) {
                        probe.open = true;
                    }
                }
            }
        }

        for (size_t index = 0; index < batchSize; ++index) {
            SocketProbe &probe = probes[index];
            if (probe.open) {
                results.push_back({probe.port, netToolsServiceName(probe.port)});
            }
            closeProbe(probe);
        }

        scanned += static_cast<uint32_t>(batchSize);
        if (progress && !progress(scanned, totalPorts)) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return results;
}

} // namespace

std::vector<PortScanResult> scanTcpPorts(
    const IPAddress &target,
    uint16_t startPort,
    uint16_t endPort,
    uint32_t timeoutMs,
    PortScanProgress progress
) {
    if (startPort == 0 || endPort == 0 || startPort > endPort) return {};

    const uint32_t totalPorts = static_cast<uint32_t>(endPort) - static_cast<uint32_t>(startPort) + 1;
    return scanGeneratedPorts(
        target,
        totalPorts,
        timeoutMs,
        progress,
        [startPort](uint32_t index) { return static_cast<uint16_t>(startPort + index); }
    );
}

std::vector<PortScanResult> scanCommonTcpPorts(
    const IPAddress &target,
    uint32_t timeoutMs,
    PortScanProgress progress
) {
    const uint32_t totalPorts = sizeof(COMMON_TCP_PORTS) / sizeof(COMMON_TCP_PORTS[0]);
    return scanGeneratedPorts(
        target,
        totalPorts,
        timeoutMs,
        progress,
        [](uint32_t index) { return COMMON_TCP_PORTS[index]; }
    );
}

uint32_t commonTcpPortCount() { return sizeof(COMMON_TCP_PORTS) / sizeof(COMMON_TCP_PORTS[0]); }
