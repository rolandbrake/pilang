# Type Functions

Type functions inspect and convert values at runtime.

## Inspection

- `type(value)`: returns the value's type name
- `is_num(value)`: true for numbers
- `is_str(value)`: true for strings
- `is_bool(value)`: true for booleans
- `is_list(value)`: true for lists
- `is_map(value)`: true for maps

```pilang
println(type(10))       // number
println(is_num(10))     // true
println(is_str("pi"))   // true
println(is_list([1]))   // true
```

## Conversion

- `num(value)`: converts a compatible value to a number
- `str(value)`: converts a value to a string
- `bool(value)`: converts a value to a boolean using Pilang truthiness
- `list(value)`: converts an iterable value to a list

```pilang
println(num("42"))    // 42
println(str(42))      // "42"
println(bool(""))     // false
println(bool([1, 2])) // true
println(list(1..4))   // [1, 2, 3]
```

## Truthiness

`bool` follows the same truthiness rules used by `if`, `while`, `&&`, `||`,
and `!`.

Falsey values include:

- `false`
- `nil`
- `0`
- `""`
- `[]`
- empty maps
- empty modules
- zero-size tensors
- ranges whose start and end are the same

All other values are truthy.

More detailed type utilities are exported by the `type` module.
