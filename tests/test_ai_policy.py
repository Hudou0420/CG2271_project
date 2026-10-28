"""Host checks for the production AI policy and Gemini parser; never reads secrets."""
import os
import shlex
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class AiPolicyTests(unittest.TestCase):
    def test_policy_and_mock_provider(self):
        json_include = pathlib.Path(os.environ.get("ARDUINOJSON_INCLUDE", str(ROOT / "esp32/.pio/libdeps/esp32s2/ArduinoJson/src")))
        self.assertTrue(json_include.exists(), "Set ARDUINOJSON_INCLUDE with python3 ci/setup_tools.py json")
        with tempfile.TemporaryDirectory(prefix="vault-ai-tests-") as folder:
            binary = str(pathlib.Path(folder) / "ai-tests")
            subprocess.run(shlex.split(os.environ.get("CXX", "g++")) + [ "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-g",
                "-I" + str(ROOT / "esp32/src"), "-I" + str(ROOT / "common/vault_protocol"),
                "-I" + str(json_include), str(ROOT / "tests/test_ai_policy.cpp"),
                str(ROOT / "esp32/src/ai_policy.cpp"), str(ROOT / "esp32/src/ai_response.cpp"),
                "-o", binary,
            ], check=True)
            subprocess.run([binary], check=True)

if __name__ == "__main__":
    unittest.main()
