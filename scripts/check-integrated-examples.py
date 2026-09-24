#!/usr/bin/env python3
"""SPEC-030h million-record correctness/stack checks; Python 3, supported POSIX host.

Usage: python3 scripts/check-integrated-examples.py build/gloinc
Temporary inputs/reports are removed on exit. Each compiler invocation gets a
2 MiB stack and 60-second timeout. This is not a performance benchmark.
"""
import pathlib
import resource
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def stack_limit():
    _, hard = resource.getrlimit(resource.RLIMIT_STACK)
    resource.setrlimit(resource.RLIMIT_STACK, (2 * 1024 * 1024, hard))


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    compiler = str(pathlib.Path(sys.argv[1]).resolve())

    def execute(example, *arguments, status=0):
        result = subprocess.run(
            [compiler, "--jit", str(ROOT / "examples" / example), "--", *map(str, arguments)],
            capture_output=True, text=True, preexec_fn=stack_limit, timeout=60,
        )
        assert result.returncode == status, (result.returncode, result.stdout, result.stderr)
        if status == 0:
            assert not result.stderr, result.stderr
        else:
            assert not result.stdout and result.stderr, result
        return result.stdout

    with tempfile.TemporaryDirectory(prefix="gloinc-spec030h-") as directory:
        work = pathlib.Path(directory)
        config = work / "large.conf"
        config.write_text("label=retained\nseed=42\nsamples=1000000\n" + "# reuse scratch\n" * 999997)
        assert execute("config_reader.gloin", config) == "label=retained\nseed=42\nsamples=1000000\n"
        with config.open("a") as stream:
            stream.write("# one line over the limit\n")
        execute("config_reader.gloin", config, status=2)
        print("PASS: config at 1,000,000 lines; 1,000,001 rejected; label retained")

        data, report = work / "data.txt", work / "report.txt"
        data.write_text("row,-2,4\n" * 1000000)
        assert execute("statistics_tool.gloin", data, report, "1,2") == ""
        assert report.read_text() == (
            "column,count,min,max,mean\n"
            "1,1000000,-2.000000,-2.000000,-2.000000\n"
            "2,1000000,4.000000,4.000000,4.000000\n"
        )
        with data.open("a") as stream:
            stream.write("row,-2,4\n")
        absent = work / "must-not-exist.txt"
        execute("statistics_tool.gloin", data, absent, "1,2", status=2)
        assert not absent.exists()
        print("PASS: statistics at 1,000,000 records; 1,000,001 rejected before output creation")

        mask, state = (1 << 64) - 1, 42

        def draw():
            nonlocal state
            state = (state + 0x9E3779B97F4A7C15) & mask
            word = state
            word = ((word ^ (word >> 30)) * 0xBF58476D1CE4E5B9) & mask
            word = ((word ^ (word >> 27)) * 0x94D049BB133111EB) & mask
            return word ^ (word >> 31)

        inside = dice = 0
        for _ in range(1000000):
            x, y = draw() >> 11, draw() >> 11
            inside += x * x + y * y <= (1 << 106)  # Exact integer circle oracle.
            word = draw()
            while word < 4:  # 2^64 % 6
                word = draw()
            dice += word % 6 + 1
        assert (inside, dice) == (785115, 3503215)
        output = execute("simulation_lab.gloin", 42, 1000000)
        prefix = (
            f"seed=42\nsamples=1000000\ninside={inside}\ndice_sum={dice}\n"
            "pi=3.140460\nabs_error=0.001133\nelapsed_ns="
        )
        assert output.startswith(prefix) and output.endswith("\n"), output
        assert output[len(prefix):-1].isdigit(), output
        print("PASS: 1,000,000 simulation samples match independent integer oracle")
        print("All programs compile and run with a 2 MiB stack.")


if __name__ == "__main__":
    main()
