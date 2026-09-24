# A practical standard library before generics

This is the scope for SPEC-030a through SPEC-030h, ahead of SPEC-031. The
[byte-string API guide](strings.md) describes SPEC-030a and the
[text construction guide](text-construction.md) describes SPEC-030b. The
[numeric guide](numbers.md) covers SPEC-030c and the [I/O guide](io.md) covers
SPEC-030d. The [filesystem/process guide](filesystem-process.md) covers SPEC-030e.
The [math guide](math.md), [time/random guide](time-random.md), and
[integrated examples](integrated-examples.md) cover SPEC-030f through SPEC-030h.
All stages below are implemented and verified; the [language specification](https://github.com/kubabialy/gloinc/wiki/Language-Spec) defines their
contracts, with evidence recorded in the [implementation checklist](https://github.com/kubabialy/gloinc/wiki/Implementation-Checklist).
SPEC-030's [existing API](standard-library.md) remains compatible.

The objective is to write useful command-line tools, text processors, and numerical
programs with a small, composable vocabulary. Ruby is inspiration for the ease of
expressing an operation. Gloin's ownership, types, allocation, and failure paths
remain explicit.

## Design rules

- Public APIs live in ordinary lowercase `.gloin` files. Use methods on concrete
  library types such as `File`, `StringBuilder`, and `SplitCursor`; use module
  functions for built-in strings and numbers. No new method syntax is required.
- Document every public function/method at its definition and in the API guide:
  complete signature, a concrete usage example, success/failure behavior, ownership
  and invalidation, worst-case time, auxiliary storage, and allocation/I/O costs.
  Compile and run guide examples in maintained tests.
- Allocating text operations take a caller-supplied `&arena.GeneralArena`.
  Document whether each result borrows input, borrows arena bytes, or owns an
  external resource. Copying a descriptor never transfers or extends ownership.
  Other arena types can receive their own APIs later.
- Use concrete result structs with named status constants until generic
  `Result<T, E>` exists. Result payloads are valid only on documented statuses;
  empty text, zero, EOF, and failure remain distinguishable.
- `status.gloin` is a dependency-free home for shared statuses; retain
  existing `std.OK`, `std.END`, and other constants with their existing values.
  Result structs stay in their owning modules. This avoids requiring every
  module to import `@std` and creating cycles when utilities compose.
- New fallible allocation APIs return status results. Keep existing trapping
  APIs compatible; any additional trapping convenience must be explicitly
  documented. Specify maximum sizes and check size arithmetic before allocation.
- Specify failure effects: bytes consumed/written, retained arena storage, cursor
  advancement, and resource state. A failed operation is not automatically a
  transaction. Recoverable I/O, parse, and numeric-domain errors return results.
- Narrow private native primitives provide byte access, numeric conversions,
  operating-system services, and numerical routines where needed. Keep public
  policy/composition in Gloin. Validate the native ABI in JIT and external execution.

## Ordered implementation list

### SPEC-030a — Byte-string foundations (`strings.gloin`, `status.gloin`)

- `byte_length`, `is_empty`, `equal`, and lexicographic `compare`.
- Checked `byte_at` and `slice_bytes(start, length)`; slices borrow the input.
- `starts_with`, `ends_with`, `contains`, and `find` with an explicit found flag
  and byte offset. A match at offset zero must differ from no match.
- `trim_ascii`, `trim_start_ascii`, and `trim_end_ascii`, returning borrowed views;
  document the exact ASCII whitespace set.
- `copy(&memory, text)` for an explicit independent arena-backed copy.

Strings remain counted bytes: embedded NUL is ordinary data, indexing and lengths
count bytes, and slicing may cut a UTF-8 sequence. No locale or implicit Unicode
normalization. Specify empty needles, empty slices, out-of-range indices, and
comparison ordering. UTF-8 validation and character traversal can be a later task.

Acceptance: a configuration-line parser can trim, find `=`, slice, and compare
keys without allocation. Test boundaries, empty values, NUL, arbitrary bytes,
multibyte UTF-8, and copied strings surviving reset of their source arena.

### SPEC-030b — Traversal and text construction (`strings.gloin`)

Implemented signatures, costs, and examples are in [the API guide](text-construction.md).

- Concrete `SplitCursor` and `LineCursor`, each with `next()` returning a concrete
  item/end result. Tokens borrow the original string. No array or callback needed.
- Define trailing/adjacent empty tokens; reject an empty split delimiter. Define
  LF/CRLF handling and final unterminated lines consistently with input.
- Arena-backed `concat`, `repeat`, `replace_all`, `lower_ascii`, and `upper_ascii`.
  Bound output size; reject an empty replacement needle unless a precise useful
  behavior is agreed during specification.
- Concrete `StringBuilder.create(&memory, capacity)`, `append`, `append_byte`,
  `clear`, and `to_string(&destination_memory)`.

Start with a fixed-capacity builder. Append checks the whole addition before
writing; insufficient capacity returns a status without partial mutation.
`clear` reuses storage; `to_string` creates an independent copy. Do not initially
expose a borrowed view of mutable builder storage. These rules make repeated
construction predictable without hidden growth or view invalidation. Keep the
buffer and mutable metadata in shared arena-owned state: builder copies alias
the same builder, including its length, so append/clear through an alias is
visible through every alias. Copying is not cloning. Reset/free of the owning
arena invalidates every handle.

Acceptance: tokenize settings and construct escaped output in a bounded buffer;
test empty/trailing tokens, capacity exhaustion, size overflow, source lifetimes,
and copies remaining unchanged after builder reuse. A later growable builder
must specify its allocation policy separately.

### SPEC-030c — Parsing, formatting, and explicit numeric conversion (`std.gloin`)

Implemented; see [numeric API usage, ownership, failures, and costs](numbers.md).

- Type-specific `parse_i32`, `parse_i64`, `parse_u64`, `parse_f32`, `parse_f64`,
  and `parse_bool`; corresponding `format_*` functions.
- Preserve `to_int`, `to_string`, and their current contracts. New names make
  width and recoverable allocation failure explicit without overloading.
- Strict decimal integer parsing, strict whole-input floating grammar, and
  exact `true`/`false` boolean spelling. Trimming is a separate string operation.
- Locale-independent float formatting: shortest round-trip form first, then
  bounded fixed precision for reports. Define negative zero, exponent spelling,
  precision limits, overflow, and nonzero input underflow.
- Named checked conversions needed by real programs: i32 to/from i64, i64 to/from
  u64, integer to/from f64, and f32 to/from f64. Specify range, precision loss,
  and rounding for each; distinguish exact conversion from explicitly requested
  rounding/truncation. Extend this small matrix when a concrete use needs it.

These are library functions backed by narrow primitives, not implicit casts or
generic numeric dispatch. Numerical routines must preserve the language's finite
float contract. Reject textual NaN/infinity and report non-finite results.

Acceptance: wide integer and finite float round trips, min/max boundaries,
negative zero, malformed input, non-ASCII bytes, locale independence, precision
loss, and allocation failure. A statistics program can explicitly convert a
sample count into a floating divisor. Small-width parsers/formatters and
base/width/padding options are follow-ups, not prerequisites for generics.

### SPEC-030d — Streams and files (`io.gloin`)

Implemented; see [the I/O API guide](io.md) for signatures, ownership, progress, and costs.

- Borrowed stdin/stdout/stderr stream handles and owned `File` handles.
- Explicit open operations: read, create-new, truncate, and append. Destructive
  behavior must be visible at the call site.
- Bounded `read_line(&memory, max_bytes)`, `read_chunk(&memory, max_bytes)`, and
  `read_all(&memory, max_bytes)` returning counted byte strings.
- `write`, `write_all`, `write_line`, `flush`, and `close` with concrete results.
  Write results expose bytes written, including progress before a failure.
- Stable library error categories plus an OS error code where available;
  formatting a diagnostic is separate and takes an arena if it allocates.

Specify partial reads/writes, EOF after a final partial chunk, overlong-line
draining, flush/close failures, repeated close, and aliased handles. Standard
streams are borrowed and cannot be closed through the public borrowed handle.
Define whether and how owned streams buffer, and how close affects aliases.
Paths containing NUL fail validation; file contents preserve NUL. Keep `std.print`,
`println`, and `input` compatible while sharing implementation where appropriate.
Use explicit close when the caller must observe its result; deferred cleanup
cannot silently become evidence of successful output.

Acceptance: a bounded-memory file copier and line filter, separate stderr output,
empty/binary files, missing files, permission errors, partial/failing native I/O,
and flush/close failures. Reuse/reset a caller arena between chunks without
retaining invalid views. No user byte array is required for this first API.

### SPEC-030e — Practical command-line context (`fs.gloin`, `process.gloin`)

Implemented; see [filesystem/process API contracts](filesystem-process.md).

- Lexical path helpers: basename, dirname, and arena-backed join; define the
  supported host's separator, root, and absolute-path rules. Joining is not
  filesystem canonicalization or a security check.
- File metadata with concrete kind/size fields, `mkdir`, explicit file removal,
  and rename with a documented replacement policy. Queries preserve errors;
  permission failure must not masquerade as absence.
- `process.arg_count`, checked `arg(&memory, index)`, and
  `env(&memory, name)` distinguishing missing from empty; allocated results use
  the supplied arena. Define text/byte and embedded-NUL handling.
- Arena-backed current working directory query. Arguments require explicit
  CLI/JIT runtime plumbing: define forwarding after `--`, argument zero, and
  the embedding API's argument ownership. Do not expose compiler options as
  application arguments accidentally.

Acceptance: run the same file-processing program against a user-selected path
from another working directory, including paths with spaces, empty/missing
environment values, and malformed options. Test native, installed, and relocated
execution. Directory iteration, recursion, subprocesses, and a full option parser
remain follow-ups.

### SPEC-030f — Numerical utilities (`math.gloin`)

Implemented; see the [numerical utilities guide](math.md) for API contracts and
the [completion record](https://github.com/kubabialy/gloinc/wiki/Implementation-Checklist#spec-030f-verification) for test evidence.

- Concrete i32/i64/u64 and f32/f64 min/max/clamp functions; signed and floating
  absolute value. Define invalid clamp bounds and minimum signed-integer input.
- f32/f64 constants such as pi/tau; floor, ceil, truncation, and round with a
  specified tie rule. Rounding retains the floating type; integer conversion
  stays explicit through SPEC-030c.
- f32/f64 sqrt, pow, exp, log, log10, sin, cos, tan, atan2, and hypot.
  Angles use radians. Add inverse trig when an acceptance program requires it.
- Concrete checked results for domain/range failures; define each routine's
  boundary behavior, finite underflow, and signed zero. Document accuracy and
  portability expectations; do not promise bit-identical host math results.

Acceptance: geometric calculations and streaming statistics with reference
values/tolerances, numerical identities, domain edges, signed zero, and overflow.
No hidden RNG, implicit conversion, generic min/max, or wrapping arithmetic.

### SPEC-030g — Timing and reproducible simulation (`time.gloin`, `random.gloin`)

Implemented. See [API contracts and examples](time-random.md) and the
[completion record](https://github.com/kubabialy/gloinc/wiki/Implementation-Checklist#spec-030g-verification) for verification evidence.

- A fallible monotonic clock reading with explicit units and checked elapsed
  duration; use for benchmarks, never as a civil date/time representation.
- Concrete seeded PRNG state with `next_u64`, unbiased bounded integer sampling,
  and a precisely bounded f64 unit-interval result. Name and version its algorithm
  and publish deterministic test vectors. No global state or automatic seeding.

The PRNG's internal wrapping/bit operations need a narrowly specified primitive
because source arithmetic currently traps and bitwise operators are deferred.
It is for simulation, not cryptography. Copying PRNG state intentionally creates
the same subsequent sequence. Clock values depend on the execution environment;
the embedding/JIT API must support deterministic clock tests.

Acceptance: seeded simulations reproduce numeric inputs, integer ranges contain
only valid endpoints, and elapsed-time reporting uses an injected clock in tests.
Cryptographic randomness, wall-clock calendars, time zones, and timers are later
work. This stage follows the core text/I/O/math work in priority.

### SPEC-030h — Demonstrate the library before starting generics

Implemented. See [formats, usage, and resource contracts](integrated-examples.md)
and the [completion record](https://github.com/kubabialy/gloinc/wiki/Implementation-Checklist#spec-030h-verification). SPEC-031 is next.

Ship runnable, tested examples which compose the public modules:

1. A configuration reader: file input, borrowed key/value slices, numeric parsing,
   diagnostics on stderr, and explicit resource cleanup.
2. A streaming delimited-data statistics tool: CLI paths, selected numeric columns,
   count/min/max/mean, bounded memory, and formatted file output. Specify a simple
   unquoted format; do not label it a complete CSV parser.
3. A numerical simulation: explicit seed and parameters, math, timing, and data
   output, with an independent correctness oracle.

Each stage needs source-level success/error fixtures, native boundary/failure
tests where primitives are introduced, JIT and external execution, and package
installation/relocation coverage. Exercise allocation failure and size overflow;
use sanitizers for native code. Verify valid lifetime use and intentional copies,
without requiring the language to detect dangling references. Resource counts
and long-running bounded-memory tests must detect leaks or arena accumulation.
Keep existing deferred-feature failures visible and unchanged.

## Boundary before generics

These tasks use ordinary functions, concrete structs, methods, references, and
arenas. Cursors replace the immediate need for a returned collection; fixed-size
builders and chunk strings avoid requiring array syntax. This is not a promise
that no compiler/runtime integration is necessary.

Defer general lists/maps/sets, generic results and allocator interfaces, iterator
protocols, callbacks/blocks, interpolation, regex, Unicode case folding, networking,
async I/O, serialization frameworks, and broad filesystem/process coverage.
Generics can later reduce duplicated result/numeric APIs and enable containers;
they need not delay useful text, I/O, or numerical programs today.
