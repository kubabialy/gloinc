#!/usr/bin/env python3
"""Exercise the Gloin formatter's layout, traversal, and source safety contract."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile


formatter, compiler, root = map(Path, sys.argv[1:4])


def run(arguments: list[str], *, source: bytes | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(arguments, input=source, capture_output=True, timeout=30)


with tempfile.TemporaryDirectory(prefix="gloinfmt-smoke-") as temporary:
    tree = Path(temporary)
    (tree / "nested").mkdir()
    (tree / ".git").mkdir()
    (tree / "build").mkdir()
    samples = {
        tree / "a.gloin": (
            b'def main() -> i32 {\nif true {\nstd.println("// }"); // {\n}\nreturn 0;\n}\n',
            b'def main() -> i32 {\n    if true {\n        std.println("// }"); // {\n'
            b'    }\n    return 0;\n}\n',
        ),
        tree / "nested" / "b.gloin": (
            b'def main() -> i32 {\r\nreturn 0;\r\n}\r\n',
            b'def main() -> i32 {\r\n    return 0;\r\n}\r\n',
        ),
        tree / "z.gloin": (b'def foo() -> i32 {\nreturn 3;\n}\n',
                            b'def foo() -> i32 {\n    return 3;\n}\n'),
    }
    for path, (before, _) in samples.items():
        path.write_bytes(before)
    (tree / ".git" / "ignored.gloin").write_text("def x() {\nreturn;\n}\n")
    (tree / "build" / "ignored.gloin").write_text("def x() {\nreturn;\n}\n")
    (tree / "linked").symlink_to(tree / "nested", target_is_directory=True)

    for path, (before, after) in samples.items():
        result = run([str(formatter), str(path)])
        assert result.returncode == 0 and result.stdout == after and not result.stderr, result
        assert path.read_bytes() == before
        repeat = run([str(formatter), "/dev/stdin"], source=after)
        assert repeat.returncode == 0 and repeat.stdout == after, repeat

    result = run([str(formatter), "--check", str(tree)])
    expected_paths = [tree / "a.gloin", tree / "nested" / "b.gloin", tree / "z.gloin"]
    assert result.returncode == 1 and result.stdout == (
        "".join(f"{path}\n" for path in expected_paths).encode()
    ) and not result.stderr, result
    assert run([str(formatter), "--check", str(tree / "missing")]).returncode == 2

    jit = run([str(compiler), "--jit", str(root / "tools/gloinfmt/main.gloin"),
               "--", str(tree / "a.gloin")])
    assert jit.returncode == 0 and jit.stdout == samples[tree / "a.gloin"][1], jit

# The prior token fingerprint remains an independent oracle while the formatter
# migrates into Gloin. This checks comments and quoted bytes as well as syntax.
spec = importlib.util.spec_from_file_location("legacy_formatter", root / "scripts/format-gloin.py")
legacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)
checked = 0
for path in sorted(root.rglob("*.gloin")):
    if any(part == ".git" or part == "build" or part.startswith("build-") for part in path.parts):
        continue
    original = path.read_bytes()
    formatted = run([str(formatter), str(path)])
    assert formatted.returncode == 0, (path, formatted.stderr)
    assert legacy.tokens(original.decode()) == legacy.tokens(formatted.stdout.decode()), path
    again = run([str(formatter), "/dev/stdin"], source=formatted.stdout)
    assert again.returncode == 0 and again.stdout == formatted.stdout, path
    if b"\r\n" in original:
        assert b"\r\n" in formatted.stdout, path
    checked += 1

print(f"Gloin formatter smoke passed; {checked} source files preserve tokens and are idempotent")
