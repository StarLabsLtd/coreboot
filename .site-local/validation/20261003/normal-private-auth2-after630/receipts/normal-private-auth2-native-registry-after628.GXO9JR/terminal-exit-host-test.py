#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Modeled terminal-reap guards from the actual external observer AST."""

import ast
from pathlib import Path
import json
import re
import struct
import subprocess
import sys
from types import SimpleNamespace
import unittest
import uuid


SOURCE = Path(__file__).with_name('run-native-retry.py')
TEST = Path('/home/sean/Documents/.cdk2-worktrees/normal-private-auth2-older-append-after628')
sys.path.insert(0, str(TEST / 'util/qemu/bin'))
from qmp_cbmem_console import ConsoleSnapshotMotion, PhysicalReadError, parse_table
from qmp_cbmem_console import validate_saved_console


TREE = ast.parse(SOURCE.read_text())
NODES = [node for node in TREE.body if isinstance(node, ast.FunctionDef) and
         node.name == 'natural_transport_exit']
if len(NODES) != 1:
    raise ValueError('exact actual observer terminal-reap helper required')


class TerminalTest(unittest.TestCase):
    def check(self, error, status=3, metadata=True, remaining=10.0, advance=0.1, expired=False):
        clock = SimpleNamespace(value=100.0)
        time = SimpleNamespace(monotonic=lambda: clock.value)
        namespace = {'time': time, 'subprocess': subprocess, 'PhysicalReadError': PhysicalReadError}
        exec(compile(ast.Module(body=NODES, type_ignores=[]), str(SOURCE), 'exec'), namespace)
        calls = []

        def wait(*, timeout):
            calls.append(timeout)
            clock.value += advance
            if expired:
                raise subprocess.TimeoutExpired('modeled genuine wrapper', timeout)
            return status

        process = SimpleNamespace(wait=wait)
        reader = SimpleNamespace(metadata={'accepted': True}) if metadata else None
        result = namespace['natural_transport_exit'](error, process, reader, 100.0 + remaining)
        return result, calls

    def test_oserror_real3_bounded(self):
        self.assertEqual(self.check(ConnectionResetError(104, 'closed')), (True, [2.0]))

    def test_eof_real3_bounded(self):
        self.assertEqual(self.check(EOFError('closed')), (True, [2.0]))

    def test_physical_oserror_cause(self):
        error = PhysicalReadError('physical read closed')
        error.__cause__ = OSError('closed')
        self.assertEqual(self.check(error), (True, [2.0]))

    def test_physical_semantic_cause_fatal(self):
        error = PhysicalReadError('physical read shape')
        error.__cause__ = ValueError('truncated')
        self.assertEqual(self.check(error), (False, []))

    def test_physical_untyped_fatal(self):
        self.assertEqual(self.check(PhysicalReadError('read')), (False, []))

    def test_semantic_fatal(self):
        self.assertEqual(self.check(ValueError('checksum')), (False, []))

    def test_motion_fatal(self):
        self.assertEqual(self.check(ConsoleSnapshotMotion('motion')), (False, []))

    def test_before_metadata_fatal(self):
        self.assertEqual(self.check(OSError('closed'), metadata=False), (False, []))

    def test_unaccepted_metadata_fatal(self):
        clock = SimpleNamespace(monotonic=lambda: 100.0)
        namespace = {'time': clock, 'subprocess': subprocess, 'PhysicalReadError': PhysicalReadError}
        exec(compile(ast.Module(body=NODES, type_ignores=[]), str(SOURCE), 'exec'), namespace)
        process = SimpleNamespace(wait=lambda **_: self.fail('no accepted metadata must not wait'))
        self.assertFalse(namespace['natural_transport_exit'](
            EOFError('closed'), process, SimpleNamespace(metadata=None), 110.0))

    def test_still_live_fatal(self):
        self.assertEqual(self.check(OSError('closed'), expired=True), (False, [2.0]))

    def test_bad_status_fatal(self):
        for status in (0, 1, 2, 124, 143, -15):
            with self.subTest(status=status):
                self.assertEqual(self.check(OSError('closed'), status=status), (False, [2.0]))

    def test_remaining_caps_wait(self):
        self.assertEqual(self.check(OSError('closed'), remaining=0.5), (True, [0.5]))

    def test_deadline_expired_fatal(self):
        self.assertEqual(self.check(OSError('closed'), remaining=0.0), (False, []))

    def test_natural_exit_after_deadline_fatal(self):
        self.assertEqual(self.check(OSError('closed'), remaining=0.5, advance=0.6),
                         (False, [0.5]))

    def test_callsite_catches_only_transport(self):
        catches = [node for node in ast.walk(TREE) if isinstance(node, ast.ExceptHandler) and
                   any(isinstance(call, ast.Call) and isinstance(call.func, ast.Name) and
                       call.func.id == 'natural_transport_exit' for call in ast.walk(node))]
        self.assertEqual(len(catches), 2)
        self.assertEqual([ast.unparse(node.type) for node in sorted(catches, key=lambda node: node.lineno)],
                         ['(OSError, EOFError)', '(PhysicalReadError, OSError, EOFError)'])

    def test_saved_semantic_checks_remain_mandatory(self):
        body = SOURCE.read_text()
        self.assertIn('check_saved(RUN)', body)
        self.assertIn('installed[65536:] != initial[65536:]', body)
        self.assertIn("'--expect-live-older-append'", body)
        self.assertIn("raise ValueError('immutable inputs changed during actual final-media HOST replay')", body)

    def test_original_failed_saved_outcome_still_rejected(self):
        saved = [node for node in TREE.body if isinstance(node, ast.FunctionDef) and
                 node.name == 'check_saved']
        self.assertEqual(len(saved), 1)
        markers = [node for node in TREE.body if isinstance(node, ast.Assign) and
                   any(isinstance(target, ast.Name) and target.id == 'MARKERS'
                       for target in node.targets)]
        self.assertEqual(len(markers), 1)
        namespace = {'json': json, 're': re, 'struct': struct, 'uuid': uuid,
                     'parse_table': parse_table, 'validate_saved_console': validate_saved_console,
                     'MARKERS': ast.literal_eval(markers[0].value)}
        exec(compile(ast.Module(body=saved, type_ignores=[]), str(SOURCE), 'exec'), namespace)
        with self.assertRaisesRegex(ValueError, 'real guest success'):
            namespace['check_saved'](SOURCE.parent / 'run-1')


if __name__ == '__main__':
    unittest.main()
