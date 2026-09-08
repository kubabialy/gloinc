# Gloin Language Spec

This document defines the intended language. It does not claim that the current
compiler implements it. [README.md](README.md) records measured implementation
status; [SPEC-TODO.md](SPEC-TODO.md) tracks implementation and verification.

## First release contract (SPEC-006)

The first release is the **executable scalar core**, with an in-process JIT on
**Apple Silicon macOS**, using the supported LLVM/MLIR 21.1.6 toolchain. Native
object/executable output, cross-compilation, Linux, Windows, and Intel macOS are
outside this release. This is a scope decision, not a release announcement.

| Required for the first release | Implementation and acceptance |
| --- | --- |
| Valid UTF-8 source, the declaration/type/terminator rules below, source-aware errors, rejection of unsupported constructs | SPEC-007 through SPEC-010 |
| Local immutable/mutable variables, compile-time scalar constants, lexical scopes, definite initialization | SPEC-011/SPEC-012 |
| `bool`; `i8`, `i16`, `i32`, `i64`; `u8`, `u16`, `u32`, `u64`; `f32`, `f64`; aliases `int` and `usize`; `void` return type | SPEC-010/SPEC-013 |
| Decimal, hexadecimal, binary integer literals and decimal floating literals; checked literal compatibility | SPEC-013 |
| Top-level functions with typed value parameters and explicit return types, direct/forward/recursive calls, `def main() -> i32` | SPEC-011/SPEC-014 |
| Assignment; unary `-` and `!`; binary `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `\|\|`; parentheses and function calls | SPEC-015 |
| Boolean `if`/`else`, `unless`, `while`, C-style `for`, nested blocks, and early returns | SPEC-016/SPEC-017 |
| Verified shared lowering, in-process JIT, file-reading CLI with checking/IR inspection modes and reliable failure status | SPEC-018 through SPEC-020 |
| Complete source-file execution and expected-error fixtures for every advertised core feature | SPEC-021 |
| Installation instructions, packaging, feature-to-test matrix, and release validation | SPEC-046 |

SPEC-021 is the executable-core milestone; SPEC-046 is still required before
publishing a release. Detailed numeric overflow/conversion rules, operator
semantics, scope rules, and CLI exit conventions must be settled under their
listed tasks before their implementation is accepted. The feature list above is
fixed for this release; those tasks must not silently expand it.

The first release has no imports, standard library, text output, strings,
aggregates, pointers/references, allocation, `defer`, or concurrency. Its minimal
complete program is:

```gloin
def main() -> i32 {
    return 42;
}
```

Its observable language result is the `i32` returned by `main`. Runtime/compiler
failure must remain distinguishable from every valid `i32`, including `-1`;
SPEC-020 defines how the CLI presents results within host exit-status limits.
The later hello-world milestone requires strings and `@std` (SPEC-022/SPEC-023).

## Core source and syntax rules

These rules are normative, including for declarations in later feature designs.
Examples in this section specify required behavior, not passing compiler tests.

### Source encoding, identifiers, and trivia

Source files must be valid UTF-8. The first release restricts identifiers to
ASCII `[A-Za-z_][A-Za-z0-9_]*`, except that `_` alone is reserved and cannot bind a
name. Names are case-sensitive, with no normalization or case folding. UTF-8
text is permitted in comments and, when strings are implemented, string data;
it does not make non-ASCII identifiers legal. Invalid UTF-8, an embedded NUL
source byte, and non-ASCII identifier characters must produce a diagnostic,
never truncate the input or depend on the host locale.

Spaces, tabs, LF, and CRLF are whitespace; LF and CRLF each advance the source
line once. Newlines do not terminate statements. `//` comments run to the end
of the line or file. Block comments and a UTF-8 BOM are not accepted in the
first release. SPEC-007/SPEC-008 own encoding validation and source positions.
A lexer may expose newline tokens internally, but the parser treats them as
trivia, including inside expressions.

