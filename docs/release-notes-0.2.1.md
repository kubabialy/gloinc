# Gloin 0.2.1

Version 0.2.1 adds arena-backed hash maps to the standard library. It includes
the compiler primitives, runtime support, documentation, and packaged example
needed to use them on the maintained macOS and Ubuntu platforms. See the
[release guide](release-0.2.1.md) for installation and validation, the
[language guide](site/0.2.1/index.html) for examples, and the
[HashMap contract](hashmaps.md) for complete signatures and lifetime rules.

## Hash maps

Import `@hashmap` and use `hashmap.HashMap<K, V>`. Keys may be integers of any
supported width, booleans, or counted-byte strings. Values may be any storable
type, including structs, arrays, and nullable pointers; no dummy value is needed.

- `create`, `reserve`, and `insert` report allocation/capacity failures through
  built-in results. Failed growth preserves existing entries and capacity.
- `insert` distinguishes a new entry from replacement. `get` and `get_mut`
  return nullable borrowed value pointers; `contains`, `remove`, `clear`, and
  cursors provide membership, deletion, and iteration.
- Copies share the complete map state. Keys and values are shallow copies.
  String keys compare counted bytes, including embedded NUL, and borrow their
  bytes. The arena and any borrowed storage must outlive the map.
- Mutation invalidates value pointers and cursors. Removal repairs probe chains
  in place, so repeated removal and insertion within capacity need no cleanup
  allocations. Growth retains old arena buffers until reset/free.
- Insertion reuses its lookup probe; power-of-two bucket indexing uses a private
  compiler bit-mask operation. Integer hashing retains its mixing function.

The [packaged word-count example](../examples/hashmap.gloin) prints `apple=2`:

```sh
gloinc --jit examples/hashmap.gloin
```

Custom struct keys, floating-point keys, randomized collision protection,
concurrent access, and map literal/indexing syntax are not included. All map
allocation, probing, growth, deletion and iteration policy lives in Gloin source.

## Compatibility

Existing 0.2.0 source APIs are unchanged. Use the 0.2.1 compiler and its matching
standard modules together: a 0.2.0 compiler does not implement the new private
HashMap and typed-allocation primitives. Rebuild native objects and programs
when updating the compiler/runtime.

Programs coming from 0.1.0 still need the [0.2.0 result-API migration](release-notes-0.2.0.md#migrating-from-010).
The numbered guides for earlier versions retain their original feature boundary.

## Validation and platforms

Twelve HashMap regression cases cover every key width, strings, aggregate values,
aliases, collision chains and wraparound, allocation failure/rollback, and the
packaged example. A 12,000-operation workload checks against an independent
array model; distinct-key churn at maximum live occupancy is verified with
allocation failure armed. Tests exercise JIT, native O0/O2, external LLVM and
installed/relocated packages.

The large native-arithmetic matrix now runs alone on macOS as well as Linux
after hosted package validation exposed a child-compilation timeout under
contention. Its assertions and deadlines are unchanged.

Targets remain Apple Silicon macOS with LLVM/MLIR 21.1.6, and Ubuntu 24.04
ARM64/x86_64 with LLVM/MLIR 21.1.8. The compiler needs matching LLVM shared
libraries and OpenSSL 3. Packages do not bundle these dependencies. Release
assets include archives, checksums, and validation evidence; see the
[verification boundary](release-0.2.1.md#verification-boundary).
