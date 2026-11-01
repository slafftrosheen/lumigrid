#!/usr/bin/env python3
"""Host regressions for firmware candidate provenance and flash integrity."""
import hashlib
from functools import reduce
import json
from operator import xor
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from firmware_release_manifest import (
    FLASH_CONFIG, input_inventory, managed_source_paths, package_candidate,
    validate_flash_config, validate_image, validate_merged,
)


def image(payload=b"firmware", flash_size=3):
    # A complete one-segment ESP32 fixture, including the XOR checksum and
    # appended digest. The fixture format was cross-checked with esptool 4.5.1.
    payload = payload.ljust((len(payload) + 3) // 4 * 4, b'\0')
    header = bytes((0xe9, 1, 2, flash_size << 4)) + bytes(19) + b'\x01'
    body = header + struct.pack('<II', 0x3ffb0000, len(payload)) + payload
    body += bytes(15 - len(body) % 16) + bytes([reduce(xor, payload, 0xef)])
    return body + hashlib.sha256(body).digest()


class ReleaseManifestTests(unittest.TestCase):
    def test_image_requires_production_flash_header(self):
        result = validate_image(image(), "firmware.bin")
        self.assertEqual(result['integrity'], 'VERIFIED')
        self.assertEqual(result['segment_count'], 1)
        for data in (image(flash_size=2), image()[:12], b"bad!" + image()[4:]):
            with self.subTest(data=data[:4]), self.assertRaises(ValueError):
                validate_image(data, "firmware.bin")

    def test_image_rejects_truncation_at_every_boundary(self):
        data = image()
        for length in range(len(data)):
            with self.subTest(length=length), self.assertRaises(ValueError):
                validate_image(data[:length], "firmware.bin")

    def test_image_detects_payload_corruption_even_with_a_recalculated_digest(self):
        data = bytearray(image())
        data[32] ^= 1
        data[-32:] = hashlib.sha256(data[:-32]).digest()
        with self.assertRaisesRegex(ValueError, 'payload checksum mismatch'):
            validate_image(data, "firmware.bin")

    def test_image_digest_detects_damage_that_preserves_xor_checksum(self):
        data = bytearray(image())
        data[32] ^= 1
        data[33] ^= 1
        with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
            validate_image(data, "firmware.bin")

    def test_image_rejects_changed_digest_and_header(self):
        for offset in (4, -1):
            data = bytearray(image())
            data[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
                validate_image(data, "firmware.bin")

    def test_image_rejects_wrong_chip_even_when_digest_matches(self):
        data = bytearray(image())
        struct.pack_into('<H', data, 12, 9)
        data[-32:] = hashlib.sha256(data[:-32]).digest()
        with self.assertRaisesRegex(ValueError, 'different chip'):
            validate_image(data, "firmware.bin")

    def test_image_rejects_bad_counts_lengths_and_digest_flags(self):
        for offset, value in ((1, 0), (1, 17), (23, 0), (23, 2), (28, 7), (31, 255)):
            data = bytearray(image())
            data[offset] = value
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                validate_image(data, "firmware.bin")

    def test_image_rejects_trailing_content_and_nonzero_padding(self):
        for suffix in (b'\x00', b'\xff', b'signature-without-a-verified-policy'):
            with self.subTest(suffix=suffix), self.assertRaisesRegex(ValueError, 'trailing data'):
                validate_image(image() + suffix, "firmware.bin")
        data = bytearray(image())
        data[40] = 1
        data[-32:] = hashlib.sha256(data[:-32]).digest()
        with self.assertRaisesRegex(ValueError, 'nonzero image checksum padding'):
            validate_image(data, "firmware.bin")

    def test_merged_image_matches_all_inputs_and_erased_gaps(self):
        inputs = {0x1000: image(b"boot"), 0x8000: b"partitions", 0x10000: image()}
        merged = bytearray(b"\xff" * (0x10000 + len(inputs[0x10000])))
        for offset, data in inputs.items():
            merged[offset:offset + len(data)] = data
        validate_merged(bytes(merged), inputs)
        for offset in (0x1003, 0x8001, 0x10010, 0x9000):
            damaged = bytearray(merged)
            damaged[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                validate_merged(bytes(damaged), inputs)
        with self.assertRaises(ValueError):
            validate_merged(bytes(merged[:-1]), inputs)
        with self.assertRaises(ValueError):
            validate_merged(bytes(merged) + b"\xff" * 0x800000, inputs)

    def test_merged_inputs_cannot_overlap(self):
        with self.assertRaisesRegex(ValueError, "overlap"):
            validate_merged(b"\xff" * 0x2000, {0x1000: image(), 0x1001: image()})

    def test_effective_configs_must_agree_with_production_flash_contract(self):
        values = {
            "IDF_TARGET": "esp32", "ESPTOOLPY_FLASHSIZE": "8MB",
            "ESPTOOLPY_FLASHMODE": "dio", "ESPTOOLPY_FLASHFREQ": "40m",
            "PARTITION_TABLE_OFFSET": 0x8000, "BOOTLOADER_OFFSET_IN_FLASH": 0x1000,
        }
        sdk = '\n'.join(f"CONFIG_{key}={value!r}".replace("'", '"')
                        for key, value in values.items())
        validate_flash_config(sdk, values)
        for key, value in (("ESPTOOLPY_FLASHSIZE", "4MB"),
                           ("IDF_TARGET", "esp32s3"),
                           ("PARTITION_TABLE_OFFSET", 0x9000)):
            invalid = dict(values, **{key: value})
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_flash_config(sdk, invalid)
        with self.assertRaises(ValueError):
            validate_flash_config(sdk.replace('"8MB"', '"4MB"'), values)
        with self.assertRaises(ValueError):
            validate_flash_config("", values)

    def test_source_inventory_identifies_dirty_content_and_copy_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "repo"
            copy_root = Path(tmp) / "build-copy"
            relative = "LumigridLedNode/main/main.cpp"
            for base in (root, copy_root):
                (base / relative).parent.mkdir(parents=True)
                (base / relative).write_bytes(b"original source")
            before = input_inventory(root, copy_root / "LumigridLedNode", [relative])
            self.assertEqual(before[relative]["sha256"], hashlib.sha256(b"original source").hexdigest())
            (root / relative).write_bytes(b"modified source")
            with self.assertRaisesRegex(ValueError, "source differs"):
                input_inventory(root, copy_root / "LumigridLedNode", [relative])
            (copy_root / relative).write_bytes(b"modified source")
            after = input_inventory(root, copy_root / "LumigridLedNode", [relative])
            self.assertNotEqual(before, after)
            (copy_root / relative).unlink()
            with self.assertRaises((ValueError, FileNotFoundError)):
                input_inventory(root, copy_root / "LumigridLedNode", [relative])

    def test_empty_source_files_are_valid_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "LumigridLedNode/empty.h"
            source.parent.mkdir()
            source.touch()
            inputs = input_inventory(root, source.parent, ["LumigridLedNode/empty.h"])
            self.assertEqual(inputs["LumigridLedNode/empty.h"]["bytes"], 0)

    def test_managed_inventory_includes_hidden_metadata_and_detects_extra_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "repo"
            copy_node = Path(tmp) / "copy"
            for node in (root / "LumigridLedNode", copy_node):
                component = node / "managed_components/vendor__driver"
                component.mkdir(parents=True)
                (component / "driver.c").write_text("driver source")
                (component / ".component_hash").write_text("package metadata")
            paths = managed_source_paths(root, copy_node)
            self.assertEqual(len(paths), 2)
            self.assertIn("LumigridLedNode/managed_components/vendor__driver/.component_hash", paths)
            input_inventory(root, copy_node, paths)
            extra = copy_node / "managed_components/vendor__driver/untracked.c"
            extra.write_text("different build input")
            with self.assertRaisesRegex(ValueError, "component files differ"):
                managed_source_paths(root, copy_node)
            extra.unlink()
            (copy_node / "managed_components/vendor__driver/.component_hash").write_text("changed")
            with self.assertRaisesRegex(ValueError, "source differs"):
                input_inventory(root, copy_node, managed_source_paths(root, copy_node))


class CandidatePackagingTests(unittest.TestCase):
    """External compiler/IDF commands are faked; package handling is exercised."""
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name)
        self.node = self.repo / "LumigridLedNode"
        self.build = self.node / ".pio/build"
        self.core = self.repo / "tools"
        self.output = self.repo / "candidate"
        self.merged = self.repo / "merged.bin"

        def write(path, data):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data if isinstance(data, bytes) else data.encode())
        self.write = write
        idf = self.core / "packages/framework-espidf"
        compiler = self.core / "packages/toolchain/bin/xtensa-esp32-elf-gcc"
        write(idf / "package.json", '{"version":"3.50201.240515"}')
        write(compiler.parent.parent / "package.json", '{"version":"13.2.0"}')
        write(self.core / "platforms/espressif32/platform.json", '{"version":"6.7.0"}')
        write(self.node / "platformio.ini", "\n".join(
            f"[env:{name}]\nplatform=espressif32@6.7.0\nboard_upload.flash_size=8MB\n"
            "board_upload.maximum_size=8388608\nboard_build.partitions=partitions.factory.csv\n"
            for name in ("factory", "test")))
        production_csv = Path(__file__).resolve().parents[1] / "partitions.factory.csv"
        write(self.node / "partitions.factory.csv", production_csv.read_bytes())
        write(self.node / "dependencies.lock", "locked dependencies")
        for name in ("generated/dashboard_assets.hpp", "components/factory/include/dashboard_assets.hpp"):
            write(self.node / name, "generated assets")
        for name in ("factory", "test"):
            build = self.build / name
            sdkconfig = self.node / f"sdkconfig.{name}"
            write(sdkconfig, '\n'.join(f"CONFIG_{k}={json.dumps(v)}" for k, v in FLASH_CONFIG.items()))
            write(build / "config/sdkconfig.json", json.dumps(FLASH_CONFIG))
            write(build / "config/sdkconfig.h", "effective config")
            write(build / "project_description.json", json.dumps({
                "project_path": str(self.node), "build_dir": str(build), "target": "esp32",
                "config_file": str(sdkconfig), "idf_path": str(idf), "c_compiler": str(compiler),
                "project_version": "test-only", "git_revision": "v5.2.1",
            }))
            write(build / "bootloader.bin", image(b"boot"))
            write(build / "firmware.bin", image())
            write(build / "partitions.bin", b"partition fixture")
            write(build / "firmware.elf", b"synthetic host ELF fixture")
        merged = bytearray(b"\xff" * (0x10000 + len(image())))
        for offset, data in ((0x1000, image(b"boot")), (0x8000, b"partition fixture"), (0x10000, image())):
            merged[offset:offset + len(data)] = data
        self.merged.write_bytes(merged)

    def fake_run(self, command, cwd=None):
        if any("gen_esp32part.py" in str(arg) for arg in command):
            Path(command[-1]).write_bytes(b"partition fixture")
            return ""
        return "synthetic tool result"

    def package(self):
        with patch("firmware_release_manifest.run", side_effect=self.fake_run), \
             patch("firmware_release_manifest.version", return_value="synthetic-version"), \
             patch("firmware_release_manifest.source_snapshot", return_value={"commit": "a" * 40, "dirty": True}):
            return package_candidate(self.repo, self.node, self.build, self.core, self.merged, self.output)

    def test_complete_package_has_binary_hashes_and_never_claims_hardware_validation(self):
        manifest = self.package()
        self.assertEqual(manifest["hardware_validation"], "NOT_RUN")
        self.assertEqual(manifest["compliance_assessment"], "NOT_PERFORMED")
        self.assertTrue(manifest["source"]["dirty"])
        self.assertFalse(manifest["builds"]["test"]["production_candidate"])
        self.assertEqual(manifest["builds"]["factory"]["image_integrity"]["firmware.bin"]["integrity"], "VERIFIED")
        self.assertTrue((self.output / "manifest.json").is_file())
        for name, record in manifest["artifacts"].items():
            data = (self.output / name).read_bytes()
            self.assertEqual(record, {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})

    def test_wrong_built_partition_table_cannot_publish_a_manifest(self):
        (self.build / "factory/partitions.bin").write_bytes(b"wrong layout")
        with self.assertRaisesRegex(ValueError, "partition binary differs"):
            self.package()
        self.assertFalse(self.output.exists())

    def test_missing_binary_cannot_publish_a_manifest(self):
        (self.build / "test/firmware.bin").unlink()
        with self.assertRaises(FileNotFoundError):
            self.package()
        self.assertFalse(self.output.exists())

    def test_corrupt_factory_or_test_images_cannot_publish_a_manifest(self):
        for environment in ('factory', 'test'):
            for filename in ('bootloader.bin', 'firmware.bin'):
                path = self.build / environment / filename
                original = path.read_bytes()
                damaged = bytearray(original)
                damaged[-1] ^= 1
                path.write_bytes(damaged)
                try:
                    with self.subTest(environment=environment, filename=filename), self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
                        self.package()
                    self.assertFalse(self.output.exists())
                finally:
                    path.write_bytes(original)

    def test_changed_validated_bytes_cannot_acquire_a_new_success_fingerprint(self):
        valid_replacement = image(b'other structurally valid image')
        corrupted_image = bytearray(image())
        corrupted_image[-1] ^= 1
        changes = (
            (self.build / 'test/firmware.bin', bytes(corrupted_image)),
            (self.build / 'test/firmware.bin', valid_replacement),
            (self.build / 'factory/bootloader.bin', valid_replacement),
            (self.build / 'test/partitions.bin', b'changed partitions'),
            (self.merged, b'changed merged image'),
        )
        original_run = self.fake_run
        for path, replacement in changes:
            original = path.read_bytes()

            def replace_after_validation(command, cwd=None):
                if command[-1] == 'version' and 'esptool.py' in str(command[-2]):
                    path.write_bytes(replacement)
                return original_run(command, cwd)

            self.fake_run = replace_after_validation
            try:
                with self.subTest(path=path, replacement=replacement[:8]), self.assertRaisesRegex(ValueError, 'Validated build artifact changed'):
                    self.package()
                self.assertFalse(self.output.exists())
            finally:
                path.write_bytes(original)
                self.fake_run = original_run


if __name__ == "__main__":
    unittest.main()
