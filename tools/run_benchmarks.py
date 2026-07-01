"""Run Pilang's benchmark suite and compare pure execution times (excluding startup/compilation)."""

import argparse
import re
import shutil
import statistics
import subprocess
import sys
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent.parent
BENCHMARK_DIR = BASE_DIR / "benchmark"
DEFAULT_COMMAND = BASE_DIR / (
    "bin/pilang.exe" if sys.platform.startswith("win") else "bin/pilang"
)
NUMBER_RE = re.compile(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?")


class Color:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run Pilang benchmarks and compare pure execution times against Python and Lua."
    )

    parser.add_argument(
        "--command",
        type=Path,
        default=DEFAULT_COMMAND,
        help="Pilang executable (default: bin/pilang[.exe]).",
    )

    parser.add_argument(
        "--python",
        default=sys.executable,
        help="Python interpreter used for .py benchmarks.",
    )

    parser.add_argument(
        "--lua",
        default="lua",
        help="Lua interpreter used for .lua benchmarks (default: lua).",
    )

    parser.add_argument(
        "--iterations",
        type=int,
        default=5,
        help="Measured runs per benchmark (default: 5).",
    )

    parser.add_argument(
        "--warmup",
        type=int,
        default=1,
        help="Unreported warm-up runs per benchmark (default: 1).",
    )

    parser.add_argument(
        "--filter",
        help="Only run benchmark filenames containing this text.",
    )

    return parser.parse_args()


def collect_benchmarks(filter_text: str | None) -> list[Path]:
    paths = sorted(BENCHMARK_DIR.glob("*.pi"))

    if filter_text:
        needle = filter_text.lower()
        paths = [path for path in paths if needle in path.name.lower()]

    return paths


def run_once(command: list[str]) -> float:
    """Run the command and return the numeric time (ms) printed by the benchmark."""
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        cwd=BASE_DIR,
    )

    if result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or "no output"
        raise RuntimeError(
            f"Command failed (exit {result.returncode}):\n{message}"
        )

    # Benchmarks print their own internal elapsed runtime in milliseconds.
    # Use the last numeric line so accidental debug output before it is ignored.
    for line in reversed(result.stdout.splitlines()):
        match = NUMBER_RE.search(line.strip())
        if match:
            return float(match.group())

    output = result.stdout.strip() or result.stderr.strip() or "no output"
    raise RuntimeError(
        "Benchmark did not print a numeric runtime in milliseconds:\n"
        f"{output}"
    )


def benchmark_runtime(
    command: list[str],
    iterations: int,
    warmup: int,
) -> tuple[float, float, float]:
    for _ in range(warmup):
        run_once(command)

    samples = [run_once(command) for _ in range(iterations)]

    return (
        statistics.median(samples),
        min(samples),
        statistics.mean(samples),
    )


def speedup_color(speedup: float) -> str:
    if speedup > 1.05:
        return Color.GREEN
    if speedup < 0.55:
        return Color.RED
            
    return Color.YELLOW


def pilang_comparison(label: str, speedup: float, color: str) -> str:
    if speedup <= 0:
        text = "no comparable result"
        return f"{label:<6}: {color}{text}{Color.RESET}"
    if speedup >= 1:
        text = f"Pilang is {speedup:.2f}x faster"
    else:
        text = f"Pilang is {(1 / speedup):.2f}x slower"
    return f"{label:<6}: {color}{text}{Color.RESET}"


def resolve_executable(command: str, option: str, label: str) -> str:
    command_path = Path(command).expanduser()

    if command_path.is_file():
        return str(command_path.resolve())

    resolved = shutil.which(command)
    if resolved:
        return resolved

    raise SystemExit(
        f"{label} interpreter not found: {command}\n"
        f"Install it or pass {option} with the interpreter path."
    )


def main():
    args = parse_args()

    pilang_command = args.command.resolve()
    python_command = resolve_executable(args.python, "--python", "Python")
    lua_command = resolve_executable(args.lua, "--lua", "Lua")

    if args.iterations < 1:
        raise SystemExit("--iterations must be at least 1.")

    if args.warmup < 0:
        raise SystemExit("--warmup cannot be negative.")

    if not pilang_command.is_file():
        raise SystemExit(
            f"Interpreter not found: {pilang_command}\n"
            f"Build it first, then rerun this command."
        )

    benchmarks = collect_benchmarks(args.filter)

    if not benchmarks:
        raise SystemExit("No benchmark files matched.")

    print(f"{Color.BOLD}{Color.CYAN}Pilang Benchmark Suite (pure runtime){Color.RESET}")

    print(
        f"{'Benchmark':<24}"
        f"{'Pilang(ms)':>12}"
        f"{'Python(ms)':>12}"
        f"{'Lua(ms)':>12}"
        f"{'Python':>12}"
        f"{'Lua':>12}"
    )

    print("-" * 84)

    pilang_totals = []
    python_totals = []
    lua_totals = []

    for benchmark in benchmarks:
        python_file = benchmark.with_suffix(".py")
        lua_file = benchmark.with_suffix(".lua")

        if not python_file.exists():
            print(
                f"{Color.YELLOW}Skipping {benchmark.name} "
                f"(missing {python_file.name}){Color.RESET}"
            )
            continue

        if not lua_file.exists():
            print(
                f"{Color.YELLOW}Skipping {benchmark.name} "
                f"(missing {lua_file.name}){Color.RESET}"
            )
            continue

        try:
            pilang_median, _, _ = benchmark_runtime(
                [str(pilang_command), str(benchmark)],
                args.iterations,
                args.warmup,
            )

            python_median, _, _ = benchmark_runtime(
                [python_command, str(python_file)],
                args.iterations,
                args.warmup,
            )

            lua_median, _, _ = benchmark_runtime(
                [lua_command, str(lua_file)],
                args.iterations,
                args.warmup,
            )

        except RuntimeError as error:
            print(f"\nERROR: {error}", file=sys.stderr)
            raise SystemExit(1)

        pilang_totals.append(pilang_median)
        python_totals.append(python_median)
        lua_totals.append(lua_median)

        python_speedup = python_median / pilang_median
        lua_speedup = lua_median / pilang_median
        python_color = speedup_color(python_speedup)
        lua_color = speedup_color(lua_speedup)

        print(
            f"{benchmark.stem:<24}"
            f"{pilang_median:>12.2f}"
            f"{python_median:>12.2f}"
            f"{lua_median:>12.2f}"
            f"{python_color}{python_speedup:>11.2f}x{Color.RESET}"
            f"{lua_color}{lua_speedup:>11.2f}x{Color.RESET}"
        )

    print("-" * 84)

    pilang_total = sum(pilang_totals)
    python_total = sum(python_totals)
    lua_total = sum(lua_totals)

    if pilang_total == 0:
        overall_speedup = 0.0
    else:
        overall_speedup = python_total / pilang_total

    lua_overall_speedup = 0.0 if pilang_total == 0 else lua_total / pilang_total

    python_color = speedup_color(overall_speedup)
    lua_color = speedup_color(lua_overall_speedup)

    print(
        f"{Color.BOLD}Pilang total : "
        f"{pilang_total:.2f} ms{Color.RESET}"
    )

    print(
        f"{Color.BOLD}Python total : "
        f"{python_total:.2f} ms{Color.RESET}"
    )

    print(
        f"{Color.BOLD}Lua total    : "
        f"{lua_total:.2f} ms{Color.RESET}"
    )

    print(pilang_comparison("Python", overall_speedup, python_color))
    print(pilang_comparison("Lua", lua_overall_speedup, lua_color))


if __name__ == "__main__":
    main()
