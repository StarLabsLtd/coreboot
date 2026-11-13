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


verifier = Path(__file__).with_name("verify-source.sh").resolve()
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
                  git(source, "status", "--porcelain"))
        result = subprocess.run(
            ["sh", str(verifier), str(path), development], cwd=root,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            env={**os.environ, "development": "1"})
        assert result.returncode == expected, result.stderr
        after = (git(source, "rev-parse", "HEAD"), tracked.read_bytes(),
                 git(source, "status", "--porcelain"))
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
print("Pinned source, dirty checkout and explicit development mode: PASS")
