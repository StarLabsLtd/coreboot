# SPDX-License-Identifier: GPL-2.0-only
"""Read a bounded Q35 CBMEM console through QMP, without guest memory writes.

Wire contracts follow coreboot util/cbmem/devmem_drv.c and cbmem_util.h,
and CDK2 coreboot.c/coreboot_handoff.c. This is a HOST diagnostic observer,
not a producer, firmware admission mechanism, or arbitrary-platform mapper.
"""

import hashlib
import json
import struct
import tempfile
import time
from pathlib import Path


HEADER_BYTES = 24
MAX_TABLE_BYTES = 1024 * 1024
MAX_RECORDS = 256
MAX_FORWARD_DEPTH = 4
MAX_PUBLICATION_SECONDS = 10.0
PUBLICATION_RETRY_INTERVAL = 0.05
MAX_PUBLICATION_RETRIES = int(MAX_PUBLICATION_SECONDS / PUBLICATION_RETRY_INTERVAL)
MAX_CONSOLE_BYTES = 1024 * 1024
MAX_PHYSICAL_BYTES = 4 * 1024 * 1024 * 1024
CONSOLE_ID = 0x434F4E53
CURSOR_MASK = (1 << 28) - 1
OVERFLOW = 1 << 31


class TableNotReady(ValueError):
    """No valid low coreboot locator has been published yet."""


class PhysicalReadError(ValueError):
    """A failed QMP transport/read is never an unfinished table."""


class ConsoleSnapshotMotion(ValueError):
    """All bounded console reads changed; no stable snapshot was accepted."""


def checksum(data):
    total = 0
    for index, byte in enumerate(data):
        total += byte << (8 if index & 1 else 0)
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def checked_range(address, size, limit):
    if type(address) is not int or type(size) is not int or \
            address < 0 or size <= 0 or address >= limit or size > limit - address:
        raise ValueError("physical extent is outside the bounded guest RAM")


def parse_table(data):
    if len(data) < HEADER_BYTES:
        raise ValueError("truncated LBIO header")
    signature, header_size, header_sum, table_size, table_sum, count = \
        struct.unpack_from("<4sIIIII", data)
    if signature != b"LBIO" or not HEADER_BYTES <= header_size <= 4096 or \
            not 0 < table_size <= MAX_TABLE_BYTES or \
            not 0 < count <= MAX_RECORDS or len(data) != header_size + table_size or \
            header_sum > 0xFFFF or table_sum > 0xFFFF or \
            checksum(data[:header_size]) != 0 or checksum(data[header_size:]) != table_sum:
        raise ValueError("LBIO geometry/checksum rejected")
    records = []
    offset = header_size
    for _ in range(count):
        if len(data) - offset < 8:
            raise ValueError("truncated LBIO record")
        tag, size = struct.unpack_from("<II", data, offset)
        if size < 8 or size > len(data) - offset:
            raise ValueError("LBIO record extent rejected")
        records.append((tag, data[offset:offset + size]))
        offset += size
    if offset != len(data):
        raise ValueError("LBIO record count does not consume the table")
    return records


def console_extent(records, physical_limit):
    ranges = []
    consoles = []
    for tag, record in records:
        if tag == 1:
            if (len(record) - 8) % 20:
                raise ValueError("LBIO memory-map shape rejected")
            for offset in range(8, len(record), 20):
                address, size, kind = struct.unpack_from("<QQI", record, offset)
                if size == 0 or address > (1 << 64) - 1 - size:
                    raise ValueError("LBIO memory-map extent overflow")
                ranges.append((address, size, kind))
        elif tag == 0x31:
            if len(record) != 24:
                raise ValueError("LBIO CBMEM allocation shape rejected")
            address, size, identifier = struct.unpack_from("<QII", record, 8)
            if identifier == CONSOLE_ID:
                consoles.append((address, size))
    if len(consoles) != 1:
        raise ValueError("CBMEM console allocation is missing or duplicated")
    address, size = consoles[0]
    checked_range(address, size, physical_limit)
    if address == 0 or address & 3 or not 8 < size <= MAX_CONSOLE_BYTES:
        raise ValueError("CBMEM console allocation bounds rejected")
    if not any(kind == 16 and address >= base and size <= extent and
               address - base <= extent - size for base, extent, kind in ranges):
        raise ValueError("CBMEM console lacks complete CB_MEM_TABLE coverage")
    return address, size


def console_bytes(data):
    if len(data) < 9:
        raise ValueError("truncated CBMEM console")
    size, raw_cursor = struct.unpack_from("<II", data)
    cursor = raw_cursor & CURSOR_MASK
    if size != len(data) - 8 or size == 0 or size > CURSOR_MASK or \
            raw_cursor & ~(CURSOR_MASK | OVERFLOW) or cursor > size or \
            (raw_cursor & OVERFLOW and cursor >= size):
        raise ValueError("CBMEM console body/cursor rejected")
    body = data[8:]
    if raw_cursor & OVERFLOW:
        return body[cursor:] + body[:cursor]
    return body[:cursor]


