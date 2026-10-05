#!/usr/bin/env python3
"""Check the Gloin JSON codec against Python's independent JSON/UTF-8 codecs."""

import argparse
import json
from pathlib import Path
import random
import subprocess
import tempfile


def byte_array(name, data):
    return f'def {name}: [u8; {len(data)}] = {{{", ".join(map(str, data))}}};\n'


def matrix():
    valid = [b'null', b'true', b'false', b'0', b'-0', b'1e400', b'1e-400',
             b'18446744073709551615', b'-9223372036854775808', b'1.2300e+04',
             b'{}', b'[]', b' [true, null, {"a":false,"a":2}]\r\n',
             b'[' * 64 + b'0' + b']' * 64]
    invalid = [b'', b' ', b'01', b'-01', b'+1', b'NaN', b'Infinity', b'-Infinity',
               b'.1', b'1.', b'1e', b'1e+', b'--1', b'-', b'true false', b'nullx',
               b'[]0', b'[1,]', b'[,1]', b'[1 2]', b'{"a":}', b'{"a",1}',
               b'{"a":1,}', b'{1:2}', b'{"a" 1}', b'/*x*/0', b'[}', b'{]',
               b'"', b'"\\', b'"\\x00"', b'"\\u000"', b'"\\uZZZZ"',
               b'"\\uD800"', b'"\\uDC00"', b'"\\uD800\\u0000"',
               b'"\\uD800x"', b'"a\x00b"', b'"a\nb"', b'0\v',
               b'\xef\xbb\xbf{}', b'[' * 65 + b'0' + b']' * 65]
    for bad_utf8 in [b'\x80', b'\xc0\x80', b'\xc1\xbf', b'\xc2', b'\xe0\x80\x80',
                     b'\xed\xa0\x80', b'\xf0\x80\x80\x80', b'\xf4\x90\x80\x80',
                     b'\xf5\x80\x80\x80', b'\xff', b'\xe2(\xa1']:
        invalid.append(b'"' + bad_utf8 + b'"')

    rng = random.Random(8259)
    scalars = [0, 1, 8, 9, 10, 12, 13, 31, 32, 34, 47, 92, 127, 128, 0x85,
               0x7ff, 0x800, 0x2028, 0x2029, 0xd7ff, 0xe000, 0xffff, 0x10000, 0x1f30d, 0x10ffff]
    for _ in range(24):
        value = rng.randrange(0x110000)
        if not 0xd800 <= value <= 0xdfff:
            scalars.append(value)
    strings = ['', 'Hello, café 🌍', 'quote " slash / backslash \\', 'a\0b']
    strings += [chr(value) for value in scalars]
    strings.append(''.join(chr(value) for value in scalars))
    for text in strings:
        valid.extend(json.dumps(text, ensure_ascii=ascii_only).encode() for ascii_only in (True, False))
    for _ in range(20):
        document = {'integer': rng.randrange(-(2**63), 2**64),
                    'items': [rng.choice(strings), True, None, rng.randrange(100) / 8],
                    'nested': {'ready': False}}
        valid.append(json.dumps(document, ensure_ascii=rng.choice((False, True))).encode())

    functions = []
    calls = []
    for index, (data, expected) in enumerate([(v, True) for v in valid] + [(v, False) for v in invalid]):
        if expected:
            json.loads(data)
        name = f'document_{index}'
        functions.append(f'''def {name}() -> bool {{
{byte_array('bytes', data)}
def text: string = strings.view_bytes(bytes[..]);
def checked: result<void> = json.validate(text, 16384, 64);
if checked.erroneous {{ return {str(not expected).lower()}; }}
return {str(expected).lower()};
}}''')
        calls.append(f'if !{name}() {{ std.println("{name} failed"); return 1; }}')

    for index, text in enumerate(strings):
        data = text.encode('utf-8')
        quoted = json.dumps(text, ensure_ascii=True).encode()
        name = f'string_{index}'
        functions.append(f'''def {name}() -> bool {{
{byte_array('source', quoted)}
{byte_array('expected', data)}
def mut decoded: [u8; {len(data)}] = zeroed;
def read: result<string> = json.decode_string(strings.view_bytes(source[..]), decoded[..]);
if read.erroneous {{ return false; }}
if !strings.equal(read.value, strings.view_bytes(expected[..])) {{ return false; }}
def mut encoded: [u8; {len(data) * 6 + 2}] = zeroed;
def written: result<string> = json.encode_string(read.value, encoded[..]);
if written.erroneous {{ return false; }}
std.println(written.value);
return true;
}}''')
        calls.append(f'if !{name}() {{ std.println("{name} failed"); return 2; }}')

    program = 'import "@json"; import "@strings"; import "@std";\n'
    program += '\n'.join(functions)
    program += '\ndef main() -> i32 {\n' + '\n'.join(calls) + '\nreturn 0;\n}\n'
    return program, strings, len(valid), len(invalid)


def run(command, expected=None):
    completed = subprocess.run(command, capture_output=True, timeout=120)
    if completed.returncode != 0 or completed.stderr:
        raise RuntimeError(f'{command!r}: exit {completed.returncode}\n{completed.stdout.decode(errors="replace")}\n{completed.stderr.decode(errors="replace")}')
    if expected is not None and completed.stdout != expected:
        raise RuntimeError(f'{command!r}: unexpected output {completed.stdout!r}')
    return completed.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('compiler', type=Path)
    parser.add_argument('--example', type=Path, default=Path(__file__).resolve().parent.parent / 'examples/json.gloin')
    args = parser.parse_args()
    compiler = str(args.compiler.resolve())
    program, strings, accepted, rejected = matrix()
    fixture = Path(__file__).resolve().parent / 'fixtures/json/behavior.gloin'
    with tempfile.TemporaryDirectory(prefix='gloin-json-') as temporary:
        folder = Path(temporary)
        matrix_source = folder / 'matrix.gloin'
        matrix_source.write_text(program)
        for mode in ('jit', 'O0', 'O2'):
            for source in (matrix_source, fixture, args.example.resolve()):
                if mode == 'jit':
                    command = [compiler, '--jit', str(source)]
                else:
                    executable = folder / f'{source.stem}-{mode}'
                    run([compiler, f'-{mode}', '-o', str(executable), str(source)])
                    command = [str(executable)]
                output = run(command)
                if source == matrix_source:
                    if not output.endswith(b'\n'):
                        raise RuntimeError(f'{mode}: missing final output newline')
                    decoded = [json.loads(line) for line in output[:-1].split(b'\n')]
                    if decoded != strings:
                        raise RuntimeError(f'{mode}: JSON writer disagrees with Python decoder')
                elif source == fixture:
                    if output != b'JSON state and limit checks passed\n':
                        raise RuntimeError(f'{mode}: unexpected state-check output {output!r}')
                else:
                    lines = output.decode('utf-8').splitlines()
                    expected = {'message': 'Hello, Gloin!\nUnicode: café 🌍',
                                'count': 18446744073709551615, 'tags': [True, None]}
                    if len(lines) != 2 or json.loads(lines[0]) != expected or lines[1] != 'JSON payload and exact u64 verified':
                        raise RuntimeError(f'{mode}: example output differs from independent oracle')
            print(f'{mode}: {accepted} accepted documents, {rejected} rejected documents, {len(strings)} Unicode round trips, state/limit checks and example passed', flush=True)


if __name__ == '__main__':
    main()
