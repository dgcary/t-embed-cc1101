# T-Embed CC1101 Plus Hardware Dashboard Design

Date: 2026-08-19
Status: Approved design, pending implementation plan
Base: `baseline/bruce-1.16.1` @ `72dceb99cd7b8854438071164491c9dca9e0a675`
Target branch: `feature/hardware-dashboard`
Hardware: LILYGO T-Embed CC1101 Plus

## 1. Goal

Add a dedicated `Dashboard` item as the first entry in the Bruce main menu. Opening it performs a bounded, low-side-effect hardware health snapshot and displays status plus key parameters for the hardware that is relevant to this board.

The Dashboard is diagnostic only. It must not silently reconfigure radios, start Wi-Fi/BLE, transmit RF, alter saved settings, or change the Bruce boot flow.

## 2. Scope

The single-page dashboard covers exactly these ten items:

1. CC1101
2. NRF24
3. PN532
4. IR
5. SD
6. WiFi
7. BLE
8. Battery
9. PSRAM
10. Flash

LoRa, FM, W5500, GPS, microphone, speaker, RGB LED, USB, LittleFS, uptime, and heap are out of scope for this first version.

## 3. Entry and Interaction

- `Dashboard` is inserted as the first main-menu item.
- Boot behavior is unchanged; the Dashboard does not auto-open.
- Entering the Dashboard shows a short `Scanning...` state and performs one snapshot.
- Pressing the rotary encoder/select button refreshes the complete snapshot.
- Pressing Back/ESC exits to the Bruce main menu.
- There is no continuous background probing and no periodic timer-driven radio reinitialization.

## 4. Status Semantics

Status text is authoritative; color is only a visual aid.

- `OK`: the current snapshot positively verified the device/resource.
- `FAIL`: the current snapshot attempted a valid check and it failed.
- `READY`: the board has the expected fixed hardware/pins, but there is no safe and trustworthy software self-test for the physical path.
- `ON`: the corresponding software subsystem is currently active/connected.
- `OFF`: the subsystem is currently inactive; this is not a fault.
- `N/A`: the resource is not currently available, for example no SD card inserted; this is not a fault.

The implementation must not report `OK` when it only knows that pins are configured.

## 5. Probe Strategy

Use an independent, minimal `HardwareProbe` layer rather than calling full feature flows that reconfigure hardware.

### 5.1 CC1101

Goal: verify SPI communication without changing the user's RF configuration.

- Reuse the configured CC1101 SPI bus/pins.
- Perform the minimum non-transmitting identity/connectivity check supported by the existing SmartRC-CC1101 integration.
- Do not call the full `initRfModule()` flow for health checking because it changes radio parameters and mode.
- Do not transmit.
- Display the configured Bruce RF frequency as a parameter, explicitly treating it as configuration rather than proof of the radio's live tuning state.

Display example:

`CC1101   OK`
`433.92 MHz`

### 5.2 NRF24

Goal: verify the onboard nRF24 over SPI with minimal disturbance.

- Use the configured nRF24 SPI/CE/CS pins.
- Initialize only as required to verify that the radio responds.
- Do not scan, transmit, jam, or open attack flows.
- If the probe temporarily powers the radio, finish by placing it in a benign powered-down state unless preserving an already-active state is safely possible.

Display example:

`NRF24    OK`
`2.4 GHz / SPI`

### 5.3 PN532

Goal: verify that the onboard PN532 responds on the configured I2C path.

- Perform a lightweight presence/firmware-version style query using the existing PN532 integration where practical.
- Do not scan for tags as part of Dashboard refresh.
- Do not enable emulation or write operations.

Display example:

`PN532    OK`
`I2C / 13.56M`

### 5.4 IR

IR must not be falsely self-certified.

- Do not transmit an IR test pulse.
- Do not require an external target or loopback.
- If board configuration contains the expected IR RX/TX pins, report `READY`.
- Show the configured pins.

