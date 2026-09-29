## Pilang Formatter + Minifier

This project provides a native Pilang formatter and minifier. It includes a scanner, parser, AST nodes, and formatting/minification utilities compiled into `bin/PiForminator.px`.

The native Pilang CLI runs the PX tool directly, so `fmt` and `min` do not require Node.js.

## Features

- Parses pilang source into an AST
- Stable, readable formatting with comment preservation
- Minification support (with optional identifier mangling)
- CLI-style tester for quick local runs

## Requirements

- Pilang with `bin/PiForminator.px` present (rebuild it with `pilang build utils\PiForminator.pi` if needed)

## Quick Start

Format or minify a file in place from the repository root:

```bash
pilang fmt path\to\script.pi
pilang min path\to\script.pi
```

You can run the compiled tool directly:

```powershell
pilang run bin\PiForminator.px fmt path\to\script.pi
pilang run bin\PiForminator.px min path\to\script.pi
```

## Programmatic Usage

### Formatter

```pi
import PiFormatter.{formatSource}

const result = formatSource("let x=1+2")
println(result.code)
```

### Minifier

Minification is driven by AST nodes and a `PiContext` (which can mangle identifiers). Use the scanner + parser, then call `minify` on the statements.

```pi
import PiMinifier.{minifySource}

const result = minifySource("let counter = 1 + 2")
println(result.code)
```

## Notes

- Formatting preserves leading/trailing comments and uses indentation rules implemented in the AST node classes.
- Minification can mangle identifiers while keeping built-in names intact.
- The list of built-ins used in `PiTester.js` is a good starting point for real scripts.

## Files of Interest

- `PiForminator.pi` - native CLI entry point
- `PiForminator.px` - compiled PX package copied to `bin/`
- `PI/PiFormatter.pi` - formatting entry point
- `PI/PiMinifier.pi` - minification entry point
- `PI/PiContext.pi` - scope handling for minification
