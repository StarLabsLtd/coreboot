# SPDX-License-Identifier: BSD-3-Clause

import os
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path


TOOLS = Path(os.environ.get("CBFSTOOL_TEST_BIN_DIR", Path(__file__).resolve().parents[1])).resolve()
PAYLOAD_SEGMENT = struct.Struct(">4sIIQII")


def run(*args):
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(f"{args} failed ({result.returncode}):\n{result.stderr}")
    return result.stdout


class ExtractionTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)
        self.image = self.work / "image.bin"
        self.output = self.work / "output.bin"

    def command(self, tool, *args):
        return run(TOOLS / tool, self.image, *args)

    def assert_rejected(self, tool, diagnostic, *args):
        result = subprocess.run([TOOLS / tool, self.image, *args], capture_output=True,
                                text=True)
        self.assertGreater(result.returncode, 0, result.stderr)
        self.assertIn(diagnostic, result.stderr)
        self.assertFalse(self.output.exists())

    def add_payload(self, filename, *args):
        self.command("cbfstool", "create", "-m", "x86", "-s", "64K")
        self.command("cbfstool", *args, "-n", "payload", "-f", filename)
        self.command("cbfstool", "extract", "-m", "x86", "-n", "payload", "-f", self.output)

    def assert_section_bytes(self, name, contents):
        section = self.work / "section.bin"
        section.unlink(missing_ok=True)
        run("objcopy", "--dump-section", f"{name}={section}", self.output,
            self.work / "copy.elf")
        self.assertEqual(section.read_bytes(), contents)

    def test_payload_roundtrip(self):
        asm, layout = self.work / "fixture.s", self.work / "fixture.ld"
        obj, elf = self.work / "fixture.o", self.work / "fixture.elf"
        asm.write_text("""
.globl _start
.text
_start:
.fill 1024, 1, 0x43
.data
.ascii "round trip data"
.bss
.space 73
""")
        layout.write_text("""
ENTRY(_start)
PHDRS { code PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); bss PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x100000; .text : { *(.text) } :code
    . = 0x110000; .data : { *(.data) } :data
    . = 0x120000; .bss (NOLOAD) : { *(.bss) } :bss
}
""")
        run("as", "--32", "-o", obj, asm)
        run("ld", "-m", "elf_i386", "-T", layout, "-o", elf, obj)

        for compression in ("none", "lz4", "lzma", "zstd"):
            with self.subTest(compression=compression):
                self.add_payload(elf, "add-payload", "-c", compression)
                self.assert_section_bytes(".text", b"C" * 1024)
                self.assert_section_bytes(".data", b"round trip data")
                self.assertRegex(run("readelf", "-WS", self.output),
                                 r"\.bss\s+NOBITS\s+00120000\s+\w+\s+0*49\s")
                self.assertRegex(run("readelf", "-h", self.output),
                                 r"Entry point address:\s+0x100000\b")

    def test_legacy_payload_with_notes_and_reordered_offsets(self):
        note = struct.pack("<III4s4s", 4, 4, 1, b"CBF\0", b"DEMO")
        note_at = 4 * PAYLOAD_SEGMENT.size + 8
        data_at = note_at + len(note) + 3
        text_at = data_at + len(b"DATA") + 5
        payload = b"".join(PAYLOAD_SEGMENT.pack(*seg) for seg in (
            (b"CODE", 0, text_at, 0x100000, 4, 9),
            (b"DATA", 0, data_at, 0x110000, 4, 4),
            (b"PARA", 0, note_at, 0, len(note), 0),
            (b"ENTR", 0, 0, 0x100000, 0, 0),
        )) + bytes(8) + note + bytes(3) + b"DATA" + bytes(5) + b"TEXT"
        filename = self.work / "legacy.self"
        filename.write_bytes(payload)

        self.add_payload(filename, "add", "-t", "simple elf", "-c", "lz4")
        self.assert_section_bytes(".text", b"TEXT")
        self.assert_section_bytes(".data", b"DATA")
        self.assert_section_bytes(".note.pinfo", note)
        self.assertRegex(run("readelf", "-WS", self.output),
                         r"\.empty\s+NOBITS\s+00100004\s+\w+\s+0*5\s")
        raw = self.work / "raw.self.lz4"
        self.command("cbfstool", "extract", "-U", "-n", "payload", "-f", raw)
        self.assertEqual(raw.read_bytes()[:4], bytes.fromhex("04 22 4d 18"))

    def test_payload_without_file_data(self):
        filename = self.work / "bss.self"
        filename.write_bytes(PAYLOAD_SEGMENT.pack(b"BSS ", 0, 0, 0x120000, 0, 73) +
                             PAYLOAD_SEGMENT.pack(b"ENTR", 0, 0, 0x100000, 0, 0))

        self.add_payload(filename, "add", "-t", "simple elf")
        self.assertRegex(run("readelf", "-WS", self.output),
                         r"\.bss\s+NOBITS\s+00120000\s+\w+\s+0*49\s")

    def write_fpt(self, version, prefix):
        offset = prefix + 32 + 2 * 32
        header = bytearray(struct.pack("<4sIBBB21x", b"$FPT", 2, version, 0x10, 32))
        entries = (struct.pack("<4s4sII12xI", b"CODE", b"\xff" * 4, offset, 4, 0) +
                   struct.pack("<4s4xII12xI", b"FS", offset + 4, 3, 1))
        if version == 0x20:
            header[11] = (-sum(header)) & 0xff
        else:
            struct.pack_into("<I", header, 20, zlib.crc32(header + entries))
        self.image.write_bytes(bytes(prefix) + header + entries + b"1234abc")

    def test_fpt_names(self):
        for version in (0x20, 0x21):
            for prefix in (0, 16):
                with self.subTest(version=version, prefix=prefix):
                    self.write_fpt(version, prefix)
                    self.command("cse_fpt", "dump", "-o", self.work)
                    for name, contents in (("CODE", b"1234"), ("FS", b"abc")):
                        dumped = self.work / name
                        self.assertEqual(dumped.read_bytes(), contents)
                        dumped.unlink()
                        self.command("cse_fpt", "dump", "-n", name, "-o", self.work)
                        self.assertEqual(dumped.read_bytes(), contents)
                        dumped.unlink()
                        printed = self.command("cse_fpt", "print", "-n", name)
                        self.assertEqual(printed[:25].strip(), name)
                    self.assert_rejected("cse_fpt", "not found", "dump", "-n", "CODE-more",
                                         "-o", self.work)
                    self.assertFalse((self.work / "CODE").exists())

    def test_fpt_truncation(self):
        for version in (0x20, 0x21):
            self.write_fpt(version, 16)
            image = self.image.read_bytes()
            for size, diagnostic in ((16 + 31, "FPT header format"),
                                     (16 + 32 + 32, "FPT entries"),
                                     (len(image) - 1, "Offset out of bounds")):
                with self.subTest(version=version, size=size):
                    self.image.write_bytes(image[:size])
                    self.assert_rejected("cse_fpt", diagnostic, "dump", "-n", "FS",
                                         "-o", self.work)
                    self.assertFalse((self.work / "FS").exists())

    def write_ifwi(self, data, directory_size=None):
        header = bytearray(struct.pack("<4sIBBBB4s", b"$CPD", 1, 1, 1, 16, 0, b"SMIP"))
        entry = struct.pack("<12sIII", b"twelve.chars", len(header) + 24, len(data), 0)
        header[11] = (-sum(header + entry)) & 0xff
        directory = header + entry + data
        if directory_size is not None:
            directory = directory[:directory_size]
        bpdt = (struct.pack("<IHHIIQ", 0x55aa, 1, 1, 0, 0, 0) +
                struct.pack("<HHII", 0, 0, 4096, len(directory)))
        self.image.write_bytes(bpdt.ljust(4096, b"\xff") + directory.ljust(4096, b"\xff"))

    def test_ifwi_directory_at_end_of_subpartition(self):
        for data in (b"IFWI contents", b""):
            with self.subTest(data=data):
                self.write_ifwi(data)
                self.command("ifwitool", "extract", "-d", "-n", "SMIP", "-e", "twelve.chars",
                             "-f", self.output)
                self.assertEqual(self.output.read_bytes(), data)

    def test_ifwi_directory_truncation(self):
        data = b"IFWI contents"
        for size, diagnostic in ((15, "Truncated subpart_dir header"),
                                 (39, "Truncated subpart_dir entries"),
                                 (40 + len(data) - 1, "extends past its subpartition")):
            with self.subTest(size=size):
                self.write_ifwi(data, size)
                self.assert_rejected("ifwitool", diagnostic, "extract", "-d", "-n", "SMIP",
                                     "-e", "twelve.chars", "-f", self.output)


if __name__ == "__main__":
    unittest.main()
