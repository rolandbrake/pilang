# I/O Functions

Global I/O functions read from standard input and write to standard output.

## Output

- `print(value, ...)`: writes values without automatically adding a newline
- `println(value, ...)`: writes values followed by a newline
- `printf(format, ...)`: writes formatted output
- `format(format, ...)`: returns formatted text
- `log(value, ...)`: writes diagnostic-style output

```swift
print("Hello")
print(" ")
println("Pilang")
```

Use `println` for normal line output and `print` when you want to control line
breaks yourself.

Pilang writes output as UTF-8. On Windows consoles, built-in output functions
write through a Unicode-aware console path, so box drawing, Greek text, emoji,
and other Unicode symbols can be printed directly when your source file is
saved as UTF-8.

```swift
println("box: ╔═╦╗ ║ ╚═╩╝")
println("greek: αβγ ΔΩ")
println("emoji: 😀 🚀 ✨")
```

```swift
let name = "Ada"
let score = 42

printf("{0} scored {1:d}\n", name, score)

let line = format("{0} scored {1:d}", name, score)
println(line)
```

`printf`, `format`, and `io.format` use the same placeholder specs, including
numeric precision, alignment, colors, and styles:

```swift
printf("{0:<12} {1:>8 .2f} {2:fg:green bold}\n", "accuracy", 0.9375, "ok")
let summary = format("{0:<12} {1:>8 .1%} {2:fg:yellow bold}", "coverage", 0.842, "watch")
println(summary)
```

## Input

- `input(prompt)`: prints a prompt string and reads a line from standard input

```swift
let name = input("Name: ")
println("Hello, " + name)
```

For module-scoped I/O helpers such as `io.readline`, `io.prompt`, and
`io.format`, see the built-in modules chapter. Object methods named `format()`
are display hooks and are separate from the global `format(...)` function.
