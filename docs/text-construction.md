# Text traversal and construction (SPEC-030b, 0.2.0)

These APIs live in [strings.gloin](../stdlib/strings.gloin), alongside
[byte-string operations](strings.md). Every public definition includes a usage
example, ownership/failure rules, and costs. All text is counted bytes, including
NUL and invalid UTF-8. There is no implicit Unicode processing or allocation.

## Borrowed cursors

```gloin
import "@strings";
import "@status";
import "@std";

def main() -> i32 {
    def made: result<strings.SplitCursor> = strings.SplitCursor.create("a,,b,", ",");
    if made.erroneous { return 1; }
    def mut cursor: strings.SplitCursor = made.value;
    def mut item: strings.TextItem = cursor.next();
    while !item.end {
        std.print("["); std.print(item.value); std.println("]");
        item = cursor.next();
    }
    return 0;
}
```

This prints `[a]`, `[]`, `[b]`, and `[]` on separate lines. Empty items have
`end = false`; `end = true` means there are no more items. Both cursor types
allocate **zero bytes** and
use O(1) auxiliary space. Copying a cursor copies its position independently,
while keeping references to the same source bytes. Returned items and all cursor
copies require those bytes to remain live and unchanged; advancing does not
invalidate earlier items. There is no cursor free or automatic ownership.

| Function/method | Usage and result | Worst-case cost |
| --- | --- | --- |
| `SplitCursor.create(text: string, delimiter: string) -> result<SplitCursor>` | Borrows **both** text and delimiter. Empty delimiter fails with `INVALID`, with no cursor payload. | O(1), no allocation |
| `SplitCursor.next(self: &SplitCursor) -> TextItem` | Next borrowed token with `end = false`; repeated `end = true, value = ""` after exhaustion. | O(1+r*m) per call, O(1+n*m) complete traversal; no allocation |
| `LineCursor.create(text: string) -> LineCursor` | Borrows the input; cannot fail. | O(1), no allocation |
| `LineCursor.next(self: &LineCursor) -> TextItem` | Next borrowed line with `end = false`; repeated `end = true, value = ""` after exhaustion. | O(1+scanned bytes) per call, O(1+n) complete traversal; no allocation |

Here n is input length, r remaining length, and m delimiter length, all in bytes.
`TextItem` is an ordinary record with read-only `end: bool` and `value: string`.
The compiler does **not** require checking `end` before reading `value`: the
empty text at end is valid. Check it to distinguish an empty item from exhaustion.
Advancing a valid cursor cannot fail. A failed split constructor has no readable
cursor, and applications cannot construct or alter its private state. Forged or
dangling storage is outside the cursor contract. Bind a cursor as `def mut`
because next advances its position.

Splitting uses leftmost **nonoverlapping** delimiters, preserving leading,
adjacent, and trailing empty fields. Empty input yields one empty field.
Splitting `"aaaaa"` on `"aa"` yields `""`, `""`, `"a"`.

Line traversal removes LF and one CR immediately preceding it, matching
`std.input`. Standalone CR is data; a final unterminated line retains it.
Empty input has no lines, `"\n"` has one empty line, and terminal LF adds no
phantom line. A newline cursor is therefore different from splitting on LF.

## Bounded transformations

Every function below returns `result<string>`, takes an explicit
`memory: &arena.GeneralArena`, and requires `max_bytes: u64`. Text parameters
are `string`; repeat's count is u64. Example calls assume a live `memory` arena.

| Function and argument order | Example | Worst-case byte work |
| --- | --- | --- |
| `concat(memory, a, b, max_bytes)` | `strings.concat(&memory, "ab", "cd", 4)` → `"abcd"` | O(1+output size) |
| `repeat(memory, text, count, max_bytes)` | `strings.repeat(&memory, "ab", 3, 6)` → `"ababab"` | O(1+output size) |
| `replace_all(memory, text, needle, replacement, max_bytes)` | `strings.replace_all(&memory, "aaaaa", "aa", "b", 3)` → `"bba"` | O(1+n*m+output size), sizing and filling passes |
| `lower_ascii(memory, text, max_bytes)` | `strings.lower_ascii(&memory, "ABC É", 6)` → `"abc É"` | O(1+n) |
| `upper_ascii(memory, text, max_bytes)` | `strings.upper_ascii(&memory, "abc é", 6)` → `"ABC é"` | O(1+n) |