For the current T-Embed CC1101 Plus baseline this is expected to render approximately:

`IR       READY`
`RX1 / TX2`

### 5.5 SD

- Use the existing mounted/card state where trustworthy.
- If no card is present, report `N/A` and `No Card` rather than `FAIL`.
- If a card is mounted, display capacity in a compact form when the API can provide it without remounting.
- A genuine mount/probe error with a card expected/present may be shown as `FAIL`.

### 5.6 WiFi

The Dashboard must not start Wi-Fi.

- Read current Bruce/ESP32 Wi-Fi state only.
- If connected, report `ON` and show SSID plus RSSI where available.
- If not connected, report `OFF` and `Not connected`.

### 5.7 BLE

The Dashboard must not start BLE.

- Read the current Bruce BLE state only.
- Report `ON` or `OFF`.
- Do not advertise, scan, or connect merely to populate the Dashboard.

### 5.8 Battery

- Query the existing BQ27220-backed battery data path used by the board/firmware.
- On success, display battery percentage and voltage.
- On communication/read failure, report `FAIL` rather than inventing values.

Display example:

`Battery  OK`
`82% / 3.96V`

### 5.9 PSRAM

- Read ESP32 runtime PSRAM information.
- Verify expected PSRAM availability.
- Display total and free PSRAM in compact units.

Display example:

`PSRAM    OK`
`8MB / 6.2MB free`

### 5.10 Flash

- Read runtime flash size.
- Display total flash size.
- Display firmware/application usage only if a reliable runtime value is available without brittle assumptions; otherwise show a simpler stable parameter such as total size plus app partition size.
- This field must not hard-code the Golden Baseline's 25% build result as a runtime fact.

## 6. UI Layout

Target display: 320 x 170 landscape.

The screen uses a header plus a 5-row by 2-column status grid and a compact footer.

```text
+--------------------------------------+ 
|          HARDWARE DASHBOARD          |
+------------------+-------------------+
| CC1101     OK    | NRF24       OK    |
| 433.92 MHz       | 2.4 GHz / SPI     |
+------------------+-------------------+
| PN532      OK    | IR        READY   |
| I2C / 13.56M     | RX1 / TX2         |
+------------------+-------------------+
| SD         N/A   | WiFi        ON    |
| No Card          | MySSID / -52dBm   |
+------------------+-------------------+
| BLE        OFF   | Battery     OK    |
| Not active       | 82% / 3.96V       |
+------------------+-------------------+
| PSRAM      OK    | Flash       OK    |
| 8MB / 6.2M free  | 16MB / app size   |
+--------------------------------------+
| Press: Refresh             Back: Exit|
+--------------------------------------+
```

Exact pixel spacing may be adjusted during implementation to fit Bruce font metrics, but the information architecture must remain a single page with no scrolling.

### Visual treatment

- `OK` / `ON`: positive status color derived from the current Bruce theme where practical.
- `READY`: warning/neutral accent.
- `OFF` / `N/A`: neutral/dimmed.
- `FAIL`: error/red.
- Always render the textual status, even when color is present.
- Follow current Bruce background, primary color, orientation, and text rendering conventions; do not introduce a separate theme system.

## 7. Architecture

Add two narrowly scoped components:

### 7.1 Dashboard menu item

A new `MenuItemInterface` implementation owns:

- main-menu label/icon
- Dashboard screen lifecycle
- rendering
- refresh input handling

It must not contain detailed device-driver logic.

### 7.2 Hardware probe service

A new hardware-probe module owns:

- status/result model
- one probe function per dashboard item
- collection of a complete snapshot
- formatting-neutral data values

Proposed result model conceptually contains:

- status enum
- short primary value
- optional secondary/detail value

Rendering consumes the snapshot and does not directly manipulate radios.

## 8. Side-Effect Rules

The following are hard requirements:

