#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import re
import sys


EVALUATING = re.compile(r"^Evaluating (\\[^ ]+)$")
RETURNED = re.compile(r"^Evaluation of (\\[^ ]+) returned object .+$")
NO_OBJECT = re.compile(r"^No object was returned from evaluation of (\\[^ ]+)$")
TOP_OBJECT = re.compile(r"^  \[([A-Za-z]+)\](?: = ([0-9A-Fa-f]+))?.*$")
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
            current = {
                "path": match.group(1),
                "completion": None,
                "type": None,
                "value": None,
            }
            evaluations.append(current)
            continue
        if ERROR.search(line):
            raise TranscriptError(f"unexpected interpreter error: {line}")
        if current is None:
            continue

        match = RETURNED.match(line)
        if match:
            if match.group(1) != current["path"]:
                raise TranscriptError(
                    f"completion names {match.group(1)}, active evaluation is {current['path']}"
                )
            if current["completion"] is not None:
                raise TranscriptError(f"{current['path']}: ambiguous completion")
            current["completion"] = "object"
            continue

        match = NO_OBJECT.match(line)
        if match:
            if match.group(1) != current["path"]:
                raise TranscriptError(
                    f"completion names {match.group(1)}, active evaluation is {current['path']}"
                )
            if current["completion"] is not None:
                raise TranscriptError(f"{current['path']}: ambiguous completion")
            current["completion"] = "none"
            continue

        match = TOP_OBJECT.match(line)
        if match:
            if current["completion"] != "object" or current["type"] is not None:
                raise TranscriptError(f"{current['path']}: unexpected or ambiguous result dump")
            current["type"] = match.group(1)
            if match.group(2) is not None:
                current["value"] = int(match.group(2), 16)
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
            if actual["completion"] != "none" or actual["type"] is not None:
                raise TranscriptError(f"{path}: expected successful completion without a result")
        elif (actual["completion"], actual["type"], actual["value"]) != (
            "object",
            "Integer",
            value,
        ):
            raise TranscriptError(
                f"{path}: expected scalar integer {value}, found "
                f"{actual['completion']}/{actual['type']}/{actual['value']}"
            )


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
    package = """Evaluating \\_SB.TARG
Evaluation of \\_SB.TARG returned object 0x1, external buffer length 30
  [Package] Contains 1 Elements:
    [Integer] = 0000000000000000
"""
    nested_package = """Evaluating \\_SB.TARG
Evaluation of \\_SB.TARG returned object 0x2, external buffer length 50
  [Package] Contains 1 Elements:
    [Package] Contains 1 Elements:
      [Integer] = 0000000000000000
"""
    string_result = """Evaluating \\_SB.TARG
Evaluation of \\_SB.TARG returned object 0x3, external buffer length 24
  [String] Value: Test
"""
    dump_without_completion = """Evaluating \\_SB.TARG
  [Integer] = 0000000000000000
"""
    wrong_completion = """Evaluating \\_SB.TARG
Evaluation of \\_SB.OTHR returned object 0x4, external buffer length 18
  [Integer] = 0000000000000000
"""

    check(success, [("\\_SB.TEST", "integer", 1)])
    check(no_result, [("\\_SB.OPER", "none", None)])
    rejected = (
        (failed_then_match, [("\\_SB.OPER", "none", None), ("\\_SB.CNTR", "integer", 0)]),
        (missing_operation, [("\\_SB.OPER", "none", None), ("\\_SB.CNTR", "integer", 0)]),
        (wrong_then_unrelated, [("\\_SB.TARG", "integer", 0), ("\\_SB.OTHR", "integer", 0)]),
        (truncated, [("\\_SB.TARG", "integer", 0)]),
        (package, [("\\_SB.TARG", "integer", 0)]),
        (nested_package, [("\\_SB.TARG", "integer", 0)]),
        (string_result, [("\\_SB.TARG", "integer", 0)]),
        (dump_without_completion, [("\\_SB.TARG", "integer", 0)]),
        (wrong_completion, [("\\_SB.TARG", "integer", 0)]),
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
