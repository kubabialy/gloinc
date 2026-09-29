# Generics (0.0.4)

Gloin 0.0.4 supports explicit type arguments on generic
structs, instance and static methods (including methods with their own type
parameters), and generic functions. These features first ship in 0.0.4.

```gloin
def struct Box<T> {
    def pub mut value: T,

    def pub static create(value: T) -> Box<T> {
        return Box<T> { value: value };
    }

    def pub get(self: &const Box<T>) -> T {
        return self.value;
    }
}

def main() -> i32 {
    def box: Box<i32> = Box<i32>.create(42);
    return box.get();
}
```

`def struct Name<T, U> { ... }` declares type parameters. Each use supplies
all arguments explicitly: `Name<i32, bool>`. Arguments may be scalar types,
ordinary or generic structs, pointers/references, or fixed arrays. For example,
`Box<Box<i32>>` and `Box<[i32; 4]>` are valid types. Nested `>>` closes two
type applications. Generic struct literals repeat the concrete type, as in
`Box<i32> { value: 42 }`. A module may export a template with `def pub struct`;
other modules use its qualified name, such as `mod.Box<i32>`.

Generic structs may also declare compile-time size parameters after `;`:
`def struct Buffer<T; const N: usize> { def data: [T; N], }`. A use supplies a
decimal size or another bound size parameter, such as `Buffer<i32; 16>`.
The size is part of the concrete type and may be used as an `i64` constant
in that struct's methods. An array field `[T; N]` and a repeated initializer
`{fill; N}` use the same bound. The maximum size is 1,048,576. Different
numbers produce distinct layouts and nominal specializations. Templates with
the same name may differ by argument count, as with `vector.Vector<T>` and
`vector.Vector<T; N>`. Size parameters are currently supported on structs;
generic functions and methods can declare type parameters only.

Each distinct resolved argument list has one nominal struct identity and
layout. Aliases for the same scalar type, such as `int` and `i32`, select the
same specialization. A generic template without arguments, wrong argument
count, unknown argument, inaccessible template, and recursive by-value layout
receive compile errors. Recursive pointers to the same specialization are
allowed. Field types are checked when a specialization is used; duplicate
parameters and fields are checked on the template declaration.

Methods are checked and emitted separately for each concrete struct type. An
instance method spells its receiver, such as `self: &const Box<T>`; a static
method uses `def static` and is called as `Box<i32>.create(42)`. Method bodies
may refer to the enclosing type parameters, use private fields/helpers in
their defining module, and call themselves recursively. A method without its
own type parameters is checked when its enclosing struct specialization is
used. The compiler limits a program to 256 concrete generic struct
specializations, and a directly nested application to 64 levels.

A method may also declare its own type parameters, independently of its
struct's parameters. Both ordinary and generic structs can do this. Every
call supplies all method arguments explicitly:

```gloin
def struct Triple<A, B, C> {
    def pub first: A,
    def pub second: B,
    def pub third: C,
}

def struct Pair<A, B> {
    def pub first: A,
    def pub second: B,

    def pub with_third<C>(self: &const Pair<A, B>, third: C) -> Triple<A, B, C> {
        return Triple<A, B, C> {
            first: self.first,
            second: self.second,
            third: third
        };
    }
}
```

Call it as `pair.with_third<bool>(true)`. A static method on an ordinary
struct uses a form such as `Pairs.create<i32, string>(40, "x")`; an imported
type keeps its module prefix. The `<` touches the method name and `(` touches
the closing `>`.
The [runnable example](../examples/generic_methods.gloin) also shows a
generic static method on an ordinary struct.

Specializations are keyed by concrete receiver type and canonical method
arguments; aliases such as `int` and `i32` reuse one instance. Recursive
calls with the same arguments reuse it. A method with its own type parameters
is checked only when that concrete method is called, even if its enclosing
struct was specialized. An unused method template is not proven valid for
every argument. Wrong arity, unknown/private arguments, omitted arguments,
type arguments on an ordinary method, invalid receivers, and duplicate or
shadowing parameter names are compile errors. Private methods remain private
across modules; public specialized methods may call private helpers in their
defining module. A program may create at most 256 concrete method
specializations with own type arguments.

Generic functions declare type parameters after their name and require all
type arguments at every call. They can be public and called through an import.
There is no type inference or overload resolution.

```gloin
def identity<T>(value: T) -> T { return value; }

def main() -> i32 {
    return identity<i32>(42);
}
```

The call spelling is `name<T>(...)` or `module.name<T>(...)`. The `<` must
touch the function name and `(` must touch the closing `>`; spaces inside the
type argument list are allowed. This keeps `a < b > c` a comparison. Nested
arguments such as `identity<Box<i32>>(box)` are accepted. A call without type
arguments, an incorrect count, an inaccessible function, and unknown type
arguments are compile errors.

The compiler creates one checked function body per distinct canonical argument
list; `int` and `i32` select the same specialization. Recursive calls with
the same type arguments reuse it. Function bodies are checked when specialized,
so an unused generic function is not proven valid for every possible type.
At most 256 concrete generic function specializations may be created in one
program. A generic function can call private helpers in its defining module,
even when specialized from another module. Its argument and return types are
checked for each specialization.

Built-in `result<T>` and `error` have a separate
[partial design record](https://github.com/kubabialy/gloinc/wiki/Language-Spec#resultt-and-error-spec-034-proposed-not-implemented);
they are not implemented. Gloin 0.0.4 also has
[borrowed slices and both vector forms](slices-vectors.md). See
[fixed arrays](fixed-arrays.md) for the `zeroed` initializer.
