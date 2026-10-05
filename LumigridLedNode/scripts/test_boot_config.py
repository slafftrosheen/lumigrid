"""LG-038.1 configuration regressions; no firmware or configuration mutation."""
import contextlib
import io
from pathlib import Path
import runpy
import unittest
from unittest.mock import patch

CHECKER = Path(__file__).with_name("check_invariants.py")
READ_TEXT = Path.read_text

class BootConfigTests(unittest.TestCase):
    def check(self, mutation=None, allow_owner=False, platform=None, hook=False, async_i2c=False):
        def read(path, *args, **kwargs):
            text = READ_TEXT(path, *args, **kwargs)
            if path.name == "platformio.ini" and platform:
                text = text.replace("espressif32@6.7.0", platform)
            if path.name == "Pca9685Driver.cpp" and async_i2c:
                text = text.replace(".trans_queue_depth = 0,", ".trans_queue_depth = 4,")
            if path.name == "main.cpp" and hook:
                text += "\nvoid vApplicationGetTimerTaskMemory() {}\n"
            if path.name == "boot_memory_policy.json" and not allow_owner:
                return "{}"
            if path.name in ("sdkconfig.factory", "sdkconfig.defaults", "sdkconfig.factory.defaults"):
                text = text.replace("CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=y",
                                    "# CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION is not set")
                if mutation:
                    text = mutation(path.name, text)
            return text
        output = io.StringIO()
        with patch.object(Path, "read_text", read), contextlib.redirect_stdout(output):
            with self.assertRaises(SystemExit) as result:
                runpy.run_path(str(CHECKER), run_name="__main__")
        return result.exception.code, output.getvalue()

    def test_dynamic_config_passes(self):
        self.assertEqual(self.check()[0], 0)

    def test_static_allocation_rejected_in_all_production_configs(self):
        for name in ("sdkconfig.factory", "sdkconfig.defaults", "sdkconfig.factory.defaults"):
            with self.subTest(name=name):
                code, output = self.check(lambda n, t: t + "\nCONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=y\n" if n == name else t)
                self.assertEqual(code, 1)
                self.assertIn("static-allocation owner", output)

    def test_pinned_framework_owner_allows_generated_static_option(self):
        code, _ = self.check(lambda n, t: t + "\nCONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=y\n" if n == "sdkconfig.factory" else t, allow_owner=True)
        self.assertEqual(code, 0)

    def test_owner_requires_exact_platform_pin(self):
        code, _ = self.check(lambda n, t: t + "\nCONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=y\n" if n == "sdkconfig.factory" else t, allow_owner=True, platform="espressif32@6.8.0")
        self.assertEqual(code, 1)

    def test_application_scheduler_hook_rejected(self):
        code, output = self.check(hook=True)
        self.assertEqual(code, 1)
        self.assertIn("scheduler memory hook forbidden", output)

    def test_async_i2c_rejected_for_stack_buffers(self):
        code, output = self.check(async_i2c=True)
        self.assertEqual(code, 1)
        self.assertIn("stack buffers require synchronous I2C", output)

    def test_invalid_timer_stack_rejected(self):
        code, output = self.check(lambda n, t: t.replace("CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=2048", "CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=0"))
        self.assertEqual(code, 1)
        self.assertIn("STACK_DEPTH must be positive", output)

    def test_disabled_timer_service_rejected(self):
        code, output = self.check(lambda n, t: t + "\n# CONFIG_FREERTOS_USE_TIMERS is not set\n" if n == "sdkconfig.factory" else t)
        self.assertEqual(code, 1)
        self.assertIn("timer service must remain enabled", output)

if __name__ == "__main__":
    unittest.main()
