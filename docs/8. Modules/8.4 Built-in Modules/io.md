# io Module

The `io` module provides module-scoped input and formatting helpers.

```swift
import io
```

## `io.format(format, value, ...)`

Returns formatted text. Placeholders use zero-based indexes in braces, such as
`{0}` and `{1}`. Use `{{` and `}}` for literal braces.
The global `format(format, value, ...)` function uses the same rules.

```swift
let text = io.format("{0} scored {1}", "Ada", 42)
println(text) // Ada scored 42

let same = format("{0} scored {1}", "Ada", 42)
println(same) // Ada scored 42
```

Placeholders can include a format spec after `:`. Specs are space-separated
tokens, so number formatting, alignment, colors, and styles can be combined.

```swift
println(io.format("{0:.2f}", 3.14159))        // 3.14
println(io.format("{0:+.2f}", 3.14159))       // +3.14
println(io.format("{0:x}", 255))              // ff
println(io.format("{0:b}", 10))               // 1010
println(io.format("[{0:^10}]", "Pi"))         // [    Pi    ]
println(io.format("{0:fg:red bold}", "Error"))
```

Number specs:

| Spec | Meaning |
| --- | --- |
| `.2f` | fixed decimal precision |
| `.3` | general numeric precision |
| `+.2f` | fixed precision with a sign for positive numbers |
| `d` | decimal integer |
| `o` | octal integer |
| `x` | hexadecimal integer |
| `b` | binary integer |
| `.1%` | percentage, multiplying the value by 100 and adding `%` |

Alignment specs:

| Spec | Meaning |
| --- | --- |
| `<10` | left-align in a field of width 10 |
| `>10` | right-align in a field of width 10 |
| `^10` | center-align in a field of width 10 |

Style specs emit ANSI escape sequences:

| Spec | Meaning |
| --- | --- |
| `fg:red` | named foreground color |
| `bg:#202020` | RGB background color |
| `fg:2` | 256-color foreground index |
| `bg:0` | 256-color background index |
| `bold` | bold text |
| `italic` | italic text |
| `underline` | underlined text |

Named colors are `black`, `red`, `green`, `yellow`, `blue`, `magenta`,
`cyan`, `white`, and `default`.

```swift
let line = io.format("{0:<12} {1:>8 .2f} {2:fg:green bold}", "accuracy", 0.9375, "ok")
println(line)
```

Object methods named `format()` are separate display hooks. They customize how
an object is converted to text, while `format(...)` and `io.format(...)` apply
placeholder formatting to a format string.

## `io.readline()`

Reads one line from standard input and returns it without the trailing newline.

```swift
println("Type a line:")
let line = io.readline()
println(line)
```

## `io.key(timeout_ms = -1)`

Reads one key from the terminal without waiting for Enter. Returns a one-character
string, or `nil` when a non-negative timeout expires.

```swift
let key = io.key(50)
if (key == "q")
    println("quit")
```

## `io.clear()`

Clears the terminal screen and moves the cursor to the top-left corner.

```swift
io.clear()
```

## `io.pos(x, y, clear = false)`

Moves the terminal cursor to a 0-based column and row. When `clear` is truthy,
the terminal is cleared from that position to the end of the screen.

```swift
io.pos(0, 0)       // top-left corner
io.pos(4, 2)       // column 4, row 2
io.pos(0, 0, true) // top-left, then clear to end of screen
```

## `io.cursor(visible = true)`

Shows or hides the terminal cursor.

```swift
io.cursor(false)
// draw a terminal UI
io.cursor(true)
```

## `io.prompt(text, ...)`

Prints prompt text, reads a line from standard input, and returns the entered
text.

```swift
let name = io.prompt("Name: ")
println("Hello, " + name)
```
