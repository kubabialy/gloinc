# Standard modules

The [standard-library guides](../docs/standard-library.md) describe the public
APIs, ownership, failures, and costs. The
[integrated examples](../docs/integrated-examples.md) compose these APIs and verify
resource use.

`import "@std";` loads [std.gloin](std.gloin). Its public `print` and `println`
functions are ordinary Gloin functions, compiled with the caller. The compiler
does not supply their names, signatures, or bodies.

`import "@net";` and `import "@http";` add nonblocking IPv4 TCP and bounded
HTTP/1.1 head operations in the 0.1.0 candidate. Their public APIs use built-in
`result<T>` for failures and ordinary payloads for expected readiness/EOF
states. See [networking](../docs/networking.md) and the
[local example](../examples/network_http.gloin).
`@net` also provides verified client TLS, reusable PEM server configurations,
accepted TLS sessions and readiness-driven bidirectional shutdown. See
[server TLS, ownership and cleanup](../docs/server-tls.md).
`@http_client` adds bounded streaming HTTP/HTTPS exchanges with overall
deadlines, backpressure, cancellation, early upload rejection, built-in results,
application headers, and chunked/fixed/EOF response decoding. Its synchronous
wrapper drives the same state machine; event loops use `Exchange` and
`net.wait_many`. See [the client guide](../docs/http-client.md). All protocol logic is
Gloin source and follows the normal GloinIR path.

To add another standard module, place its lowercase `.gloin` file here and declare public functions
with `def pub`. The loader maps `@name` directly to `name.gloin`; names use lowercase
ASCII letters, digits, and underscores, starting with a letter. Public structs are available as `name.Type`; their private fields stay within
the defining module. Public functions
are called as `name.function(...)`. Public scalar/string constants are available as `name.CONSTANT`; private helpers
and constants stay in the module's own scope. Files are read and checked on every
compilation. Standard files can import other standard or local files through the
shared dependency graph. See [module rules](../docs/modules.md).