def validate_saved_console(run):
    """Revalidate the observer's exact saved table/console snapshot and text."""
    run = Path(run)

    def regular(name):
        path = run / name
        if not path.is_file() or path.is_symlink():
            raise ValueError(f"CBMEM evidence is not a regular file: {name}")
        return path.read_bytes()

    metadata = json.loads(regular("cbmem-observer.json"))
    if not isinstance(metadata, dict) or metadata.get("source") != "qmp-physical-cbmem":
        raise ValueError("CBMEM observer provenance rejected")
    limit = metadata.get("physical_limit")
    chain = metadata.get("tables")
    retries = metadata.get("publication_retries")
    if type(limit) is not int or not 1024 * 1024 <= limit <= MAX_PHYSICAL_BYTES or \
            not isinstance(chain, list) or not 1 <= len(chain) <= MAX_FORWARD_DEPTH or \
            type(retries) is not int or not 0 <= retries <= MAX_PUBLICATION_RETRIES:
        raise ValueError("CBMEM observer table chain bounds rejected")
    visited = set()
    for index, entry in enumerate(chain):
        if not isinstance(entry, dict):
            raise ValueError("CBMEM table snapshot metadata rejected")
        address = entry.get("address")
        data = regular(f"cbmem-table-{index}.bin")
        checked_range(address, len(data), limit)
        if address in visited or entry.get("bytes") != len(data) or \
                entry.get("sha256") != hashlib.sha256(data).hexdigest():
            raise ValueError("CBMEM table snapshot identity rejected")
        visited.add(address)
        if (index == 0 and not (0 <= address <= 4096 - HEADER_BYTES or
                                0xF0000 <= address <= 0xF1000 - HEADER_BYTES)) or address & 15:
            raise ValueError("CBMEM low locator address rejected")
        records = parse_table(data)
        forwards = [record for tag, record in records if tag == 0x11]
        if index < len(chain) - 1:
            if len(forwards) != 1 or len(forwards[0]) != 16 or \
                    not isinstance(chain[index + 1], dict) or \
                    struct.unpack_from("<Q", forwards[0], 8)[0] != chain[index + 1].get("address"):
                raise ValueError("saved CBMEM forward chain rejected")
        elif forwards:
            raise ValueError("saved CBMEM leaf is still a forwarding table")
    address, size = console_extent(records, limit)
    data = regular("cbmem-console.bin")
    body = console_bytes(data)
    if len(data) != size or struct.unpack_from("<I", data, 4)[0] & OVERFLOW or \
            metadata.get("console_address") != address or metadata.get("console_bytes") != size or \
            metadata.get("cursor") != len(body) or \
            metadata.get("snapshot_sha256") != hashlib.sha256(data).hexdigest() or \
            regular("cbmem-live.log") != body:
        raise ValueError("saved CBMEM console snapshot/log identity rejected")
    return body