Each uses O(1) auxiliary space plus its output allocation. Arena allocation costs
are additional. Successful nonempty output requests exactly its byte length;
the arena zeroes that storage before it is filled, and may reserve a larger
backing block. No terminator is appended. Results are independent copies, even
when replacement finds no match or case conversion changes nothing. Source
bytes stay unchanged. Destination reset/free invalidates the result; using the
source arena as destination does not extend lifetime beyond that arena.

| Outcome/category | Behavior |
| --- | --- |
| Success | Complete result; empty output is static empty text and never touches the arena |
| INVALID | Replacement needle is empty; checked before limits/allocation |
| TOO_LONG | Final output exceeds max_bytes, including counts that cannot fit u64; no output allocation or writes |
| NO_MEMORY | Nonempty output cannot be allocated; source remains unchanged |

Failure has no readable string payload. Check `.erroneous` before `.error.code`
or `.value`. All errors use static messages and OS code zero. Size checks precede
allocation and
avoid overflowing addition/multiplication. A cleared arena traps only when a
nonempty result actually needs allocation, under existing arena rules.

Repeat with zero count or empty text is empty, without looping over a huge count.
Replacement scans original input for nonoverlapping matches; inserted bytes are
not searched again. Empty replacement deletes matches. Limits apply to **final
output**, so replacing every `"a"` in `"aaaa"` with `""` succeeds at limit zero.
ASCII conversion changes only A–Z/a–z, preserving NUL, all other bytes, and length.

## A fixed-capacity builder

```gloin
import "@strings";
import "@status";
import "@arena";
import "@std";

def main() -> i32 {
    def mut storage: arena.GeneralArena = arena.GeneralArena.create();
    defer storage.free();
    def mut retained: arena.GeneralArena = arena.GeneralArena.create();
    defer retained.free();
    def made: result<strings.StringBuilder> = strings.StringBuilder.create(&storage, 16);
    if made.erroneous { return 1; }
    def mut builder: strings.StringBuilder = made.value;
    def appended: result<void> = builder.append("hello");
    if appended.erroneous { return 2; }
    def saved: result<string> = builder.to_string(&retained);
    if saved.erroneous { return 3; }
    builder.clear();
    storage.free(); // Builder is now invalid; saved remains live in retained.
    std.println(saved.value);
    return 0;
}
```

Creation returns `result<StringBuilder>`. Prove success before using the handle.
Allocation failure exposes only an error with code `NO_MEMORY`; it no longer
supplies a dummy null handle. Successful handles become dangling after owning-arena
reset/free; manual lifetimes still apply, with no automatic dangling detection.

| Function/method | Usage and behavior | Time and allocation |
| --- | --- | --- |
| `StringBuilder.create(memory: &arena.GeneralArena, capacity: u64) -> result<StringBuilder>` | `create(&storage, 128)` creates an empty fixed-capacity builder | O(1+capacity) initialization plus arena allocation; one state object, then capacity bytes if nonzero |
| `append(self: &StringBuilder, text: string) -> result<void>` | `builder.append("abc")` copies complete text; success or `TOO_LONG` failure | O(1+text size), no allocation |
| `append_byte(self: &StringBuilder, byte: u8) -> result<void>` | `builder.append_byte(10)` appends LF; 0 appends NUL | O(1), no allocation; success or `TOO_LONG` failure |
| `clear(self: &StringBuilder) -> void` | `builder.clear()` resets shared length to zero | O(1), no allocation; keeps storage, does not securely erase old bytes |
| `byte_length(self: &const StringBuilder) -> u64` | `builder.byte_length()` reports used bytes | O(1), no allocation |
| `capacity(self: &const StringBuilder) -> u64` | `builder.capacity()` reports fixed capacity | O(1), no allocation |
| `to_string(self: &const StringBuilder, memory: &arena.GeneralArena) -> result<string>` | `builder.to_string(&retained)` makes an independent snapshot | O(1+length) initialization/copy plus arena allocation; exactly length bytes, none for empty |

Creation allocates the state first; if the subsequent buffer allocation fails,
that state storage stays in the arena until reset/free. Zero capacity still
allocates state and requires a live arena. It accepts empty append, rejects
nonempty append, and produces empty snapshots without allocation. There is no
automatic growth, hidden allocator, or individual builder free.

