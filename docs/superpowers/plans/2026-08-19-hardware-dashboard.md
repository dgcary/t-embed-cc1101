# Hardware Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a first-entry `Dashboard` menu to the T-Embed CC1101 Plus Bruce 1.16.1 custom branch that shows a one-page, low-side-effect hardware health snapshot for CC1101, NRF24, PN532, IR, SD, WiFi, BLE, Battery, PSRAM, and Flash.

**Architecture:** Keep rendering and hardware access separate. `DashboardMenu` owns menu integration, input handling, and the 320x170 UI; `HardwareProbe` owns a formatting-neutral snapshot model and ten bounded probes. The feature is compiled/registered only for `T_EMBED_1101`, so no other Bruce board target is changed by this board-specific diagnostic feature.

**Tech Stack:** C++/Arduino ESP32 3.3.9, PlatformIO 6.1.x, TFT_eSPI/Bruce display helpers, SmartRC-CC1101, RF24 1.4.11, Adafruit PN532, ESP32 Arduino WiFi/ESP runtime APIs, SD/FS, Wire/I2C, Python 3 stdlib contract tests.

**Spec:** `docs/superpowers/specs/2026-08-19-hardware-dashboard-design.md`

## Global Constraints

- Base behavior is the accepted Bruce 1.16.1 Golden Baseline from `baseline/bruce-1.16.1` at `72dceb99cd7b8854438071164491c9dca9e0a675`.
- Work only on `feature/hardware-dashboard`; never modify `baseline/bruce-1.16.1`.
- PlatformIO target remains exactly `lilygo-t-embed-cc1101`.
- Do not change `platformio.ini`, board pin definitions, dependency versions, boot flow, or firmware version for this feature.
- Dashboard is the first main-menu item and does not auto-open at boot.
- Exactly ten items are in scope: CC1101, NRF24, PN532, IR, SD, WiFi, BLE, Battery, PSRAM, Flash.
- No RF transmission, NRF24 attack/jammer behavior, NFC tag scan/write/emulation, WiFi startup/scan/connect/disconnect/AP creation, BLE startup/advertise/scan/connect/disconnect, or persistent config writes.
- Status semantics are exact: `OK`, `FAIL`, `READY`, `ON`, `OFF`, `N/A`.
- A probe failure must not abort the remaining probes.
- No continuous polling; opening the page performs one snapshot and Select performs one new snapshot.
- Back/ESC must exit without reset.
- Preserve the existing Golden Baseline known issues; do not refactor or repair unrelated Bruce warnings in this change.

---

## File Structure

### Create

- `src/core/diagnostics/HardwareProbe.h` — status enum, item/snapshot data model, public snapshot API.
- `src/core/diagnostics/HardwareProbe.cpp` — all ten low-side-effect probes and compact detail formatting.
- `src/core/menu_items/DashboardMenu.h` — `MenuItemInterface` declaration for the new first menu item.
- `src/core/menu_items/DashboardMenu.cpp` — Dashboard icon, single-page rendering, Scanning state, Select refresh, Back exit.
- `tools/tests/test_hardware_dashboard_contract.py` — host-side static contract tests for menu ordering and prohibited side effects.

### Modify

- `src/core/main_menu.h` — guarded include/member for `DashboardMenu` on `T_EMBED_1101`.
- `src/core/main_menu.cpp` — guarded insertion of `dashboardMenu` at `_menuItems[0]`.

No other file is required by the initial implementation. In particular, do not expose the board's private `BQ27220 bq` object: the battery probe performs bounded read-only I2C register reads through the already configured system `Wire` bus.

---

### Task 1: Add executable Dashboard safety-contract tests

**Files:**
- Create: `tools/tests/test_hardware_dashboard_contract.py`

**Interfaces:**
- Consumes: repository source tree.
- Produces: executable stdlib `unittest` contract suite run with `python tools/tests/test_hardware_dashboard_contract.py`.

- [ ] **Step 1: Write the failing contract test before Dashboard source exists**

Create `tools/tests/test_hardware_dashboard_contract.py` with these concrete checks:

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / "src/core/diagnostics/HardwareProbe.cpp"
MENU_H = ROOT / "src/core/main_menu.h"
MENU_CPP = ROOT / "src/core/main_menu.cpp"
DASH_CPP = ROOT / "src/core/menu_items/DashboardMenu.cpp"


