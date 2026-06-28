"""Run Pilang's end-to-end benchmark suite and compare against Python."""

import argparse
import statistics
import subprocess
import sys
import time
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent.parent
BENCHMARK_DIR = BASE_DIR / "benchmark"
DEFAULT_COMMAND = BASE_DIR / (
    "bin/pilang.exe" if sys.platform.startswith("win") else "bin/pilang"
)


class Color:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run Pilang end-to-end benchmarks and compare against Python."
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
    started = time.perf_counter()

    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        cwd=BASE_DIR,
    )

    elapsed_ms = (time.perf_counter() - started) * 1000.0

    if result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or "no output"
        raise RuntimeError(
            f"Command failed (exit {result.returncode}):\n{message}"
        )

    return elapsed_ms


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
    # speedup = python_time / pilang_time
    # >1 => Pilang faster
    if speedup > 1.05:
        return Color.GREEN
    if speedup < 0.95:
        return Color.RED
    return Color.YELLOW


def main():
    args = parse_args()

    pilang_command = args.command.resolve()

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

    print(f"{Color.BOLD}{Color.CYAN}Pilang Benchmark Suite{Color.RESET}")

    print(
        f"{'Benchmark':<24}"
        f"{'Pilang(ms)':>12}"
        f"{'Python(ms)':>12}"
        f"{'Speedup':>12}"
    )

    print("-" * 60)

    pilang_totals = []
    python_totals = []

    for benchmark in benchmarks:
        python_file = benchmark.with_suffix(".py")

        if not python_file.exists():
            print(
                f"{Color.YELLOW}Skipping {benchmark.name} "
                f"(missing {python_file.name}){Color.RESET}"
            )
            continue

        try:
            pilang_median, _, _ = benchmark_runtime(
                [str(pilang_command), str(benchmark)],
                args.iterations,
                args.warmup,
            )

            python_median, _, _ = benchmark_runtime(
                [args.python, str(python_file)],
                args.iterations,
                args.warmup,
            )

        except RuntimeError as error:
            print(f"\nERROR: {error}", file=sys.stderr)
            raise SystemExit(1)

        pilang_totals.append(pilang_median)
        python_totals.append(python_median)

        speedup = python_median / pilang_median
        color = speedup_color(speedup)

        print(
            f"{benchmark.stem:<24}"
            f"{pilang_median:>12.2f}"
            f"{python_median:>12.2f}"
            f"{color}{speedup:>11.2f}x{Color.RESET}"
        )

    print("-" * 60)

    pilang_total = sum(pilang_totals)
    python_total = sum(python_totals)

    if pilang_total == 0:
        overall_speedup = 0.0
    else:
        overall_speedup = python_total / pilang_total

    color = speedup_color(overall_speedup)

    print(
        f"{Color.BOLD}Pilang total : "
        f"{pilang_total:.2f} ms{Color.RESET}"
    )

    print(
        f"{Color.BOLD}Python total : "
        f"{python_total:.2f} ms{Color.RESET}"
    )

    print(
        f"{Color.BOLD}Overall      : "
        f"{color}{overall_speedup:.2f}x "
        f"{'faster' if overall_speedup >= 1 else 'slower'}"
        f"{Color.RESET}"
    )


if __name__ == "__main__":
    main()