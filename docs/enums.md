# Enums (0.0.4)

Gloin 0.0.4 supports enums whose variants carry no payload. Run the
[example](../examples/enums.gloin) with
`./build/gloinc --jit examples/enums.gloin`; it exits 42.

```gloin
def enum Direction { North, East, South, West }

def turn_right(direction: Direction) -> Direction {
    if direction == Direction.North { return Direction.East; }
    return Direction.North;
}

def main() -> i32 {
    def facing: Direction = turn_right(Direction.North);
    if facing == Direction.East { return 42; }
    return 1;
}
```

Declare an enum at file scope with `def enum Name { Variant, ... }`. At least
one variant is required. Variants have unique names, are separated by commas,
and may have a trailing comma. Construct a value with `Name.Variant`; there is
no bare variant name or `Name { ... }` constructor. A module may export an enum
with `def pub enum`; another module refers to `module.Name` and constructs
`module.Name.Variant`. All variants of an exported enum are visible. A private
enum remains available only in its defining file.

Enums are nominal value types. A binding, parameter, return value, struct
field, or fixed-array element can have enum type. Assignment and passing copy
the value. Values of different enum types never convert to one another, even
when their variant names match. `==` and `!=` compare two values of the same
enum type. No integer conversion, ordering, arithmetic, or implicit boolean
conversion is available. The source cannot read or write the internal tag.
An uninitialized enum binding follows the normal whole-value initialization
rule. `zeroed` does not initialize enum arrays.

Gloin 0.0.4 represents a payloadless enum as one `u32` tag in
declaration order, starting at zero. The tag is private and its numeric values
are not part of the source-language API. No foreign-function ABI guarantee is
made for enums yet.

Payload variants, pattern matching, and exhaustiveness checking remain open
under SPEC-034. Until those rules exist, use ordinary `if` comparisons and an
explicit fallback path. Built-in `result<T>` and `error` also remain
unimplemented; see their [partial design record](https://github.com/kubabialy/gloinc/wiki/Language-Spec#resultt-and-error-spec-034-proposed-not-implemented).
