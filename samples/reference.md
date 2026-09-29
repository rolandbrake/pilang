# Pilang: Language Reference

Use this as a compact reference for writing and reviewing Pilang (`.pi`) code.
It describes the syntax implemented in this repository; when it conflicts with
an older example, prefer the parser, tests, and documentation under `docs/`.

## Core rules

- Statements end at a newline or `;`. Braces define blocks; indentation is style
  only.
- Comments are `// line comment` and `/* block comment */`.
- Identifiers use letters, digits, and `_` (and cannot start with a digit).
- `nil`, `true`, `false`, `INF`, and `NAN` are literals.
- `let` creates a mutable binding. `const` cannot be rebound. `var` is also
  accepted; prefer `let` unless a project convention says otherwise.
- Top-level public names in a user module are exported automatically. A leading
  `_` makes a top-level name private to that module.

```swift
let score = 10
const title = "Pilang"
score += 5
println(title, score) // Pilang 15
```

## Values and literals

```swift
let decimal = 42
let floating = 3.14
let hexadecimal = 0xff
let text = "hello\nworld"
let enabled = true
let absent = nil

let list = [1, 2, 3]
let tuple = (1, "two", true)
let one_item_tuple = (1,)
let map = {name: "Ada", "role": "engineer"}
let empty_map = {}
let set = {1, 2, 3} // Non-empty braces without key:value entries make a set.
```

Numbers are floating-point language values. Bitwise operators operate on 32-bit
unsigned values; use `>>>` for an unsigned right shift. Strings, lists, tuples,
maps, sets, ranges, tensors, functions, classes, instances, and modules are
also first-class values.

## Collections, access, ranges, and slices

```swift
let items = [10, 20, 30, 40]
items[1] = 25
println(items[-1])   // 40
println(items[1:3])  // [25, 30]
println(items[::2])  // [10, 30]
println(items[::-1]) // [40, 30, 25, 10]

let name = "Pilang"
println(name[0])     // "P"; string indexing returns a one-character string
println(#name)       // 6

let user = {name: "Ada", active: true}
println(user.name)
println(user["name"])
user.score = 100

for i in 0..3 { println(i) }     // 0, 1, 2: range end is exclusive
for i in 10..0:-2 { println(i) } // 10, 8, 6, 4, 2
```

`#value` returns collection length (or the leading tensor dimension). `len`
also supports collections and returns tensor size. Use `in` for membership:
`"pi" in "pilang"`, `2 in [1, 2]`, `"name" in user`, and `4 in 0..10:2`.

## Assignment and spreading

```swift
[left, right] = [10, 20]
(name, score) = ("Ada", 95)

let base = [2, 3]
let merged = [1, ...base, 4]
let settings = {...{theme: "dark"}, debug: true}

if value <- read_value() {
    println(value)
}
```

Assignments may target bindings, properties, indexes, and supported slices.
`...` spreads iterables in lists and argument calls, and spreads entries in map
literals. `<-` assigns and returns its assigned value; do not chain it.

## Operators

```text
Arithmetic:       +  -  *  /  %  **  @  *.
Comparison:       ==  !=  <  <=  >  >=  is  in
Logical:          !  &&  ||
Bitwise:          ~  &  |  ^  <<  >>  >>>
Assignment:       =  +=  -=  *=  /=  %=  &=  |=  ^=  <<=  >>=  >>>=  **=  *.=
Other:            ? :   #   typeof   ..   ...   =>   <-
Postfix:          .member   [index]   [start:stop:step]   (...)   ++   --
```

`+` concatenates strings and tuples; adding to a list appends the right-hand
value. `*` repeats strings/lists/tuples with a numeric count. `@` is list dot
product and `*.` is elementwise multiplication. `&&` and `||` short-circuit.

```swift
let label = score >= 60 ? "pass" : "fail"
let rotated = (word << 7) | (word >>> 25)
let chained = a = b = 0
items[0]++
```

## Control flow

```swift
if score >= 90 {
    grade = "A"
} elif score >= 60 {
    grade = "pass"
} else {
    grade = "fail"
}

while running {
    if should_stop() break
    tick()
}

for index, value in items {
    println(index, value)
}

switch command {
    command == "start": start()
    command in ["stop", "quit"]: stop()
    _: error("unknown command")
}
```

`switch` requires at least one case. `break` and `continue` are valid only in
loops. A one-statement body may omit braces, but braces are clearer for agents
and are recommended for generated code.

## Functions, closures, and calls

```swift
fun add(a, b = 0) {
    return a + b
}

let square = x -> x * x
let multiply = (a, b) -> { return a * b }
let callback = fun (value) { return value + 1 }

println(add(2, b = 3))
println(add(...[2, 3]))
```

Functions are values and close over surrounding bindings. Parameters can have
defaults. Calls may use positional arguments, then named arguments; after the
first named argument, all remaining arguments must be named. `...values`
spreads a list/tuple into call arguments.

