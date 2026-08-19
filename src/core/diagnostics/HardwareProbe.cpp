#include "HardwareProbe.h"
#include <globals.h>

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

#ifdef T_EMBED_1101

#include "core/bus_HAL.h"
#include "modules/NRF24/nrf_common.h"
#include "modules/rf/rf_utils.h"
#include <Adafruit_PN532.h>
#include <SD.h>
#include <WiFi.h>
#include <Wire.h>

namespace {

constexpr uint8_t BQ27220_ADDRESS = 0x55;
constexpr uint8_t BQ27220_REG_VOLTAGE = 0x08;
constexpr uint8_t BQ27220_REG_STATE_OF_CHARGE = 0x2C;

HardwareItemStatus item(HardwareStatus status, const String &detail) {
    HardwareItemStatus result;
    result.status = status;
    result.detail = detail;
    return result;
}

String compactBytes(uint64_t bytes) {
    constexpr uint64_t KB = 1024ULL;
    constexpr uint64_t MB = 1024ULL * KB;

    if (bytes >= MB) return String(bytes / static_cast<float>(MB), 1) + "M";
    if (bytes >= KB) return String(bytes / static_cast<float>(KB), 1) + "K";
    return String(static_cast<unsigned long>(bytes)) + "B";
}

bool readI2cU16LE(TwoWire &wire, uint8_t address, uint8_t reg, uint16_t &value) {
    wire.beginTransmission(address);
    wire.write(reg);
    if (wire.endTransmission(true) != 0) return false;

    const size_t received = wire.requestFrom(address, static_cast<uint8_t>(2));
    if (received != 2 || wire.available() < 2) return false;

    const uint8_t lo = wire.read();
    const uint8_t hi = wire.read();
    value = uint16_t(lo) | (uint16_t(hi) << 8);
    return true;
}

HardwareItemStatus probeCC1101() {
    SPIClass *ccSpi = acquireSPIBus(
        bruceConfigPins.CC1101_bus.sck,
        bruceConfigPins.CC1101_bus.miso,
        bruceConfigPins.CC1101_bus.mosi
    );

    if (ccSpi) {
        ELECHOUSE_cc1101.setBeginEndLogic(false);
        initCC1101once(ccSpi);
    } else {
        ELECHOUSE_cc1101.setBeginEndLogic(true);
        initCC1101once(nullptr);
    }

    const bool connected = ELECHOUSE_cc1101.getCC1101();
    if (!connected) Serial.println("[Dashboard] CC1101 probe failed");

    return item(
        connected ? HardwareStatus::Ok : HardwareStatus::Fail,
        String(bruceConfigPins.rfFreq, 2) + " MHz"
    );
}

HardwareItemStatus probeNRF24() {
    const bool connected = nrf_start(NRF_MODE_SPI);
    if (connected) {
        NRFradio.stopListening();
        NRFradio.powerDown();
        return item(HardwareStatus::Ok, "2.4 GHz / SPI");
    }

    Serial.println("[Dashboard] NRF24 probe failed");
    return item(HardwareStatus::Fail, "Not detected");
}

HardwareItemStatus probePN532() {
    TwoWire *sysWire = getSysI2CBus();
    if (sysWire != &Wire) {
        Serial.println("[Dashboard] PN532 system I2C bus is not Wire");
        return item(HardwareStatus::Fail, "I2C unavailable");
    }

    // The Adafruit fork allocates its I2C device in the constructor and has no
    // matching destructor. Keep one instance for the firmware lifetime so
    // repeated Dashboard refreshes cannot leak heap memory.
    static Adafruit_PN532 nfc(PN532_IRQ, PN532_RF_REST, &Wire);

    lockSysI2CBus();
    const bool begun = nfc.begin();
    const uint32_t version = begun ? nfc.getFirmwareVersion() : 0;
    if (version != 0) nfc.powerDown();
    unlockSysI2CBus();

    if (version == 0) Serial.println("[Dashboard] PN532 probe failed");
    return item(version != 0 ? HardwareStatus::Ok : HardwareStatus::Fail, "I2C / 13.56M");
}

HardwareItemStatus probeIR() {
    const bool configured = bruceConfigPins.irRx >= 0 && bruceConfigPins.irTx >= 0;
    const String detail = "RX" + String(bruceConfigPins.irRx) + " / TX" + String(bruceConfigPins.irTx);
    return item(configured ? HardwareStatus::Ready : HardwareStatus::Fail, detail);
}

HardwareItemStatus probeSD() {
    if (!sdcardMounted) return item(HardwareStatus::NotAvailable, "No Card");

    const uint64_t capacity = SD.cardSize();
    if (capacity == 0) {
        Serial.println("[Dashboard] SD mounted but capacity read failed");
        return item(HardwareStatus::Fail, "Read error");
    }
    return item(HardwareStatus::Ok, compactBytes(capacity));
}

HardwareItemStatus probeWiFi() {
    if (WiFi.status() != WL_CONNECTED) return item(HardwareStatus::Off, "Not connected");

    String ssid = WiFi.SSID();
    if (ssid.length() > 12) ssid = ssid.substring(0, 12);
    return item(HardwareStatus::On, ssid + " / " + String(WiFi.RSSI()) + "dBm");
}

HardwareItemStatus probeBLE() {
    return item(BLEConnected ? HardwareStatus::On : HardwareStatus::Off, BLEConnected ? "Connected" : "Not active");
}

HardwareItemStatus probeBattery() {
    TwoWire *wire = getSysI2CBus();
    if (wire == nullptr) {
        Serial.println("[Dashboard] Battery probe has no system I2C bus");
        return item(HardwareStatus::Fail, "I2C unavailable");
    }

    uint16_t soc = 0;
    uint16_t voltageMv = 0;

    lockSysI2CBus();
    const bool socOk = readI2cU16LE(*wire, BQ27220_ADDRESS, BQ27220_REG_STATE_OF_CHARGE, soc);
    const bool voltageOk = readI2cU16LE(*wire, BQ27220_ADDRESS, BQ27220_REG_VOLTAGE, voltageMv);
    unlockSysI2CBus();

    const bool plausible = socOk && voltageOk && soc <= 100 && voltageMv >= 2500 && voltageMv <= 5000;
    if (!plausible) {
        Serial.println("[Dashboard] BQ27220 probe failed");
        return item(HardwareStatus::Fail, "Read error");
    }

    return item(HardwareStatus::Ok, String(soc) + "% / " + String(voltageMv / 1000.0f, 2) + "V");
}

HardwareItemStatus probePSRAM() {
    const size_t total = ESP.getPsramSize();
    const size_t free = ESP.getFreePsram();
    if (total == 0) {
        Serial.println("[Dashboard] PSRAM not detected");
        return item(HardwareStatus::Fail, "Not detected");
    }

    return item(HardwareStatus::Ok, compactBytes(total) + " / " + compactBytes(free) + " free");
}

HardwareItemStatus probeFlash() {
    const uint32_t total = ESP.getFlashChipSize();
    const size_t sketch = ESP.getSketchSize();
    if (total == 0) {
        Serial.println("[Dashboard] Flash size read failed");
        return item(HardwareStatus::Fail, "Read error");
    }

    return item(HardwareStatus::Ok, compactBytes(total) + " / " + compactBytes(sketch) + " app");
}

} // namespace

HardwareSnapshot collectHardwareSnapshot() {
    HardwareSnapshot snapshot;

    snapshot.cc1101 = probeCC1101();
    snapshot.nrf24 = probeNRF24();
    snapshot.pn532 = probePN532();
    snapshot.ir = probeIR();
    snapshot.sd = probeSD();
    snapshot.wifi = probeWiFi();
    snapshot.ble = probeBLE();
    snapshot.battery = probeBattery();
    snapshot.psram = probePSRAM();
    snapshot.flash = probeFlash();

    return snapshot;
}

#else

HardwareSnapshot collectHardwareSnapshot() {
    return HardwareSnapshot{};
}

#endif
