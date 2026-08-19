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