The core keywords are `def`, `mut`, `const`, `pub`, `priv`, `return`, `if`,
`else`, `unless`, `while`, `for`, `true`, `false`, and the core type spellings.
The later-feature words `import`, `extern`, `struct`, `enum`, `static`, `self`,
`defer`, `deferred`, `spawnable`, `run`, `packed`, `bit`, `at`, `null`, `string`,
`i128`, `u128`, `f128`, `char`, `in`, `break`, and `continue` are reserved. Legacy or
undecided words `fn`, `spawn`, `await`, `switch`, `match`, `case`, and `default`
are also reserved; using them as syntax or names must not succeed accidentally.
Endian-qualified and custom-width integer type spellings are reserved for the
later layout tasks. Unknown type names must never default to `i32`.

### Declarations, modifiers, and explicit types

Every independent declaration starts with `def`, including constants and
future structs, fields, enums, and methods. An import is a directive, not a
`def` declaration. Function parameters and struct-literal field labels are
parts of a declaration/expression and do not take their own `def`.

Canonical forms are:

```text
local binding:      def [mut] name: Type [= expression];
constant:           def [pub|priv] const name: Type = constant_expression;
function:           def [pub|priv] name(name: Type, ...) -> ReturnType { ... }
later struct:       def [pub|priv] struct Name { ... }
later field:        def [pub|priv] [mut] name: Type,
later method:       def [pub|priv] [static|deferred|spawnable] name(...) -> Type { ... }
```

Square brackets in these forms mean optional syntax; `...` is explanatory,
not a language token. Visibility, when present, comes **immediately after
`def`**, before other modifiers. At most one of `pub`/`priv` is allowed;
private is the default. Visibility is allowed on top-level functions/constants
and later type/member declarations, not on local bindings or parameters.
Visibility is recorded but does not restrict calls within the single source
file of a core program. Exports/member-access checks arrive with
SPEC-024/SPEC-026/SPEC-029.
`mut` and `const` cannot be combined. Other modifier combinations must be
explicitly specified by their feature tasks before they are accepted.

Every binding, constant, field, and parameter needs a type annotation. Every
function, including a function returning `void`, needs `-> ReturnType`.
There is no variable, parameter, return-type, or generic-argument inference.
A literal may take its type from an explicit declaration, parameter, return,
or typed operand context; this does not infer a missing declaration annotation.
SPEC-013 must define compatibility and any default type for context-free
literal expressions. A typed expression cannot silently change its type to
match another annotation.

Function parameters are immutable value bindings in the core. Reassigning a
parameter requires an explicitly declared mutable local copy. Later instance
methods must spell their receiver type, for example `self: *Person`; bare
`self` is not an exception to explicit typing. No implicit second receiver is
added when lowering an explicitly declared receiver.

`def x: i32 = compute();` is an immutable runtime binding. In contrast,
`def const LIMIT: i32 = 10;` requires a compile-time initializer. SPEC-012
must specify and check the permitted constant-expression subset; runtime calls
are not implicitly compile-time evaluable. Local bindings without an
initializer may only be read after definite initialization (SPEC-012).
The core allows functions and constants at file scope; runtime global
variables, nested functions, and user-defined type aliases are not in scope.

### Built-in type spellings

`int` is an exact alias of `i32`, independent of the host. `usize` is an exact
alias of the target's pointer-width unsigned integer (`u64` on the selected
arm64 target); it is not a separate nominal type. Aliases do not permit implicit
conversions to other widths. Built-in names and aliases cannot be redefined.
`void` is allowed only as a function return type, never as a value binding or
parameter. `bool` is distinct from every integer type; conditions require
`bool`, with no numeric truthiness.

The language spelling for the later pointer-plus-length text type is **`string`**.
`String` is not a built-in name or alias. A backend IR type named `String` is
an implementation detail; it cannot create a language-visible type. A later
user-defined `String` would be an ordinary distinct type. String layout and
ownership remain SPEC-022; this naming decision does not add strings to the
first release. `i128`, `u128`, `f128`, `char`, custom-width integers, and
endian-qualified types are outside the initial scalar set and must be rejected
until their contracts and implementations are added.