class HardwareDashboardContractTest(unittest.TestCase):
    def test_dashboard_sources_exist(self):
        self.assertTrue(PROBE.is_file())
        self.assertTrue(DASH_CPP.is_file())

    def test_probe_has_no_prohibited_active_operations(self):
        text = PROBE.read_text(encoding="utf-8")
        forbidden = (
            "initRfModule(",
            "SetTx(",
            "startConstCarrier(",
            "WiFi.begin(",
            "WiFi.scanNetworks(",
            "softAP(",
            "NimBLEDevice::init(",
            "startAdvertising(",
            "startScan(",
            "readPassiveTargetID(",
        )
        for token in forbidden:
            with self.subTest(token=token):
                self.assertNotIn(token, text)

    def test_dashboard_is_t_embed_only_and_first(self):
        header = MENU_H.read_text(encoding="utf-8")
        source = MENU_CPP.read_text(encoding="utf-8")
        self.assertIn('#include "menu_items/DashboardMenu.h"', header)
        self.assertIn("DashboardMenu dashboardMenu;", header)
        first = source.index("&dashboardMenu")
        wifi = source.index("&wifiMenu")
        self.assertLess(first, wifi)
        self.assertIn("#ifdef T_EMBED_1101", header)
        self.assertIn("#ifdef T_EMBED_1101", source)

    def test_dashboard_interaction_contract_is_present(self):
        text = DASH_CPP.read_text(encoding="utf-8")
        self.assertIn('"Scanning..."', text)
        self.assertIn("check(SelPress)", text)
        self.assertIn("check(EscPress)", text)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the contract test and verify the expected failure**

Run:

```bash
python tools/tests/test_hardware_dashboard_contract.py
```

Expected: FAIL because `HardwareProbe.cpp` and `DashboardMenu.cpp` do not exist yet.

- [ ] **Step 3: Commit the red test**

```bash
git add tools/tests/test_hardware_dashboard_contract.py
git commit -m "test: define hardware dashboard safety contract"
```

---

### Task 2: Implement the formatting-neutral HardwareProbe snapshot model

**Files:**
- Create: `src/core/diagnostics/HardwareProbe.h`
- Create: `src/core/diagnostics/HardwareProbe.cpp`

**Interfaces:**
- Consumes: `bruceConfigPins`, `sdcardMounted`, `wifiConnected`/ESP WiFi state, `BLEConnected`, system `Wire`, SD, ESP runtime APIs, existing CC1101 and NRF24 integration.
- Produces:
  - `enum class HardwareStatus : uint8_t { Ok, Fail, Ready, On, Off, NotAvailable };`
  - `struct HardwareItemStatus { HardwareStatus status; String detail; };`
  - `struct HardwareSnapshot` with fields `cc1101`, `nrf24`, `pn532`, `ir`, `sd`, `wifi`, `ble`, `battery`, `psram`, `flash`.
  - `const char *hardwareStatusText(HardwareStatus status);`
  - `HardwareSnapshot collectHardwareSnapshot();`

- [ ] **Step 1: Add the public model and API**

`src/core/diagnostics/HardwareProbe.h` must expose exactly:

```cpp
#pragma once

#include <Arduino.h>
#include <stdint.h>

enum class HardwareStatus : uint8_t {
    Ok,
    Fail,
    Ready,
    On,
    Off,
    NotAvailable,
};

struct HardwareItemStatus {
    HardwareStatus status = HardwareStatus::Fail;
    String detail;
};

struct HardwareSnapshot {
    HardwareItemStatus cc1101;
    HardwareItemStatus nrf24;
    HardwareItemStatus pn532;
    HardwareItemStatus ir;
    HardwareItemStatus sd;
    HardwareItemStatus wifi;
    HardwareItemStatus ble;
    HardwareItemStatus battery;
    HardwareItemStatus psram;
    HardwareItemStatus flash;
};

const char *hardwareStatusText(HardwareStatus status);
HardwareSnapshot collectHardwareSnapshot();
```

- [ ] **Step 2: Implement stable status text mapping first**

In `HardwareProbe.cpp`, implement:

```cpp
const char *hardwareStatusText(HardwareStatus status) {
    switch (status) {
        case HardwareStatus::Ok: return "OK";
        case HardwareStatus::Fail: return "FAIL";
        case HardwareStatus::Ready: return "READY";
        case HardwareStatus::On: return "ON";
        case HardwareStatus::Off: return "OFF";
        case HardwareStatus::NotAvailable: return "N/A";
    }
    return "FAIL";
}
```

The default is deliberately fail-safe rather than optimistic.