Useful globals: `print`, `println`, `printf`, `format`, `input`, `assert`,
`error`, `type`, `is_num`, `is_str`, `is_bool`, `is_list`, `is_map`, `num`,
`str`, `bool`, `list`, `tuple`, `set`, `range`, `copy`, `keys`, `values`,
`char`, `ord`, `trim`, `upper`, `lower`, `abs`, `min`, `max`, `pow`, `round`,
`rand`, `seed`, `sleep`, and `time`.

## Pipelines and comprehensions

```swift
let total = [1, 2, 3, 4] =>
    filter(n -> n % 2 == 0) =>
    map(n -> n * n) =>
    reduce((a, b) -> a + b, 0)

let squares = [x * x : x in 0..10 : x % 2 == 0]
let pairs = [[x, y] : x in 0..3, y in 0..3]
```

`=>` passes the value on its left as the first argument to the next stage.
List-comprehension filters use `:` after iterators, not Python's `if` syntax.

## Classes, objects, and inheritance

```swift
class Point {
    kind = "point"

    constructor(x, y) {
        this.x = x
        this.y = y
    }

    move(dx, dy) {
        this.x += dx
        this.y += dy
    }

    format() {
        return "Point(" + this.x + ", " + this.y + ")"
    }
}

class ColoredPoint : Point {
    constructor(x, y, color) {
        super(x, y)
        this.color = color
    }
}

let point = Point(2, 3)
println(point.x)
```

Use `this` for instance fields and methods, and `super(...)` to call a parent
constructor. Maps can also be used as lightweight objects with dot or bracket
access, but maps are not classes.

Instances can customize language behavior with methods named `format`, `call`,
`equals`, `compare`, `compute`, `rcompute`, `getItem`, `setItem`, and `slice`.
For example, `compute(op, other)` participates in operators; import `lang` for
operator constants such as `lang.OP_ADD` when implementing it.

## Modules and imports

```swift
import math
println(math.sqrt(25))

import math:mt.{sin, cos}
import tensor.{zeros, shape}
import tensor.ones:make_ones
import math.*
```

Forms:

```text
import module                    // module object, unless same-name default applies
import module:alias              // module object under alias
import module.{name, other:local}// selected exports
import module.*                  // all public exports
import path.to.module
import path.to.module.member:alias
```

User-module top-level names are exported automatically, except names beginning
with `_`. Modules are cached and run once per resolved path.

Class-per-file convention: if `PiExpression.pi` exports `class PiExpression`,
then `import PiExpression` binds the class directly. The same behavior applies
to a same-named function. To explicitly keep the module object, use an alias:
`import PiExpression:expression_module`.

## Functional and collection patterns

```swift
let squares = [x * x : x in 0..10]
let even_squares = [x * x : x in 0..10 : x % 2 == 0]
let doubled = map([1, 2, 3], x -> x * 2)
let evens = filter([1, 2, 3, 4], x -> x % 2 == 0)
let total = reduce([1, 2, 3], (a, b) -> a + b, 0)

let words = ["pi", "lang"]
words.push("fast")
println(words.join("-"))
```

Lists and strings support receiver-style methods. Common methods include
`push`/`append`, `pop`, `peek`, `insert`, `remove`, `contains`, `index`,
`count`, `copy`, `reverse`, `repeat`, and `join`. Strings also support `split`,
`trim`, `upper`, and `lower`. Sets support `add`, `clear`, `union`,
`intersection`, `difference`, and subset/superset checks.

## Tensors and built-in modules

```swift
import tensor:t

let matrix = t.from([[1, 2], [3, 4]])
let identity = t.eye(2, 2)
println(t.matmult(matrix, identity))
println(t.mean(matrix))
println(t.shape(matrix))
```

Built-in modules include `math`, `stats`, `random`, `time`, `io`, `fs`, `os`,
`sys`, `col`, `func`, `string`, `type`, `lang`, `obj`, `tensor`, `draw`,
`plot`, `plot3d`, `image`, and `net.socket`. Import only what is needed and
consult `docs/8. Modules/8.4 Built-in Modules/` for complete APIs.

## Agent checklist

1. Use `let` for normal bindings; do not invent `export`, `async`, `await`, or
   JavaScript `function` syntax.
2. Use `fun name(...) { ... }` or `(...) -> ...` for functions.
3. Use `class Name { constructor(...) { ... } }` for classes.
4. Ranges use `start..end` with an exclusive end; slices use `[start:stop:step]`.
5. Use `nil`, not `null` or `None`; use `true`/`false`, not Python casing.
6. Use `import module.{name}` for explicit imports. For a same-name class file,
   `import ClassName` imports the class directly.
7. Do not use an empty `switch`; include at least one case, commonly `_`.
8. Prefer braces and explicit returns in generated multi-line code.
9. Run `pilang <file.pi>` (or `pilang run <file.pi>`) to validate generated code.