### Terminators and delimiters

A declaration of a binding/constant, assignment, expression statement,
`return`, later `defer`, or import directive ends in `;`. A newline cannot
replace it, even immediately before `}`. A function/type definition or a
braced control-flow statement has no trailing `;`. Empty statements are not
part of the core. Parameters, call arguments, and later struct fields/literal
entries are separated by commas. A final comma is permitted in those lists;
struct field declarations use commas rather than statement semicolons.

Braces are mandatory for control-flow bodies; condition parentheses are
optional expression grouping. In `if ready { ... }`, the brace starts the
body, never a literal of a type named `ready`. SPEC-009 must preserve this
rule when aggregate expressions are implemented.

A C-style loop has the shape
`for def mut i: i32 = 0; i < 3; i = i + 1 { ... }`: two header semicolons
and no semicolon after its update. SPEC-017 defines omitted components and
loop-variable scope. Range-loop syntax remains deferred under SPEC-036.
Assignment is a statement (also allowed in a loop update), not a value-producing
expression: chained assignments and assignments inside conditions are rejected.

### Canonical core examples

A complete typed program with a forward call and a constant returns `42`:

```gloin
def const OFFSET: int = 2;

def main() -> i32 {
    def input: i32 = 40;
    return add(input, OFFSET);
}

def add(left: i32, right: i32) -> i32 {
    return left + right;
}
```

A complete mutation/control-flow program returns `3`:

```gloin
def main() -> i32 {
    def mut count: i32 = 0;
    def enabled: bool = true;
    if enabled {
        for def mut i: i32 = 0; i < 3; i = i + 1 {
            count = count + 1;
        }
    } else {
        return -1;
    }
    unless count == 3 {
        return -1;
    }
    while count > 3 {
        count = count - 1;
    }
    return count;
}
```

These independent fragments require diagnostics (SPEC-007 through SPEC-014):

| Invalid source | Required reason |
| --- | --- |
| `const LIMIT: i32 = 3;` | Missing `def`. |
| `pub def work() -> void {}` | Visibility must follow `def`. |
| `def const mut n: i32 = 0;` | Conflicting modifiers. |
| `def n = 1;` | Missing binding type. |
| `def work(x) -> i32 { return x; }` | Missing parameter type. |
| `def work() { return; }` | Missing explicit return type. |
| `def n: i32 = 1` | Missing semicolon, including at end of file. |
| `def café: i32 = 1;` | Non-ASCII identifier. |
| `def _: i32 = 1;` | Reserved name. |
| `def n: Mystery = 1;` | Unknown type; no `i32` fallback. |
| `def main() -> i32 { if 1 { return 0; } return 1; }` | Condition is not `bool`. |

## Deferred features and implementation-only syntax

No implementation-only syntax is grandfathered into the first release.
Tokenization, parsing, AST/IR printing, and existing unit-test expectations are
not language acceptance. The first release must diagnose unsupported features
before execution, using SPEC-007/SPEC-009/SPEC-010 and the SPEC-021 negative
fixtures. Feature tasks below remain open even if an initial rejection is added.
The full test suite continues reporting failures; scope decisions do not disable
tests or turn unimplemented features into expected successes.

