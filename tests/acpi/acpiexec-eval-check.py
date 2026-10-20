#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import re
import sys


EVALUATING = re.compile(r"^Evaluating (\\[^ ]+)$")
INTEGER = re.compile(r"^\s*\[Integer\] = ([0-9A-Fa-f]+)\s*$")
ERROR = re.compile(
    r"AE_[A-Z_]+|ACPI (?:Error|Exception)|failed with status|"
    r"Could not (?:load|locate|find)|Table .* failed",
    re.IGNORECASE,
)


class TranscriptError(Exception):
    pass


def parse(transcript):
    evaluations = []
    current = None
    for line in transcript.splitlines():
        match = EVALUATING.match(line)
        if match:
            current = {"path": match.group(1), "integers": [], "none": False}
            evaluations.append(current)
            continue
        if ERROR.search(line):
            raise TranscriptError(f"unexpected interpreter error: {line}")
        if current is None:
            continue
        match = INTEGER.match(line)
        if match:
            current["integers"].append(int(match.group(1), 16))
        elif line == f"No object was returned from evaluation of {current['path']}":
            current["none"] = True
    return evaluations


def check(transcript, expected):
    evaluations = parse(transcript)
    if len(evaluations) != len(expected):
        raise TranscriptError(f"expected {len(expected)} evaluations, found {len(evaluations)}")
    for index, (actual, wanted) in enumerate(zip(evaluations, expected), 1):
        path, kind, value = wanted
        if actual["path"] != path:
            raise TranscriptError(f"evaluation {index}: expected {path}, found {actual['path']}")
        if kind == "none":
            if not actual["none"] or actual["integers"]:
                raise TranscriptError(f"{path}: expected successful completion without a result")
        elif actual["none"] or actual["integers"] != [value]:
            raise TranscriptError(f"{path}: expected integer {value}, found {actual['integers']}")


def decode(spec):
    try:
        path, outcome = spec.split("=", 1)
        if outcome == "none":
            return path, "none", None
        kind, value = outcome.split(":", 1)
        if kind != "integer":
            raise ValueError
        return path, kind, int(value, 0)
    except ValueError as error:
        raise TranscriptError(f"invalid expectation: {spec}") from error


def self_test():
    success = """Evaluating \\_SB.TEST
Evaluation of \\_SB.TEST returned object 0x1, external buffer length 18
  [Integer] = 0000000000000001
"""
    failed_then_match = """Evaluating \\_SB.OPER
Evaluation of \\_SB.OPER failed with status AE_AML_UNINITIALIZED_LOCAL
Evaluating \\_SB.CNTR
Evaluation of \\_SB.CNTR returned object 0x1, external buffer length 18
  [Integer] = 0000000000000000
"""
    missing_operation = """Evaluating \\_SB.CNTR
Evaluation of \\_SB.CNTR returned object 0x1, external buffer length 18
  [Integer] = 0000000000000000
"""
    wrong_then_unrelated = """Evaluating \\_SB.TARG
Evaluation of \\_SB.TARG returned object 0x1, external buffer length 18
  [Integer] = 0000000000000001
Evaluating \\_SB.OTHR
Evaluation of \\_SB.OTHR returned object 0x2, external buffer length 18
  [Integer] = 0000000000000000
"""
    truncated = """Evaluating \\_SB.TARG
Evaluation of \\_SB.TARG returned object 0x1, external buffer length 18
"""
    no_result = """Evaluating \\_SB.OPER
No object was returned from evaluation of \\_SB.OPER
"""

    check(success, [("\\_SB.TEST", "integer", 1)])
    check(no_result, [("\\_SB.OPER", "none", None)])
    rejected = (
        (failed_then_match, [("\\_SB.OPER", "none", None), ("\\_SB.CNTR", "integer", 0)]),
        (missing_operation, [("\\_SB.OPER", "none", None), ("\\_SB.CNTR", "integer", 0)]),
        (wrong_then_unrelated, [("\\_SB.TARG", "integer", 0), ("\\_SB.OTHR", "integer", 0)]),
        (truncated, [("\\_SB.TARG", "integer", 0)]),
    )
    for transcript, expected in rejected:
        try:
            check(transcript, expected)
        except TranscriptError:
            continue
        raise TranscriptError("invalid transcript was accepted")

    # The replaced runner compared only the final integer and accepted this fixture.
    old_result = [match.group(1) for line in failed_then_match.splitlines()
                  if (match := INTEGER.match(line))]
    if not old_result or int(old_result[-1], 16) != 0:
        raise TranscriptError("false-pass fixture no longer represents the old runner")


def main():
    if sys.argv[1:] == ["--self-test"]:
        self_test()
        return
    if len(sys.argv) < 3:
        raise TranscriptError("usage: checker TRANSCRIPT EXPECTATION...")
    with open(sys.argv[1], encoding="utf-8", errors="replace") as file:
        transcript = file.read()
    check(transcript, [decode(spec) for spec in sys.argv[2:]])


try:
    main()
except (OSError, TranscriptError) as error:
    print(f"acpiexec transcript rejected: {error}", file=sys.stderr)
    sys.exit(1)
