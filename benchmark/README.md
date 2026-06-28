# Pilang Benchmarks

This directory contains small, repeatable end-to-end benchmarks for the Pilang interpreter.

The benchmark runner measures the complete execution pipeline, including:

* Source file loading
* Parsing
* Compilation
* VM startup
* Program execution

Each Pilang benchmark (`.pi`) has matching Python (`.py`) and Lua (`.lua`) implementations. The runner executes all three versions and reports median wall-clock execution times along with speedup ratios against Python and Lua.

The goal is to compare Pilang, Python, and Lua on identical workloads in the same environment. Results should not be interpreted as language-wide performance claims.

## Benchmark Coverage

The suite includes a variety of common runtime workloads, including:

* Integer arithmetic in tight loops
* User-defined function calls and returns
* Bound instance-method calls and returns
* List creation, growth, indexing, and traversal
* Recursive algorithms and branch-heavy execution
* Large-scale numeric accumulation
* Random number generation
* Sorting workloads

Additional benchmarks may be added over time as the language evolves.

## Running

Build the optimized Pilang interpreter first and make sure Python and Lua are available, then run the benchmark suite from the repository root:

```powershell
python tools/run_benchmarks.py
```

Example output:

```text
Pilang Benchmark Suite

Benchmark               Pilang(ms)  Python(ms)     Lua(ms)   vs Python      vs Lua
------------------------------------------------------------------------------------
arithmetic                   8.14       24.90       12.04       3.06x       1.48x
calls                       12.03       41.52       29.80       3.45x       2.48x
list                         5.27        6.10        4.92       1.16x       0.93x
fib                         18.52       44.71       31.33       2.41x       1.69x
sum                         10.34       13.27        9.56       1.28x       0.92x
sort                        95.43       87.12       74.21       0.91x       0.78x
------------------------------------------------------------------------------------
Pilang total : 149.73 ms
Python total : 217.62 ms
Lua total    : 162.92 ms
vs Python    : 1.45x faster
vs Lua       : 1.09x faster
```

## Color Coding

The benchmark table uses ANSI terminal colors:

* Green: Pilang is faster than Python or Lua.
* Yellow: Results are within +/-5% (effectively tied).
* Red: Python or Lua is faster than Pilang.

## Command-Line Options

Run more iterations:

```powershell
python tools/run_benchmarks.py --iterations 10
```

Filter benchmarks by name:

```powershell
python tools/run_benchmarks.py --filter list
```

Specify a custom Pilang executable:

```powershell
python tools/run_benchmarks.py --command .\release\pilang.exe
```

Specify a Python interpreter:

```powershell
python tools/run_benchmarks.py --python python3.13
```

Specify a Lua interpreter:

```powershell
python tools/run_benchmarks.py --lua lua5.4
```

Combine options:

```powershell
python tools/run_benchmarks.py --iterations 10 --warmup 3
```

## Notes

* The runner reports median wall-clock execution times.
* Every benchmark is executed multiple times to reduce noise.
* Warm-up runs are excluded from reported results.
* Pilang, Python, and Lua should perform the same benchmark workload.
* For each benchmark, Pilang, Python, and Lua are run in sequence.
* Matching `.py` and `.lua` files are required for comparison; benchmarks without both counterparts are skipped.
* Results may vary significantly depending on CPU, operating system, Python version, compiler settings, and system load.
* These benchmarks are intended to track relative performance trends and regressions, not to represent all real-world workloads.
