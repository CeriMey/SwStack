#!/usr/bin/env python3
"""Exercise the real SwBuild event loop and process cleanup on POSIX signals."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


FAKE_CMAKE = '''#!/usr/bin/env python3
import os, signal, sys
from pathlib import Path
root = Path(os.environ["SWBUILD_SIGNAL_TEST_ROOT"])
stage = "build" if "--build" in sys.argv else "configure"
(root / ("stage-" + stage)).touch()
if stage == os.environ.get("SWBUILD_SIGNAL_TEST_HOLD"):
    # Reproduce a child that consumes Ctrl+C and reports success anyway.
    signal.signal(signal.SIGINT, lambda *_: sys.exit(0))
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    (root / "ready").write_text(str(os.getpid()))
    while True: signal.pause()
if stage == os.environ.get("SWBUILD_SIGNAL_TEST_FAIL"):
    sys.exit(7)
'''


def verify(binary, hold="", stop_signal=None, parent_only=False, failure="", options=(), empty=False):
    with tempfile.TemporaryDirectory(prefix="swbuild-interrupt-") as directory:
        root = Path(directory)
        source = root / "src/demo"
        source.mkdir(parents=True)
        if not empty:
            (source / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.10)\nproject(Demo NONE)\n")
        cmake = root / "cmake"
        cmake.write_text(FAKE_CMAKE)
        cmake.chmod(0o755)
        child = subprocess.Popen([str(binary), "--root", str(root), "--scan", "src",
            "--build_root", str(root / "build"), "--cmake", str(cmake), "--no_install",
            *options], start_new_session=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, env=dict(os.environ, SWBUILD_SIGNAL_TEST_ROOT=str(root),
                SWBUILD_SIGNAL_TEST_HOLD=hold, SWBUILD_SIGNAL_TEST_FAIL=failure))
        try:
            worker_pid = None
            if stop_signal is not None:
                deadline = time.monotonic() + 5
                while not (root / "ready").exists() or not (root / "ready").read_text():
                    assert child.poll() is None and time.monotonic() < deadline, "CMake did not become ready"
                    time.sleep(0.01)
                worker_pid = int((root / "ready").read_text())
                # Only this fixture's dedicated session (or its SwBuild) is signalled.
                (os.kill if parent_only else os.killpg)(child.pid, stop_signal)
            output, _ = child.communicate(timeout=5)
            if stop_signal is not None:
                assert child.returncode > 0, (child.returncode, output)
                if hold == "configure":
                    assert not (root / "stage-build").exists(), output
                try:
                    os.kill(worker_pid, 0)
                except ProcessLookupError:
                    pass
                else:
                    raise AssertionError(f"CMake worker {worker_pid} survived SwBuild")
            else:
                assert child.returncode == (7 if failure else 0), (child.returncode, output)
            label = f"{hold} {signal.Signals(stop_signal).name} ({'parent' if parent_only else 'group'})" if stop_signal else (
                f"failure {failure}" if failure else f"success {options or ('empty' if empty else 'build')}" )
            print(f"PASS SwBuild: {label}, exit {child.returncode}")
        finally:
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            child.communicate(timeout=5)


if __name__ == "__main__":
    if os.name != "posix":
        raise SystemExit("This regression requires POSIX process groups")
    binary = Path(sys.argv[1]).resolve(strict=True)
    for stage in ("configure", "build"):
        for stop_signal in (signal.SIGINT, signal.SIGTERM):
            verify(binary, stage, stop_signal)
        verify(binary, stage, signal.SIGINT, parent_only=True)
        verify(binary, failure=stage)
    verify(binary)
    verify(binary, empty=True)
    for options in (("--dry_run",), ("--configure_only",), ("--build_only",)):
        verify(binary, options=options)
