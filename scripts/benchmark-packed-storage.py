#!/usr/bin/env python3
"""Compare packed indexing compilers with separately compiled, checked native kernels."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import random
import shutil
import statistics
import subprocess

LIMIT = 65536
ELEMENTS = [('u3', 3), ('u13', 13), ('be_u32', 32), ('Record', 24)]
CASES = [mode + '_' + element for element, _ in ELEMENTS for mode in ('sequential', 'indexed')]


def run(command):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise RuntimeError(f'{command}: exit {result.returncode}\n{result.stdout}\n{result.stderr}')
    return result.stdout


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sources():
    gloin = 'def packed struct(msb_first) Record { def value: be_u16, def spare: u8, }\n'
    declarations = ''
    for element, _ in ELEMENTS:
        for mode in ('sequential', 'indexed'):
            name = mode + '_' + element
            array = f'packed(msb_first) [{element}; {LIMIT}]'
            slice_type = f'packed(msb_first) [const {element}]'
            index = 'i'
            selection = ''
            if mode == 'indexed':
                selection = 'def index: u64 = positions[i]; if index >= view.len { return 0; }'
                index = 'index'
            value = f'view[{index}]' + ('.value' if element == 'Record' else '')
            gloin += f'''
                def {name}(input: &const {array}, indices: &const [u64; {LIMIT}], count: u64) -> u64 {{
                    def view: {slice_type} = (*input)[3..count + 3];
                    def positions: [const u64] = (*indices)[..count];
                    def mut i: u64 = 0;
                    def mut sum: u64 = 0;
                    while i < view.len {{
                        {selection}
                        def value: result<u64> = u64({value});
                        if value.erroneous {{ return 0; }}
                        sum = sum + value.value;
                        i = i + 1;
                    }}
                    return sum;
                }}
            '''
            declarations += f'extern uint64_t {name}(const void *, const uint64_t *, uint64_t);\n'
    # Object emission keeps module functions; the runner renames this wrapper.
    gloin += 'def main() -> i32 { return 0; }\n'
    cases = ',\n'.join(f'{{"{mode}_{element}", {mode}_{element}, {width}, '
                        f'{int(mode == "indexed")}, {int(element == "Record")}}}'
                        for element, width in ELEMENTS for mode in ('sequential', 'indexed'))
    driver = r'''
        #define _POSIX_C_SOURCE 200809L
        #include <stdint.h>
        #include <stdio.h>
        #include <stdlib.h>
        #include <string.h>
        #include <time.h>
        typedef uint64_t (*Kernel)(const void *, const uint64_t *, uint64_t);
        struct Case { const char *name; Kernel kernel; unsigned width, indexed, record; };
    ''' + declarations + '\nstatic const struct Case cases[] = {\n' + cases + r'''};
        static uint64_t next(uint64_t *state) {
            *state ^= *state << 13; *state ^= *state >> 7; *state ^= *state << 17; return *state;
        }
        static uint64_t stamp(clockid_t clock) {
            struct timespec value;
            if (clock_gettime(clock, &value)) abort();
            return (uint64_t)value.tv_sec * UINT64_C(1000000000) + value.tv_nsec;
        }
        int main(int argc, char **argv) {
            if (argc != 5) return 1;
            const struct Case *test = 0;
            for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
                if (!strcmp(argv[1], cases[i].name)) test = &cases[i];
            uint64_t count = strtoull(argv[2], 0, 10), repetitions = strtoull(argv[3], 0, 10);
            uint64_t random = strtoull(argv[4], 0, 10);
            if (!test || !count || count > 65533 || !repetitions || !random) return 2;
            size_t size = (65536 * test->width + 7) / 8;
            unsigned char *allocation = malloc(size + 2), *original = malloc(size);
            uint64_t *indices = malloc(65536 * sizeof(uint64_t));
            if (!allocation || !original || !indices) return 3;
            unsigned char *data = allocation + 1; /* Deliberately unaligned. */
            allocation[0] = 165; allocation[size + 1] = 90;
            for (size_t i = 0; i < size; ++i) original[i] = data[i] = (unsigned char)next(&random);
            for (uint64_t i = 0; i < count; ++i) indices[i] = next(&random) % count;
            uint64_t expected = 0;
            for (uint64_t i = 0; i < count; ++i) {
                uint64_t index = (test->indexed ? indices[i] : i) + 3, value = 0;
                unsigned bits = test->record ? 16 : test->width;
                for (unsigned j = 0; j < bits; ++j) {
                    uint64_t position = index * test->width + j;
                    value = (value << 1) | ((original[position / 8] >> (7 - position % 8)) & 1);
                }
                expected += value;
            }
            if (test->kernel(data, indices, count) != expected) return 4;
            uint64_t wall = stamp(CLOCK_MONOTONIC), cpu = stamp(CLOCK_PROCESS_CPUTIME_ID);
            for (uint64_t i = 0; i < repetitions; ++i)
                if (test->kernel(data, indices, count) != expected) return 5;
            cpu = stamp(CLOCK_PROCESS_CPUTIME_ID) - cpu;
            wall = stamp(CLOCK_MONOTONIC) - wall;
            if (memcmp(data, original, size) || allocation[0] != 165 || allocation[size + 1] != 90) return 6;
            printf("{\"cpu_ns\":%llu,\"wall_ns\":%llu,\"checksum\":%llu}\n",
                   (unsigned long long)cpu, (unsigned long long)wall, (unsigned long long)expected);
            free(indices); free(original); free(allocation);
            return 0;
        }
    '''
    return gloin, driver


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', action='append', required=True, help='label=/path/to/gloinc (matching runtime/stdlib alongside)')
    parser.add_argument('--llvm-bin', type=Path, default=Path('/opt/homebrew/opt/llvm/bin'))
    parser.add_argument('--output', type=Path, required=True, help='new directory; raw output is retained')
    parser.add_argument('--counts', default='512,16384,65520')
    parser.add_argument('--samples', type=int, default=7)
    parser.add_argument('--warmups', type=int, default=2)
    args = parser.parse_args()
    counts = [int(x) for x in args.counts.split(',')]
    if any(n < 1 or n > LIMIT - 3 for n in counts) or args.samples < 3 or args.warmups < 1:
        parser.error('counts must be 1..65533, samples >=3 and warmups >=1')
    compilers = {}
    for item in args.compiler:
        label, path = item.split('=', 1)
        if not label.replace('_', '').replace('-', '').isalnum() or label in compilers or label in {"driver", "kernels", "benchmark"}:
            parser.error('compiler labels must be unique simple names')
        compilers[label] = Path(path).resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    shutil.copy2(__file__, output / 'benchmark.py')
    gloin, driver = sources()
    (output / 'kernels.gloin').write_text(gloin)
    (output / 'driver.c').write_text(driver)
    llvm = args.llvm_bin.resolve()
    run([llvm / 'clang', '-O2', '-Wall', '-Wextra', '-Werror', '-c', output / 'driver.c', '-o', output / 'driver.o'])
    environment = {'host': platform.platform(), 'machine': platform.machine(),
                   'clang': run([llvm / 'clang', '--version']), 'compilers': {},
                   'counts': counts, 'samples': args.samples, 'warmups': args.warmups,
                   'sources': {f: digest(output / f) for f in ('kernels.gloin', 'driver.c', 'benchmark.py')}}
    for label, compiler in compilers.items():
        runtime = compiler.parent / 'libgloin_runtime.a'
        obj, renamed = output / (label + '.o'), output / (label + '-linked.o')
        run([compiler, '--stdlib-dir', compiler.parent / 'stdlib', '--emit-object', '-O2', '-o', obj, output / 'kernels.gloin'])
        prefix = '_' if platform.system() == 'Darwin' else ''
        run([llvm / 'llvm-objcopy', '--redefine-sym', prefix + 'main=' + prefix + 'gloin_unused_main', obj, renamed])
        run([llvm / 'clang++', output / 'driver.o', renamed, runtime, '-o', output / label])
        environment['compilers'][label] = {'path': str(compiler), 'sha256': digest(compiler),
            'version': run([compiler, '--version']), 'runtime_sha256': digest(runtime),
            'object_sha256': digest(obj), 'executable_sha256': digest(output / label),
            'size': run([llvm / 'llvm-size', obj])}
        (output / (label + '.asm')).write_text(run([llvm / 'llvm-objdump', '--disassemble', '--no-show-raw-insn', obj]))
    (output / 'environment.json').write_text(json.dumps(environment, indent=2) + '\n')
    randomizer = random.Random(309)
    records = []
    with (output / 'samples.jsonl').open('w') as log:
        for case in CASES:
            for count in counts:
                calibration = []
                for label in compilers or label in {"driver", "kernels", "benchmark"}:
                    sample = json.loads(run([output / label, case, count, 20, 12345]))
                    calibration.append(sample['cpu_ns'] / 20)
                repetitions = max(1, min(1000000, int(30000000 / max(1, min(calibration)))))
                for round_number in range(-args.warmups, args.samples):
                    labels = list(compilers)
                    randomizer.shuffle(labels)
                    seed = randomizer.randrange(1, 1 << 63)
                    checksums = []
                    for label in labels:
                        sample = json.loads(run([output / label, case, count, repetitions, seed]))
                        sample.update(variant=label, case=case, count=count, repetitions=repetitions, round=round_number, seed=seed)
                        log.write(json.dumps(sample) + '\n'); log.flush()
                        records.append(sample); checksums.append(sample['checksum'])
                    if len(set(checksums)) != 1:
                        raise RuntimeError('compiler outputs disagree')
                print(f'{case}: {count} elements checked', flush=True)
    summary = []
    for case in CASES:
        for count in counts:
            row = {'case': case, 'count': count}
            for label in compilers or label in {"driver", "kernels", "benchmark"}:
                values = [r['cpu_ns'] / r['repetitions'] / count for r in records
                          if r['case'] == case and r['count'] == count and r['variant'] == label and r['round'] >= 0]
                row[label + '_ns_per_element'] = statistics.median(values)
            summary.append(row)
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