Public structs can expose static constructors (`name.Type.create(...)`) and
instance methods (`value.method(...)`). Export methods with `def pub` or
`def pub static`; instance methods declare one typed `self` pointer/reference.
Public methods may use private fields/helpers inside the module. Callers still
need the receiver capability required by the signature, and cannot call private
methods directly. See [SPEC-026](https://github.com/kubabialy/gloinc/wiki/Language-Spec#methods-spec-026).

`import "@arena";` loads [arena.gloin](arena.gloin), exposing initialized-value
allocation through `arena.GeneralArena`. The same file can export additional
allocator types with their own storage policies. See [the arena guide](../docs/arenas.md)
for ownership, failure behavior, and the typed allocation bridge.

`import "@memory";` loads [memory.gloin](memory.gloin). It exposes fallible raw
aligned allocation, individual release, native type layout queries, and checked
typed placement for user-written allocators. See [raw memory](../docs/raw-memory.md).

`import "@vector";` provides inline and arena vectors. The growable form now
has `Vector<T>.empty(&memory, capacity)` so a dummy `T` value is unnecessary.
`import "@slices";` provides generic fill, reverse, and nonoverlapping copy on
borrowed slices. See [slices and vectors](../docs/slices-vectors.md).

Native primitives are deliberately limited: `__write_stdout(value: string)` is
available inside standard modules for exact byte output and flushing. `println`
adds its newline in Gloin by calling `print` twice. The arena module additionally
has private native allocation/reset/free primitives and a non-null guard.
The memory module has private raw allocation/release primitives and compiler
layout/placement intrinsics.
Application source cannot invoke these primitives directly; general FFI remains
deferred. `GeneralArena.alloc` and `try_alloc` declare explicit size/alignment
layout hooks in the library, while application calls supply one initialized
value. Sema validates the hooks and the compiler supplies the layout and typed
store. The separate `@memory` module supports generic type layout queries and
initialized placement in raw storage.

CMake copies `*.gloin` files beside the build executable under `stdlib/` and
installs them to `share/gloinc/stdlib/`. Rebuild after changing the source library,
or use the source directory directly during development:

```sh
./build/gloinc --stdlib-dir stdlib examples/hello_world.gloin
```

An explicit directory is authoritative. Missing/unreadable files, unknown/private
members, and syntax/type errors produce diagnostics; there is no built-in fallback.


SPEC-030 adds `input(&memory, max_bytes)`, `to_int(text)`, and
`to_string(&memory, i32_value)`. The shipped `std.gloin` imports `@arena` and
`@status`, so an explicit replacement standard-library directory must include
both dependencies.
Input returns `InputResult`; parsing returns `IntResult`. Both expose `status`
and `value`. Successful strings borrow caller-owned arena bytes until reset/free.
See [the standard-library guide](../docs/standard-library.md) for statuses,
strict decimal grammar, line limits, and cleanup.

Only canonical `@std` code can use the private input/parse/format/string-view
primitives. Byte allocation uses `GeneralArena.alloc_bytes`/`try_alloc_bytes`,
which zero all requested storage before returning it. The original typed value
allocation bridge is unchanged. The shared native runtime also exports standard
routines and the byte-output ABI for external LLVM execution.

SPEC-030a adds `import "@strings";` and dependency-free `import "@status";`.
`strings.gloin` documents every public function with an example, ownership,
failure behavior, and cost. The [string API guide](../docs/strings.md) covers
byte length/access, slicing, equality/ordering, search, ASCII trimming, and
explicit arena copies. All operations except `copy` allocate nothing; views
borrow their source bytes. `copy` returns a concrete status result.

`std.gloin` now also imports `@status`; replacement directories supplying the
shipped `std.gloin` must provide both `arena.gloin` and `status.gloin`.
The development tree adds `std.parse_i32_checked(value) -> result<i32>` as a
checked alternative to the existing status-returning parser. See
[results](../docs/results.md) for required handling and static error messages.
`strings.byte_at_checked` and `strings.slice_bytes_checked` likewise expose
byte bounds failures as `result<u8>` and `result<string>`; successful slices
still borrow their source bytes.
Existing std statuses keep their names/values as aliases. Only canonical
`@strings` can use its four private byte/descriptor/copy primitives. Public
algorithms and allocation policy remain ordinary Gloin source.

SPEC-030b adds concrete `SplitCursor`, `LineCursor`, and `StringBuilder` types
alongside bounded concat/repeat/replace and ASCII case conversion. See
[usage, ownership, and costs](../docs/text-construction.md) for every API.
Cursors borrow bytes without allocating. Builder copies alias shared fixed-capacity
storage; append/clear allocate nothing, and `to_string` copies explicitly into a
caller-supplied arena. Three additional module-private primitives handle checked
buffer views/stores/writes using the existing native copy ABI.

SPEC-030c adds 29 concrete numeric APIs in `std.gloin`: six parsers, eight
formatters, and fifteen explicit checked conversions. See the [numeric guide](../docs/numbers.md)
for every signature, usage example, range/rounding rule, failure, ownership, and
cost. Numeric formatters allocate in the caller's arena and return recoverable
results; boolean formatting returns static text. Parsing and conversion allocate
nothing. Existing input/to_int/to_string contracts remain unchanged. Only
canonical `@std` can invoke the new native primitives. Locale-independent float
parsing uses the pinned MIT-licensed [fast_float dependency](../third_party/fast_float/README.md);
its implementation is compiled into the runtime, and its license ships with packages.

SPEC-030d adds `import "@io";` through [io.gloin](io.gloin), depending on `@arena`,
`@strings`, and `@status`. File copies share arena-owned close state; borrowed
standard streams cannot be closed. All read sizes and open modes are explicit;
partial progress, OS errors, flush/close results, native buffering, and costs are
specified in [the I/O guide](../docs/io.md). Public code owns allocation and lifecycle
policy. Nine private native primitives perform actual I/O and bounded diagnostic
lookup; an additional guarded descriptor primitive constructs returned strings.
The native line reader is shared with unchanged legacy `std.input`.
Direct byte read/write primitives added after 0.0.4 work on caller-owned slices
without constructing strings; see [direct byte I/O](../docs/io.md#direct-byte-io).

SPEC-030e adds [fs.gloin](fs.gloin) and [process.gloin](process.gloin). Lexical
paths borrow bytes or copy into caller arenas; older filesystem mutations use explicit
names and preserve OS errors. New symlink/workspace helpers and checked file
replacement use built-in results with static errors. `fs.replace_file` supports
checked staged writes with ordinary Unix permission preservation; its concurrency
and metadata limits are documented in the filesystem guide.
Directory cursors copy each entry name into a
caller arena and require explicit close. Arguments are scoped per invocation;
environment values distinguish missing/empty, and cwd is explicitly bounded.
Both modules import `@strings`, `@arena`, and `@status`; private native routines and
module-scoped descriptor primitives support their public source implementations.
See [every API's usage, ownership, and costs](../docs/filesystem-process.md).

The unreleased 0.1.0 `@process` also supports explicit child launch, pipes,
bounded capture, deadlines, cwd/environment options and process-group cleanup.
`Options.stack_limit_bytes` sets only the child's soft stack limit; zero
inherits and nonzero requires the host's original main thread on macOS.
See the [child-process API and platform contract](../docs/child-processes.md).

## Bounded JSON (`@json`, 0.1.0 candidate)

[json.gloin](json.gloin) provides a pull reader over complete input, validation,
quoted-string encoding/decoding, and a structural writer over caller-owned
buffers. Public failures use built-in `result<T>`. Unicode scalars and surrogate
pairs are checked; number text remains exact until explicitly converted.
The implementation uses ordinary Gloin and GloinIR with no JSON runtime ABI.
See the [API, ownership and limits](../docs/json.md), including duplicate-key
behavior and the distinction between JSON events and incremental network input.

## Numerical utilities (`@math`, SPEC-030f)

[math.gloin](math.gloin) contains 47 concrete public functions: min/max/clamp for
five scalar types, signed/float absolute value, floating rounding, and the common
finite math operations listed in [the guide](../docs/math.md). It depends only on
`@status`, never on an arena. Constants are PI/TAU at f32/f64 widths. Source policy
and concrete results remain ordinary Gloin; four canonical-module-only typed
native calls provide checked host math, including preserved errno/floating state.
Every function documents an example, failure values, and cost. See
[math_lab.gloin](../examples/math_lab.gloin) for streaming geometry/statistics.

## Timing and seeded randomness (`@time`, `@random`, SPEC-030g)

[time.gloin](time.gloin) exposes monotonic `Instant` readings, unsigned nanosecond
`Duration` values, checked arithmetic/construction, and explicit unit conversion.
[random.gloin](random.gloin) exposes seeded `SplitMix64` values, full-width words,
unbiased bounded sampling, and the exact 53-bit `[0, 1)` floating grid. Both import
`@status` and `@std`; operations use no arena or hidden global generator. Each
module has one canonical-module-only native primitive. Clock reads allocate
nothing in the runtime; binding a host clock scope allocates one small record.

All 13 public functions/methods document examples, failure rules, and costs.
See the [guide](../docs/time-random.md) for clock ownership/injection, algorithm
version and vectors, sampling costs, and the [simulation](../examples/simulation_lab.gloin).
