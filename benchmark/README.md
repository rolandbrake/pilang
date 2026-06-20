# Pilang Benchmarks

This directory contains small, repeatable end-to-end benchmarks for the Pilang interpreter.

The benchmark runner measures the complete execution pipeline, including:

* Source file loading
* Parsing
* Compilation
* VM startup
* Program execution

Each Pilang benchmark (`.pi`) has a matching Python implementation (`.py`). The runner executes both versions and reports median wall-clock execution times along with a speedup ratio.

The goal is to compare Pilang and Python on identical workloads in the same environment. Results should not be interpreted as language-wide performance claims.

## Benchmark Coverage

The suite includes a variety of common runtime workloads, including:

* Integer arithmetic in tight loops
* User-defined function calls and returns
* List creation, growth, indexing, and traversal
* Recursive algorithms and branch-heavy execution
* Large-scale numeric accumulation
* Random number generation
* Sorting workloads

Additional benchmarks may be added over time as the language evolves.

## Running

Build the optimized Pilang interpreter first, then run the benchmark suite from the repository root:

```powershell
python tools/run_benchmarks.py
```

Example output:

```text
Pilang Benchmark Suite

Benchmark               Pilang(ms)  Python(ms)     Speedup
----------------------------------------------------------
arithmetic                   8.14       24.90       3.06x
calls                       12.03       41.52       3.45x
list                         5.27        6.10       1.16x
fib                         18.52       44.71       2.41x
sum                         10.34       13.27       1.28x
sort                        95.43       87.12       0.91x
----------------------------------------------------------
Pilang total : 149.73 ms
Python total : 217.62 ms
Overall      : 1.45x faster
```

## Color Coding

The benchmark table uses ANSI terminal colors:

* Green: Pilang is faster than Python.
* Yellow: Results are within ±5% (effectively tied).
* Red: Python is faster than Pilang.

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

Combine options:

```powershell
python tools/run_benchmarks.py --iterations 10 --warmup 3
```

## Notes

* The runner reports median wall-clock execution times.
* Every benchmark is executed multiple times to reduce noise.
* Warm-up runs are excluded from reported results.
* Benchmark programs intentionally produce no output.
* Matching `.py` files are required for comparison; benchmarks without a Python counterpart are skipped.
* Results may vary significantly depending on CPU, operating system, Python version, compiler settings, and system load.
* These benchmarks are intended to track relative performance trends and regressions, not to represent all real-world workloads.