| Syntax or capability outside the core | Disposition and owning tasks |
| --- | --- |
| Bare `const`, `pub def`, bare `struct`, untyped declarations/returns, implicit `self`, omitted semicolons | Reject; use canonical forms above (SPEC-008/SPEC-009/SPEC-012/SPEC-014/SPEC-026). |
| `fn`, `extern`, `switch`/`match`/`case`/`default`, `=>`, `?`, character literals | No accepted core extension. Reject under SPEC-008/SPEC-009/SPEC-015; any future syntax requires a separate contract before implementation. |
| Compound assignment, bitwise operators, shifts, unary `+`, assignment expressions | Reject for the first release (SPEC-015); integer remainder `%` is included, floating remainder is not. |
| `i128`, `u128`, `f128` | Deferred numeric extension (SPEC-013b); core type resolution rejects them (SPEC-010/SPEC-013). |
| Strings, `@std`, standard I/O and conversions | Deferred (SPEC-022/SPEC-023/SPEC-030); `string` is canonical. |
| Structs, raw pointers/references, methods, `defer`, arenas | Deferred (SPEC-024 through SPEC-028). |
| Local modules and `#package` imports | Deferred (SPEC-029/SPEC-044). |
| Generic types/functions, enums, `Result` | Deferred (SPEC-031 through SPEC-034); capitalization must not decide grammar. |
| `[i32; 3]`, `u8[1024]`, array literals, indexing/slicing | Deferred syntax/layout choice (SPEC-035); neither array spelling is approved for the core. |
| `for ... in ...`, `..`, `break`, `continue` | Deferred (SPEC-036); lex as reserved syntax and diagnose unsupported use. |
| Endian spellings, custom-width integers, packed layouts | Deferred (SPEC-037 through SPEC-039). |
| `deferred`, `spawnable`, `run`, `Deferred`, `Spawn`, joins | Deferred contract/runtime (SPEC-040 through SPEC-043). |
| Implementation-only `spawn` and `await` | Reject for the first release; SPEC-040 decides whether to retain any later compatibility syntax. |
| Native object/executable emission and additional targets | Explicitly deferred (SPEC-045). |

## Later language design

The remaining sections describe intended later features and conceptual examples,
not additional first-release requirements. Their task IDs are in the table
above. Core spelling rules still apply. Unsettled APIs, memory guarantees,
packed layouts, and concurrency types must be resolved in their feature tasks
before these examples become normative executable fixtures. In particular,
formatted/numeric printing and the named HTTP package are not established APIs.

All Gloin programs are written in UTF-8. Gloin is by design explicit, and does not have implicit typing or type inference.
The reason for this is that Gloin is designed to be a simple, safe, fast language but most importantly transparent.
It will not hide complexity from you, but will instead provide you with the tools to understand it.

The later hello-world program requires SPEC-022/SPEC-023; the core entry-point
example above has no imports:

```gloin
import "@std";

def main() -> i32 {
    std.println("Hello World!");
    return 0;
}
```

### Def keyword

Every declarable item in Gloin must be preceded by the `def` keyword. Whether it's a variable, function, struct, or type you can declare it with the `def` keyword.
It was chosen so that the intent is clear.

### Variables

Variables by default are immutable. You can make them mutable by adding the `mut` keyword before the variable name.
Each variable must be declared with a type.

```gloin
def main() -> i32 {
    def x: i32 = 5;
    x = 6; // Error: cannot assign to immutable variable
    
    def mut y: i32 = 5;
    y = 6;
    std.println(y); // Prints 6
    return 0;
}
```

### Constants

Constants are declared with `def const`. They are immutable and must be initialized at declaration.

```gloin
    def const PI: f64 = 3.14159;
    std.println(PI); // Prints 3.14159
```

### Endianness

Gloin allows explicit handling of endianness using `be_` (Big Endian) and `le_` (Little Endian) prefixes for integer types.
These can be used when defining types or performing conversions to ensure data portability.

```gloin
def network_packet: be_u32 = 0x12345678;
def local_data: le_u16 = 0x1234;
```

### Import System

Gloin supports three types of imports:

##### Standard Library `@std`

```gloin
import "@std";

def main() -> i32 {
    std.println("Hello World");           // Print with newline
    std.print("Enter name: ");           // Print without newline

    def input: string = std.input();     // Read user input
    def number: i32 = std.to_int("123"); // Convert string to int
    def text: string = std.to_string(42); // Convert int to string
    
    return 0;
}
```

##### Local Modules (./module)

