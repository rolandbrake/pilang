import subprocess
from pathlib import Path

# locate tests folder (parent directory + tests)
BASE_DIR = Path(__file__).resolve().parent.parent
TEST_DIR = BASE_DIR / "test"

PI_COMMAND = f"{BASE_DIR}/pi"


def run_test(file):
    print(f"  Running {file.name}...")

    result = subprocess.run(
        [PI_COMMAND, str(file)],
        capture_output=True,
        text=True
    )

    if result.returncode == 0:
        print(f"  [PASS] {file.name}")
    else:
        print(f"  [FAIL] {file.name}")
        print(result.stderr)


def collect_tests():
    categorized = {}

    # tests directly inside /test
    root_tests = sorted(TEST_DIR.glob("*.pi"))
    if root_tests:
        categorized["root"] = root_tests

    # scan all subdirectories
    for subdir in sorted([d for d in TEST_DIR.iterdir() if d.is_dir()]):
        pi_files = sorted(subdir.glob("*.pi"))
        if pi_files:
            categorized[subdir.name] = pi_files

    return categorized


def main():
    categorized_tests = collect_tests()

    if not categorized_tests:
        print("No .pi tests found")
        return

    total = sum(len(v) for v in categorized_tests.values())
    print(f"Found {total} tests in {len(categorized_tests)} categories\n")

    for category, tests in categorized_tests.items():
        print(f"== {category.upper()} ({len(tests)} tests) ==")

        for test in tests:
            run_test(test)

        print()  # spacing between categories

    print("Finished")


if __name__ == "__main__":
    main()