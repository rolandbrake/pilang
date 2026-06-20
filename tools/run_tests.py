import argparse
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent.parent
TEST_DIR = BASE_DIR / "test"

if sys.platform.startswith("win"):
    PI_COMMAND = BASE_DIR / "release/pilang.exe"
else:
    PI_COMMAND = BASE_DIR / "pi"


GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
DIM = "\033[90m"
RESET = "\033[0m"


def is_fixture(path: Path) -> bool:
    return any(part.startswith("_") for part in path.parts) or path.stem.endswith("_fixture")


def is_expected_failure(path: Path) -> bool:
    name = path.stem.lower()
    return "_fail" in name or name.startswith("fail_")


def normalize_output(text: str) -> str:
    lines = [line.rstrip() for line in text.replace("\r\n", "\n").split("\n")]
    filtered = [line for line in lines if line and not line.startswith("Execution Time:")]
    return "\n".join(filtered).strip()


def collect_tests(filter_text: str | None = None):
    grouped = defaultdict(list)

    for path in sorted(TEST_DIR.rglob("*.pi")):
        rel = path.relative_to(TEST_DIR)

        if is_fixture(rel):
            continue

        if filter_text and filter_text.lower() not in str(rel).lower():
            continue

        category = rel.parts[0] if len(rel.parts) > 1 else "examples"
        grouped[category].append(path)

    return dict(sorted(grouped.items()))


def run_test(path: Path):
    started = time.perf_counter()
    result = subprocess.run(
        [str(PI_COMMAND), str(path)],
        capture_output=True,
        text=True,
        cwd=BASE_DIR,
    )
    duration_ms = (time.perf_counter() - started) * 1000.0

    expected_fail = is_expected_failure(path)
    ok = (result.returncode != 0) if expected_fail else (result.returncode == 0)
    label = (
        "XPASS" if expected_fail and result.returncode == 0
        else "XFAIL" if expected_fail
        else "PASS" if ok
        else "FAIL"
    )
    color = GREEN if ok else RED

    print(f"  {color}[{label}]{RESET} {path.relative_to(TEST_DIR)} {DIM}({duration_ms:.1f} ms){RESET}")

    return {
        "ok": ok,
        "expected_fail": expected_fail,
        "returncode": result.returncode,
        "stdout": normalize_output(result.stdout),
        "stderr": normalize_output(result.stderr),
        "path": path,
        "duration_ms": duration_ms,
    }


def print_failure(record):
    rel = record["path"].relative_to(TEST_DIR)
    print(f"{RED}{rel}{RESET}")

    if record["expected_fail"] and record["returncode"] == 0:
        print("  Expected this test to fail, but it exited successfully.")
    elif not record["expected_fail"]:
        print(f"  Exit code: {record['returncode']}")

    if record["stdout"]:
        print("  stdout:")
        for line in record["stdout"].splitlines():
            print(f"    {line}")

    if record["stderr"]:
        print("  stderr:")
        for line in record["stderr"].splitlines():
            print(f"    {line}")

    print()


def ensure_binary():
    if PI_COMMAND.exists():
        return True

    print(f"{RED}Interpreter not found:{RESET} {PI_COMMAND}")
    print("Build `pilang.exe` first, then rerun the test suite.")
    return False


def parse_args():
    parser = argparse.ArgumentParser(description="Run Pilang diagnostic test suite.")
    parser.add_argument("--filter", help="Only run tests whose relative path contains this text.")
    return parser.parse_args()


def main():
    args = parse_args()

    if not ensure_binary():
        raise SystemExit(1)

    grouped = collect_tests(args.filter)
    if not grouped:
        print("No test files found.")
        return

    total = 0
    passed = 0
    failed = []
    expected_failures = 0
    total_duration = 0.0  # <-- added

    print(f"\n{CYAN}Pilang Diagnostic Test Suite{RESET}")
    print(f"{DIM}Interpreter: {PI_COMMAND}{RESET}")
    print(f"{DIM}Test root:    {TEST_DIR}{RESET}\n")

    for category, tests in grouped.items():
        print(f"{YELLOW}== {category.upper()} ({len(tests)} tests) =={RESET}")

        for path in tests:
            record = run_test(path)
            total += 1
            total_duration += record["duration_ms"]  # <-- added

            if record["expected_fail"]:
                expected_failures += 1

            if record["ok"]:
                passed += 1
            else:
                failed.append(record)

        print()

    print(f"{YELLOW}==== SUMMARY ===={RESET}")
    print(f"Total:              {total}")
    print(f"{GREEN}Passed:             {passed}{RESET}")
    print(f"{RED}Failed:             {len(failed)}{RESET}")
    print(f"{CYAN}Expected failures:  {expected_failures}{RESET}")

    avg_duration = total_duration / total if total > 0 else 0.0
    print(f"{DIM}Average runtime:   {avg_duration:.1f} ms{RESET}")  # <-- added

    if failed:
        print(f"\n{RED}---- FAILURE DETAILS ----{RESET}")
        for record in failed:
            print_failure(record)
        raise SystemExit(1)

    print("\nFinished")


if __name__ == "__main__":
    main()