```gloin
// utils.gloin
def pub calculate(x: i32, y: i32) -> i32 {
    return x * y + 10;
}

// main.gloin
import "@std";
import "./utils";

def main() -> i32 {
    def result: i32 = utils.calculate(5, 3);
    std.println(std.to_string(result));
    return 0;
}

```

##### External Packages (#package)

```gloin
import "@std";
import "#math";      // External package
import "#http";      // Another external package

def main() -> i32 {
    def sqrt_val: i32 = math.sqrt(16);
    std.println(std.to_string(sqrt_val));
    return 0;
}
```

### Pointers, References and Memory Management

Gloin supports pointers and references in the same way as C or C++ does, with some key differences for safety and clarity.

#### Pointers (`*T` vs `&T`)

- `*T`: A raw, nullable pointer. Equivalent to `T*` in C. It can be null and requires explicit checks or unsafe blocks to dereference (in future versions).
- `&T`: A non-nullable reference. It is guaranteed to point to a valid object. It cannot be null.

`self` in struct methods is always a pointer.

#### Memory Management

Gloin does not have garbage collection and expects you to manage memory manually. To assist with this, it provides:

1.  **Arena Allocation**: The preferred way to manage memory for request lifecycles or temporary objects.
2.  **`defer` statement**: Executed when the current function returns, in LIFO order (Last-In-First-Out).

```gloin
def main() -> i32 {
    // Arena allocation example (conceptual)
    def arena: Arena = Arena::new();
    defer arena.free(); // Frees everything allocated in this arena

    def x: *SomeX = arena.alloc(SomeX);
    
    return 0;
}
```

Example of pointer usage:

```gloin
import "@std";

def main() -> i32 {
    def mut value: i32 = 42;
    def ptr: &i32 = &value;  // Get address of value as non-nullable reference
    
    std.print("Value: ");
    std.println(std.to_string(value));
    
    std.print("Via pointer: ");
    std.println(std.to_string(*ptr));  // Dereference pointer
    
    *ptr = 100;  // Modify through pointer
    
    std.print("New value: ");
    std.println(std.to_string(value)); // Value was modified too and is now 100
    
    return 0;
}
```

### Strings

The `string` type in Gloin is a fat pointer consisting of a pointer to the character data and a length.

```gloin
// Conceptual representation, not a built-in type declaration.
def struct StringLayout {
    def ptr: *u8,
    def len: usize,
}
```

This means passing strings by value is cheap (two words), and slicing is efficient.

### Structs and Enums

Gloin supports structs and enums. Structs are similar to Go structs, except that they have methods declared in the same scope.

#### Method Lowering (Syntactic Sugar)

Methods defined inside a struct are purely syntactic sugar. They are lowered to global functions with the struct instance passed as a pointer in the first argument.

```gloin
def struct Foo {
    def x: int,
    
    // Instance method
    def bar(self: *Foo) -> int {
        return 1; 
    }
}
```

Is exactly equivalent to:

```gloin
def struct Foo {
    def x: int
}

// Lowered global function
// Naming convention: StructName_MethodName
def Foo_bar(self: *Foo) -> int {
    return 1;
}
```

When you call a method:
```gloin
def f: Foo = ...;
f.bar();
```

It is compiled as:
```gloin
Foo_bar(&f);
```

Accessing `self.x` inside the method is simply accessing the field of the pointer passed as the first argument.

To make a method public, you must place `pub` immediately after `def`. The `priv` keyword can be used to make a method private, but since it's the default visibility, it's not necessary to write it.

```gloin
import "@std";

def struct Person {
    def pub name: string,
    def pub age: i32,
    
    def pub greet(self: *Person) -> void {
        std.print("Hello, I'm ");
        std.println(self.name);
    }
    
    def pub is_adult(self: *Person) -> bool {
        return self.age >= 18;
    }
}

def main() -> i32 {
    def person: Person = Person {
        name: "Alice",
        age: 25
    };
    
    person.greet();
    
    if person.is_adult() {
        std.println("Person is an adult");
    }
    
    return 0;
}
```