- [ ] **Step 3: Implement a bounded read-only BQ27220 register helper**

Use the board's already initialized system `Wire` bus. Do not call `Wire.begin()` from Dashboard. The helper writes the register address and reads two little-endian bytes:

```cpp
static bool readI2cU16LE(uint8_t address, uint8_t reg, uint16_t &value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(address, uint8_t(2)) != 2) return false;
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    value = uint16_t(lo) | (uint16_t(hi) << 8);
    return true;
}
```

For T-Embed CC1101 Plus use the board definition `BQ27220_I2C_ADDRESS` (`0x55`), voltage register `0x08`, and state-of-charge register `0x2C`. Reject SOC values above 100 and implausible zero voltage as a failed battery read.

- [ ] **Step 4: Implement CC1101 probe without the destructive RF feature initializer**

The implementation must:

1. acquire the configured CC1101 SPI bus with `acquireSPIBus(...)`;
2. call `initCC1101once(...)` only to ensure the one-time SmartRC driver binding is present;
3. call `ELECHOUSE_cc1101.getCC1101()` as the connectivity check;
4. never call `initRfModule()` or any TX mode;
5. detail uses the configured `bruceConfigPins.rfFreq`, formatted to two decimals plus ` MHz`.

Expected result shape:

```cpp
return {
    connected ? HardwareStatus::Ok : HardwareStatus::Fail,
    String(bruceConfigPins.rfFreq, 2) + " MHz"
};
```

- [ ] **Step 5: Implement NRF24 probe using existing safe SPI start then power-down**

Call `nrf_start(NRF_MODE_SPI)`. If it succeeds, immediately finish the diagnostic transaction with:

```cpp
NRFradio.stopListening();
NRFradio.powerDown();
```

Return `OK / "2.4 GHz / SPI"` on success and `FAIL / "Not detected"` on failure. Do not configure a channel, address, payload, constant carrier, scan, or TX payload.

- [ ] **Step 6: Implement PN532 presence/firmware probe without tag scanning**

For the fixed T-Embed CC1101 Plus I2C path:

```cpp
Adafruit_PN532 nfc(PN532_IRQ, PN532_RF_REST);
nfc.setInterface(SYS_I2C_SDA, SYS_I2C_SCL);
nfc.begin();
const uint32_t version = nfc.getFirmwareVersion();
const bool ok = version != 0;
if (ok) nfc.powerDown();
```

Return `OK / "I2C / 13.56M"` only if the firmware query succeeds. Do not call `readPassiveTargetID()` or any tag operation.

- [ ] **Step 7: Implement IR, SD, WiFi, and BLE passive-state probes**

Use these exact semantics:

```cpp
// IR
status = (bruceConfigPins.irRx >= 0 && bruceConfigPins.irTx >= 0)
    ? HardwareStatus::Ready
    : HardwareStatus::Fail;
detail = "RX" + String(bruceConfigPins.irRx) + " / TX" + String(bruceConfigPins.irTx);

// SD
if (!sdcardMounted) {
    status = HardwareStatus::NotAvailable;
    detail = "No Card";
} else {
    status = HardwareStatus::Ok;
    detail = compactBytes(SD.cardSize());
}

// WiFi
if (WiFi.status() == WL_CONNECTED) {
    status = HardwareStatus::On;
    detail = WiFi.SSID() + " / " + String(WiFi.RSSI()) + "dBm";
} else {
    status = HardwareStatus::Off;
    detail = "Not connected";
}

// BLE
status = BLEConnected ? HardwareStatus::On : HardwareStatus::Off;
detail = BLEConnected ? "Connected" : "Not active";
```

No state-changing WiFi or BLE API is allowed.

- [ ] **Step 8: Implement Battery, PSRAM, and Flash runtime probes**

Battery reads BQ27220 SOC (`0x2C`) and voltage (`0x08`) using the helper. On success:

```cpp
detail = String(soc) + "% / " + String(voltageMv / 1000.0f, 2) + "V";
```

PSRAM uses:

```cpp
const size_t total = ESP.getPsramSize();
const size_t free = ESP.getFreePsram();
```

`total > 0` means `OK`; detail is compact total plus free, e.g. `8.0M / 6.2M free`.

Flash uses:

```cpp
const uint32_t total = ESP.getFlashChipSize();
const size_t sketch = ESP.getSketchSize();
```

`total > 0` means `OK`; detail shows total and current sketch size, not the historical baseline build percentage.

- [ ] **Step 9: Implement `collectHardwareSnapshot()` so failures remain isolated**

