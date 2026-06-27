# io Module

The `io` module provides module-scoped input and formatting helpers.

```swift
import io
```

## `io.format(format, value, ...)`

Returns formatted text. Placeholders use zero-based indexes in braces, such as
`{0}` and `{1}`. Use `{{` and `}}` for literal braces.

```swift
let text = io.format("{0} scored {1}", "Ada", 42)
println(text) // Ada scored 42
```

## `io.readline()`

Reads one line from standard input and returns it without the trailing newline.

```swift
println("Type a line:")
let line = io.readline()
println(line)
```

## `io.prompt(text, ...)`

Prints prompt text, reads a line from standard input, and returns the entered
text.

```swift
let name = io.prompt("Name: ")
println("Hello, " + name)
```