- No RF transmission.
- No NRF24 jammer/MouseJack behavior.
- No PN532 tag scan/write/emulation during health refresh.
- No Wi-Fi startup, scan, connect, disconnect, or AP creation.
- No BLE startup, advertisement, scan, connection, or disconnect.
- No persistent configuration writes.
- No boot-flow modification.
- No modification of `baseline/bruce-1.16.1`.

If a low-level probe cannot be made safe without changing an active subsystem state, prefer a non-invasive state such as `READY`/current-state reporting over a destructive `OK` test.

## 9. Error Handling

- A failure in one probe must not abort the rest of the Dashboard.
- Each probe returns its own status; the page always renders all ten items.
- Probe failures should produce concise serial diagnostics for development without flooding the serial log on every draw frame.
- Refresh must remain responsive even if one device is absent; hardware calls must use bounded waits/timeouts.
- Back/ESC must always remain usable after a completed snapshot.

## 10. Expected Source Changes

Implementation is expected to touch a small set of files only, approximately:

- new Dashboard menu header/source under `src/core/menu_items/`
- new hardware probe header/source under `src/core/` or a focused diagnostics subdirectory
- `src/core/main_menu.*` or menu item declarations/registration needed to insert Dashboard first
- minimal build/include wiring if required by the existing project structure

Do not refactor unrelated Bruce modules as part of this feature.

## 11. Testing

### 11.1 Build verification

Required:

`platformio run -e lilygo-t-embed-cc1101`

The build must succeed from the feature branch without changing the PlatformIO environment or board pin definitions.

### 11.2 Regression checks

Compare against the accepted Golden Baseline:

- normal boot remains unchanged
- original main-menu entries remain functional
- Back button behavior remains correct
- CC1101 normal receive/spectrum still works after entering/exiting Dashboard
- NRF24 Spectrum still works after entering/exiting Dashboard
- PN532 tag read still works after entering/exiting Dashboard
- Wi-Fi/BLE remain off if they were off before Dashboard was opened

### 11.3 Dashboard hardware checks

On the known-good T-Embed CC1101 Plus unit:

- CC1101 -> `OK`
- NRF24 -> `OK`
- PN532 -> `OK`
- IR -> `READY`
- SD with no card -> `N/A`
- WiFi disconnected -> `OFF`
- WiFi connected -> `ON` with meaningful detail
- BLE inactive -> `OFF`
- Battery -> `OK` with plausible percent/voltage
- PSRAM -> `OK`, approximately 8 MB total on this hardware
- Flash -> `OK`, approximately 16 MB total on this hardware

Then insert an SD card in a separate test and confirm SD changes from `N/A` to `OK` without regressions.

### 11.4 Refresh and navigation

- First open performs exactly one snapshot.
- Select/encoder press causes one new snapshot.
- Repeated refreshes do not leak memory or progressively break SPI/I2C devices.
- Back exits normally without RST.

## 12. Acceptance Criteria

The feature is accepted when:

1. `Dashboard` is the first main-menu entry and boot behavior remains unchanged.
2. All ten scoped items fit on one 320 x 170 screen with readable status and key parameters.
3. Status semantics distinguish `OK`, `FAIL`, `READY`, `ON`, `OFF`, and `N/A` correctly.
4. Dashboard probing does not transmit RF or start Wi-Fi/BLE.
5. Entering, refreshing, and exiting Dashboard does not break subsequent CC1101, NRF24, PN532, Wi-Fi, BLE, or menu operation.
6. The `lilygo-t-embed-cc1101` PlatformIO environment builds successfully.
7. Codex physical-device regression testing passes against the established Bruce 1.16.1 Golden Baseline.

## 13. Non-Goals

This version does not:

- repair existing Bruce 1.16.1 warnings or baseline known issues
- hide unsupported LoRa/FM menu entries
- add Chinese localization
- auto-run at boot
- implement historical telemetry, logs, charts, or background monitoring
- provide RF transmit/attack shortcuts

Those can be separate feature changes after this Dashboard is stable.