Collect every field independently. Do not early-return after a failed probe. Serial logging is one concise line per failed probe per refresh, not per render frame.

- [ ] **Step 10: Run the contract test**

Run:

```bash
python tools/tests/test_hardware_dashboard_contract.py
```

Expected: the source-existence test now advances; prohibited-operation test must pass. Main-menu and Dashboard interaction tests may still fail because those tasks are not implemented yet.

- [ ] **Step 11: Compile the target to catch driver/API mismatches**

Run:

```bash
pio run -e lilygo-t-embed-cc1101
```

Expected: SUCCESS. If an external library API differs from the signatures above, adapt only the local probe implementation while preserving the published `HardwareProbe.h` API and all side-effect constraints.

- [ ] **Step 12: Commit the probe service**

```bash
git add src/core/diagnostics/HardwareProbe.h src/core/diagnostics/HardwareProbe.cpp
git commit -m "feat: add low-side-effect hardware probes"
```

---

### Task 3: Implement the 320x170 Dashboard menu and interaction loop

**Files:**
- Create: `src/core/menu_items/DashboardMenu.h`
- Create: `src/core/menu_items/DashboardMenu.cpp`

**Interfaces:**
- Consumes: `collectHardwareSnapshot()`, `hardwareStatusText()`, `HardwareSnapshot`, Bruce TFT globals/input events.
- Produces: `DashboardMenu : public MenuItemInterface` with `optionsMenu()`, `drawIcon()`, no custom theme dependency.

- [ ] **Step 1: Declare the menu item with no independent theme state**

`DashboardMenu.h`:

```cpp
#ifndef __DASHBOARD_MENU_H__
#define __DASHBOARD_MENU_H__

#include <MenuItemInterface.h>

class DashboardMenu : public MenuItemInterface {
public:
    DashboardMenu() : MenuItemInterface("Dashboard") {}

    void optionsMenu(void) override;
    void drawIcon(float scale) override;
    bool hasTheme() override { return false; }
    const String &themePath() override {
        static const String empty;
        return empty;
    }
};

#endif
```

- [ ] **Step 2: Implement status-color mapping as an internal render concern**

Use status text as authoritative. Use these existing palette semantics:

```cpp
static uint16_t statusColor(HardwareStatus status) {
    switch (status) {
        case HardwareStatus::Ok:
        case HardwareStatus::On:
            return bruceConfig.priColor;
        case HardwareStatus::Ready:
            return bruceConfig.secColor;
        case HardwareStatus::Fail:
            return TFT_RED;
        case HardwareStatus::Off:
        case HardwareStatus::NotAvailable:
            return TFT_DARKGREY;
    }
    return TFT_RED;
}
```

- [ ] **Step 3: Implement a compact main-menu icon**

Draw a small 2-column by 3-row diagnostic-grid icon inside `clearIconArea()` using Bruce primary/secondary colors. Do not add image assets or theme JSON changes.

- [ ] **Step 4: Implement the Scanning state**

Before collecting a snapshot:

```cpp
tft.fillScreen(bruceConfig.bgColor);
tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
tft.setTextSize(FM);
tft.drawCentreString("Scanning...", tftWidth / 2, tftHeight / 2 - 8, 1);
```

Then call `collectHardwareSnapshot()` exactly once.

- [ ] **Step 5: Implement the single-page 5x2 renderer**

Render exactly these cells in this order:

1. CC1101 | NRF24
2. PN532 | IR
3. SD | WiFi
4. BLE | Battery
5. PSRAM | Flash

Each cell has:

- compact item label at left/top;
- `hardwareStatusText()` at right/top;
- one detail line below;
- clipping/truncation to the cell width rather than horizontal scrolling.

Target geometry for landscape 320x170:

```cpp
const int headerH = 18;
const int footerH = 14;
const int gridTop = headerH;
const int gridBottom = tftHeight - footerH;
const int rowH = (gridBottom - gridTop) / 5;
const int colW = tftWidth / 2;
```

Header text is `HARDWARE DASHBOARD`. Footer text is `Press: Refresh` on the left and `Back: Exit` on the right. Keep all ten cells visible without scrolling.

- [ ] **Step 6: Implement refresh and exit loop**

The lifecycle must be:

