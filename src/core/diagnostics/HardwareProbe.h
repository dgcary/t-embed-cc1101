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
