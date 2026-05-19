# String Functions

String functions convert between characters and character codes and perform
common case and whitespace operations.

## Character Conversion

- `char(code)`: returns the character for a numeric code
- `ord(character)`: returns the numeric code for a character

```pilang
println(char(65)) // A
println(ord("A")) // 65
```

## Whitespace and Case

- `trim(string)`: removes leading and trailing whitespace
- `upper(string)`: converts a string to uppercase
- `lower(string)`: converts a string to lowercase

```pilang
let text = "  Pilang  "

println(trim(text))      // Pilang
println(upper("pi"))     // PI
println(lower("PI"))     // pi
```

## Related Operations

Strings also support indexing, slicing, concatenation, repetition, and `in`.

```pilang
let name = "Pilang"

println(name[0])      // P
println(name[1..4])   // ila
println(name * 2)     // PilangPilang
println("lang" in name)
```

More string helpers, such as `string.replace`, `string.split`, and character
classification functions, are exported by the `string` module.
