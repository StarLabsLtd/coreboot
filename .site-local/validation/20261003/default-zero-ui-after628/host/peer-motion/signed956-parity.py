#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Read-only final motion delta comparison against the original signed source."""
import ast
import importlib.util
from pathlib import Path
import subprocess
import sys

root = Path('/home/sean/Documents/.cdk2-worktrees/default-zero-native-ui-observer-after628')
relative = 'util/qemu/bin/qmp-setup-acceptance.py'
baseline = ast.parse(subprocess.check_output([
    'git', '-C', str(root), 'show',
    '956a7af7c8e2aca4e936cb549fc81bafa3999e74:' + relative]).decode())
candidate = ast.parse((root / relative).read_text())

def ordinary_nodes(tree, candidate=False):
    nodes = []
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == 'wait_marker':
            continue
        if candidate and isinstance(node, ast.ImportFrom) and \
                node.module == 'qmp_cbmem_console':
            matching = [alias for alias in node.names
                        if alias.name == 'ConsoleSnapshotMotion']
            if len(matching) != 1 or matching[0].asname is not None:
                raise ValueError('typed motion import is not exact')
            node.names = [alias for alias in node.names
                          if alias.name != 'ConsoleSnapshotMotion']
        nodes.append(node)
    return ast.dump(ast.Module(body=nodes, type_ignores=[]),
                    include_attributes=False)

if ordinary_nodes(baseline) != ordinary_nodes(candidate, candidate=True):
    raise ValueError('source outside the typed wait helper/import changed')
sys.path.insert(0, str(root / 'util/qemu/bin'))
spec = importlib.util.spec_from_file_location(
    'motion_host', root / 'tests/default_zero_setup_controller_test.py')
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)
case = host.MotionTest()
try:
    case.execute_wait(mode='boot-to-fw-ui')
except host.fixtures.observer.ConsoleSnapshotMotion:
    pass
else:
    raise ValueError('original BootToFwUI mode retried typed motion')
print('Signed956 AST parity PASS outside the typed wait helper/import')
print('Actual helper BootToFwUI typed-motion refusal PASS; HOST-only, no guest')
