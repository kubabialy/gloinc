# Local modules (SPEC-029)

Import a file relative to the source that contains the import:

```gloin
// utils.gloin
def pub const OFFSET: i32 = 10;
def pub calculate(x: i32, y: i32) -> i32 {
    return x * y + OFFSET;
}
```

```gloin
// main.gloin
import "./utils";
def main() -> i32 {
    return utils.calculate(5, 3);
}
```

Running `gloinc --jit /path/to/main.gloin` from any directory returns exit status 25,
without printing. `"./utils.gloin"` is equivalent. Paths must start with
`./` or `../`; the filename supplies the namespace and must be a non-reserved
Gloin identifier. Directory names can contain spaces. Bare paths, absolute paths,
other extensions, aliases, wildcard imports, and package imports are unsupported.

Export functions, structs, and scalar/string constants with `def pub`.
Other declarations stay private to their file. Qualified names include
`utils.calculate(...)`, `utils.OFFSET`, and `model.Particle`. A public struct
can separately expose public fields, instance methods, and static methods.
Public methods may use their defining file's private helpers and fields.
Function values and addresses of constants remain unsupported.

Each file has its own imports. A caller cannot access a dependency's imports,
and dependencies cannot see declarations in their callers. Import directives
apply throughout their file; their position does not change visibility.
File declarations cannot reuse an import namespace, while local variables may
shadow one. Imported `main` functions are ordinary functions; only the root
source supplies the executable entry point.

The loader canonicalizes paths, including symlinks, and loads each source file
once per compilation. Several files can import the same dependency: they share
its nominal types and generate one copy of its functions. Distinct files with
the same basename retain distinct types and emitted symbols. Duplicate imports
of the same file or namespace within one file are errors. A symlinked module's
relative imports resolve from its canonical target directory.

All dependency cycles are rejected, including self-imports and cycles back to
the root. Diagnostics identify the import location and canonical path chain.
At most 128 files, including the root, may be active in an import chain.
Files are read again for every compilation; there is no persistent module cache.
The compiler API uses its supplied filename as the base for relative imports,
including when the root source exists only in memory.

Imports have no runtime initialization or cleanup. Runtime globals are rejected,
and unused imported code is still checked. Dependencies are checked before their
importers. Constants evaluate in lexical order within a file and can use exported
constants from dependencies; same-file forward constant references remain errors.

Standard imports use the same graph. `@std` maps to `std.gloin` in the selected
standard-library directory, and standard files can import other standard or local
files. A canonical file reached through a standard import receives that standard
identity regardless of traversal order. Two different standard names cannot
identify the same file. A local file named `arena.gloin` alone does not receive
native primitives or the typed allocation bridge.
See [standard module development](../stdlib/README.md).

## Runnable example and implementation

[module_lab.gloin](../examples/module_lab.gloin) and its
[supporting modules](../examples/modules) exercise a shared dependency graph:
public constants, shared particle types, methods, 10,000 linked arena allocations,
and three reset/reuse rounds. Run:

```sh
./build/gloinc --jit examples/module_lab.gloin
# module lab: ok
```

[module_loader.cpp](../src/module_loader.cpp) owns canonical-file loading and
cycle detection. Import edges share parsed `SourceModule` objects.
[sema_modules.cpp](../src/sema_modules.cpp) builds isolated file scopes and
resolves public declarations to ordinary symbol/type identities. Checked data
records dependencies in order and binds qualified constant uses to folded values.
Codegen declares and emits each module once, then the root; it performs no path
or export lookup. The same checked graph supports JIT and external LLVM execution.
