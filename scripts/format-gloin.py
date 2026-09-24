#!/usr/bin/env python3
"""Format tracked Gloin sources without changing their lexical tokens."""

import argparse
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PAIRED_OPERATORS = {"->", "==", "!=", "<=", ">=", "&&", "||", "::", "+=", "-=", "*=", "/="}


def pieces(source: str):
    """Yield text, quoted strings, line comments, punctuation, and newlines."""
    index = 0
    while index < len(source):
        if source.startswith("//", index):
            end = source.find("\n", index)
            if end < 0:
                end = len(source)
            yield "comment", source[index:end].rstrip("\r")
            index = end
        elif source[index] in {'"', "'"}:
            quote = source[index]
            end = index + 1
            while end < len(source) and source[end] not in "\r\n":
                if source[end] == "\\" and end + 1 < len(source):
                    end += 2
                elif source[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
            yield "quoted", source[index:end]
            index = end
        elif source[index] in "\r\n":
            if source.startswith("\r\n", index):
                index += 2
            else:
                index += 1
            yield "newline", "\n"
        elif source[index] in "{};,()[]":
            yield source[index], source[index]
            index += 1
        else:
            end = index + 1
            while end < len(source) and source[end] not in "\r\n{};,()[]\"'" and not source.startswith("//", end):
                end += 1
            yield "text", source[index:end]
            index = end


def tokens(source: str):
    """Whitespace-independent token fingerprint, including comments and quotes."""
    found = []
    for kind, value in pieces(source):
        if kind in {"newline"}:
            continue
        if kind in {"quoted", "comment"}:
            found.append(value)
            continue
        if kind != "text":
            found.append(value)
            continue
        index = 0
        while index < len(value):
            if value[index].isspace():
                index += 1
            elif index + 1 < len(value) and value[index:index + 2] in PAIRED_OPERATORS:
                found.append(value[index:index + 2])
                index += 2
            elif value[index].isalnum() or value[index] == "_":
                end = index + 1
                while end < len(value) and (value[end].isalnum() or value[end] == "_"):
                    end += 1
                found.append(value[index:end])
                index = end
            else:
                found.append(value[index])
                index += 1
    return found


def format_source(source: str) -> str:
    original_newline = "\r\n" if "\r\n" in source else "\n"
    lines = []
    top_level_closes = set()
    method_closes = set()
    frames = []
    depth = 0
    parentheses = 0
    brackets = 0
    current = ""
    in_for_header = False
    source_line_has_token = False
    emitted_on_source_line = False

    def emit(value: str, level: int | None = None):
        nonlocal emitted_on_source_line
        value = value.strip()
        if not value:
            return
        indent = depth if level is None else level
        if value.startswith(("||", "&&")):
            indent += 1
        lines.append("    " * indent + value)
        emitted_on_source_line = True

    def flush():
        nonlocal current
        emit(current)
        current = ""

    def append(value: str):
        nonlocal current
        if not current or current.endswith(" "):
            current += value.lstrip(" \t")
        else:
            current += value

    scanned = list(pieces(source))
    skip_until = -1
    for index, (kind, value) in enumerate(scanned):
        if index < skip_until:
            continue
        if kind == "newline":
            flush()
            if not source_line_has_token and lines and lines[-1] != "":
                lines.append("")
            source_line_has_token = False
            emitted_on_source_line = False
        elif kind == "comment":
            source_line_has_token = True
            if current.strip():
                current = current.rstrip() + " " + value
                flush()
            elif emitted_on_source_line and lines and lines[-1]:
                lines[-1] += " " + value
            else:
                if frames and frames[-1][2] == "struct" and lines and lines[-1].rstrip().endswith(","):
                    lines.append("")
                emit(value)
        elif kind in {"text", "quoted"}:
            if value.strip():
                source_line_has_token = True
            append(value)
        elif kind in {"(", "["}:
            source_line_has_token = True
            append(value)
            if kind == "(":
                parentheses += 1
            else:
                brackets += 1
        elif kind in {")", "]"}:
            source_line_has_token = True
            if not current.strip() and lines and lines[-1].rstrip().endswith(("}", ")", "]")):
                lines[-1] += value
            else:
                append(value)
            if kind == ")":
                parentheses = max(0, parentheses - 1)
            else:
                brackets = max(0, brackets - 1)
        elif kind == "{":
            source_line_has_token = True
            prefix = current.strip()
            if prefix.startswith(("if ", "unless ")):
                end = index + 1
                while end < len(scanned) and scanned[end][0] not in {"newline", "comment", "{", "}"}:
                    end += 1
                if end < len(scanned) and scanned[end][0] == "}":
                    body = ""
                    for inner_kind, inner_value in scanned[index + 1:end]:
                        if inner_kind == ",":
                            body = body.rstrip() + ", "
                        else:
                            body += inner_value.lstrip(" \t") if body.endswith(" ") else inner_value
                    body = body.strip()
                    after = end + 1
                    while after < len(scanned) and scanned[after][0] in {"newline", "text"} and not scanned[after][1].strip():
                        after += 1
                    has_else = after < len(scanned) and scanned[after][0] == "text" and scanned[after][1].lstrip().startswith("else")
                    if body.count(";") == 1 and body.endswith(";") and not has_else and len("    " * depth + prefix + " { " + body + " }") <= 100:
                        current = prefix + " { " + body + " }"
                        skip_until = end + 1
                        continue
            current = ""
            if prefix.startswith("else") and lines and lines[-1].strip() == "}":
                lines[-1] += " " + prefix + " {"
            elif prefix:
                if frames and frames[-1][2] == "struct" and prefix.startswith("def ") and lines and lines[-1].rstrip().endswith(","):
                    lines.append("")
                emit(prefix + " {")
            elif lines and lines[-1] and not lines[-1].endswith((";", "}")):
                lines[-1] += " {"
            else:
                emit("{")
            structure = "struct" if prefix.startswith(("def struct ", "def pub struct ", "def priv struct ")) else "other"
            frames.append((parentheses, brackets, structure))
            depth += 1
            in_for_header = False
        elif kind == "}":
            source_line_has_token = True
            flush()
            parent_is_struct = len(frames) >= 2 and frames[-2][2] == "struct"
            depth = max(0, depth - 1)
            if frames:
                frames.pop()
            emit("}")
            if depth == 0:
                top_level_closes.add(len(lines) - 1)
            elif parent_is_struct:
                method_closes.add(len(lines) - 1)
        elif kind == ";":
            source_line_has_token = True
            head = current.lstrip()
            if head == "for" or head.startswith(("for ", "for;")):
                in_for_header = True
            if in_for_header:
                current = current.rstrip() + "; "
            elif not current.strip() and lines and lines[-1].rstrip().endswith(("}", ")", "]")):
                lines[-1] += ";"
            else:
                current = current.rstrip() + ";"
                flush()
        elif kind == ",":
            source_line_has_token = True
            if frames and (parentheses, brackets) == frames[-1][:2]:
                if current.strip():
                    current = current.rstrip() + ","
                    flush()
                elif lines and lines[-1].rstrip().endswith(("}", ")", "]")):
                    lines[-1] += ","
                else:
                    append(", ")
            else:
                current = current.rstrip() + ", "

    flush()
    output = []
    for index, line in enumerate(lines):
        if not line:
            if output and output[-1]:
                output.append("")
            continue
        output.append(line.rstrip())
        if index + 1 < len(lines) and (index in top_level_closes or index in method_closes or
                                        (line.startswith("import ") and
                                         not lines[index + 1].startswith("import "))):
            output.append("")
    while output and not output[-1]:
        output.pop()
    result = "\n".join(output) + "\n"
    if tokens(source) != tokens(result):
        raise ValueError("formatting changed lexical tokens")
    return result.replace("\n", original_newline)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true", help="report files that need formatting")
    action.add_argument("--write", action="store_true", help="format files in place")
    parser.add_argument("files", nargs="*", type=Path, help="default: all tracked .gloin files")
    arguments = parser.parse_args()
    paths = arguments.files or [ROOT / item.decode() for item in
                                subprocess.check_output(["git", "ls-files", "-z", "--", "*.gloin"], cwd=ROOT).split(b"\0")
                                if item]
    changed = []
    for path in paths:
        source = path.read_bytes().decode("utf-8")
        formatted = format_source(source)
        if format_source(formatted) != formatted:
            raise ValueError(f"formatter is not idempotent: {path}")
        if formatted != source:
            changed.append(path)
            if arguments.write:
                path.write_bytes(formatted.encode("utf-8"))
    for path in changed:
        print(path.relative_to(ROOT) if path.is_relative_to(ROOT) else path)
    print(f"{len(paths)} Gloin files checked; {len(changed)} {'formatted' if arguments.write else 'need formatting'}")
    if arguments.check and changed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
