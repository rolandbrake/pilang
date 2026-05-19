# I/O Functions

Global I/O functions read from standard input and write to standard output.

## Output

- `print(value, ...)`: writes values without automatically adding a newline
- `println(value, ...)`: writes values followed by a newline
- `printf(format, ...)`: writes formatted output
- `log(value, ...)`: writes diagnostic-style output

```pilang
print("Hello")
print(" ")
println("Pilang")
```

Use `println` for normal line output and `print` when you want to control line
breaks yourself.

```pilang
let name = "Ada"
let score = 42

printf("%s scored %d\n", name, score)
```

## Input

- `input(prompt = "")`: reads a line from standard input

```pilang
let name = input("Name: ")
println("Hello, " + name)
```

For module-scoped I/O helpers such as `io.readline`, `io.prompt`, and
`io.format`, see the built-in modules chapter.
