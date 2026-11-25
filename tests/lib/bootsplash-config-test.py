#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Resolve real Kconfig defaults for shared and independent splash inputs."""

import pathlib
import os
import shlex
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class SplashConfigTest(unittest.TestCase):
    def resolve_splash_config(self, path, payload, overrides):
        config = path / "config"
        settings = {
            "VENDOR_STARLABS": "y",
            "BOARD_STARLABS_LITE_ADL": "y",
            f"PAYLOAD_{payload}": "y",
            **overrides,
        }
        config.write_text("".join(f"CONFIG_{key}={value}\n"
                                  for key, value in settings.items()))
        result = subprocess.run(
            ["make", "--no-print-directory", "-C", str(ROOT),
             f"obj={path / 'build'}", f"DOTCONFIG={config}", "UPDATED_SUBMODULES=1",
             "olddefconfig"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(result.returncode, 0, result.stdout)
        return dict(line.split("=", 1) for line in config.read_text().splitlines()
                    if line.startswith("CONFIG_") and "=" in line)

    def test_source_density_follows_the_selected_input(self):
        cases = (
            ({}, (2, 2)),
            ({"BMP_LOGO_FILE_NAME": '"custom.bmp"'}, (1, 2)),
            ({"EDK2_BOOTSPLASH_FILE": '"custom.bmp"'}, (1, 1)),
            ({"EDK2_BOOTSPLASH_FILE": '"custom.bmp"',
              "EDK2_BOOTSPLASH_SOURCE_SCALE": "2"}, (2, 2)),
        )
        with tempfile.TemporaryDirectory(prefix="bootsplash-config-") as directory:
            path = pathlib.Path(directory)
            for overrides, expected in cases:
                with self.subTest(overrides=overrides):
                    values = self.resolve_splash_config(path, "EDK2", overrides)
                    actual = tuple(int(values[f"CONFIG_{key}"]) for key in (
                        "BMP_LOGO_SOURCE_SCALE", "EDK2_BOOTSPLASH_SOURCE_SCALE"))
                    self.assertEqual(actual, expected)
                    self.assertEqual(values["CONFIG_BOOT_SPLASH_HANDOFF"], "y")

    def test_ediie_uses_coreboot_bmp_settings(self):
        stock_hidpi = '"3rdparty/blobs/mainboard/starlabs/Logo-HiDPI.bmp"'
        stock_logo = '"3rdparty/blobs/mainboard/starlabs/Logo.bmp"'
        with tempfile.TemporaryDirectory(prefix="bootsplash-config-") as directory:
            path = pathlib.Path(directory)
            for overrides, filename, density, handoff in (
                    ({}, stock_hidpi, "2", "y"),
                    ({"BOOT_SPLASH_HANDOFF": "n"}, stock_logo, "1", None),
                    ({"USE_BLOBS": "n", "BMP_LOGO": "y",
                      "BMP_LOGO_FILE_NAME": '"custom.bmp"'}, '"custom.bmp"', "1", "y")):
                with self.subTest(overrides=overrides):
                    values = self.resolve_splash_config(path, "EDIIE", overrides)
                    self.assertEqual(values["CONFIG_PAYLOAD_EDIIE"], "y")
                    self.assertEqual(values.get("CONFIG_BOOT_SPLASH_HANDOFF"), handoff)
                    self.assertEqual(values["CONFIG_BMP_LOGO_FILE_NAME"], filename)
                    self.assertEqual(values["CONFIG_BMP_LOGO_SOURCE_SCALE"], density)

            for option in ("BMP_LOGO", "USE_BLOBS"):
                with self.subTest(disabled=option):
                    values = self.resolve_splash_config(path, "EDIIE", {option: "n"})
                    self.assertEqual(values["CONFIG_PAYLOAD_EDIIE"], "y")
                    self.assertNotIn("CONFIG_BMP_LOGO", values)
                    self.assertNotIn("CONFIG_BOOT_SPLASH_HANDOFF", values)

    @unittest.skipUnless(shutil.which("convert"), "ImageMagick is required")
    def test_fsp_conversion_requires_complete_double_density_pixels(self):
        with tempfile.TemporaryDirectory(prefix="bootsplash-fsp-") as directory:
            path = pathlib.Path(directory)
            for width, fail_probe, expected_success in (
                    (4, False, True), (3, False, False), (4, True, False)):
                with self.subTest(width=width, fail_probe=fail_probe):
                    label = f"{width}-{'failed-probe' if fail_probe else 'normal'}"
                    source = path / f"input-{label}.bmp"
                    pitch = (width * 3 + 3) & ~3
                    data = bytes(pitch * 4)
                    source.write_bytes(struct.pack(
                        "<2sIHHIIiiHHIIiiII", b"BM", 54 + len(data), 0, 0, 54,
                        40, width, 4, 1, 24, 0, len(data), 0, 0, 0, 0) + data)
                    config = path / f"config-{label}"
                    config.write_text(
                        "CONFIG_VENDOR_STARLABS=y\nCONFIG_BOARD_STARLABS_LITE_ADL=y\n"
                        "CONFIG_PAYLOAD_EDK2=y\nCONFIG_BOOT_SPLASH_HANDOFF=n\n"
                        f'CONFIG_EDK2_BOOTSPLASH_FILE="{source}"\n'
                        "CONFIG_EDK2_BOOTSPLASH_SOURCE_SCALE=2\n")
                    obj = path / f"build-{label}"
                    command = ["make", "--no-print-directory", "-C", str(ROOT),
                               f"obj={obj}", f"DOTCONFIG={config}", "UPDATED_SUBMODULES=1"]
                    result = subprocess.run(command + ["olddefconfig"], text=True,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    self.assertEqual(result.returncode, 0, result.stdout)
                    image = obj / "starlabs-logo.bmp"
                    environment = dict(os.environ)
                    if fail_probe:
                        tools = path / "tools"
                        tools.mkdir()
                        probe = tools / "convert"
                        probe.write_text(
                            '#!/bin/sh\nif [ "$1" = "-ping" ]; then\n'
                            "  printf '0\\n'\n  exit 1\nfi\nexec " +
                            shlex.quote(shutil.which("convert")) + ' "$@"\n')
                        probe.chmod(0o755)
                        environment["PATH"] = str(tools) + os.pathsep + environment["PATH"]
                    result = subprocess.run(command + [str(image)], text=True,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                            env=environment)
                    if expected_success:
                        self.assertEqual(result.returncode, 0, result.stdout)
                        self.assertEqual(struct.unpack_from("<ii", image.read_bytes(), 18),
                                         (2, 2))
                    else:
                        self.assertNotEqual(result.returncode, 0, result.stdout)
                        self.assertFalse(image.exists())


if __name__ == "__main__":
    unittest.main()
