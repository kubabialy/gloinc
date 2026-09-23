# Executable core acceptance (SPEC-021)

These are maintained source files executed by `CoreAcceptanceTest` through the
built `gloinc` CLI. Build and run instructions are in [the root README](../../../README.md).
The test definitions in [core_acceptance_test.cpp](../../core_acceptance_test.cpp)
record each expected value or diagnostic. CMake rejects unregistered fixture files.

```sh
./build/gloinc tests/fixtures/core/run/forward_call.gloin  # 42
./build/gloinc tests/fixtures/core/run/control_flow.gloin # 3
./build/gloinc tests/fixtures/core/run/recursion.gloin    # 120
ctest --test-dir build -j 4 --no-tests=error -R '^CoreAcceptanceTest\.' --output-on-failure
```

## Feature-to-fixture matrix

| Contract | Source fixtures |
| --- | --- |
| Complete normative examples and forward calls | [minimal](run/minimal.gloin), [forward_call](run/forward_call.gloin), [control_flow](run/control_flow.gloin) |
| Decimal, hex, binary, and leading-zero decimal values | [bases](run/bases.gloin) |
| Signed integer widths, boundary literals, calls, arithmetic/comparison operators, negation, truncating division, signed remainder | [i8](run/scalar_i8.gloin), [i16](run/scalar_i16.gloin), [i32](run/scalar_i32.gloin), [i64](run/scalar_i64.gloin) |
| Unsigned widths, full u64 range, unsigned comparison/division/remainder | [u8](run/scalar_u8.gloin), [u16](run/scalar_u16.gloin), [u32](run/scalar_u32.gloin), [u64](run/scalar_u64.gloin) |
| `int` and `usize` aliases | [int](run/scalar_int.gloin), [usize](run/scalar_usize.gloin), [scope_initialization](run/scope_initialization.gloin) |
| Floating literals, exponents, subnormals, negative zero, arithmetic/comparisons, calls | [f32](run/scalar_f32.gloin), [f64](run/scalar_f64.gloin) |
| bool, `!`, `&&`, `\|\|`, equality, explicit/implicit void returns | [bool_void](run/bool_void.gloin), [short_circuit](run/short_circuit.gloin) |
| Direct, forward, recursive and mutually recursive calls | [forward_call](run/forward_call.gloin), [recursion](run/recursion.gloin) |
| Constants, visibility, immutable/mutable locals, shadowing, definite initialization | [scope_initialization](run/scope_initialization.gloin) |
| Mutation, nested if/else/while/for/unless and early returns | [control_flow](run/control_flow.gloin), [nested_early_returns](run/nested_early_returns.gloin) |
| Omitted for components, initializer effects/scope, skipped body/update | [for_components](run/for_components.gloin) |
| Precedence and parentheses | [precedence](run/precedence.gloin) |
| UTF-8 comments, CRLF, multiline expressions | [utf8_comments](run/utf8_comments.gloin) |
| i32 result mapped to host exit status modulo 256, with no implicit output | [negative](run/negative_result.gloin), [zero](run/zero_result.gloin), [minimum](run/minimum_result.gloin), [maximum](run/maximum_result.gloin) |
| Function-exit LIFO defer and checked registration (SPEC-027) | [defer](run/defer.gloin), [invalid operand](reject/defer_operand.gloin), [cleanup trap](trap/defer_cleanup.gloin) |
| Initialized arena allocation, import requirement, and freed-handle traps (SPEC-028) | [arena](run/arena.gloin), [type argument](reject/arena_value.gloin), [missing import](reject/unimported_arena.gloin), [freed handle](trap/arena_freed.gloin) |
| Instance/static methods and explicit self (SPEC-026) | [methods](run/methods.gloin), [invalid receiver](reject/method_receiver.gloin), [null method access](trap/null_method.gloin) |
| Pointers, references, indirect writes, and read-only aliases (SPEC-025) | [pointers](run/pointers.gloin) |
| Null reference and pointee mismatch diagnostics | [null_reference](reject/null_reference.gloin), [pointer_type_mismatch](reject/pointer_type_mismatch.gloin) |
| Null dereference traps before loading | [null_dereference](trap/null_dereference.gloin) |
| Ordinary nested structs, value calls/copies, and field assignment (SPEC-024) | [ordinary_struct](run/ordinary_struct.gloin) |
| Standard import and exact hello-world output (SPEC-023) | [hello_world](run/hello_world.gloin) |
| Eleven normative invalid fragments | `reject/canonical_01.gloin` through `canonical_11.gloin`; the unknown-type binding is wrapped in a function to reach type checking |
| Initialization, immutability, scopes, types, literal ranges, calls, returns, constants | Named files under [reject](reject), including [uninitialized](reject/uninitialized.gloin), [mixed_widths](reject/mixed_widths.gloin), [missing_return](reject/missing_return.gloin) |
| Deferred syntax/type families and unsupported operators | `reject/deferred_*.gloin`: local/package imports, unsupported string escapes, packed structs, arrays, generics, concurrency, extended numeric/layout types, legacy syntax, range loops, bitwise/shift/compound operators |
| Checked integer overflow at every width; zero division and signed-minimum remainder | `trap/overflow_i*.gloin`, `trap/overflow_u*.gloin`, [division_zero](trap/division_zero.gloin), [signed_remainder](trap/signed_remainder.gloin) |
| Floating zero division and non-finite results at both widths | [f32 division](trap/division_zero_f32.gloin), [f64 division](trap/division_zero_f64.gloin), [f32 overflow](trap/overflow_f32.gloin), [f64 overflow](trap/overflow_f64.gloin) |

## What each case proves

The scalar `run` fixtures pass checking and then return the expected host exit
status with empty stdout/stderr in three independent compile/run processes. The
hello-world fixture separately asserts exact stdout and exit 0. These
checks establish repeatability for the selected toolchain/platform, not native
binary reproducibility or portability.

Negative fixtures must exit 1 with empty stdout and the expected diagnostic
text plus filename/line/column in all four modes. The expected text is matched
against the diagnostic message, not the filename. Many fixtures place an integer
trap before the invalid construct: execution before validation cannot pass as a
compiler rejection. Canonical invalid fragments are retained even where an earlier
top-level grammar error is the first diagnostic.

The 17 `trap` fixtures must pass checking, then terminate execution with SIGTRAP
or SIGILL and no result. Launch errors, ordinary compiler-error exits, and timeouts
do not count as expected traps. Each invocation has a 10-second timeout; each
CTest case has a 30-second timeout and independent temporary output files.

The shared fixture harness also serves `CliTest`. Existing lower-level tests
continue checking IR structure, exact floating bits, evaluation order, and
additional error boundaries. The unfiltered suite retains its deferred-feature
failures. Passing core acceptance alone does not publish version 0.0.1;
SPEC-046 remains the packaging/release gate.