```cpp
void DashboardMenu::optionsMenu() {
    auto refresh = []() {
        drawScanning();
        const HardwareSnapshot snapshot = collectHardwareSnapshot();
        drawSnapshot(snapshot);
    };

    refresh();
    while (true) {
        if (check(EscPress)) return;
        if (check(SelPress)) refresh();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

Do not refresh on encoder rotation. Do not add a timer.

- [ ] **Step 7: Run the contract test**

```bash
python tools/tests/test_hardware_dashboard_contract.py
```

Expected: Dashboard source existence, prohibited-operation, and interaction tests pass; menu-registration test still fails until Task 4.

- [ ] **Step 8: Compile the board target**

```bash
pio run -e lilygo-t-embed-cc1101
```

Expected: SUCCESS.

- [ ] **Step 9: Commit the Dashboard UI**

```bash
git add src/core/menu_items/DashboardMenu.h src/core/menu_items/DashboardMenu.cpp
git commit -m "feat: add hardware dashboard screen"
```

---

### Task 4: Register Dashboard as the T-Embed CC1101 Plus first main-menu item

**Files:**
- Modify: `src/core/main_menu.h`
- Modify: `src/core/main_menu.cpp`

**Interfaces:**
- Consumes: `DashboardMenu` from Task 3.
- Produces: first main-menu entry named `Dashboard` only when `T_EMBED_1101` is defined.

- [ ] **Step 1: Add guarded include and member in `main_menu.h`**

Add:

```cpp
#ifdef T_EMBED_1101
#include "menu_items/DashboardMenu.h"
#endif
```

Inside `MainMenu` public members, before the other menu objects:

```cpp
#ifdef T_EMBED_1101
    DashboardMenu dashboardMenu;
#endif
```

- [ ] **Step 2: Insert Dashboard first in the constructor list**

At the start of `_menuItems` in `main_menu.cpp`:

```cpp
_menuItems = {
#ifdef T_EMBED_1101
    &dashboardMenu,
#endif
    &wifiMenu,
    &bleMenu,
    // existing entries unchanged below
};
```

Do not reorder or remove any original Bruce entries.

- [ ] **Step 3: Run the full contract suite**

```bash
python tools/tests/test_hardware_dashboard_contract.py
```

Expected: all tests PASS.

- [ ] **Step 4: Run the required clean board build**

```bash
pio run -e lilygo-t-embed-cc1101 -t clean
pio run -e lilygo-t-embed-cc1101
```

Expected: SUCCESS with no new compile errors. Record RAM/Flash usage and compare it to Golden Baseline RAM `130,784 / 327,680` and Flash `4,198,578 / 16,777,216`; growth is expected but must remain comfortably within the existing limits.

- [ ] **Step 5: Commit menu integration**

```bash
git add src/core/main_menu.h src/core/main_menu.cpp
git commit -m "feat: register hardware dashboard menu"
```

---

### Task 5: Source review and automated regression verification

**Files:**
- Review: all Dashboard changes since `72dceb99cd7b8854438071164491c9dca9e0a675`.
- No production source modification unless a review finding requires a focused fix.

**Interfaces:**
- Consumes: completed feature branch.
- Produces: verified build and safety-contract evidence ready for physical-device testing.

- [ ] **Step 1: Inspect the branch diff against the Golden Baseline**

Run:

```bash
git diff --stat 72dceb99cd7b8854438071164491c9dca9e0a675..HEAD
git diff 72dceb99cd7b8854438071164491c9dca9e0a675..HEAD -- \
  src/core/diagnostics/HardwareProbe.* \
  src/core/menu_items/DashboardMenu.* \
  src/core/main_menu.* \
  tools/tests/test_hardware_dashboard_contract.py
