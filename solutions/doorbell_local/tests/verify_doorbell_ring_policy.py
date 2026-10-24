import os
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[3]
TEST = ROOT / "solutions" / "doorbell_local" / "tests" / "doorbell_ring_policy_test.c"
PRODUCTION = ROOT / "solutions" / "doorbell_local" / "main" / "doorbell_ring_policy.c"
MAIN = ROOT / "solutions" / "doorbell_local" / "main"


def main():
    compiler = os.environ.get("CC", r"D:\mingw64\bin\gcc.exe")
    with tempfile.TemporaryDirectory() as directory:
        executable = pathlib.Path(directory) / "doorbell_ring_policy_test.exe"
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(MAIN),
            str(TEST),
            str(PRODUCTION),
            "-o",
            str(executable),
        ]
        subprocess.run(command, check=True, timeout=30)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    main()
