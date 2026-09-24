# Monotonic timing and seeded randomness (SPEC-030g)

`@time` loads [time.gloin](../stdlib/time.gloin); `@random` loads
[random.gloin](../stdlib/random.gloin). Public types and policy are ordinary Gloin,
with two narrow native primitives for clock reads and the SplitMix64 transition.
Neither module needs a caller arena for its operations. Both import `@std` for
explicit numeric conversions; importing it does not allocate an arena at runtime.
Every public function/method documents usage, failure, lifetime, and cost in source.

## Instants, durations, and units

`time.Instant { ticks_ns: u64 }` holds raw monotonic ticks. The clock epoch is
unspecified: these are **not calendar timestamps**. Only compare instants from the
same clock/provider domain. Raw public fields allow explicit construction for
arithmetic/tests; the library cannot infer that two values share an epoch.

`time.Duration { nanoseconds: u64 }` represents a nonnegative duration with exact
integer nanosecond storage. Its maximum is 18,446,744,073,709,551,615 ns. Public
fields are immutable through ordinary bindings; create another value for a change.
Reading either scalar field is O(1), with no allocation.

`time.InstantResult { status: i32, os_error: i32, value: Instant }` has zero ticks
on failure. Only IO_ERROR carries an OS error number, with EIO as fallback.
`time.DurationResult { status: i32, value: Duration }` has zero nanoseconds on
failure. Zero is also a valid success: always inspect status.

| API and example | Behavior and cost |
| --- | --- |
| `time.monotonic_now()` | Checked host CLOCK_MONOTONIC reading converted to u64 ns; no allocation; one OS read plus fixed conversion, or the injected callback's cost |
| `time.elapsed(start, end)` | Checked `end - start`; backwards ticks are INVALID/zero, equal ticks are OK/zero; O(1), no allocation or hidden read |
| `time.Duration.from_milliseconds(1500)` | OK/1,500,000,000 ns; values above 18,446,744,073,709 fail OVERFLOW/zero before multiplication; O(1), no allocation |
| `time.Duration.from_seconds(2)` | OK/2,000,000,000 ns; values above 18,446,744,073 fail OVERFLOW/zero before multiplication; O(1), no allocation |
| `duration.whole_microseconds()` | u64 nanoseconds divided by 1000, flooring the fraction; O(1), no allocation/failure |
| `duration.whole_milliseconds()` | u64 nanoseconds divided by 1,000,000, flooring the fraction; O(1), no allocation/failure |
| `duration.seconds_f64_rounded()` | Rounded u64-to-f64 conversion followed by division by 1e9; finite f64, O(1), no allocation/failure; large durations lose nanosecond precision |
| `a.checked_add(b)` | DurationResult; unrepresentable sum is OVERFLOW/zero; operands unchanged, O(1), no allocation |
| `a.checked_sub(b)` | DurationResult; a negative result is OVERFLOW/zero; equal operands give OK/zero; O(1), no allocation |

Nanoseconds specify **units, not clock resolution or accuracy**. Consecutive reads
may be equal. The host monotonic clock measures elapsed time rather than CPU time;
resolution, suspend behavior, and scheduling effects follow the host. No sleep,
timer, wall-clock, timezone, or CPU affinity interface is added here. A clock's
failure is IO_ERROR with errno; unrepresentable native ticks produce OVERFLOW/zero,
and invalid native seconds/nanoseconds or a malformed callback produce INVALID/zero.
Clock reads preserve the caller's errno. The library operations retain no input
pointers and allocate no storage; an injected callback controls its own effects,
and binding an embedding clock has separate costs below.

The floating seconds conversion is explicitly approximate and uses two rounding
steps. Keep `nanoseconds` or use integer whole-unit methods for exact comparisons.

```gloin
import "@time";
import "@status";
import "@std";

def main() -> i32 {
    def start: time.Instant = time.Instant { ticks_ns: 100 };
    def finish: time.Instant = time.Instant { ticks_ns: 2100100 };
    def elapsed: time.DurationResult = time.elapsed(start, finish);
    if elapsed.status != status.OK || elapsed.value.nanoseconds != 2100000 { return 1; }
    if elapsed.value.whole_milliseconds() != 2 { return 2; }
    std.println("elapsed=2 ms");
    return 0;
}
```

For live timing, obtain both instants using `monotonic_now()` and check each
reading's status before calculating elapsed time. Equal ticks are not a failed
measurement; do not assume a minimum nonzero interval.

