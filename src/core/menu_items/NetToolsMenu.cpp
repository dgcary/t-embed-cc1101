#include "NetToolsMenu.h"

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"
#include "core/utils.h"
#include "core/wifi/wifi_common.h"
#include "modules/nettools/IcmpProbe.h"
#include "modules/nettools/NetToolsUtils.h"
#include "modules/nettools/PortScanner.h"

#include <WiFi.h>
#include <algorithm>
#include <globals.h>
#include <vector>

#ifdef T_EMBED_1101

namespace {

static constexpr uint32_t MAX_DISCOVERY_HOSTS = 1024;
static constexpr uint8_t TRACE_MAX_HOPS = 20;
static constexpr uint32_t PING_TIMEOUT_MS = 1000;
static constexpr uint32_t TRACE_TIMEOUT_MS = 1000;
static constexpr uint32_t DISCOVERY_TIMEOUT_MS = 100;
static constexpr uint32_t PORT_SCAN_TIMEOUT_MS = 150;

bool ensureConnected() {
    if (WiFi.status() == WL_CONNECTED) return true;

    wifiConnectMenu(WIFI_STA);
    if (WiFi.status() == WL_CONNECTED) return true;

    displayError("WiFi not connected", true);
    return false;
}

String promptTarget(const String &title) {
    String preset;
    if (WiFi.status() == WL_CONNECTED) {
        preset = WiFi.gatewayIP().toString();
    }
    const String target = keyboard(preset, 63, title);
    if (target == "\x1B") return "";
    return target;
}

bool resolveTargetOrError(const String &targetText, IPAddress &target) {
    if (targetText.length() == 0) return false;
    if (resolveNetToolsTarget(targetText, target)) return true;
    displayError("DNS lookup failed", true);
    return false;
}

String rttText(uint32_t rttUs) {
    return String(static_cast<float>(rttUs) / 1000.0f, 2) + " ms";
}

bool netToolsResultExitPressed() { return check(SelPress) || check(EscPress); }

void updateNetToolsResult(ScrollableTextArea &area, bool force) {
#ifdef HAS_ENCODER
    int32_t rotarySteps = drainRotarySteps();
    if (rotarySteps != 0) {
        check(PrevPress);
        check(NextPress);
        check(UpPress);
        check(DownPress);
        while (rotarySteps > 0) {
            area.scrollUp();
            --rotarySteps;
        }
        while (rotarySteps < 0) {
            area.scrollDown();
            ++rotarySteps;
        }
        vTaskDelay(4 / portTICK_PERIOD_MS);
        PrevPress = false;
        NextPress = false;
        UpPress = false;
        DownPress = false;
    }
#endif

    if (check(PrevPress) || check(UpPress)) area.scrollUp();
    else if (check(NextPress) || check(DownPress)) area.scrollDown();

    area.draw(force);
}

void showNetToolsResult(ScrollableTextArea &area, bool force = false) {
    area.draw(force);

    // Drain the input that opened/cancelled the result view, then wait for
    // either Select or Back. This keeps NetTools consistent with the device's
    // normal navigation without changing Bruce's global ScrollableTextArea.
    while (netToolsResultExitPressed()) {
        updateNetToolsResult(area, force);
        yield();
    }
    while (!netToolsResultExitPressed()) {
        updateNetToolsResult(area, force);
        yield();
    }
}

void showNetworkInfo() {
    if (!ensureConnected()) return;

    ScrollableTextArea area("NETWORK INFO");
    area.addLine("SSID: " + WiFi.SSID());
    area.addLine("BSSID: " + WiFi.BSSIDstr());
    area.addLine("IP: " + WiFi.localIP().toString());
    area.addLine("Mask: " + WiFi.subnetMask().toString());
    area.addLine("Gateway: " + WiFi.gatewayIP().toString());
    area.addLine("DNS1: " + WiFi.dnsIP(0).toString());
    area.addLine("DNS2: " + WiFi.dnsIP(1).toString());
    area.addLine("MAC: " + WiFi.macAddress());
    area.addLine("RSSI: " + String(WiFi.RSSI()) + " dBm");
    area.addLine("Channel: " + String(WiFi.channel()));
    showNetToolsResult(area);
}

void runPing() {
    if (!ensureConnected()) return;

    const String targetText = promptTarget("Ping host/IP");
    IPAddress target;
    if (!resolveTargetOrError(targetText, target)) return;

    IcmpProbeSession session;
    if (!session.begin()) {
        displayError("ICMP socket unavailable", true);
        return;
    }

    ScrollableTextArea area("PING");
    area.addLine(targetText + " -> " + target.toString());

    uint32_t minRtt = UINT32_MAX;
    uint32_t maxRtt = 0;
    uint64_t totalRtt = 0;
    uint32_t transmitted = 0;
    uint32_t received = 0;
    bool cancelled = false;

    for (uint16_t sequence = 1; sequence <= 4; ++sequence) {
        if (check(EscPress)) {
            cancelled = true;
            break;
        }

        ++transmitted;
        const IcmpProbeResult result = session.probe(target, 64, sequence, PING_TIMEOUT_MS);
        if (result.reachedTarget) {
            ++received;
            minRtt = std::min(minRtt, result.rttUs);
            maxRtt = std::max(maxRtt, result.rttUs);
            totalRtt += result.rttUs;
            area.addLine(
                "Reply " + result.responder.toString() + " " + rttText(result.rttUs) +
                " TTL=" + String(result.replyTtl)
            );
        } else {
            area.addLine("Request timeout seq=" + String(sequence));
        }
        area.draw(true);

        if (sequence < 4) {
            const uint32_t waitStart = millis();
            while (millis() - waitStart < 250) {
                if (check(EscPress)) {
                    cancelled = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (cancelled) break;
        }
    }

    const uint32_t loss = transmitted == 0 ? 0 : ((transmitted - received) * 100U) / transmitted;
    area.addLine("Tx=" + String(transmitted) + " Rx=" + String(received) + " Loss=" + String(loss) + "%");
    if (received > 0) {
        area.addLine(
            "RTT min/avg/max " + rttText(minRtt) + " / " +
            rttText(static_cast<uint32_t>(totalRtt / received)) + " / " + rttText(maxRtt)
        );
    }
    if (cancelled) area.addLine("Cancelled");
    showNetToolsResult(area, true);
}

void runTraceroute() {
    if (!ensureConnected()) return;

    const String targetText = promptTarget("Traceroute host/IP");
    IPAddress target;
    if (!resolveTargetOrError(targetText, target)) return;

    IcmpProbeSession session;
    if (!session.begin()) {
        displayError("ICMP socket unavailable", true);
        return;
    }

    ScrollableTextArea area("TRACEROUTE");
    area.addLine(targetText + " -> " + target.toString());
    area.addLine("Max hops: " + String(TRACE_MAX_HOPS));
    area.draw(true);

    bool cancelled = false;
    bool reached = false;
    for (uint8_t ttl = 1; ttl <= TRACE_MAX_HOPS; ++ttl) {
        if (check(EscPress)) {
            cancelled = true;
            break;
        }

        const IcmpProbeResult result = session.probe(target, ttl, ttl, TRACE_TIMEOUT_MS);
        if (result.success) {
            area.addLine(
                String(ttl) + "  " + result.responder.toString() + "  " + rttText(result.rttUs)
            );
            reached = result.reachedTarget;
        } else {
            area.addLine(String(ttl) + "  *");
        }
        area.draw(true);
        if (reached) break;
    }

    if (reached) area.addLine("Reached target");
    else if (cancelled) area.addLine("Cancelled");
    else area.addLine("Max hops reached");
    showNetToolsResult(area, true);
}

void drawDiscoveryProgress(uint32_t scanned, uint32_t total, uint32_t up) {
    displayRedStripe(
        "Scanning " + String(scanned) + "/" + String(total) + "  up:" + String(up),
        getComplementaryColor2(bruceConfig.priColor),
        bruceConfig.priColor
    );
}

void runHostDiscovery() {
    if (!ensureConnected()) return;

    const uint32_t local = ipToHostOrder(WiFi.localIP());
    const uint32_t mask = ipToHostOrder(WiFi.subnetMask());
    if (local == 0 || mask == 0) {
        displayError("Invalid network config", true);
        return;
    }

    const uint32_t network = local & mask;
    const uint32_t broadcast = network | (~mask);
    if (broadcast <= network + 1) {
        displayError("No usable subnet hosts", true);
        return;
    }

    const uint64_t usableHosts = static_cast<uint64_t>(broadcast) - network - 1ULL;
    if (usableHosts > MAX_DISCOVERY_HOSTS) {
        displayError("Subnet too large (>1024 hosts)", true);
        return;
    }

    IcmpProbeSession session;
    if (!session.begin()) {
        displayError("ICMP socket unavailable", true);
        return;
    }

    std::vector<IPAddress> responsive;
    responsive.reserve(std::min<uint64_t>(usableHosts, 64));
    uint32_t scanned = 0;
    uint16_t sequence = 1;
    bool cancelled = false;

    for (uint32_t address = network + 1; address < broadcast; ++address) {
        if (address == local) continue;
        if (check(EscPress)) {
            cancelled = true;
            break;
        }

        ++scanned;
        if (scanned == 1 || (scanned % 8) == 0) {
            drawDiscoveryProgress(scanned, static_cast<uint32_t>(usableHosts - 1), responsive.size());
        }

        const IPAddress target = hostOrderToIp(address);
        const IcmpProbeResult result = session.probe(target, 64, sequence++, DISCOVERY_TIMEOUT_MS);
        if (result.reachedTarget) responsive.push_back(target);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ScrollableTextArea area("HOST DISCOVERY");
    area.addLine("Subnet: " + hostOrderToIp(network).toString() + " / " + WiFi.subnetMask().toString());
    area.addLine("Scanned: " + String(scanned));
    area.addLine("Up: " + String(responsive.size()));
    if (cancelled) area.addLine("Scan cancelled");
    area.addLine("--- Responsive hosts ---");
    if (responsive.empty()) area.addLine("None");
    for (const IPAddress &host : responsive) area.addLine(host.toString());
    showNetToolsResult(area, true);
}

void drawPortProgress(uint32_t scanned, uint32_t total, uint32_t openCount) {
    displayRedStripe(
        "Ports " + String(scanned) + "/" + String(total) + "  open:" + String(openCount),
        getComplementaryColor2(bruceConfig.priColor),
        bruceConfig.priColor
    );
}

void showPortResults(
    const String &targetText,
    const IPAddress &target,
    uint32_t requestedPorts,
    uint32_t scannedPorts,
    uint32_t elapsedMs,
    bool cancelled,
    const std::vector<PortScanResult> &results
) {
    ScrollableTextArea area("PORT SCANNER");
    area.addLine(targetText + " -> " + target.toString());
    area.addLine("Scanned: " + String(scannedPorts) + "/" + String(requestedPorts));
    area.addLine("Open: " + String(results.size()));
    area.addLine("Elapsed: " + String(static_cast<float>(elapsedMs) / 1000.0f, 1) + " s");
    if (cancelled) area.addLine("Scan cancelled");
    area.addLine("--- Open TCP ports ---");
    if (results.empty()) area.addLine("None");
    for (const PortScanResult &result : results) {
        String line = String(result.port);
        if (result.service.length() > 0) line += "  " + result.service;
        area.addLine(line);
    }
    showNetToolsResult(area, true);
}

void runPortScanCommon(const String &targetText, const IPAddress &target) {
    const uint32_t total = commonTcpPortCount();
    uint32_t scanned = 0;
    bool cancelled = false;
    const uint32_t start = millis();

    std::vector<PortScanResult> results = scanCommonTcpPorts(
        target,
        PORT_SCAN_TIMEOUT_MS,
        [&](uint32_t done, uint32_t count) {
            scanned = done;
            drawPortProgress(done, count, 0);
            if (check(EscPress)) {
                cancelled = true;
                return false;
            }
            return true;
        }
    );

    showPortResults(targetText, target, total, scanned, millis() - start, cancelled, results);
}

void runPortScanRange(const String &targetText, const IPAddress &target) {
    const String startText = num_keyboard("1", 5, "Start TCP port");
    if (startText == "\x1B" || startText.length() == 0) return;
    const String endText = num_keyboard("1024", 5, "End TCP port");
    if (endText == "\x1B" || endText.length() == 0) return;

    const long startValue = startText.toInt();
    const long endValue = endText.toInt();
    if (startValue < 1 || startValue > 65535 || endValue < startValue || endValue > 65535) {
        displayError("Invalid port/range", true);
        return;
    }

    const uint16_t startPort = static_cast<uint16_t>(startValue);
    const uint16_t endPort = static_cast<uint16_t>(endValue);
    const uint32_t total = static_cast<uint32_t>(endPort) - startPort + 1;
    if (startPort == 1 && endPort == 65535) displayInfo("Full scan may take minutes", true);

    uint32_t scanned = 0;
    bool cancelled = false;
    const uint32_t start = millis();
    std::vector<PortScanResult> results = scanTcpPorts(
        target,
        startPort,
        endPort,
        PORT_SCAN_TIMEOUT_MS,
        [&](uint32_t done, uint32_t count) {
            scanned = done;
            drawPortProgress(done, count, 0);
            if (check(EscPress)) {
                cancelled = true;
                return false;
            }
            return true;
        }
    );

    showPortResults(targetText, target, total, scanned, millis() - start, cancelled, results);
}

void runPortScanner() {
    if (!ensureConnected()) return;

    const String targetText = promptTarget("Port scan host/IP");
    IPAddress target;
    if (!resolveTargetOrError(targetText, target)) return;

    std::vector<Option> scanOptions = {
        {"Common Ports", [=]() { runPortScanCommon(targetText, target); }},
        {"Port Range", [=]() { runPortScanRange(targetText, target); }},
        {"Back", []() {}},
    };
    loopOptions(scanOptions, MENU_TYPE_SUBMENU, "Port Scanner");
}

} // namespace

void NetToolsMenu::optionsMenu() {
    options = {
        {"Network Info", showNetworkInfo},
        {"Ping", runPing},
        {"Traceroute", runTraceroute},
        {"Host Discovery", runHostDiscovery},
        {"Port Scanner", runPortScanner},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "NetTools");
    options.clear();
}

void NetToolsMenu::drawIcon(float scale) {
    clearIconArea();

    const int radius = std::max(3, static_cast<int>(5 * scale));
    const int dx = std::max(16, static_cast<int>(28 * scale));
    const int dy = std::max(12, static_cast<int>(20 * scale));

    const int leftX = iconCenterX - dx;
    const int rightX = iconCenterX + dx;
    const int topY = iconCenterY - dy;
    const int bottomY = iconCenterY + dy;

    tft.drawLine(iconCenterX, iconCenterY, leftX, topY, bruceConfig.priColor);
    tft.drawLine(iconCenterX, iconCenterY, rightX, topY, bruceConfig.priColor);
    tft.drawLine(iconCenterX, iconCenterY, leftX, bottomY, bruceConfig.priColor);
    tft.drawLine(iconCenterX, iconCenterY, rightX, bottomY, bruceConfig.priColor);

    tft.fillCircle(iconCenterX, iconCenterY, radius + 1, bruceConfig.secColor);
    tft.fillCircle(leftX, topY, radius, bruceConfig.priColor);
    tft.fillCircle(rightX, topY, radius, bruceConfig.priColor);
    tft.fillCircle(leftX, bottomY, radius, bruceConfig.priColor);
    tft.fillCircle(rightX, bottomY, radius, bruceConfig.priColor);
}

#else

void NetToolsMenu::optionsMenu() {}
void NetToolsMenu::drawIcon(float) {}

#endif
