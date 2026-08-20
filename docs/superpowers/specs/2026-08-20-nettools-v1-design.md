# NetTools v1 Design

## Status

Approved direction: proceed directly under the project workflow. This feature is implemented on top of `feature/hardware-dashboard` and does not modify the immutable `baseline/bruce-1.16.1` branch.

## Goal

Add a first-class **NetTools** menu for the LILYGO T-Embed CC1101 Plus that turns the device into a practical pocket network-engineering diagnostic tool without changing Bruce's existing Wi-Fi, RF, NFC, BLE, or boot behavior.

## Scope

NetTools v1 contains exactly these five tools:

1. **Network Info**
2. **Ping**
3. **Traceroute**
4. **Host Discovery**
5. **Port Scanner**

Out of scope for v1: banner grabbing, SNMP, DNS lookup UI, HTTP probing, Netcat UI, SSH/Telnet rework, Ethernet/W5500 support, SYN scanning, OS fingerprinting, vulnerability detection, Wi-Fi attacks, ARP spoofing, credential testing, or Nmap NSE-style scripting.

## Target and menu placement

- Compile/register NetTools only when `T_EMBED_1101` is defined.
- Main menu order becomes:
  - Dashboard
  - NetTools
  - WiFi
  - Bluetooth
  - RF
  - ...existing Bruce order unchanged...
- NetTools participates in Bruce's existing Hide/Show Apps behavior through `MenuItemInterface`.
- No boot-flow change.

## Design principles

### 1. Diagnostic, non-destructive behavior

NetTools v1 only performs network diagnostics:

- ICMP Echo requests
- ICMP Time Exceeded handling for traceroute
- TCP connect probes
- passive reads of current Wi-Fi configuration

It does not deauthenticate clients, spoof ARP, flood packets, brute-force credentials, or modify the remote host.

### 2. Reuse Bruce connection management

NetTools does not maintain its own Wi-Fi credentials. If a tool requires networking and Wi-Fi is disconnected, it calls Bruce's existing Wi-Fi connection flow. If connection is cancelled or fails, the tool exits cleanly.

### 3. Small independent network layer

Do not transplant ESP32 Bit Pirate wholesale. Use its architectural idea—separating transport/probe logic from UI—while keeping the implementation native to this Bruce fork.

Planned structure:

```text
src/modules/nettools/
  IcmpProbe.h/.cpp       raw ICMP socket, ping/traceroute primitive
  PortScanner.h/.cpp     bounded-concurrency TCP connect scanner
  NetToolsUtils.h/.cpp   subnet math, target resolution, service names

src/core/menu_items/
  NetToolsMenu.h/.cpp    Bruce UI and user interaction
```

The menu/UI layer must not contain packet parsing logic.

## Tool behavior

### Network Info

Show the current Wi-Fi state and, when connected:

- SSID
- BSSID
- local IPv4 address
- subnet mask
- default gateway
- DNS1 / DNS2
- station MAC
- RSSI
- Wi-Fi channel

The screen is read-only. Back returns to NetTools.

### Ping

Input:

- IPv4 address or DNS hostname using Bruce's existing keyboard

Behavior:

- Resolve hostname with the existing Arduino Wi-Fi resolver.
- Send 4 ICMP Echo probes.
- Default timeout: 1000 ms per probe.
- Default interval: 250 ms between probes.
- Display each result and final statistics:
  - transmitted
  - received
  - loss percentage
  - min/avg/max RTT for successful probes
- Back may cancel between probes.

Implementation:

- raw IPv4 ICMP socket (`SOCK_RAW`, `IPPROTO_ICMP`)
- correct ICMP checksum
- unique identifier per session
- validate Echo Reply identifier/sequence before accepting it

### Traceroute

Input:

- IPv4 address or DNS hostname

Behavior:

- Maximum 20 hops in v1.
- One ICMP Echo probe per hop.
- Set `IP_TTL` from 1 through 20 using lwIP socket options.
- Default timeout: 1000 ms per hop.
- Parse:
  - ICMP Time Exceeded (type 11) for intermediate routers
  - ICMP Echo Reply (type 0) for target reached
- Validate the quoted original ICMP identifier/sequence in Time Exceeded responses so unrelated ICMP packets are ignored.
- Show hop number, responder IPv4 address, and RTT.
- Timeout displays `*`.
- Stop immediately when target replies.
- Back may cancel between hops.

This is a real ICMP traceroute, not an approximation based on repeated normal pings.

### Host Discovery

Behavior:

- Scan the currently connected Wi-Fi IPv4 subnet.
- Derive network and broadcast from local IP + subnet mask.
- Exclude network address, broadcast address, and the T-Embed's own IP.
- Refuse an automatically derived subnet containing more than 1024 usable host addresses to prevent accidental multi-minute scans on very large networks.
- Use the same ICMP probe engine, one probe per host.
- Local-LAN timeout: 100 ms per host.
- Show progress and collect responsive hosts.
- Back cancels between hosts.
- Final screen lists responsive IPv4 addresses and a scanned/up summary.

No ARP spoofing or offensive follow-up actions are exposed from the NetTools result screen.

### Port Scanner

Input:

- target IPv4 address or DNS hostname
- scan mode:
  - Common TCP ports
  - TCP port range