## Deterministic clocks for embedding and tests

`JitRunner::run(module, arguments, clock)` accepts an optional
`const GloinClockSource *`. The descriptor is declared in the installed
[time_runtime.h](../src/time_runtime.h):

```cpp
struct GloinClockSource {
    GloinClockRead read;
    void *userdata;
};
// Callback type:
// int32_t (*)(void *userdata, uint64_t *nanoseconds, int32_t *os_error)
```

The descriptor is copied for the synchronous invocation; userdata is **borrowed**
and must stay alive until the invocation returns. A null descriptor explicitly
selects the OS clock, even when the calling thread already has an injected clock.
A nonnull descriptor with a null callback is an execution-setup error. Binding
allocates one small native context per invocation (including the default OS
binding); failure produces a setup diagnostic before executing main. The context
is freed and the previous thread-local provider restored on every returning path.
Other thread contexts are unaffected. Fatal process traps do not unwind scopes.

A callback runs synchronously exactly once per read and must not throw or recurse
through the same provider. It returns OK, IO_ERROR, INVALID, or OVERFLOW. On OK,
its u64 tick value is accepted (including zero/MAX), and os_error is cleared.
On IO_ERROR, a positive supplied errno is retained; zero becomes EIO and a negative
code becomes INVALID. All failures discard the supplied ticks. Other statuses
normalize to INVALID. Application code must still detect backwards clock values
through `time.elapsed`; the runtime does not keep hidden previous-tick state.

Native hosts/external runners can use `gloin_time_clock_push(&source)` and
`gloin_time_clock_pop(token)` on the **same runtime library** as the program.
Push copies the descriptor and allocates a small native context, returning null
on invalid input or allocation failure without changing the prior provider.
`push(nullptr)` temporarily selects the OS clock. Pop must occur on the same
thread in LIFO order; null/out-of-order/foreign tokens return INVALID without
changing the current provider. A token expires after successful pop. Userdata
remains caller-owned. Unconfigured external execution uses the OS clock.

Tests inject fixed/incrementing/failing/backwards clocks, assert exact durations,
and verify nested restoration and independent threads. They do not sleep or assert
elapsed wall-time thresholds. The full simulation's timing report is tested with
a provider returning exactly 5,000,000 ns between its two reads.

## SplitMix64-v1: explicit seed and value state

`random.SplitMix64.seeded(seed: u64)` stores the exact supplied state; it accepts
all seeds, including zero, and does not draw a number. The type contains one
private mutable u64. Copies copy that value and subsequently advance independently;
there are no heap owners, reference aliases, global generators, or automatic seeds.
`random.SPLITMIX64_VERSION` is 1. Changing the transition or sampling/float mapping
requires an explicit version/API change; these sequences are a compatibility contract.
This generator is for reproducible simulations, **not cryptographic randomness**.

| API and example | Behavior and cost |
| --- | --- |
| `random.SplitMix64.seeded(42)` | Construct value state; O(1), no allocation/failure |
| `rng.next_u64()` | One SplitMix64-v1 transition, returns full u64 including either endpoint; O(1), no allocation/failure |
| `rng.below(6)` | U64Result with OK/value in `[0,6)`; zero bound gives INVALID/0 without advancing; no allocation, expected fewer than two draws under the uniform-word model, no small fixed draw budget |
| `rng.unit_f64()` | Exactly one draw, returns one of 2^53 equally spaced f64 values in `[0,1)`; O(1), no allocation/failure |

The three drawing methods require mutable receivers. Explicitly shared mutable
state needs external synchronization; copying it gives independent reproducible
state, not an independently randomized seed.