### Functions

Functions are declared with the `def` keyword. Every parameter and return type is explicit; a `void` function returns no value.

```gloin
import "@std";

// Function with parameters and return value
def add(a: i32, b: i32) -> i32 {
    return a + b;
}

// Function with no return value
def greet(name: string) -> void {
    std.print("Hello, ");
    std.println(name);
}

def main() -> i32 {
    def sum: i32 = add(10, 20);
    std.println(std.to_string(sum));
    greet("Developer");
    return 0;
}
```


### Control Flow

Gloin supports control flow statements like `if`, `unless`, `while`, and `for` loops.

```gloin
import "@std";

def main() -> i32 {
    def x: i32 = 10;

    // If statement (no parentheses around condition)
    if x > 5 {
        std.println("x is greater than 5");
    }
    
    // Unless statement (opposite of if)
    unless x < 0 {
        std.println("x is not negative");
    }
    
    // While loop
    def mut counter: i32 = 0;
    while counter < 3 {
        std.println(std.to_string(counter));
        counter = counter + 1;
    }
    
    // For loop
    for def mut i: i32 = 0; i < 5; i = i + 1 {
        std.print("Iteration: ");
        std.println(std.to_string(i));
    }
    
    return 0;
}
```

### Concurrency
Gloin provides two distinct models for concurrent execution. 
Understanding when to use each is key to writing high-performance applications.

1. Asynchronous Operations (`deferred`)

Asynchronous programming in Gloin is designed for I/O-bound tasks (network requests, file system operations, database queries).
It allows the program to initiate a task and continue working without waiting for the hardware to respond.

Key Concepts
- `deferred` keyword: Marks a function as asynchronous.
- `Deferred<T>`: The wrapper type returned by a deferred function.
- `join()`: Blocks the current thread until the task is complete, returning a `Result<T, E>`.
- `force_join()`: Blocks and returns the value directly, but panics if the task failed.

Example: Async Network Fetch

```gloin
import "@std";
import "#http";
// Define the async function
def deferred fetch_config(url: string) -> Deferred<Result<string, AppError>> {
    println("Fetching from {}...", url);
    // Simulated async I/O call
    return http.Client::get(url);
}

def main() -> i32 {
    // Calling it returns a handle immediately (non-blocking)
    def handle: Deferred<Result<string, AppError>> = fetch_config("https://api.gloin.org/v1");

    std.println("Doing other work while waiting...");
    // Safe retrieval with Result handling
    def result: Result<string, AppError> = handle.join();
    
    if result.is_ok() {
        std.println("Config: {}", result.ok().value());
    } else {
        std.println("Error: {}", result.err().value());
    }

    // Unsafe retrieval (if you are certain it won't fail)
    // def config: string = handle.force_join().ok().value();

    return 0;
}
```

2. Multi-threading (`spawnable`)
Multi-threading in Gloin is designed for CPU-bound tasks (heavy math, image processing, data sorting). 
It utilizes system-level threads to perform work in parallel across multiple CPU cores.
Key Concepts
   - `spawnable` keyword: Marks a function as safe to run on its own thread.
   - `run` keyword: Used to execute a spawnable function on a new thread.
   - `Spawn<T>`: The handle returned by the run operation.
   - `join()`: Blocks until the thread finishes and returns a `Result<T, E>`.
 
Example: Parallel Computation

```gloin
import "@std";

def struct MathEngine {
    def factor: f32,
    def static new(f: f32) -> MathEngine {
        return MathEngine{ factor: f };
    }

    // A method marked as spawnable
    def spawnable compute_heavy_pi(self: *MathEngine, iterations: i32) -> f32 {
        def mut result: f32 = 0.0;
        for def i: i32 in 0..iterations {
            result = result + (self.factor * 3.14159);
        }
        
        return result;
    }
}

def main() -> i32 {
    def engine: MathEngine = MathEngine::new(1.5);
    defer engine.free();

    // Launch the work on a separate system thread using 'run'
    def thread_handle: Spawn<Result<f32, err>> = run engine.compute_heavy_pi(1000000);

    std.println("Main thread is free to handle UI or other tasks.");

    // Wait for the thread to finish
    def result: Result<f32, err> = thread_handle.join();

    if result.is_ok() {
        std.println("Computation Result: {}", result.ok().value());
    }

    return 0;
}
```

