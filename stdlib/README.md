# Standard modules

`import "@std";` loads [std.gloin](std.gloin). Its public `print` and `println`
functions are ordinary Gloin functions, compiled with the caller. The compiler
does not supply their names, signatures, or bodies.

For a future `@math` module, add `math.gloin` here and declare its public functions
with `def pub`. The loader maps `@name` directly to `name.gloin`; names use lowercase
ASCII letters, digits, and underscores, starting with a letter. Public structs are available as `name.Type`; their private fields stay within
the defining module. Public functions
are called as `name.function(...)`. Private helpers and file constants stay in
the module's own scope. Files are read and checked on every compilation.
Imports between library files and exported constant access remain deferred.

Public structs can expose static constructors (`name.Type.create(...)`) and
instance methods (`value.method(...)`). Export methods with `def pub` or
`def pub static`; instance methods declare one typed `self` pointer/reference.
Public methods may use private fields/helpers inside the module. Callers still
need the receiver capability required by the signature, and cannot call private
methods directly. See [SPEC-026](../SPEC.md#methods-spec-026).

`__write_stdout(value: string) -> void` is the only native primitive provided to
these modules. It writes the string's exact bytes and flushes stdout. `println`
adds its newline in Gloin by calling `print` a second time. Application source
cannot call the primitive directly; general FFI is not implemented.

CMake copies `*.gloin` files beside the build executable under `stdlib/` and
installs them to `share/gloinc/stdlib/`. Rebuild after changing the source library,
or use the source directory directly during development:

```sh
./build/gloinc --stdlib-dir stdlib examples/hello_world.gloin
```

An explicit directory is authoritative. Missing/unreadable files, unknown/private
members, and syntax/type errors produce diagnostics; there is no built-in fallback.
