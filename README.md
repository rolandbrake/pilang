<p align="center">
  <a href="https://pi-lang.netlify.app/">
    <img src="imgs/pi.png" alt="Pilang logo" width="180">
  </a>
</p>

<h1 align="center">Pilang</h1>

<p align="center">
  A lightweight, embeddable, general-purpose programming language written in C.
</p>

<p align="center">
  <a href="https://pi-lang.netlify.app/"><strong>Visit the Pilang website</strong></a>
  |
  <a href="docs/README.md">Read the docs</a>
  |
  <a href="tests">Explore examples</a>
</p>

## Overview

Pilang is a small, expressive scripting language with a compact C implementation, a bytecode virtual machine, modules, objects, tensors, and a practical standard library. It aims to feel light enough for quick scripts, but capable enough for experiments, teaching tools, numerical code, and embeddable application logic.

The language mixes familiar Python-like readability with features that are fun to compose: list comprehensions, slices, ranges, spread syntax, tuples, sets, closures, classes, callable objects, operator hooks, and native tensor helpers. The repository includes a native interpreter, a WebAssembly/browser build, documentation, editor assets, built-in modules, reusable libraries, ML examples, and a growing test suite.

## Why Pilang

- **Readable scripts with sharp edges where they help**: `let`, `const`, `fun`, `class`, ranges, slices, `#` length, `in`, ternaries, spread syntax, destructuring, and comprehensions.
- **Collections are first-class**: lists, maps, tuples, and sets have literal syntax and work naturally with loops, membership checks, copying, slicing, and collection helpers.
- **Functions are flexible**: named functions, anonymous functions, arrow functions, closures, recursion, defaults, named arguments, and higher-order helpers are all part of the language.
- **Objects are dynamic but structured**: classes, constructors, inheritance, methods, callable objects, bracket access, static behavior, and operator/magic methods let you choose between plain maps and richer objects.
- **Numerical work is built in**: tensor constructors, indexing, _transforms, reductions, broadcasting-style helpers, statistics, and linear algebra functions live in the standard modules.
- **Made to travel**: the same language can run as a native executable or as a WebAssembly/browser build.
- **Small enough to study**: the compiler, VM, object model, module system, and garbage collector live in C source files that are approachable for language/runtime hacking.

## Quick Taste

```pilang
import math:m

fun area(radius) {
    return m.PI * radius ** 2
}

radii = [2, 4, 8]

for r in radii {
    println("radius = " + r + ", area = " + area(r))
}
```

## Language Tour

### Expressive Collections

```pilang
scores = [91, 72, 88, 91, 64, 72]

unique = {91, 72, 88, 64}
curved = [min(score + 5, 100) : score in scores]
honors = []

for score in curved
    if score >= 90
        honors += score

println("unique scores: " + unique)
println("honors: " + honors)
println("top three-ish: " + curved[0:3])
```

### Functions and Closures

```pilang
fun make_counter(start = 0) {
    let value = start

    return () -> {
        value += 1
        return value
    }
}

next_id = make_counter(100)
println(next_id()) // 101
println(next_id()) // 102
```

### Classes, Inheritance, and Callable Objects

```pilang
class Model {
    parameters() {
        return []
    }
}

class Linear: Model {
    constructor(w, b) {
        this.w = w
        this.b = b
    }

    call(x) {
        return this.w * x + this.b
    }

    parameters() {
        return [this.w, this.b]
    }

    format() {
        return "Linear(w=" + this.w + ", b=" + this.b + ")"
    }
}

model = Linear(2, 1)
println(model(10))      // callable object
println(model.parameters())
```

### Operator Hooks

Objects can participate in operators by defining compute methods, which makes domain objects feel native without changing the VM for every new type.

```pilang
import lang

class Vec2 {
    constructor(x, y) {
        this.x = x
        this.y = y
    }

    compute(op, other) {
        if op == lang.OP_ADD
            return Vec2(this.x + other.x, this.y + other.y)
    }

    format() {
        return "Vec2(" + this.x + ", " + this.y + ")"
    }
}

println(Vec2(2, 3) + Vec2(4, 1))
```

### Tensors for Numerical Code

```pilang
import tensor:t

x = t.from([[1, 2], [3, 4]])
w = t.eye(2, 2)

println(t.shape(x))
println(t.matmult(x, w))
println(t.mean(x))
```


## Run Pilang

From the repository root on Windows:

```powershell
pilang run test.pi
```

You can also use the shorthand form:

```powershell
pilang test.pi
```

Show available commands:

```powershell
pilang help
```

Developer helpers:

```powershell
pilang dis test.pi
pilang dis -o bytecode.txt test.pi
pilang fmt test.pi
pilang min test.pi
```

`fmt` and `min` rewrite the target file in place and use the JavaScript utilities in `utils/`, so Node.js and the formatter/minifier modules must be available.

## Build From Source

The repository includes a Makefile for native and browser builds. On Windows with MinGW available, use `make` from the repository root. Some MinGW installs expose this as `mingw32-make`.

```powershell
make release
```

Common targets:

- `make release`: build the optimized native executable, `release/pilang.exe`.
- `make debug`: build a debug native executable with `DEBUG_BUILD` enabled at `release/pilang.exe`.
- `make web`: build the Emscripten/WebAssembly output in `release/`.
- `make run`: build and run the native executable.
- `make test`: build the native executable and run `python tools/run_tests.py`.
- `make clean`: remove generated build outputs.

The native build expects MinGW GCC and the SDL2 development libraries used by the project. The browser build expects Emscripten's `emcc`.

## Project Layout

- `pi_*.c`, `pi_*.h`: core compiler, parser, VM, values, objects, modules, and runtime internals.
- `builtin/`: built-in native modules.
- `libs/`: Pilang libraries written in `.pi`.
- `ML/`: numerical and machine-learning experiments written in Pilang.
- `release/`: local build outputs.
- `docs/`: language documentation and reference material.
- `tests/`: examples and regression tests grouped by language area.

## Documentation

Start with the [documentation index](docs/README.md), then explore:

- [Language features](docs/1.%20Introduction/1.2-features.md)
- [Running Pilang](docs/1.%20Introduction/1.4-running-pilang.md)
- [Data types](docs/3.%20Data%20Types/README.md)
- [Functions](docs/6.%20Functions/README.md)
- [Objects and classes](docs/7.%20Objects%20and%20Classes/README.md)
- [Modules](docs/8.%20Modules/README.md)
- [Examples](docs/13.%20Examples/README.md)

## Testing

Run the test suite with:

```bash
python tools/run_tests.py
```

Tests cover core language behavior, tensors, modules, object/class behavior, runtime types, built-ins, and larger example programs.

## Website

The language website and playground are available at:

**https://pi-lang.netlify.app/**

Use it to read docs, browse examples, and try Pilang in the browser.

## License

This project is licensed under the terms in [LICENSE](LICENSE).
