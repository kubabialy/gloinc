# Byte strings (SPEC-030a, 0.2.0)

Import `@strings` for operations. Fallible functions return built-in `result<T>`;
import `@status` when you need to distinguish error codes.
The implementation is [strings.gloin](../stdlib/strings.gloin); every public
function has an inline usage example, cost, and ownership/failure description.
These are ordinary source functions, not built-in string methods.
For the subsequent cursor, bounded transformation, and StringBuilder APIs, see
[text traversal and construction (SPEC-030b)](text-construction.md).

```gloin
import "@strings";
import "@status";
import "@std";

def main() -> i32 {
    def text: string = strings.trim_ascii("  name = Gloin\r\n");
    def separator: strings.FindResult = strings.find(text, "=");
    if !separator.found { return 1; }
    def key: result<string> = strings.slice_bytes(text, 0, separator.offset);
    if key.erroneous { return 2; }
    if !strings.equal(strings.trim_ascii(key.value), "name") { return 3; }
    std.println("found name");
    return 0;
}
```

All the string operations in this example allocate **zero bytes**. The returned
strings are small descriptors over the original literal. The
[configuration example](../examples/strings_lab.gloin) also demonstrates explicit
copying between arenas and scratch reuse across 10,000 iterations:

```sh
./build/gloinc --jit examples/strings_lab.gloin
# count = 42
# strings lab: ok
```

## Results and byte semantics

Strings contain counted bytes. `"café"` has five bytes; `"a\0b"` has three.
UTF-8 continuation bytes, embedded NUL, and invalid UTF-8 are preserved. Indices
and search offsets count bytes; `slice_bytes("é", 1, 1)` succeeds and returns
one continuation byte. These APIs do not validate Unicode or change locale.
`view_bytes([const u8]) -> string` borrows a byte slice as counted text without
copying or validating UTF-8. Keep the slice live and unchanged while the view
is used; this is useful for parsing a buffer filled by `net.Socket.read`.

| Return type | Success/normal outcome | Failure |
| --- | --- | --- |
| `result<u8>` | Requested byte, including zero | `OUT_OF_RANGE`; no readable byte payload |
| `result<string>` | Borrowed or arena-owned string according to the operation; empty text can succeed | `OUT_OF_RANGE` for slicing, `NO_MEMORY` for copying; no readable string payload |
| `FindResult { found: bool, offset: u64 }` | First match when `found` is true; absence is `found = false, offset = 0` | Search is infallible; absence is an ordinary outcome |

Bind each result and check `.erroneous`, or use an exhaustive `match`, before
reading its active payload. The compiler rejects an unchecked `.value`, an
`.error` on a successful path, and a discarded result. Zero and empty text do
not indicate failure. `FindResult` remains an ordinary record: the compiler does
not enforce checking `found` before using `offset`. See [results](results.md).

Errors preserve the category in `.error.code`; all string-library errors have
`.error.os_error = 0` because no OS diagnostic is captured. Messages are static
literals and allocate nothing. `@status` constants are categories, not success
payloads. The old `ByteResult` and `StringResult` types have been removed.
`byte_at_checked` and `slice_bytes_checked` remain compatibility aliases of the
primary functions, with identical messages, codes, bounds and ownership rules.

## API reference and costs

All text parameters below are `string`. Let n be the first text's byte length,
and m the second's. Costs include worst-case byte work with constant overhead
implicit. Every function uses O(1) auxiliary storage; only `copy` allocates
payload storage. Costs do not include compilation or subsequent output.

| Function | Example and behavior | Time / allocation |
| --- | --- | --- |
| `byte_length(text) -> u64` | `byte_length("café")` → 5 | O(1), none |
| `view_bytes(bytes: [const u8]) -> string` | `view_bytes(buffer[0..count])` → borrowed counted text, including NUL | O(1), none |
| `is_empty(text) -> bool` | `is_empty("")` → true; `is_empty("\0")` → false | O(1), none |
| `equal(a, b) -> bool` | `equal("ab", "ab")` → true; compares all counted bytes | O(min(n,m)); O(1) if lengths differ; none |
| `compare(a, b) -> i32` | `compare("a", "ab")` → -1; exactly -1/0/1, unsigned lexicographic ordering | O(min(n,m)), none |
| `byte_at(text, index: u64) -> result<u8>` | `byte_at("abc", 1)` → 98; index must be less than size | O(1), none |
| `byte_at_checked(text, index: u64) -> result<u8>` | `byte_at_checked("abc", 1)` → 98; invalid index yields a static error | O(1), none |
| `slice_bytes(text, start: u64, length: u64) -> result<string>` | `slice_bytes("abc", 1, 2)` → borrowed `"bc"` | O(1), none |
| `slice_bytes_checked(text, start: u64, length: u64) -> result<string>` | `slice_bytes_checked("abc", 1, 2)` → borrowed `"bc"`; invalid bounds yield a static error | O(1), none |
| `starts_with(text, prefix) -> bool` | `starts_with("file.gloin", "file")` → true; empty prefix matches | O(m), none |
| `ends_with(text, suffix) -> bool` | `ends_with("file.gloin", ".gloin")` → true; empty suffix matches | O(m), none |
| `find(text, needle) -> FindResult` | `find("a=b=c", "=")` → true, 1; empty needle matches 0, even in empty text | O(1+n*m), none |
| `contains(text, needle) -> bool` | `contains("abc", "bc")` → true; same matching rules as find | O(1+n*m), none |
| `trim_start_ascii(text) -> string` | `trim_start_ascii(" \tx ")` → borrowed `"x "` | O(n), none |
| `trim_end_ascii(text) -> string` | `trim_end_ascii(" x\r\n")` → borrowed `" x"` | O(n), none |
| `trim_ascii(text) -> string` | `trim_ascii(" \tx\r\n")` → borrowed `"x"` | O(n), none |
| `copy(memory: &arena.GeneralArena, text) -> result<string>` | `copy(&memory, "abc")` → independent arena-backed `"abc"` | O(n) zeroing/copy plus arena allocation; n requested bytes |

