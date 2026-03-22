import subprocess
from pathlib import Path



# locate tests folder (parent directory + tests)
BASE_DIR = Path(__file__).resolve().parent.parent
TEST_DIR = BASE_DIR / "test"

PI_COMMAND = f"{BASE_DIR}/pi"

def run_test(file):
    print(f"Running {file.name}...")

    result = subprocess.run(
        [PI_COMMAND, str(file)],
        capture_output=True,
        text=True
    )

    if result.returncode == 0:
        print(f"[PASS] {file.name}")
    else:
        print(f"[FAIL] {file.name}")
        print(result.stderr)

def main():
    tests = sorted(TEST_DIR.glob("*.pi"))
        
    type_tests = sorted(TEST_DIR.glob("types/*.pi"))
    tests.extend(type_tests)
    
    obj_tests = sorted(TEST_DIR.glob("objs/*.pi"))
    tests.extend(obj_tests)
    if not tests:
        print("No .pi tests found")
        return

    print(f"Found {len(tests)} tests\n")

    for test in tests:
        run_test(test)

    print("\nFinished")

if __name__ == "__main__":
    main()