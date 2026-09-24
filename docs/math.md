# Concrete numerical utilities (SPEC-030f)

`import "@math";` loads [math.gloin](../stdlib/math.gloin), an ordinary source
module importing only `@status`. Its 47 functions have explicit concrete types;
there are no generics, implicit numeric conversions, arena parameters, or hidden
retained storage. All public functions have examples and costs in their source
comments. The [streaming geometry example](../examples/math_lab.gloin) combines
these helpers with input, strings, checked conversions, and explicit scratch reuse.

## Types, constants, and shared errors

`math.I32Result`, `I64Result`, `U64Result`, `F32Result`, and `F64Result` contain
`status: i32` and `value` of the corresponding scalar type. They are distinct
nominal types from the parsing/conversion results in `@std`.

Successful checked operations return `status.OK`. Every failure returns integer
zero or **positive floating zero**, never a partial result, NaN, or infinity:

| Status | Meaning |
| --- | --- |
| `INVALID` | Invalid clamp bounds or mathematical domain; native nonfinite input or invalid operation selector also rejects |
| `OVERFLOW` | Absolute value of minimum signed integer, or an unrepresentable floating result |
| `UNDERFLOW` | A mathematically nonzero floating result rounds to zero |

Nonzero subnormal results succeed, even when host libm signals underflow/ERANGE.
Approximate finite results do not return `INEXACT`; floating math does not claim
exactness. Standard error codes remain unchanged. Ordinary Gloin already requires
finite floating values; the private native boundary additionally rejects nonfinite
inputs supplied by a native caller. No math call turns failure into an arithmetic
trap. Arena creation, unrelated arithmetic, and explicit conversions retain their
existing contracts.

`PI_F32`, `TAU_F32`, `PI_F64`, and `TAU_F64` are the nearest representable constants
of their declared type (tau is 2π). Reading a constant allocates nothing. Angles
are always radians, including `atan2` results.

```gloin
import "@math";
import "@status";
import "@std";

def main() -> i32 {
    def length: math.F64Result = math.hypot_f64(3.0, 4.0);
    if length.status != status.OK || length.value != 5.0 { return 1; }
    def bounded: math.F64Result = math.clamp_f64(length.value, 0.0, 4.0);
    if bounded.status != status.OK || bounded.value != 4.0 { return 2; }
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
| `clamp_T(value, low, high)` | i32, i64, u64, f32, f64 | Checked T result; `clamp_i32(9, 1, 4)` is OK/4; `clamp_i32(2, 4, 1)` is INVALID/0 |
| `abs_i32(value)`, `abs_i64(value)` | Signed integers | Checked result; `abs_i32(-42)` is OK/42; minimum signed value is OVERFLOW/0 |
| `abs_f32(value)`, `abs_f64(value)` | Floats | Checked result; `abs_f64(-2.0)` is OK/2; either zero becomes +0 |

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
that width, and returns `math.F32Result` or `math.F64Result`. The selected host
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
| `sqrt` | `sqrt_f64(4.0)` → 2 | Negative input INVALID; -0 succeeds as -0 |
| `pow` | `pow_f64(-2.0, 3.0)` → -8 | `(base, exponent)`; negative base requires an integral exponent; zero to a negative power INVALID; **0⁰ = 1** |
| `exp` | `exp_f64(0.0)` → 1 | e raised to the argument; large magnitude can overflow or underflow |
| `log` | `log_f64(1.0)` → +0 | Natural logarithm; input ≤0 INVALID, including either zero |
| `log10` | `log10_f64(100.0)` → 2 | Base-ten logarithm; input ≤0 INVALID |
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
represent returns UNDERFLOW/+0. `hypot(±0, ±0)` returns +0. Signed-zero guarantees
are part of the supported IEEE floating platform contract; other successful
boundary signs follow the same host math semantics.

```gloin
import "@math";
import "@status";
import "@std";

def main() -> i32 {
    def some_val: int = 42;
    def foo: int = -some_val;
    if foo != -42 || math.abs_i32(foo).value != 42 { return 1; }
    def rounded: math.F64Result = math.round_f64(-2.5);
    if rounded.status != status.OK || rounded.value != -3.0 { return 2; }
    if math.abs_i32(-2147483648).status != status.OVERFLOW { return 3; }
    std.println("negation and rounding: ok");
    return 0;
}
```

Unary minus in a typed initializer is ordinary existing language syntax. It does
not need `@math`; signed-minimum negation still traps under core arithmetic rules,
whereas `math.abs_i32`/`abs_i64` return a recoverable overflow status.

## Accuracy and host state

The runtime uses host C++ `<cmath>` overloads under the **default floating-point
environment**, including round-to-nearest and nontrapping exceptions. It saves
and restores the calling thread's environment (rounding mode and exception flags)
and errno on success and failure. It does not change another thread's environment.
Failure to establish that environment returns INVALID/+0. No process-global
rounding mode, locale, or persistent math state is introduced.

Classification uses domain prechecks and host results/exceptions, then applies
Gloin's finite-result rules. Host underflow flags alone do not discard a nonzero
subnormal. The returned result is always finite on success. The underlying
[C++ math interfaces](https://eel.is/c++draft/c.math) and
[floating environment interfaces](https://eel.is/c++draft/cfenv.syn) supply the
native operations; Gloin's status, zero-payload, domain, and underflow policies are
specified here and enforced at that boundary.

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
errors. I/O failures remain separate from math status; errors use stderr and exit
2, output failures exit 3. No input record is retained.

The example does O(input bytes + record count) work with bounded scratch storage.
Its arithmetic is ordinary floating-point statistics, not exact decimal statistics.
Both guide programs and the example execute in the maintained tests, including
installed and relocated packages. Native tests also cover all operation selectors,
nonfinite native inputs, signed zero, rounding neighbors, finite subnormals, extreme
hypot, independent constants, host-state restoration, and concurrent environments.
