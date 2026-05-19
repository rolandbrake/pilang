# string Module

The `string` module provides replacement, splitting, and character-class checks.

```pilang
import string
```

## `string.replace(text, old, new)`

Returns a new string with every occurrence of `old` replaced by `new`. `old` must
not be empty.

```pilang
println(string.replace("2026-05-18", "-", "/")) // 2026/05/18
```

## `string.is_upper(text)`

Returns `true` when every alphabetic character is uppercase. Non-alphabetic
characters do not make it false.

```pilang
println(string.is_upper("HELLO")) // true
println(string.is_upper("Hi"))    // false
println(string.is_upper("123"))   // true
```

## `string.is_lower(text)`

Returns `true` when every alphabetic character is lowercase.

```pilang
println(string.is_lower("hello")) // true
println(string.is_lower("Hi"))    // false
```

## `string.is_digit(text)`

Returns `true` when the string is not empty and every character is a digit.

```pilang
println(string.is_digit("12345")) // true
println(string.is_digit("12a"))   // false
```

## `string.is_numeric(text)`

Returns `true` when the string is a valid integer or decimal number, with an
optional leading sign.

```pilang
println(string.is_numeric("123"))    // true
println(string.is_numeric("-4.25"))  // true
println(string.is_numeric("12px"))   // false
```

## `string.is_alpha(text)`

Returns `true` when the string is not empty and all characters are alphabetic.

```pilang
println(string.is_alpha("Pilang")) // true
println(string.is_alpha("pi3"))    // false
```

## `string.is_alnum(text)`

Returns `true` when the string is not empty and all characters are alphabetic or
numeric.

```pilang
println(string.is_alnum("abc123")) // true
println(string.is_alnum("abc_123")) // false
```

## `string.split(text, separator)`

Splits a string into a list. If `separator` is empty, the string is split into
single-character strings.

```pilang
println(string.split("a,b,c", ",")) // ["a", "b", "c"]
println(string.split("abc", ""))    // ["a", "b", "c"]
```