class ConsoleReader:
    def __init__(self, qmp, scratch):
        self.qmp = qmp
        self.scratch = Path(scratch)
        self.scratch.mkdir(parents=True, exist_ok=True)
        reply = qmp({"execute": "query-memory-size-summary"})
        fields = reply.get("return")
        if not isinstance(fields, dict) or fields.get("plugged-memory", 0) != 0:
            raise ValueError("quiet observer requires the bounded unexpanded RAM profile")
        self.physical_limit = fields.get("base-memory")
        if type(self.physical_limit) is not int or \
                not 1024 * 1024 <= self.physical_limit <= MAX_PHYSICAL_BYTES:
            raise ValueError("QMP base-memory extent rejected")
        self.root_address = None
        self.metadata = None
        self.table_snapshots = []
        self.last_snapshot = None
        self.publication_retries = 0
        self.publication_started = None
        self.publication_next_read = None
        self.publication_error = None

    def read(self, address, size):
        checked_range(address, size, self.physical_limit)
        if size > MAX_TABLE_BYTES + 4096:
            raise ValueError("physical snapshot exceeds the per-read bound")
        with tempfile.NamedTemporaryFile(prefix="physical-", dir=self.scratch) as output:
            try:
                reply = self.qmp({"execute": "pmemsave", "arguments": {
                    "val": address, "size": size, "filename": output.name}})
            except (OSError, ValueError) as error:
                raise PhysicalReadError(f"QMP physical read failed: {error}") from error
            if reply != {"return": {}}:
                raise PhysicalReadError(f"QMP physical read failed: {reply}")
            data = Path(output.name).read_bytes()
        if len(data) != size:
            raise PhysicalReadError("QMP physical snapshot is truncated")
        return data

    def table_publication_error(self, error, address, data):
        # coreboot publishes the low forwarding locator before finalizing the
        # target table. Retry only initial table-format validation, never QMP
        # transport failures or corruption after an accepted console snapshot.
        now = time.monotonic()
        if self.root_address is not None and self.metadata is None:
            elapsed = now - self.publication_started
            capture = {"address": address, "bytes": len(data),
                       "sha256": hashlib.sha256(data).hexdigest(),
                       "elapsed_seconds": elapsed, "error": str(error),
                       "previous_retries": self.publication_retries}
            for label in (("first", "last") if self.publication_error is None else ("last",)):
                (self.scratch / f"publication-{label}-table.bin").write_bytes(data)
                (self.scratch / f"publication-{label}.json").write_text(
                    json.dumps(capture, indent=2, sort_keys=True) + "\n")
        if self.root_address is not None and self.metadata is None and \
                now - self.publication_started < MAX_PUBLICATION_SECONDS and \
                self.publication_retries < MAX_PUBLICATION_RETRIES:
            self.publication_retries += 1
            self.publication_error = error
            self.publication_next_read = now + PUBLICATION_RETRY_INTERVAL
            raise TableNotReady(f"initial LBIO publication pending: {error}") from error
        raise error

    def table(self, address):
        header = self.read(address, HEADER_BYTES)
        signature, header_size, _, table_size, _, _ = struct.unpack("<4sIIIII", header)
        if signature != b"LBIO" or not HEADER_BYTES <= header_size <= 4096 or \
                not 0 < table_size <= MAX_TABLE_BYTES:
            self.table_publication_error(
                ValueError("LBIO table header bounds rejected"), address, header)
        data = self.read(address, header_size + table_size)
        try:
            records = parse_table(data)
        except ValueError as error:
            self.table_publication_error(error, address, data)
        return data, records

    def locate(self):
        candidates = []
        for base in (0, 0xF0000):
            window = self.read(base, 4096)
            for offset in range(0, len(window) - HEADER_BYTES + 1, 16):
                if window[offset:offset + 4] != b"LBIO":
                    continue
                try:
                    self.table(base + offset)
                except PhysicalReadError:
                    raise
                except ValueError:
                    continue
                candidates.append(base + offset)
        if not candidates:
            raise TableNotReady("valid low LBIO locator has not appeared")
        if len(candidates) != 1:
            raise ValueError("low LBIO locator is ambiguous")
        return candidates[0]

    def snapshot(self):
        if self.metadata is None and self.publication_error is not None:
            now = time.monotonic()
            if now - self.publication_started >= MAX_PUBLICATION_SECONDS or \
                    self.publication_retries >= MAX_PUBLICATION_RETRIES:
                raise self.publication_error
            if now < self.publication_next_read:
                raise TableNotReady("initial LBIO publication reprobe is not due")
        if self.root_address is None:
            self.root_address = self.locate()
            self.publication_started = time.monotonic()
        address = self.root_address
        visited = set()
        chain = []
        snapshots = []
        for _ in range(MAX_FORWARD_DEPTH):
            if address in visited:
                raise ValueError("LBIO forwarding cycle rejected")
            visited.add(address)
            data, records = self.table(address)
            chain.append({"address": address, "bytes": len(data),
                          "sha256": hashlib.sha256(data).hexdigest()})
            snapshots.append(data)
            forwards = [record for tag, record in records if tag == 0x11]
            if not forwards:
                break
            if len(forwards) != 1 or len(forwards[0]) != 16:
                raise ValueError("LBIO forwarding record rejected")
            address = struct.unpack_from("<Q", forwards[0], 8)[0]
            if address == 0 or address & 15:
                raise ValueError("LBIO forwarding target rejected")
        else:
            raise ValueError("LBIO forwarding depth exceeded")
        if self.metadata is not None and chain != self.metadata["tables"]:
            raise ValueError("accepted LBIO table chain changed")
        console_address, console_size = console_extent(records, self.physical_limit)
        for _ in range(8):
            data = self.read(console_address, console_size)
            # Append-only publication happens after body writes. Require a
            # stable header across the live read; reject persistent motion.
            if data[:8] != self.read(console_address, 8):
                continue
            body = console_bytes(data)
            if struct.unpack_from("<I", data, 4)[0] & OVERFLOW:
                raise ValueError("live quiet observer refuses an overflowing console")
            self.metadata = {"source": "qmp-physical-cbmem", "tables": chain,
                             "publication_retries": self.publication_retries,
                             "physical_limit": self.physical_limit,
                             "console_address": console_address, "console_bytes": console_size,
                             "cursor": len(body), "snapshot_sha256": hashlib.sha256(data).hexdigest()}
            self.table_snapshots = snapshots
            self.last_snapshot = data
            return body
        raise ConsoleSnapshotMotion("CBMEM console changed through every bounded snapshot")
