#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*command: str) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> None:
    run(sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py")
    with tempfile.TemporaryDirectory(prefix="native-mounts-tests-") as directory:
        binary = str(Path(directory) / "eligibility_tests")
        run("c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Isrc",
            "src/NativeMountCatalog.cpp", "src/NativeMountEligibility.cpp",
            "tests/eligibility_tests.cpp", "-o", binary)
        run(binary)
    run(sys.executable, "content/build_epf.py", "--check")


if __name__ == "__main__":
    main()
