# Gloin Language Spec

This document is a work in progress. It contains the language specification, including syntax, semantics, and standard library details for the Gloin programming language.

## Syntax

All Gloin programs are written in UTF-8. Gloin is by design explicit, and does not have implicit typing or type inference.
The reason for this is that Gloin is designed to be a simple, safe, fast language but most importantly transparent.
It will not hide complexity from you, but will instead provide you with the tools to understand it.

The entry point is the `main` function.
This is the most basic Gloin program you can write.

```gloin
import "@std"

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

Constants are declared with the `const` keyword. They are immutable and must be initialized at declaration.

```gloin
    const PI: f64 = 3.14159;
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
import "@std"

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
def calculate(x: i32, y: i32) -> i32 {
    return x * y + 10;
}

// main.gloin
import "@std"
import "./utils"

def main() -> i32 {
    def result: i32 = utils.calculate(5, 3);
    std.println(std.to_string(result));
    return 0;
}

```

##### External Packages (#package)

```gloin
import "@std"
import "#math"      // External package
import "#http"      // Another external package

def main() -> i32 {
    def sqrt_val: i32 = math.sqrt(16);
    std.println(std.to_string(sqrt_val));
    return 0;
}
```

### Pointers, References and Memory Management

Gloin supports pointers and references in the same way as C or C++ does.
You can modify the value pointed to by a pointer or reference. You can also create a new pointer or reference to the same value.

Gloin does not have garbage collection and expects you to manage memory manually, to make the process less cumbersome, it provides you with a `defer` statement.
The `defer` statement will be executed when the current function returns, similarily to how `defer` works in Go or Zig. 
An important note is that the `defer` wokrs in LIFO order, so that the last defered function is executed first.

```gloin
def main() -> i32 {
    def x: *SomeX = get_some_x();
    defer x.free();
    
    def y: &SomeY = x.get_some_y();
    defer y.free();
    
    return 0;
}
// The defered functions will be executed in LIFO order
// y.free() -> x.free() -> exit(0)
```

Example of pointer usage:

```gloin
import "@std"

def main() -> i32 {
    def mut value: i32 = 42;
    def ptr: *i32 = &value;  // Get address of value
    
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

### Structs and Enums

Gloin supports structs and enums. Structs are similar to Go structs, except that they have methods declared in the same scope and their visibility is private by default.
To make a method public, you must add the `pub` keyword before the method definition. The `priv` keyword can be used to make a method private, but since it's the default visibility, it's not necessary to write it.

```gloin
import "@std"

def struct Person {
    def pub name: string,
    def pub age: i32,
    
    pub def greet(self) -> void {
        std.print("Hello, I'm ");
        std.println(self.name);
    }
    
    pub def is_adult(self) -> bool {
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

Functions are declared with the `def` keyword. They can have parameters and must return values.

```gloin
import "@std"

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
import "@std"

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
import "@std"
import "#http"
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
import "@std"

struct MathEngine {
    def factor: f32,
    def static new(f: f32) -> MathEngine {
        return MathEngine{ factor: f };
    }

    // A method marked as spawnable
    def spawnable compute_heavy_pi(self, iterations: i32) -> f32 {
        def mut result: f32 = 0.0;
        for i: i32 in 0..iterations {
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


### Zero-cost bit-field and `packed` keyword

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

By defining the exact bit-offset, the compiler can generate perfect AND/OR/SHIFT sequences. 
Because the struct is packed, there is zero padding, making it safe to cast directly from a network buffer.

#### `packed` keyword

The `packed` keyword tells the compiler not to add padding for alignment, ensuring the memory layout matches the TCP/IP spec exactly.
Sometimes you often need to map a struct directly onto a sequence of bits from a packet. 
In C, this is messy with bitmasks. In Gloin, we can use the def keyword with explicit bit-widths.

```gloin
// A 20-byte IPv4 Header definition
def packed struct IPv4Header {
    def version: u4,           // 4 bits
    def ihl: u4,               // 4 bits
    def dscp: u6,              // 6 bits
    def ecn: u2,               // 2 bits
    def total_length: u16_be,  // 16 bits, Big Endian (Network Order)
    def identification: u16_be,
    // ... rest of the fields
}
```
