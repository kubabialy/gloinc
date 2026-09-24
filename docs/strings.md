# Byte strings (SPEC-030a)

Import `@strings` for operations and `@status` to inspect fallible results.
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
    def key: strings.StringResult = strings.slice_bytes(text, 0, separator.offset);
    if key.status != status.OK { return 2; }
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
./build/gloinc examples/strings_lab.gloin
# count = 42
# strings lab: ok
```

## Results and byte semantics

Strings contain counted bytes. `"café"` has five bytes; `"a\0b"` has three.
UTF-8 continuation bytes, embedded NUL, and invalid UTF-8 are preserved. Indices
and search offsets count bytes; `slice_bytes("é", 1, 1)` succeeds and returns
one continuation byte. These APIs do not validate Unicode or change locale.

| Concrete result | Success | Failure/absence |
| --- | --- | --- |
| `ByteResult { status: i32, value: u8 }` | `status.OK`; value is the requested byte | `status.OUT_OF_RANGE`; value is 0 |
| `StringResult { status: i32, value: string }` | `status.OK`; value follows the operation's ownership rule | OUT_OF_RANGE for slicing, NO_MEMORY for copying; value is static empty text |
| `FindResult { found: bool, offset: u64 }` | found is true; offset is the first match | found is false; offset is 0 |

Check the discriminator first. Zero is a valid byte and a valid match offset;
empty text can be a successful slice/copy. These payloads alone do not report
success. [status.gloin](../stdlib/status.gloin) defines allocation-free i32
constants: OK=0, END=1, INVALID=2, OVERFLOW=3, IO_ERROR=4, TOO_LONG=5, NO_MEMORY=6,
OUT_OF_RANGE=7. Existing `std` statuses remain compatible aliases of the first
seven. Status describes the individual operation; it does not set `main`'s result.

## API reference and costs

All text parameters below are `string`. Let n be the first text's byte length,
and m the second's. Costs include worst-case byte work with constant overhead
implicit. Every function uses O(1) auxiliary storage; only `copy` allocates
payload storage. Costs do not include compilation or subsequent output.

| Function | Example and behavior | Time / allocation |
| --- | --- | --- |
| `byte_length(text) -> u64` | `byte_length("café")` → 5 | O(1), none |
| `is_empty(text) -> bool` | `is_empty("")` → true; `is_empty("\0")` → false | O(1), none |
| `equal(a, b) -> bool` | `equal("ab", "ab")` → true; compares all counted bytes | O(min(n,m)); O(1) if lengths differ; none |
| `compare(a, b) -> i32` | `compare("a", "ab")` → -1; exactly -1/0/1, unsigned lexicographic ordering | O(min(n,m)), none |
| `byte_at(text, index: u64) -> ByteResult` | `byte_at("abc", 1)` → OK, 98; index must be less than size | O(1), none |
| `slice_bytes(text, start: u64, length: u64) -> StringResult` | `slice_bytes("abc", 1, 2)` → OK, borrowed `"bc"` | O(1), none |
| `starts_with(text, prefix) -> bool` | `starts_with("file.gloin", "file")` → true; empty prefix matches | O(m), none |
| `ends_with(text, suffix) -> bool` | `ends_with("file.gloin", ".gloin")` → true; empty suffix matches | O(m), none |
| `find(text, needle) -> FindResult` | `find("a=b=c", "=")` → true, 1; empty needle matches 0, even in empty text | O(1+n*m), none |
| `contains(text, needle) -> bool` | `contains("abc", "bc")` → true; same matching rules as find | O(1+n*m), none |
| `trim_start_ascii(text) -> string` | `trim_start_ascii(" \tx ")` → borrowed `"x "` | O(n), none |
| `trim_end_ascii(text) -> string` | `trim_end_ascii(" x\r\n")` → borrowed `" x"` | O(n), none |
| `trim_ascii(text) -> string` | `trim_ascii(" \tx\r\n")` → borrowed `"x"` | O(n), none |
| `copy(memory: &arena.GeneralArena, text) -> StringResult` | `copy(&memory, "abc")` → OK, independent arena-backed `"abc"` | O(n) zeroing/copy plus arena allocation; n requested bytes |

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
    def line: strings.StringResult = strings.copy(&scratch, "  keep me  ");
    if line.status != status.OK { return 1; }
    def saved: strings.StringResult = strings.copy(&retained, strings.trim_ascii(line.value));
    if saved.status != status.OK { return 2; }
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
returns NO_MEMORY and empty text; the source is unchanged.

Empty copy returns OK and static empty text without touching the arena, including
a cleared handle. Nonempty copy requires a live arena; using a cleared handle
traps under existing arena rules. Copying into the same arena is allowed, but
reset/free invalidates both values. Use different arenas for different lifetimes.

## Implementation and verification

Comparison, search, bounds-result policy, trimming, and allocation policy live in
ordinary Gloin source. Four private primitives in canonical `@strings` implement
descriptor length, checked byte loads, checked slices, and copying. Private byte
and slice access still traps before out-of-bounds memory access. These primitives
are unavailable in application/local/other standard modules and cannot be
redeclared inside `@strings`.
SPEC-030b adds three private buffer-view/store/write primitives for construction;
the SPEC-030a APIs and costs documented above are unchanged.

Copy alone calls the new native routine `gloin_strings_copy(source, length,
destination)`. It requires nonoverlapping live ranges of the given length and
accesses neither pointer for zero length. JIT signature validation and symbol
registration match the installed shared runtime. General pointer arithmetic,
string indexing syntax, generic APIs, and string operators remain unchanged.

`StringLibraryTest` covers public contracts, private primitive boundaries,
allocation failure, source lifetime independence, invalid source, an independent
search/order oracle, external LLVM execution, and the installed example.
`StandardRuntimeTest` checks copying arbitrary bytes with guard bytes and
exact-sized storage, including zero-length null pointers under sanitizers.
The runnable example exercises bounded scratch reuse; it is also included in
installed and relocated package checks.
