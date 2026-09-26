# Generic structs (development version)

The current development compiler supports explicit type arguments on generic
structs. This feature is not in the 0.0.3 release. Generic functions and
methods on generic structs are still rejected.

```gloin
def struct Box<T> {
    def pub mut value: T,
}

def main() -> i32 {
    def mut box: Box<i32> = Box<i32> { value: 40 };
    box.value = box.value + 2;
    return box.value;
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

Types are never inferred for generic arguments. The compiler currently
rejects `def identity<T>(...)` and methods declared inside generic structs.
Built-in `result<T>` and `error` have a separate
[partial design record](https://github.com/kubabialy/gloinc/wiki/Language-Spec#resultt-and-error-spec-034-proposed-not-implemented);
they are not implemented. Slices and vectors are planned after generic types
and functions. See [fixed arrays](fixed-arrays.md) for the development
`zeroed` initializer.
