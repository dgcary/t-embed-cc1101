#ifndef __NETTOOLS_PORT_SCANNER_H__
#define __NETTOOLS_PORT_SCANNER_H__

#include <Arduino.h>
#include <IPAddress.h>
#include <functional>
#include <stdint.h>
#include <vector>

struct PortScanResult {
    uint16_t port = 0;
    String service;
};

using PortScanProgress = std::function<bool(uint32_t scanned, uint32_t total)>;

std::vector<PortScanResult> scanTcpPorts(
    const IPAddress &target,
    uint16_t startPort,
    uint16_t endPort,
    uint32_t timeoutMs,
    PortScanProgress progress = nullptr
);

std::vector<PortScanResult> scanCommonTcpPorts(
    const IPAddress &target,
    uint32_t timeoutMs,
    PortScanProgress progress = nullptr
);

uint32_t commonTcpPortCount();

#endif