The transition follows [Sebastiano Vigna's 2015 public-domain fixed-increment
SplitMix64 reference](https://prng.di.unimi.it/splitmix64.c). It advances the state
by 0x9e3779b97f4a7c15 modulo 2^64, then uses the reference xor-shift/multiply mixer
with constants 0xbf58476d1ce4e5b9 and 0x94d049bb133111eb. The single native primitive
takes an explicit writable state reference. General source arithmetic still traps
on overflow; no general wrapping or bitwise operators are enabled by this module.

### Reproducibility vectors

These are the first six `next_u64()` outputs, starting directly from each seed:

| Draw | Seed 0 | Seed 42 |
| ---: | ---: | ---: |
| 1 | 16294208416658607535 | 13679457532755275413 |
| 2 | 7960286522194355700 | 2949826092126892291 |
| 3 | 487617019471545679 | 5139283748462763858 |
| 4 | 17909611376780542444 | 6349198060258255764 |
| 5 | 1961750202426094747 | 701532786141963250 |
| 6 | 6038094601263162090 | 16015981125662989062 |

Tests additionally cover seeds 1 and u64 MAX, counter wrap, interleaved generators,
and copied states. Seed 7046029254386353131 produces zero on its first draw;
seed 3558559446808474027 produces u64 MAX on its first draw.

```gloin
import "@random";
import "@std";
import "@arena";
import "@status";

def main() -> i32 {
    def mut rng: random.SplitMix64 = random.SplitMix64.seeded(42);
    def mut copy: random.SplitMix64 = rng;
    def first: u64 = rng.next_u64();
    if first != 13679457532755275413 || copy.next_u64() != first { return 1; }
    def mut memory: arena.GeneralArena = arena.GeneralArena.create();
    defer memory.free();
    def text: std.FormatResult = std.format_u64(&memory, first);
    if text.status != status.OK { return 2; }
    std.println(text.value);
    return 0;
}
```

The arena in this example belongs to text formatting, not the random generator.

### Bounded integers and unit floats

`random.U64Result { status: i32, value: u64 }` is used by `below`.
For a nonzero bound b, the source computes `threshold = (2^64 - b) % b` without
wrapping: `(u64_MAX - b + 1) % b`. It rejects words below that threshold and returns
`word % b` for the first accepted word. The accepted domain has a size divisible
by b, so every residue has the same number of representatives: no modulo bias is
introduced. This reasoning uses a uniform-word model, not a claim of independent
physical entropy from a deterministic generator. Bound 1 consumes one word and
returns zero. Bound u64 MAX rejects word zero; it never returns u64 MAX.

A deliberate rejection vector: seed 3 with `below(9223372036854775809)` rejects
2092789425003139053, accepts 12918135221727111561, and returns
3694763184872335752. The following `next_u64()` is 11307387092600937729.
Invalid bound zero leaves the generator completely unchanged.

`unit_f64` discards the low 11 bits using unsigned division by 2048, exactly converts
the remaining integer (at most 2^53-1) to f64, and divides by 2^53. Every resulting
binary fraction is representable exactly; zero is possible and one is excluded.
The maximum is `0.9999999999999999` (1-2^-53). There is no hidden rejection or
variable consumption. Do not scale-and-round this value to obtain uniform bounded
integers; use `below` for that purpose.

## Seeded simulation and validation

```sh
build/gloinc --jit examples/simulation_lab.gloin -- 42 1000
```

Expected reproducible prefix:

```text
seed=42
samples=1000
inside=768
dice_sum=3525
pi=3.072000
abs_error=0.069593
```

The final `elapsed_ns=...` line varies with the clock. With no program arguments,
seed 42 and 10,000 samples are used (inside 7798, dice sum 35205). Explicit sample
counts must be between 1 and 1,000,000; every u64 seed is accepted. Arguments are
forwarded after `--`; input/count/clock failures use stderr and exit 2, report
output failures exit 3. There is no implicit entropy source.

Each sample draws two unit fractions for a quarter-circle Monte Carlo estimate,
then an unbiased die in [1,6]. The dice checksum and numeric input sequence are
deterministic. Floating derived estimates use ordinary arithmetic and documented
formatting; the example is not a bitwise guarantee for every host math library.
The independent fixture oracle uses integer squared 53-bit mantissas for circle
membership, rather than using the same floating calculation as the example.

The loop has expected O(samples) work under the sampling model, with O(1) storage
and no per-sample allocation. Each sample
uses two fixed draws plus the die's rejection loop. The CLI argument arena
is separate from the simulator's report-formatting arena; neither grows per sample. The two
clock reads surround only the simulation loop: parsing, compilation/JIT setup,
formatting, and stdout are excluded from the reported interval. This is a timing
example, not a statistically controlled benchmark; it performs no warm batches,
core pinning, or throughput claim.

Maintained tests cover clock conversion limits, callback error normalization,
scoped JIT/C-host bindings, external execution, deterministic PRNG vectors,
bounded rejection and endpoints, state copies, all 13 public functions/methods,
public signatures/private ABIs, installed/relocated examples, and both guide
programs verbatim. Real clock smoke tests only require successful nondecreasing
reads, never a positive minimum time or a sleep-based assertion.
