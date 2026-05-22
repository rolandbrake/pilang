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

Pilang is a real-world scripting language with a compact C implementation, a bytecode virtual machine, modular architecture, standard library support, and operating system integration. It is designed to be easy to embed, pleasant to script with, and practical for experiments that need more than a tiny expression language.

The project includes a native interpreter, a WebAssembly/browser build, documentation, editor assets, built-in modules, reusable libraries, and a growing test suite.

## Feature Highlights

- **Small C runtime**: interpreter, compiler, bytecode VM, call frames, values, objects, modules, and garbage-collected heap objects.
- **Modern scripting syntax**: variables, constants, expressions, assignment operators, slices, ranges, destructuring, comprehensions, and control flow.
- **Rich data model**: numbers, strings, booleans, nil, lists, maps, tuples, sets, objects, classes, and tensors.
- **Functions that compose**: named functions, anonymous functions, arrow functions, closures, recursion, higher-order helpers, and functional utilities.
- **Object-oriented programming**: classes, constructors, methods, inheritance, callable objects, bracket access, static behavior, and magic/operator methods.
- **Module system**: import built-in modules, local modules, aliases, exported symbols, and private module members.
- **Standard library coverage**: math, statistics, strings, I/O, filesystem, OS, system info, time, drawing, plotting, collections, functional helpers, types, language constants, and tensors.
- **Numerical tools**: dense tensor operations, broadcasting, indexing, reductions, transforms, statistics, linear algebra helpers, and matrix operations.
- **Native and web targets**: run `.pi` files locally with `pi.exe`, or try the browser playground through the hosted site.
- **Developer-friendly repo**: documentation, examples, tests, and VS Code syntax extension files are included.

## Quick Example

```pilang
import math:m

fun area(radius) {
    return m.PI * radius ** 2
}

radii = [2, 4, 8]

for r in radii {
    println("radius = " + str(r) + ", area = " + str(area(r)))
}
```

## Run Pilang

From the repository root on Windows:

```powershell
.\pi.exe run test.pi
```

You can also use the shorthand form:

```powershell
.\pi.exe test.pi
```

Show available commands:

```powershell
.\pi.exe help
```

## Build From Source

The repository includes a Makefile for native and browser builds. On Windows with MinGW available, use `mingw32-make` from the repository root:

```powershell
mingw32-make release
```

Common targets:

- `mingw32-make release`: build the optimized native executable, `pi.exe`.
- `mingw32-make debug`: build a debug native executable with `DEBUG_BUILD` enabled.
- `mingw32-make web`: build the Emscripten/WebAssembly output, `pilang.html`, `pilang.js`, and `pilang.wasm`.
- `mingw32-make run`: build and run the native executable.
- `mingw32-make test`: build the native executable and run `python tools/run_tests.py`.
- `mingw32-make clean`: remove generated build outputs.

The native build expects MinGW GCC and the SDL2 development libraries used by the project. The browser build expects Emscripten's `emcc`.

## Project Layout

- `pi_*.c`, `pi_*.h`: core compiler, parser, VM, values, objects, modules, and runtime internals.
- `builtin/`: built-in native modules.
- `libs/`: Pilang libraries written in `.pi`.
- `docs/`: language documentation and reference material.
- `tests/`: examples and regression tests grouped by language area.
- `website/`: hosted website and browser playground source.
- `editors/`: editor integration files.
- `imgs/`: project images and README assets.

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
