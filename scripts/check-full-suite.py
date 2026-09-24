#!/usr/bin/env python3
"""Require the exact documented deferred failures in complete CTest reports."""

import sys
import xml.etree.ElementTree as ET

EXPECTED = {
    "AsyncTest.SpawnGeneration",
    "SemaAsyncTest.AsyncTypes",
    "AsyncTest.DeferredFunctionGeneration",
    "CodeGenTest.GenerateSpawn",
}


def check(path):
    root = ET.parse(path).getroot()
    cases = root.findall(".//testcase")
    if not cases:
        raise ValueError(f"{path}: no test cases")
    names = [case.get("name") for case in cases]
    if len(names) != len(set(names)):
        raise ValueError(f"{path}: duplicate test names")
    failed = {
        case.get("name")
        for case in cases
        if case.find("failure") is not None or case.find("error") is not None
    }
    skipped = {case.get("name") for case in cases if case.find("skipped") is not None}
    if failed != EXPECTED or skipped:
        raise ValueError(
            f"{path}: unexpected failures={sorted(failed - EXPECTED)}, "
            f"missing known failures={sorted(EXPECTED - failed)}, "
            f"skipped={sorted(skipped)}"
        )
    print(f"{path}: {len(cases) - len(failed)}/{len(cases)} pass; "
          f"four documented deferred failures remain visible")
    for name in sorted(failed):
        print(f"  FAIL {name}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: check-full-suite.py SERIAL_XML PARALLEL_XML")
    try:
        for report in sys.argv[1:]:
            check(report)
    except (ET.ParseError, OSError, ValueError) as exc:
        raise SystemExit(str(exc)) from exc
