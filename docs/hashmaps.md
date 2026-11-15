# Hash maps

`import "@hashmap";` provides `hashmap.HashMap<K, V>`, a growable map
backed by a caller-owned `arena.GeneralArena`. This API is available in the
0.2.1 compiler and standard library; 0.3.0 development improves backing-storage
reclamation as described below. It uses ordinary generic structs and
methods; there is no map literal or indexing syntax.

Keys may be `bool`, `string`, or any supported signed/unsigned integer width,
including the `int` and `usize` aliases. Floats, pointers, arrays, enums, and
custom structs are rejected as keys. Values may be any storable value type,
including structs, arrays, and nullable pointers. No default key or value is
needed. Built-in results are not storable struct fields and cannot be map values.

The [word-count example](../examples/hashmap.gloin) runs with:

```sh
build/gloinc --jit examples/hashmap.gloin
# apple=2
```

## API

All methods below belong to `HashMap<K, V>`. `memory` is a mutable reference to
an arena. `create`, `reserve`, and `insert` return built-in results that must
be handled before accessing `.value` or `.error`.

| Call | Return | Behavior |
| --- | --- | --- |
| `HashMap<K, V>.create(&memory, capacity)` | `result<HashMap<K, V>>` | Create an empty map with room for at least `capacity` entries; zero is valid. |
| `map.len()` | `i64` | Number of live entries. |
| `map.cap()` | `i64` | Live-entry capacity before growth; distinct from the internal bucket count. |
| `map.reserve(capacity)` | `result<void>` | Reserve total entry capacity; never shrinks. |
| `map.insert(key, value)` | `result<bool>` | `true` adds a key; `false` replaces the value of an existing key. |
| `map.get(key)` | `*const V` | Borrow a value; null means absent. |
| `map.get_mut(key)` | `*V` | Borrow a writable value; null means absent. |
| `map.contains(key)` | `bool` | Test membership. |
| `map.remove(key)` | `bool` | Remove a present key and return true, or return false when absent. |
| `map.clear()` | `void` | Remove all entries, retaining capacity. |
| `map.entries()` | `hashmap.Cursor<K, V>` | Create a cursor over the current entries. |
| `cursor.next()` | `*const hashmap.Entry<K, V>` | Borrow the next entry, or return null at exhaustion. |

`capacity` has type `i64`. Mutation requires a mutable map binding or a writable
receiver. Reads accept `&const HashMap<K, V>`. A cursor must be mutable to advance.
`Entry` exposes `.key` and `.value` through a const pointer; iteration cannot
change either field. Modify values through `get_mut` or `insert`. Keys can only
be changed by removing and inserting an entry.

An existing key's original stored key is retained when its value is replaced.
Replacement does not allocate or change the length. A stored null pointer is
distinct from an absent entry: `get` returns a non-null pointer to that null
value. Iteration order is unspecified and can change after growth or removal.

## Ownership and lifetime

The map allocates a shared state record in the arena, even for capacity zero.
Copying a map aliases that entire state: inserts, growth, removals, and clears
through one copy are visible through every copy, including their lengths and
capacities. Pass maps by reference to make mutation explicit. There is no deep
clone, implicit allocator, automatic destructor, or per-map `free`.

Keys and values are copied shallowly. String keys compare **counted bytes**,
including embedded NUL and invalid UTF-8, rather than descriptor addresses.
They borrow their original bytes. Keep those bytes alive and unchanged while
the key is in the map. To retain a temporary key, explicitly call
`strings.copy(&memory, key)` and handle its result before insertion. Stored
pointer, reference, slice, and string values retain their ordinary lifetimes.

Any call to `insert`, `remove`, `reserve`, or `clear` through any alias should
be treated as invalidating all value pointers and cursors. Finish using them
before mutating the map. Writing through the pointer from `get_mut` is allowed;
it does not change keys or structure. Do not structurally mutate the map while
iterating. These lifetimes and aliasing rules are caller obligations, not a
borrow checker guarantee. Concurrent access needs external synchronization.

Arena reset/free invalidates the map, all aliases, cursors, and borrowed entries.
The arena object itself must also outlive the map: its address is retained for
future allocations. Removing or clearing entries does not release pointed-to
resources or run destructors. In 0.3.0, growth releases the old table after
publishing its replacement. Arena reset/free releases remaining table blocks;
reset retains ordinary bump allocations for reuse, but not map tables. Removal
repairs probe chains in the existing buffers; repeated removal and insertion
within capacity do not allocate. Free the arena once through its designated
owner. The 0.2.1 implementation retained old and partially allocated table
storage in the arena instead.

## Failures

| Status | Cause |
| --- | --- |
| `status.INVALID` | Negative requested capacity. |
| `status.OVERFLOW` | Capacity rounding or `bucket_count * (sizeof(Slot<K, V>) + 1)` cannot fit the supported signed size range. |
| `status.NO_MEMORY` | Shared state or table-block allocation failed. |