Appends check the whole requested size before writing. Insufficient capacity
returns TOO_LONG without changing bytes or length. Empty append succeeds even
when full. Input bytes need only remain live for the append call. Chaining
several successful appends is not a transaction: failure in a later call does
not undo earlier calls.

Builder copies **alias the complete mutable state**, including length. If
`alias = builder`, appending or clearing through either affects both. All aliases
expire together. Bind handles as mutable to call mutating methods; queries and
snapshotting accept read-only receivers. The builder is not thread-safe.

`to_string` is always an independent snapshot, not a borrowed view. It stays
unchanged after append/clear; retaining it beyond source-arena reset/free needs
a different destination arena. Snapshot allocation failure returns NO_MEMORY
with no readable snapshot and leaves the builder unchanged. Resetting the destination
invalidates its snapshots. Repeated snapshots retain arena space until reset;
append/clear themselves retain only the original fixed storage.

## Migration from 0.1.0

| API | 0.1.0 return | Development return |
| --- | --- | --- |
| `SplitCursor.create` | `SplitCursorResult` | `result<SplitCursor>` |
| Both cursor `next` methods | `StringResult`, `OK` or `END` | `TextItem { end: bool, value: string }` |
| `concat`, `repeat`, `replace_all`, `lower_ascii`, `upper_ascii` | `StringResult` | `result<string>` |
| `StringBuilder.create` | `BuilderResult` | `result<StringBuilder>` |
| `append`, `append_byte` | i32 status | `result<void>` |
| `StringBuilder.to_string` | `StringResult` | `result<string>` |

Arguments, byte algorithms, bounds, allocation counts, borrowing and alias rules
are unchanged. Cursor end is a normal outcome; empty text alone is not an end
marker. The old invalid split-cursor failure payload is gone. `LineCursor.create`,
`clear`, `byte_length`, and `capacity` retain their infallible signatures.
`SplitCursorResult`, `BuilderResult` and `StringResult` have been removed.

| Failure | Exact message | Code |
| --- | --- | --- |
| Empty split delimiter | `split delimiter must not be empty` | `INVALID` |
| Empty replacement needle | `replacement needle must not be empty` | `INVALID` |
| Transformation limit/size overflow | `string exceeds byte limit` | `TOO_LONG` |
| Copy/transformation/snapshot allocation | `cannot allocate string` | `NO_MEMORY` |
| Builder state or buffer allocation | `cannot allocate string builder` | `NO_MEMORY` |
| Append capacity exceeded | `string builder capacity exceeded` | `TOO_LONG` |

Every error has OS code zero. Match or check results before accessing their
active payload. Builder methods that return `result<void>` cannot be discarded.
Each append is atomic, but a sequence of appends can retain earlier successful
work when a later append fails. Error construction allocates no storage.

## Example and verification

Run the [escaped configuration report](../examples/text_lab.gloin):

```sh
./build/gloinc --jit examples/text_lab.gloin
```

It combines borrowed line/field cursors, trimming, bounded replacement and case
conversion, repeat/concat, builder aliases, scratch resets, and an independent
snapshot. Output is `NAME=Gloin &amp; friends`, `EMPTY=`, `COUNT=42`, and `---done`
on separate lines. Its escaping is deliberately limited to `&` for this example;
it is not a general HTML encoder.

`TextLibraryTest` verifies cursor/replace behavior against independent C++
oracles; all byte values for case conversion; limits, overflow, and allocation
failures; aliasing, snapshot ownership, atomic failed appends, and 10,000 reuse
iterations with an allocation counter. Tests inject both construction failures
and snapshot failure through the real typed-arena bridge. They check public
types/privacy/mutability, primitive guards, external LLVM execution, and both
guide programs verbatim. The suite runs in Release, sanitizer, installed, and
relocated package validation.

Algorithms and state live in Gloin source. Built-in results and ordinary
`TextItem` records pass through the existing typed GloinIR operations; no compiler
bypass or native ABI change is introduced. Three additional private primitives
provide buffer views, guarded byte stores, and guarded counted writes; writes
reuse the native copy ABI. These primitives do not expose mutable string views
or raw byte indexing. [Nullable pointer offsets](pointer-offsets.md) are a
separate feature and do not grant string bounds checking.
