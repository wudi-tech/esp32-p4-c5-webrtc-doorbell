import os
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[3]
MAIN = ROOT / "solutions" / "doorbell_local" / "main"
TEST = ROOT / "solutions" / "doorbell_local" / "tests" / "vision_event_test.c"


def main():
    compiler = os.environ.get("CC", r"D:\mingw64\bin\gcc.exe")
    with tempfile.TemporaryDirectory() as directory:
        executable = pathlib.Path(directory) / "vision_event_test.exe"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(MAIN),
                str(TEST),
                str(MAIN / "vision_event.c"),
                str(MAIN / "vision_tracker.c"),
                "-o",
                str(executable),
            ],
            check=True,
            timeout=30,
        )
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    main()