Errors have static messages and zero `.os_error`. A failed `reserve` or
`insert` preserves existing entries, length, capacity, and probe state; it
never publishes a partially rebuilt table. A failed constructor has no map
payload. Failed creation/growth retains no attempted table storage. Constructor
validation and table allocation precede state allocation; failure to allocate
the state releases the unpublished table. Removing an absent key and looking up
an absent key are ordinary outcomes, not errors. The arena's own creation and
lifetime requirements still apply.

`cap()` describes the live-entry threshold. Inserting a new key allocates only
when length already equals that threshold. Replacement, removal, and `clear`
never allocate. `reserve` with a capacity at or below `cap()` is a no-op.

## Implementation and costs

The table uses power-of-two bucket counts, linear probing, and separate
occupancy bytes. Only occupied slots hold initialized keys and values; empty
slots are never read as entries. Live occupancy stays at or below 75%.
In 0.3.0, slots and occupancy bytes occupy one aligned, independently releasable
arena-owned block. Growth holds both old and new tables during rehash, then
releases the old block without disturbing other arena objects or maps. Each
table uses one native allocation, including an ownership header and alignment
slack. Small maps therefore have a native allocation cost that bump-only tables
could previously amortize; this change bounds retained storage, not a claim of
faster insertion. Clear retains the current table's capacity.
Removal uses backward-shift deletion: it scans to the next empty bucket and
moves entries whose probe paths cross the gap. An entry already in its home
bucket is skipped without ending the scan. This preserves lookup chains across
wraparound without tombstones, cleanup rebuilds, or extra table buffers.
Insertion reuses the first empty slot found by lookup unless growth changes
the table. A private bit-mask primitive maps hashes to power-of-two buckets.

With well-distributed keys, lookup, insertion, and removal take expected O(1)
probe work; insertion is amortized across growth. Lookups/removals can take
O(buckets) under collisions. Removal can copy multiple entries, so its cost
also depends on key/value size. A rebuild takes expected
O(buckets), worst O(buckets²) if keys collide. Creating positive capacity and
clearing take O(buckets); creating zero capacity and obtaining a cursor take
O(1). A complete iteration scans O(buckets). String hashing reads every key
byte, and equality can read the whole string on a hash match. Copying a value
also costs its native value size.

Hashing is deterministic and noncryptographic, with no randomized collision
protection. Hash values and iteration order are not a persistence contract.
The compiler checks key types and exposes hash/equality/mask primitives only to
canonical `@hashmap`. Small native routines hash integer bits or string bytes
and compare counted strings; allocation policy, probing, growth, removal, and
iteration live in [hashmap.gloin](../stdlib/hashmap.gloin) and follow GloinIR.
The typed storage bridge checks layout and capacity in GloinIR without
initializing unused slots. It and the arena-owned block methods remain private
to the canonical collection module; application code cannot release individual
arena objects or obtain uninitialized typed storage through them.

### Allocation regression measurement (2026-11-03)

`HashmapLibraryTest.GrowingMapsReleaseOldTablesWithoutDisturbingTheirSharedArena`
grows an `i64`/`i64` map from zero to 200,000 entries, interleaves growth of a
second map in the same arena, and checks every value and an unrelated arena
object. A native allocator hook tracks the exact requested sizes of live backing
allocations, including arena state, block headers and alignment slack. Cleanup
must release every tracked allocation.

On macOS ARM64, using the same Release compiler and native arena runtime for
both library versions:

| Implementation | Peak live backing bytes |
| --- | ---: |
| Previous hashmap source (`97a1692`) | 30,867,922 |
| Reclaiming hashmap source (`d4268df`) | 19,739,344 |

That is a 36.05% reduction for this growth pattern. The regression permits at
most 20 MiB: enough for the current and replacement tables during growth, the
second map, and ordinary arena storage, but not all obsolete generations. It
fails with the previous source and passes with the replacement. This measures
requested backing bytes, not allocator RSS or insertion speed.

`RepeatedConstructorFailuresReleaseTheUnpublishedTable` rejects ordinary arena
block allocation during 32 consecutive constructors. The new implementation
peaks at 503 backing bytes, including the temporary table and arena state;
each failed constructor releases its unpublished table before the next attempt.
Both tests record `peak_backing_bytes` in GoogleTest XML:

```sh
build/gloinc_test \
  --gtest_filter='HashmapLibraryTest.GrowingMaps*:HashmapLibraryTest.RepeatedConstructor*' \
  --gtest_output=xml:build/hashmap-memory.xml
```

Validation for this change on 2026-11-03:

| Check | Result |
| --- | --- |
| macOS ARM64 `check-core` | 1,146/1,146 passed. |
| Focused hashmap, arena and raw-storage lowering tests | 53/53 passed on macOS ARM64 and Linux ARM64; source cases exercise JIT, native `-O0` and `-O2`. |
| Native arena ASan/UBSan | 14/14 passed on macOS ARM64. |
| Installed, then relocated macOS package | 36/36 arena/hashmap tests passed. |
| Tracked Gloin source formatting | All 294 files passed. |
