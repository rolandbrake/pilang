# type Module

The `type` module provides type inspection and conversion helpers.

```pilang
import type:tp
```

## `type.is(value, type_name)`

Returns whether `value` has the given runtime type name.

```pilang
println(tp.is(10, "number"))    // true
println(tp.is("pi", "string"))  // true
```

## `type.of(value)`

Returns the runtime type name.

```pilang
println(tp.of([1, 2, 3])) // list
```

## `type.size(value)`

Returns a size-like value for the input. For strings, lists, maps, tensors, and
ranges, this reports their logical size. For primitives, it reports the runtime
value slot size.

```pilang
println(tp.size("abc"))     // 3
println(tp.size([1, 2, 3])) // 3
```

## `type.nil(value)`

Returns whether a value is `nil`.

```pilang
println(tp.nil(nil)) // true
```

## `type.int(value)`

Converts a number or numeric string to an integer number. Floats are truncated.

```pilang
println(tp.int(3.9))    // 3
println(tp.int("42"))   // 42
```

## `type.float(value)`

Converts a number or numeric string to a floating-point number.

```pilang
println(tp.float("3.14")) // 3.14
```

## `type.string(value)`

Converts a value to its string representation.

```pilang
println(tp.string(42)) // "42"
```

## `type.bool(value)`

Converts a value using Pilang truthiness.

```pilang
println(tp.bool(0))      // false
println(tp.bool([1, 2])) // true
```

## `type.list(value)`

Converts an iterable value to a list. Lists are shallow-copied.

```pilang
println(tp.list("abc")) // ["a", "b", "c"]
```

## `type.bytes(value)`

Converts a string to a list of byte values, or validates and returns a byte list
from integers between `0` and `255`.

```pilang
println(tp.bytes("ABC")) // [65, 66, 67]
println(tp.bytes([65, 66, 67]))
```