```

Confirm there are no changes to `platformio.ini`, `boards/lilygo-t-embed-cc1101/*`, dependencies, boot flow, or unrelated modules.

- [ ] **Step 2: Re-run the host contract test from a clean invocation**

```bash
python tools/tests/test_hardware_dashboard_contract.py
```

Expected: all tests PASS.

- [ ] **Step 3: Re-run the final PlatformIO build**

```bash
pio run -e lilygo-t-embed-cc1101
```

Expected: `1 succeeded`; no new error. Existing Bruce/third-party Golden Baseline warnings may remain, but investigate any new warning originating from Dashboard files.

- [ ] **Step 4: Check branch cleanliness**

```bash
git status --short
```

Expected: no tracked changes after verification.

- [ ] **Step 5: If review required a fix, commit the focused correction**

Use a narrow commit message matching the actual defect, for example:

```bash
git commit -am "fix: preserve dashboard probe state semantics"
```

Do not create a cleanup/refactor commit for unrelated baseline warnings.

---

### Task 6: Codex physical-device acceptance test against the Golden Baseline

**Files:**
- No source changes during the first hardware pass.

**Interfaces:**
- Consumes: final `feature/hardware-dashboard` branch binary.
- Produces: physical-device pass/fail report with screenshots/logs and exact regression findings.

- [ ] **Step 1: Build and flash the exact feature branch**

Codex/local test worker runs:

```bash
git fetch origin
git checkout feature/hardware-dashboard
git pull --ff-only
pio run -e lilygo-t-embed-cc1101
pio run -e lilygo-t-embed-cc1101 -t upload --upload-port COM8
```

If the device enumerates on a different COM port, identify it by ESP32-S3 VID/PID and do not touch unrelated COM devices.

- [ ] **Step 2: Verify boot regression**

Confirm:

- boot behavior/timing remains recognizably the Bruce Golden Baseline;
- no panic, Guru Meditation, watchdog, assertion, or reboot loop;
- Dashboard does not auto-open;
- `Dashboard` is the first main-menu entry.

- [ ] **Step 3: Validate the initial disconnected/no-SD snapshot**

Expected on the known-good device with no SD and WiFi/BLE inactive:

```text
CC1101   OK       NRF24    OK
PN532    OK       IR       READY
SD       N/A      WiFi     OFF
BLE      OFF      Battery  OK
PSRAM    OK       Flash    OK
```

Expected details include configured CC1101 frequency, NRF24 SPI label, `I2C / 13.56M`, IR RX/TX pins, `No Card`, battery percent/voltage, approximately 8 MB PSRAM, and approximately 16 MB Flash.

- [ ] **Step 4: Verify Select refresh and Back exit**

Press the rotary encoder repeatedly for at least 20 refreshes. Confirm:

- one refresh per press;
- no progressive slowdown;
- no reset;
- Back exits every time without RST;
- no recurrence of a stable multi-second input hang attributable to Dashboard.

- [ ] **Step 5: Verify WiFi state reporting without side effects**

With WiFi OFF before entering Dashboard, confirm it remains OFF after entering/exiting. Then connect WiFi using the normal Bruce WiFi flow and reopen Dashboard; expect `ON` plus SSID/RSSI. Dashboard must not be the code path that connects or disconnects WiFi.

- [ ] **Step 6: Verify SD state transition**

Power off if required for safe card insertion, insert a known-good SD card, boot normally, and confirm Dashboard changes SD from `N/A / No Card` to `OK` with capacity detail. Remove the card only using the board's safe handling procedure.

- [ ] **Step 7: Run post-Dashboard hardware regressions**

After entering, refreshing, and exiting Dashboard, validate normal Bruce functions still work:

- CC1101: RSSI/Spectrum or passive RX.
- NRF24: Spectrum.
- PN532: read the user's test NFC card.
- IR: receive and replay the user's own remote signal.
- WiFi: normal scan/connect behavior.
- BLE: normal BLE Scan.
- Battery page/data still sensible.

Do not run MouseJack, jammer, or unrelated attack functions for Dashboard acceptance.

- [ ] **Step 8: Capture final acceptance report**

Report:

- exact branch and commit flashed;
- build RAM/Flash figures;
- each Dashboard cell status/detail;
- refresh/Back behavior;
- WiFi OFF→ON test;
- SD N/A→OK test if a card is available;
- CC1101/NRF24/PN532 post-Dashboard regression results;
- serial log anomalies;
- any difference from the Golden Baseline known issues.

Do not modify source during this test. Return failures to development as reproducible findings.

---

## Final Acceptance Gate

Before calling the feature complete, all of the following must be true:

1. `python tools/tests/test_hardware_dashboard_contract.py` passes.
2. `pio run -e lilygo-t-embed-cc1101` succeeds.
3. `Dashboard` is first only on `T_EMBED_1101`; no original menu entries are removed/reordered beyond this insertion.
4. All ten items fit on one 320x170 page.
5. Status labels use only the approved semantics and never convert pin configuration alone into `OK`.
6. Source review confirms no prohibited active operations in Dashboard probes.
7. Dashboard does not start WiFi/BLE or transmit RF/NFC/IR.
8. At least 20 manual refreshes complete without reset or progressive resource failure.
9. Back/ESC exits without RST.
10. CC1101, NRF24, and PN532 normal functionality still passes after Dashboard use.
11. Codex physical-device report passes against the accepted Bruce 1.16.1 Golden Baseline.