Slices accept `start <= size` and `length <= size - start`. In particular,
`slice_bytes("abc", 3, 0)` is valid. Out-of-range requests return a result rather
than trap, even when their arguments would overflow if added together.

Comparison sorts the shorter string first when its bytes are an equal prefix.
Bytes 128–255 sort after ASCII. Neither comparison nor equality is a constant-time
secret comparison. Search scans candidate positions and stops at the first match;
repetitive inputs can take quadratic time. Prefix/suffix longer than the input
fail immediately. Search does not allocate an index or cache.

Trimming removes exactly ASCII tab (9), LF (10), vertical tab (11), form feed
(12), CR (13), and space (32). It preserves interior whitespace, NUL, nonbreaking
spaces, and all other bytes. Empty/all-whitespace input yields empty text.

## Borrowing and explicit retention

Slices and trims **borrow the original bytes**. Assigning a string copies only
its descriptor. Keep the underlying allocation live and unchanged until all
borrowed uses finish. There is no borrow checker or automatic lifetime extension.

```gloin
import "@arena";
import "@strings";
import "@status";

def main() -> i32 {
    def mut scratch: arena.GeneralArena = arena.GeneralArena.create();
    defer scratch.free();
    def mut retained: arena.GeneralArena = arena.GeneralArena.create();
    defer retained.free();
    def line: result<string> = strings.copy(&scratch, "  keep me  ");
    if line.erroneous { return 1; }
    def saved: result<string> = strings.copy(&retained, strings.trim_ascii(line.value));
    if saved.erroneous { return 2; }
    scratch.reset(); // line and its trimmed view are invalid; saved remains live.
    if !strings.equal(saved.value, "keep me") { return 3; }
    return 0;
}
```

Nonempty copy asks `GeneralArena.try_alloc_bytes` for exactly n bytes and copies
exactly n bytes. There is no extra terminator. The arena zeroes requested storage
before copying, and can allocate/retain a larger native block; see
[arena costs and ownership](arenas.md). A copied string has no individual free.
Its bytes remain until the destination arena is reset/freed. Allocation failure
returns an error with code `NO_MEMORY`; the source is unchanged and the failed
result has no readable string.

Empty copy succeeds with static empty text without touching the arena, including
a cleared handle. Nonempty copy requires a live arena; using a cleared handle
traps under existing arena rules. Copying into the same arena is allowed, but
reset/free invalidates both values. Use different arenas for different lifetimes.

## Migration from 0.1.0

| Function | 0.1.0 return | Development return |
| --- | --- | --- |
| `byte_at` | `ByteResult` | `result<u8>` |
| `slice_bytes`, `copy` | `StringResult` | `result<string>` |
| `byte_at_checked`, `slice_bytes_checked` | Built-in result with message-only errors | Same built-in result; errors now also carry `OUT_OF_RANGE` |

Names and arguments are unchanged. Replace a `status.OK` test with a direct
`.erroneous` check. Inspect `.error.code` only after proving failure. Failure no
longer supplies a fallback zero byte or empty string. A caller that deliberately
needs a sentinel must choose it explicitly in its error branch; the public API
never supplies one implicitly. Existing internal parser/lookahead helpers that
used that policy now check results and explicitly choose zero/empty text.

| Operation/category | Exact message | Code | OS code |
| --- | --- | --- | --- |
| Invalid byte index, including checked alias | `string byte index is out of range` | `OUT_OF_RANGE` | 0 |
| Invalid byte range, including checked alias | `string byte range is out of bounds` | `OUT_OF_RANGE` | 0 |
| Nonempty copy allocation failure | `cannot allocate string` | `NO_MEMORY` | 0 |

[Text traversal and construction](text-construction.md#migration-from-010)
records the cursor, transformation and builder changes. Numbered 0.1.0 HTML
continues to document its released API.

## Implementation and verification

Comparison, search, bounds-result policy, trimming, and allocation policy live in
ordinary Gloin source. Result construction, proofs and payload access use the
existing typed GloinIR result operations. The native runtime ABI is unchanged.
Four private primitives in canonical `@strings` implement
descriptor length, checked byte loads, checked slices, and copying. Private byte
and slice access still traps before out-of-bounds memory access. These primitives
are unavailable in application/local/other standard modules and cannot be
redeclared inside `@strings`.
SPEC-030b adds three private buffer-view/store/write primitives for construction;
the byte semantics and costs documented above are unchanged by the result migration.

Copy calls the native routine `gloin_strings_copy(source, length,
destination)`. It requires nonoverlapping live ranges of the given length and
accesses neither pointer for zero length. JIT signature validation and symbol
registration match the installed shared runtime. These string APIs do not add
string indexing syntax or string operators. The separate
[pointer-offset feature](pointer-offsets.md) does not add
string bounds checking.

`StringLibraryTest` covers public contracts, private primitive boundaries,
allocation failure, source lifetime independence, invalid source, an independent
search/order oracle, external LLVM execution, and the installed example.
`StandardRuntimeTest` checks copying arbitrary bytes with guard bytes and
exact-sized storage, including zero-length null pointers under sanitizers.
The runnable example exercises bounded scratch reuse; it is also included in
installed and relocated package checks.
