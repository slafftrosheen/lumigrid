#!/usr/bin/env python3
"""Regression checks for the linked boot-stack guard."""
import unittest
from verify_boot_stack import check_frames, check_startup_path, parse_assembly


class BootStackTests(unittest.TestCase):
    def test_parser_preserves_frames_and_direct_calls(self):
        nodes = parse_assembly("""40000000 <app_main>:
40000000: 000000 entry a1, 0x50
40000003: 000000 call8 40000100 <lg::SceneManager::init()>
40000100 <lg::SceneManager::init()>:
40000100: 000000 entry a1, 64
""")
        self.assertEqual(nodes["40000000"]["frame"], 80)
        self.assertEqual(nodes["40000100"]["frame"], 64)
        self.assertEqual(nodes["40000000"]["calls"], {"40000100"})

    def test_missing_required_function_is_rejected(self):
        nodes = {"1": {"name": "app_main", "frame": 80, "calls": set()}}
        with self.assertRaisesRegex(ValueError, "Missing stack evidence"):
            check_frames(nodes)

    def test_large_configuration_lambda_is_rejected(self):
        names = {
            "app_main": "app_main", "runtime_init": "lg::LumigridRuntime::init()",
            "runtime_start": "lg::LumigridRuntime::start()",
            "network_start": "lg::LumigridRuntime::start_network_services()",
            "api_start": "lg::FactoryApi::start(unsigned short)",
            "scene_init": "lg::SceneManager::init()",
            "config_load": "lg::ConfigStore::load_config(lg::DeviceConfig&)",
        }
        nodes = {str(i): {"name": name, "frame": 64, "calls": set()}
                 for i, name in enumerate(names.values())}
        nodes["lambda"] = {"name": "lg::ConfigStore::load_config(lg::DeviceConfig&)::{lambda()#1}::operator()() const",
                           "frame": 1168, "calls": set()}
        with self.assertRaisesRegex(ValueError, "config_load.*1168"):
            check_frames(nodes)

    def test_inflated_main_entry_is_rejected(self):
        nodes = {"1": {"name": "app_main", "frame": 320, "calls": set()}}
        with self.assertRaisesRegex(ValueError, "exceeds 256"):
            check_frames(nodes)

    def test_small_individual_frames_with_deep_chain_are_rejected(self):
        nodes = {str(i): {"name": "app_main" if i == 0 else f"callee{i}",
                          "frame": 800, "calls": {str(i + 1)} if i < 9 else set()}
                 for i in range(10)}
        with self.assertRaisesRegex(ValueError, "direct-call path 8000 exceeds"):
            check_startup_path(nodes)

    def test_sequential_calls_do_not_add_as_nested_calls(self):
        nodes = {
            "0": {"name": "app_main", "frame": 80, "calls": {"1", "2"}},
            "1": {"name": "init", "frame": 4000, "calls": set()},
            "2": {"name": "start", "frame": 4000, "calls": set()},
        }
        self.assertIn("4080 bytes", check_startup_path(nodes))

    def test_missing_entry_instruction_is_rejected(self):
        nodes = parse_assembly("40000000 <app_main>:\n40000000: nop\n")
        with self.assertRaisesRegex(ValueError, "Missing stack evidence"):
            check_frames(nodes)


if __name__ == "__main__":
    unittest.main()