#### Summary Comparison

| Feature | deferred (Async) | spawnable (Threading) |
| :--- | :--- | :--- |
| **Primary Use** | I/O (Network/Disk) | CPU (Logic/Math) |
| **Execution** | Event loop / Non-blocking | OS-level threads / Parallel |
| **Keyword** | `def deferred name()...` | `def spawnable name()...` |
| **Trigger** | Standard call: `name()` | Explicit: `run name()` |
| **Return Handle** | `Deferred<T>` | `Spawn<T>` |
| **Result Handling** | `join()` or `force_join()` | `join()` |


### Packed bitfield design (unresolved: SPEC-037/SPEC-038)

The examples below preserve open layout proposals: omitted backing storage,
implicit offsets, and mixed endian spellings are not approved alternatives.
SPEC-037/SPEC-038 must replace them with one consistent contract and byte-exact
examples before implementation acceptance.

Networking protocols often pack multiple flags into a single byte. C bit-fields are notoriously non-portable and implementation-defined. 
Gloin can solve this by being explicit about bit-positioning.

Explicit Bit-Mapping:

```gloin
def packed struct Flags {
    def is_syn: bit at 0,
    def is_ack: bit at 1,
    def is_fin: bit at 2,
    def reserved: u5 at 3 // Bits 3 through 7
}
```

Explicit offsets are intended to lower to masks and shifts. Packing alone does
not establish safe access to a network buffer; alignment, bounds, and pointer
rules must also be defined and checked.

### Bit Indexing and Endianness

Bit indexing in `packed` structs is strictly tied to the endianness of the backing storage type.

- **Little Endian (`u32`, `le_u32`)**: Bit 0 is the Least Significant Bit (LSB).
  - Example: `def flags: u4 at 0` occupies the lowest 4 bits of the word.
  - Usage: Standard x86/ARM local processing.

- **Big Endian (`be_u32`)**: Bit 0 is the Most Significant Bit (MSB).
  - Example: `def flags: u4 at 0` occupies the highest 4 bits of the word.
  - Usage: Network protocols (TCP/IP), file formats.

This ensures that "Bit 0" always corresponds to the "first bit" as defined by the protocol or architecture being modeled, avoiding common portability pitfalls.

```gloin
// Network Protocol (Big Endian): Bit 0 is MSB
def packed struct(be_u32) NetworkHeader {
    def version: u4 at 0, // Top 4 bits (31-28)
    def ihl: u4 at 4,     // Next 4 bits (27-24)
}

// Hardware Register (Little Endian): Bit 0 is LSB
def packed struct(le_u32) DeviceReg {
    def enable: bit at 0, // Bottom bit (0)
    def mode: u2 at 1,    // Bits 1-2
}
```

#### `packed` keyword

The intended purpose of `packed` is to control padding and bit layout; matching
a protocol requires the complete layout rules and byte-level verification.
It is mandatory to specify the storage container (backing integer type) for the packed struct to define the "Word" size for bit-manipulation operations.

```gloin
// A 20-byte IPv4 Header definition backed by u32 words
def packed struct(u32) IPv4Header {
    def version: u4,           // 4 bits
    def ihl: u4,               // 4 bits
    def dscp: u6,              // 6 bits
    def ecn: u2,               // 2 bits
    def total_length: u16_be,  // 16 bits, Big Endian (Network Order)
    def identification: u16_be,
    // ... rest of the fields
}
```

The storage type (e.g., `u32`) dictates how the compiler generates shift and mask instructions.
