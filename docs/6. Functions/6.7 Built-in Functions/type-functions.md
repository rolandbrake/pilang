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

- `as_num(value)`: converts a compatible value to a number
- `as_string(value)`: converts a value to a string
- `as_bool(value)`: converts a value to a boolean using Pilang truthiness

```pilang
println(as_num("42"))      // 42
println(as_string(42))     // "42"
println(as_bool(""))       // false
println(as_bool([1, 2]))   // true
```

## Truthiness

`as_bool` follows the same truthiness rules used by `if`, `while`, `&&`, `||`,
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