Common ports mode uses a curated network-engineering/server list covering SSH, Telnet, DNS/TCP, HTTP(S), SMB, RDP, databases, management, virtualization, MQTT, Kubernetes, printing, and common web-admin ports.

Range mode:

- start port: 1..65535
- end port: start..65535
- full `1-65535` is allowed, but the UI warns that filtered networks can take minutes.

Implementation:

- TCP connect scan, not SYN/raw TCP scan.
- Use non-blocking lwIP sockets.
- Maximum 8 simultaneous probe sockets.
- Batch timeout: 150 ms.
- Check `SO_ERROR` after `select()` to distinguish successful connections.
- Close every socket on every path.
- Back cancels between batches.
- Only report open ports in the result list; include known service label when available.
- Final result includes target, range/count scanned, number open, and elapsed time.

The bounded concurrency protects the ESP32-S3's lwIP socket table and prevents hundreds of simultaneous sockets.

## Shared ICMP data model

```cpp
struct IcmpProbeResult {
    bool success;
    bool reachedTarget;
    bool timeExceeded;
    IPAddress responder;
    uint32_t rttUs;
    uint8_t replyTtl;
};
```

`IcmpProbeSession` owns one raw socket and one ICMP identifier. The socket is reused for a Ping/Traceroute/Discovery operation and closed by RAII when the session ends.

Public API:

```cpp
class IcmpProbeSession {
public:
    IcmpProbeSession();
    ~IcmpProbeSession();

    bool begin();
    bool isReady() const;
    IcmpProbeResult probe(const IPAddress &target, uint8_t ttl, uint16_t sequence, uint32_t timeoutMs);

private:
    int socketFd;
    uint16_t identifier;
};
```

Target DNS/IP parsing remains outside the probe class in `NetToolsUtils`.

## Port scanner data model

```cpp
struct PortScanResult {
    uint16_t port;
    const char *service;
};
```

API:

```cpp
using PortScanProgress = std::function<bool(uint32_t scanned, uint32_t total)>;

std::vector<PortScanResult> scanTcpPorts(
    const IPAddress &target,
    uint16_t startPort,
    uint16_t endPort,
    uint32_t timeoutMs,
    PortScanProgress progress
);

std::vector<PortScanResult> scanCommonTcpPorts(
    const IPAddress &target,
    uint32_t timeoutMs,
    PortScanProgress progress
);
```

Returning `false` from the progress callback cancels the scan.

## UI

`NetToolsMenu` follows Bruce's current menu patterns and uses existing display helpers / `ScrollableTextArea` rather than introducing a new UI framework.

Top-level NetTools submenu:

```text
Network Info
Ping
Traceroute
Host Discovery
Port Scanner
Back
```

All labels remain English to match the Bruce baseline and avoid font-scope expansion.

## Error handling

Expected user-facing errors:

- `WiFi not connected`
- `Invalid target`
- `DNS lookup failed`
- `ICMP socket unavailable`
- `Subnet too large (>1024 hosts)`
- `Invalid port/range`
- `Scan cancelled`

Errors return to the NetTools menu without rebooting or changing Wi-Fi mode.

## Resource constraints

Current validated feature baseline is approximately 40.0% static RAM and 25.1% Flash. NetTools v1 must:

- avoid large static result buffers
- cap simultaneous TCP sockets at 8
- store only responsive discovery hosts and open ports
- close sockets deterministically
- avoid new external libraries unless compilation proves an unavoidable need

Expected Flash increase should remain small relative to the available 16 MB.

## Testing and acceptance

### Automated/source contracts

Tests must verify:

- NetTools is registered after Dashboard and before WiFi under `T_EMBED_1101`.
- Ping/traceroute uses raw ICMP rather than starting another Wi-Fi subsystem.
- Traceroute explicitly sets `IP_TTL` and recognizes ICMP type 11 and type 0.
- Host discovery enforces the 1024-host cap.
- Port scanner enforces an 8-socket maximum and closes sockets.
- No production changes are made to existing Bruce WiFi/RF/BLE/NFC modules for this feature.

### PlatformIO gate

Required environment:

```text
lilygo-t-embed-cc1101
```

A real clean build must pass with zero compilation errors before handoff to Codex. Report RAM/Flash and compare to the Hardware Dashboard build baseline.

### Codex hardware gate

Codex only performs:

- flash the exact compiled commit
- serial monitor
- physical interaction
- functional tests

Minimum tests:

1. NetTools appears after Dashboard.
2. Wi-Fi connection flow still works.
3. Network Info matches the actual network.
4. Ping gateway and one Internet IP/hostname.
5. Traceroute gateway (1 hop) and an Internet target (multiple hops where permitted).
6. Host Discovery on a known `/24` and compare several known live hosts.
7. Port Scanner against a controlled host with known open/closed ports.
8. Cancel Ping/Traceroute/Discovery/Port Scan with Back.
9. Exit NetTools and verify original WiFi/RF/NRF24/NFC menus still open normally.
10. Monitor serial for panic, watchdog, heap errors, or reboot.

## Licensing / references

ESP32 Bit Pirate is MIT licensed and is used only as an architectural/reference source for network-service separation. NetTools implementation in this repository is written for the Bruce-derived AGPL-3.0 codebase and does not wholesale copy its UI or framework.
