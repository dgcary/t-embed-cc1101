#include "DashboardMenu.h"
#include "core/diagnostics/HardwareProbe.h"
#include "core/display.h"
#include <globals.h>

#ifdef T_EMBED_1101

namespace {

uint16_t statusColor(HardwareStatus status) {
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

String fitText(String text, int maxWidth) {
    if (maxWidth <= 0) return "";
    while (text.length() > 0 && tft.textWidth(text, 1) > maxWidth) {
        text.remove(text.length() - 1);
    }
    return text;
}

void drawScanning() {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FM);
    tft.drawCentreString("Scanning...", tftWidth / 2, tftHeight / 2 - 8, 1);
}

void drawCell(
    int x,
    int y,
    int width,
    int height,
    const char *label,
    const HardwareItemStatus &hardware
) {
    const int padX = 4;
    const int topY = y + 3;
    const int detailY = y + 14;

    tft.drawRect(x, y, width, height, bruceConfig.priColor);
    tft.setTextSize(FP);

    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    const String labelText = fitText(String(label), width / 2 - padX - 2);
    tft.drawString(labelText, x + padX, topY, 1);

    tft.setTextColor(statusColor(hardware.status), bruceConfig.bgColor);
    const String statusText = fitText(String(hardwareStatusText(hardware.status)), width / 2 - padX - 2);
    tft.drawRightString(statusText, x + width - padX, topY, 1);

    tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    const String detail = fitText(hardware.detail, width - 2 * padX);
    tft.drawString(detail, x + padX, detailY, 1);
}

void drawSnapshot(const HardwareSnapshot &snapshot) {
    constexpr int headerH = 18;
    constexpr int footerH = 14;
    const int gridTop = headerH;
    const int gridBottom = tftHeight - footerH;
    const int rowH = (gridBottom - gridTop) / 5;
    const int colW = tftWidth / 2;

    tft.fillScreen(bruceConfig.bgColor);

    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawCentreString("HARDWARE DASHBOARD", tftWidth / 2, 4, 1);

    const HardwareItemStatus *cells[10] = {
        &snapshot.cc1101,
        &snapshot.nrf24,
        &snapshot.pn532,
        &snapshot.ir,
        &snapshot.sd,
        &snapshot.wifi,
        &snapshot.ble,
        &snapshot.battery,
        &snapshot.psram,
        &snapshot.flash,
    };
    const char *labels[10] = {
        "CC1101",
        "NRF24",
        "PN532",
        "IR",
        "SD",
        "WiFi",
        "BLE",
        "Battery",
        "PSRAM",
        "Flash",
    };

    for (int index = 0; index < 10; ++index) {
        const int row = index / 2;
        const int col = index % 2;
        const int x = col * colW;
        const int y = gridTop + row * rowH;
        const int width = (col == 1) ? tftWidth - x : colW;
        const int height = (row == 4) ? gridBottom - y : rowH;
        drawCell(x, y, width, height, labels[index], *cells[index]);
    }

    const int footerY = tftHeight - footerH;
    tft.setTextSize(FP);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawString("Press: Refresh", 3, footerY + 3, 1);
    tft.drawRightString("Back: Exit", tftWidth - 3, footerY + 3, 1);
}

} // namespace

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

void DashboardMenu::drawIcon(float scale) {
    clearIconArea();

    const int cellW = max(10, static_cast<int>(22 * scale));
    const int cellH = max(8, static_cast<int>(13 * scale));
    const int gap = max(2, static_cast<int>(4 * scale));
    const int totalW = 2 * cellW + gap;
    const int totalH = 3 * cellH + 2 * gap;
    const int startX = iconCenterX - totalW / 2;
    const int startY = iconCenterY - totalH / 2;

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 2; ++col) {
            const int x = startX + col * (cellW + gap);
            const int y = startY + row * (cellH + gap);
            tft.drawRect(x, y, cellW, cellH, bruceConfig.priColor);
            const uint16_t dotColor = ((row + col) % 2 == 0) ? bruceConfig.priColor : bruceConfig.secColor;
            tft.fillCircle(x + cellW - 4, y + 4, max(1, static_cast<int>(2 * scale)), dotColor);
        }
    }
}

#else

void DashboardMenu::optionsMenu() {}
void DashboardMenu::drawIcon(float) {}

#endif
