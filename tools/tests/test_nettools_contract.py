#!/usr/bin/env python3
"""Source-level regression contracts for the T-Embed NetTools v1 feature."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read_required(relative_path: str) -> str:
    path = ROOT / relative_path
    if not path.exists():
        raise AssertionError(f"required NetTools source is missing: {relative_path}")
    return path.read_text(encoding="utf-8")


class NetToolsContractTests(unittest.TestCase):
    def test_main_menu_registers_nettools_between_dashboard_and_wifi(self):
        header = read_required("src/core/main_menu.h")
        source = read_required("src/core/main_menu.cpp")
        self.assertIn('menu_items/NetToolsMenu.h', header)
        self.assertIn("NetToolsMenu netToolsMenu", header)
        self.assertIn("&dashboardMenu", source)
        self.assertIn("&netToolsMenu", source)
        self.assertIn("&wifiMenu", source)
        self.assertLess(source.index("&dashboardMenu"), source.index("&netToolsMenu"))
        self.assertLess(source.index("&netToolsMenu"), source.index("&wifiMenu"))

    def test_icmp_probe_uses_raw_socket_ttl_and_real_icmp_types(self):
        source = read_required("src/modules/nettools/IcmpProbe.cpp")
        self.assertIn("SOCK_RAW", source)
        self.assertIn("IPPROTO_ICMP", source)
        self.assertIn("IP_TTL", source)
        self.assertIn("lwip_setsockopt", source)
        self.assertTrue("type == 0" in source or "ICMP_ECHO_REPLY" in source)
        self.assertTrue("type == 11" in source or "ICMP_TIME_EXCEEDED" in source)
        self.assertIn("identifier", source)
        self.assertIn("sequence", source)
        self.assertIn("lwip_close", source)

    def test_host_discovery_has_hard_subnet_limit(self):
        source = read_required("src/core/menu_items/NetToolsMenu.cpp")
        self.assertIn("MAX_DISCOVERY_HOSTS", source)
        self.assertIn("1024", source)
        self.assertIn("Subnet too large", source)

    def test_port_scanner_is_bounded_and_closes_sockets(self):
        source = read_required("src/modules/nettools/PortScanner.cpp")
        self.assertIn("MAX_CONCURRENT_SOCKETS", source)
        self.assertIn("= 8", source)
        self.assertIn("lwip_select", source)
        self.assertIn("SO_ERROR", source)
        self.assertIn("lwip_close", source)

    def test_result_views_accept_back_without_global_scrollable_change(self):
        menu = read_required("src/core/menu_items/NetToolsMenu.cpp")
        global_scroll = read_required("src/core/scrollableTextArea.cpp")
        self.assertIn("showNetToolsResult", menu)
        self.assertIn("check(EscPress)", menu)
        self.assertIn("check(SelPress)", menu)
        self.assertGreaterEqual(menu.count("showNetToolsResult(area"), 5)
        self.assertNotIn("area.show(", menu)
        self.assertNotIn("EscPress", global_scroll)

    def test_result_force_draw_is_initial_only(self):
        menu = read_required("src/core/menu_items/NetToolsMenu.cpp")
        self.assertIn("void updateNetToolsResult(ScrollableTextArea &area)", menu)
        self.assertNotIn("void updateNetToolsResult(ScrollableTextArea &area, bool force)", menu)
        self.assertIn("area.draw(force);", menu)
        self.assertIn("area.draw();", menu)
        self.assertGreaterEqual(menu.count("updateNetToolsResult(area);"), 2)
        self.assertNotIn("updateNetToolsResult(area, force);", menu)

    def test_nettools_is_diagnostic_only_and_does_not_patch_existing_attack_modules(self):
        menu = read_required("src/core/menu_items/NetToolsMenu.cpp")
        forbidden = (
            "stationDeauth",
            "ARPSpoofer",
            "ARPoisoner",
            "DHCPStarvation",
            "MACFlooding",
            "karma",
            "deauth",
        )
        lower_menu = menu.lower()
        for token in forbidden:
            self.assertNotIn(token.lower(), lower_menu)


if __name__ == "__main__":
    unittest.main()
