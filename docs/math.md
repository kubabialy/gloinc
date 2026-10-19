# Concrete numerical utilities (SPEC-030f)

The 0.2.0 compiler returns built-in `result<T>` from every
fallible math function. The published 0.1.0 API used status structs. This is a
source API change for 0.2.0; see the migration table below. The
[0.1.0 math guide](https://github.com/kubabialy/gloinc/blob/v0.1.0/docs/math.md)
documents the released signatures and statuses.

`import "@math";` loads [math.gloin](../stdlib/math.gloin), an ordinary source
module importing only `@status`. Its 47 functions have explicit concrete types;
there are no generics, implicit numeric conversions, arena parameters, or hidden
retained storage. All public functions have examples and costs in their source
comments. The [streaming geometry example](../examples/math_lab.gloin) combines
these helpers with input, strings, checked conversions, and explicit scratch reuse.

## Types, constants, and shared errors

The 37 fallible operations return `result<i32>`, `result<i64>`, `result<u64>`,
`result<f32>`, or `result<f64>` according to their scalar width. Min/max remain
infallible and return scalars directly. Check `.erroneous` before `.value` or
use an exhaustive `match`. Discarding a result, leaving it unhandled, or reading
the wrong payload is a compile error.

A failure contains an `error` with a static, allocation-free message. It has no
usable numeric payload. The exact messages in this development API are:

| Error message | Meaning |
| --- | --- |
| `math clamp bounds are reversed` | `low > high` for any clamp width |
| `math absolute value overflow` | Absolute value of the minimum i32 or i64 |
| `math domain error` | Invalid floating domain; the native boundary also rejects nonfinite input, invalid selectors and unavailable floating environment |
| `math result overflow` | A floating result is too large for its type |
| `math result underflow` | A mathematically nonzero floating result rounds to zero |
| `math runtime failure` | An unexpected private runtime status, handled defensively |

These messages distinguish the failures formerly represented by `INVALID`,
`OVERFLOW`, and `UNDERFLOW`. Math currently uses the one-argument error
constructor, so `.code` and `.os_error` are both zero (unspecified). The math
runtime still uses numeric statuses internally and restores host errno; math
never exposed errno as a public result field. Error codes are described in
[results and errors](results.md#error-values-and-diagnostic-codes). Source
locations and stack traces are not part of the current error value.

Nonzero subnormal results succeed, even when host libm signals underflow/ERANGE.
Approximate finite results do not report inexactness; floating math does not claim
exactness. Ordinary Gloin already requires finite floating values; the private
native boundary additionally rejects nonfinite
inputs supplied by a native caller. No math call turns failure into an arithmetic
trap. Arena creation, unrelated arithmetic, and explicit conversions retain their
existing contracts.

### Migration from 0.1.0

Arguments and function names are unchanged. Each `T` below denotes a concrete
suffix and scalar type, not a newly generic math API.

| Functions | 0.1.0 return | Development return |
| --- | --- | --- |
| `min_T`, `max_T` | scalar `T` | scalar `T` (unchanged) |
| `clamp_i32`, `abs_i32` | `math.I32Result` | `result<i32>` |
| `clamp_i64`, `abs_i64` | `math.I64Result` | `result<i64>` |
| `clamp_u64` | `math.U64Result` | `result<u64>` |
| All checked `_f32` functions | `math.F32Result` | `result<f32>` |
| All checked `_f64` functions | `math.F64Result` | `result<f64>` |

The five old structs are removed from the 0.2.0 library. Replace
`r.status != status.OK` with a direct `if r.erroneous` guard and read `.value`
afterwards. Bind a call before accessing its fields, or match the call directly.
Check each result before an early return, including when another operation has
also failed. The former dummy zero on failure is no longer a language value.
Successful values, signed zeros, domains, rounding, costs and ownership remain
the same. The compiler constructs and checks results through GloinIR; the four
private native math ABIs are unchanged.

`PI_F32`, `TAU_F32`, `PI_F64`, and `TAU_F64` are the nearest representable constants
of their declared type (tau is 2π). Reading a constant allocates nothing. Angles
are always radians, including `atan2` results.

```gloin
import "@math";
import "@std";

def main() -> i32 {
    def length: result<f64> = math.hypot_f64(3.0, 4.0);
    if length.erroneous { return 1; }
    if length.value != 5.0 { return 1; }
    def bounded: result<f64> = math.clamp_f64(length.value, 0.0, 4.0);
    if bounded.erroneous { return 2; }
    if bounded.value != 4.0 { return 2; }
    std.println("length=5");
    return 0;
}
```

## Selection, clamp, and absolute value

Each `T` in this table stands for a separate function suffix, not generic syntax.
For example, `math.min_i32(2, 3)` returns 2 and `math.min_f64(2.0, 3.0)` returns 2.0.

| Functions | Types | Result and example |
| --- | --- | --- |
| `min_T(a, b)` | i32, i64, u64, f32, f64 | Scalar T; smaller operand, first operand on equality |
| `max_T(a, b)` | i32, i64, u64, f32, f64 | Scalar T; larger operand, first operand on equality |
| `clamp_T(value, low, high)` | i32, i64, u64, f32, f64 | `result<T>`; `clamp_i32(9, 1, 4)` succeeds with 4; `clamp_i32(2, 4, 1)` errors |
| `abs_i32(value)`, `abs_i64(value)` | Signed integers | `result<T>`; `abs_i32(-42)` succeeds with 42; minimum signed value errors |
| `abs_f32(value)`, `abs_f64(value)` | Floats | `result<T>`; `abs_f64(-2.0)` succeeds with 2; either zero becomes +0 |

Selection, clamp, and integer absolute value are O(1), allocation-free Gloin
functions. Integer abs checks the minimum value **before** negating it. Min/max
choose the first operand on numerical equality, including ±0; they do not impose
an IEEE total order. Clamp rejects `low > high`, accepts equal bounds, returns an
in-range value unchanged (including its zero sign), and otherwise returns the
selected bound. Thus `clamp_f64(-0.0, 0.0, 1.0)` preserves -0.

Float abs uses the checked native float path described below. No operation
silently narrows or widens types. Use `@std` conversions when needed.

## Floating rounding and elementary functions

Every function below exists separately for `_f32` and `_f64`, takes operands of
that width, and returns `result<f32>` or `result<f64>`. The selected host
float-width overload executes directly; f32 is not implemented by computing in
f64 and narrowing afterward. All calls have fixed dispatch/environment overhead
plus the host libm routine's cost, with no runtime allocation or retained storage.
The documentation does not promise constant instruction count or latency for libm.

| Function stem | Example using `_f64` | Domain, result, and special behavior |
| --- | --- | --- |
| `floor` | `floor_f64(-1.25)` → -2 | Toward negative infinity; retains floating type |
| `ceil` | `ceil_f64(-1.25)` → -1 | Toward positive infinity |
| `trunc` | `trunc_f64(-1.75)` → -1 | Toward zero |
| `round` | `round_f64(-2.5)` → -3 | Nearest integral value; ties **away from zero** |
| `sqrt` | `sqrt_f64(4.0)` → 2 | Negative input gives a domain error; -0 succeeds as -0 |
| `pow` | `pow_f64(-2.0, 3.0)` → -8 | `(base, exponent)`; negative base requires an integral exponent; zero to a negative power gives a domain error; **0⁰ = 1** |
| `exp` | `exp_f64(0.0)` → 1 | e raised to the argument; large magnitude can overflow or underflow |
| `log` | `log_f64(1.0)` → +0 | Natural logarithm; input ≤0 gives a domain error, including either zero |
| `log10` | `log10_f64(100.0)` → 2 | Base-ten logarithm; input ≤0 gives a domain error |
| `sin` | `sin_f64(0.0)` → +0 | Radians; preserves zero sign |
| `cos` | `cos_f64(0.0)` → 1 | Radians |
| `tan` | `tan_f64(0.0)` → +0 | Radians; preserves zero sign; rounded `PI/2` is not an exact mathematical pole |
| `atan2` | `atan2_f64(1.0, 0.0)` → approximately π/2 | Arguments **(y, x)**; quadrant-aware result in [-π, π] |
| `hypot` | `hypot_f64(3.0, 4.0)` → 5 | Nonnegative √(x²+y²), computed with host scaling to avoid avoidable intermediate overflow/underflow |

`floor`, `ceil`, `trunc`, and `round` preserve a zero input's sign. When rounding
a small negative value to zero, the result is -0. Unlike `std.format_f64_fixed`,
which uses ties-to-even formatting, `math.round_f64` deliberately uses ties away
from zero. Converting a rounded float to an integer remains an explicit, checked
`@std` conversion. Huge integral floats stay floating values.

`pow(-0, positive odd integer)` returns -0; other positive exponents of zero
return +0. `pow(±0, 0)` returns 1. Negative bases with negative integral exponents
are allowed and may underflow or overflow. Nonintegral negative-base powers and
zero negative powers fail before calling libm.

`atan2(±0, positive x or +0)` returns ±0; `atan2(±0, negative x or -0)` returns
±π with y's sign. A nonzero y with x=±0 gives ±π/2. A nonzero angle too small to
represent returns an underflow error. `hypot(±0, ±0)` returns +0. Signed-zero guarantees
are part of the supported IEEE floating platform contract; other successful
boundary signs follow the same host math semantics.

```gloin
import "@math";
import "@std";

def main() -> i32 {
    def some_val: int = 42;
    def foo: int = -some_val;
    def absolute: result<i32> = math.abs_i32(foo);
    if absolute.erroneous { return 1; }
    if foo != -42 || absolute.value != 42 { return 1; }
    def rounded: result<f64> = math.round_f64(-2.5);
    if rounded.erroneous { return 2; }
    if rounded.value != -3.0 { return 2; }
    def minimum: result<i32> = math.abs_i32(-2147483648);
    if !minimum.erroneous { return 3; }
    std.println("negation and rounding: ok");
    return 0;
}
```

Unary minus in a typed initializer is ordinary existing language syntax. It does
not need `@math`; signed-minimum negation still traps under core arithmetic rules,
whereas `math.abs_i32`/`abs_i64` return a recoverable overflow error.

## Accuracy and host state

The runtime uses host C++ `<cmath>` overloads under the **default floating-point
environment**, including round-to-nearest and nontrapping exceptions. It saves
and restores the calling thread's environment (rounding mode and exception flags)
and errno on success and failure. It does not change another thread's environment.
Failure to establish that environment returns a domain error. No process-global
rounding mode, locale, or persistent math state is introduced.

Classification uses domain prechecks and host results/exceptions, then applies
Gloin's finite-result rules. Host underflow flags alone do not discard a nonzero
subnormal. The returned result is always finite on success. The underlying
[C++ math interfaces](https://eel.is/c++draft/c.math) and
[floating environment interfaces](https://eel.is/c++draft/cfenv.syn) supply the
native operations. The private ABI zeroes failed output storage; the source
library exposes a checked error instead. Gloin's domain and underflow policies
are specified here and enforced at that boundary.

No universal ULP bound or cross-platform bitwise reproducibility is promised for
transcendentals. Large trig arguments in particular depend on host argument
reduction. Tests use independent decimal references (4 machine epsilons scaled by
max(1, |reference|)), exact cases, and identities with documented tolerances. These
are acceptance checks over tested inputs, not a claimed global error bound. No
fast-math flags, NaN/Inf propagation, inverse trig beyond atan2, generic overloads,
or implicit conversion are introduced by SPEC-030f.

## Streaming geometry and statistics example

```sh
printf '3,4\n0,0\n6,8\n' | build/gloinc --jit examples/math_lab.gloin
```

Expected output:

```text
count=3
min=0.000
max=10.000
mean=5.000
stddev=4.082
last_angle=0.927
```

The example reads at most 256 bytes per line, parses exactly two comma-separated
finite coordinates, bounds each absolute coordinate to 1e6, computes radius with
hypot and bearing with atan2, and accumulates min/max/mean/population variance with
Welford's update. At most one million records are accepted. A single scratch arena
is reset per record; the statistics struct retains only scalars. Empty input,
malformed records, oversized records, and coordinate/count bounds fail explicitly.
The final sqrt uses an explicit max-with-zero to handle tiny negative variance
roundoff. Float formatting requests three decimal places and checks allocation
errors. I/O failures remain separate from math failures; errors use stderr and exit
2, output failures exit 3. No input record is retained.

The example does O(input bytes + record count) work with bounded scratch storage.
Its arithmetic is ordinary floating-point statistics, not exact decimal statistics.
Both guide programs and the example execute in the maintained tests, including
installed and relocated packages. Native tests also cover all operation selectors,
nonfinite native inputs, signed zero, rounding neighbors, finite subnormals, extreme
hypot, independent constants, host-state restoration, and concurrent environments.
