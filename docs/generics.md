# Generics (development version)

The current development compiler supports explicit type arguments on generic
structs, their instance and static methods, and generic functions. These
features are not in the 0.0.3 release. Methods with their own type parameters
are still rejected.

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
their defining module, and call themselves recursively. A method cannot
declare additional type parameters. A method body is checked when its enclosing
struct specialization is used; an unused generic template does not prove its
body valid for every possible type argument. The compiler currently limits a
program to 256 concrete generic struct specializations, and a directly nested
application to 64 levels.

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
checked for each specialization. Generic methods with additional method type
parameters remain unsupported.

Built-in `result<T>` and `error` have a separate
[partial design record](https://github.com/kubabialy/gloinc/wiki/Language-Spec#resultt-and-error-spec-034-proposed-not-implemented);
they are not implemented. Slices and vectors are planned after generic types
and functions. See [fixed arrays](fixed-arrays.md) for the development
`zeroed` initializer.
