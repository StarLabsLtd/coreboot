#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the production source verifier without network access or real repos."""
from pathlib import Path
import os
import subprocess
import tempfile


def git(root, *args):
    return subprocess.check_output(
        ["git", "-C", str(root), *args], text=True).strip()


def status(root):
    result = subprocess.run(
        ["git", "-C", str(root), "status", "--porcelain"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return result.returncode, result.stdout, result.stderr


verifier = Path(__file__).with_name("verify-source.sh").resolve()
updater = Path(__file__).with_name("update-source.sh").resolve()
with tempfile.TemporaryDirectory(prefix="cdk2-source-") as directory:
    root = Path(directory)
    source = root / "payload"
    source.mkdir()
    for repo in (root, source):
        git(repo, "init", "-q")
        git(repo, "config", "user.name", "Source selection test")
        git(repo, "config", "user.email", "test@example.invalid")
    tracked = source / "source.c"
    tracked.write_text("original\n")
    git(source, "add", "source.c")
    git(source, "commit", "-qm", "Initial source")
    pinned = git(source, "rev-parse", "HEAD")
    git(root, "update-index", "--add", "--cacheinfo", "160000", pinned, "payload")

    def verify(expected, development="", path="payload"):
        before = (git(source, "rev-parse", "HEAD"), tracked.read_bytes(),
                  status(source))
        result = subprocess.run(
            ["sh", str(verifier), str(path), development], cwd=root,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            env={**os.environ, "development": "1"})
        assert result.returncode == expected, result.stderr
        after = (git(source, "rev-parse", "HEAD"), tracked.read_bytes(),
                 status(source))
        assert before == after, "source verification modified the checkout"

    verify(0)
    verify(0, path=source)
    tracked.write_text("developer edit\n")
    verify(1)
    verify(1, path=source)
    verify(0, "1")
    verify(0, "1", path=source)
    verify(2, "yes")
    git(source, "commit", "-qam", "New source")
    verify(1)
    verify(0, "1")
    git(root, "update-index", "--cacheinfo", "160000",
        git(source, "rev-parse", "HEAD"), "payload")
    verify(0)

    # HEAD still matches the pin, but cleanliness cannot be established.
    tracked.write_text("unverified developer edit\n")
    (source / ".git/index").write_bytes(b"invalid index\n")
    assert status(source)[0] != 0
    verify(1)

    # The explicit main updater must stop before fetching if status fails.
    update_root = root / "update"
    update_source = update_root / "payloads/external/cdk2/cdk2"
    (update_source / ".git").mkdir(parents=True)
    update_tracked = update_source / "source.c"
    update_tracked.write_text("preserve this edit\n")
    commands = root / "git-commands"
    binaries = root / "bin"
    binaries.mkdir()
    fake_git = binaries / "git"
    fake_git.write_text(
        '#!/bin/sh\n'
        'printf "%s\\n" "$*" >> "$CDK2_SOURCE_TEST_LOG"\n'
        'exit 128\n')
    fake_git.chmod(0o755)
    result = subprocess.run(
        ["sh", str(updater)], cwd=update_root,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        env={**os.environ, "PATH": f"{binaries}:{os.environ['PATH']}",
             "CDK2_SOURCE_TEST_LOG": str(commands)})
    assert result.returncode != 0, "updater accepted a failed status query"
    calls = commands.read_text().splitlines()
    assert len(calls) == 1 and " status " in calls[0], calls
    assert update_tracked.read_text() == "preserve this edit\n"
print("Pinned source, dirty/failed status and explicit development mode: PASS")
