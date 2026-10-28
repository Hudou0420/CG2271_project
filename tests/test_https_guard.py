"""Test the production HTTPS lease using a host FreeRTOS shim, without secrets."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class HttpsGuardTests(unittest.TestCase):
    def test_competing_clients_and_release(self):
        with tempfile.TemporaryDirectory(prefix="vault-https-tests-") as folder:
            binary = str(Path(folder) / "https-tests")
            subprocess.run(shlex.split(os.environ.get("CXX", "g++")) + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                "-fsanitize=address,undefined", "-g", "-I" + str(ROOT / "tests/fakes"),
                "-I" + str(ROOT / "esp32/src"), str(ROOT / "tests/test_https_guard.cpp"),
                str(ROOT / "esp32/src/https_guard.cpp"), "-o", binary,
            ], check=True)
            subprocess.run([binary], check=True)

if __name__ == "__main__":
    unittest.main()
