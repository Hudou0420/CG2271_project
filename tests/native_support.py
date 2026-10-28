"""Compile production sources into isolated, sanitized host test programs."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def native(name, sources, *, cxx=False, includes=(), defines=(), arguments=()):
    destination = os.environ.get("VAULT_TEST_OUTPUT")
    with tempfile.TemporaryDirectory(prefix="vault-native-") as temporary:
        folder = Path(destination).resolve() / name if destination else Path(temporary)
        folder.mkdir(parents=True, exist_ok=True)
        binary = folder / name
        compiler = shlex.split(os.environ.get("CXX" if cxx else "CC", "g++" if cxx else "gcc"))
        flags = ["-std=c++17" if cxx else "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        if cxx:
            flags += ["-x", "c++"]
        if os.environ.get("VAULT_COVERAGE") == "1":
            flags += ["--coverage", "-O0"]
        # A developer may have a local secrets.h in the public include directory.
        # Use only a filtered copy, even when tests are run outside CI.
        public = ROOT / "build-ci/fixture-public-include"
        public.mkdir(parents=True, exist_ok=True)
        for header in (ROOT / "esp32/include").glob("*.h"):
            if header.name != "secrets.h":
                shutil.copy2(header, public / header.name)
        includes = [str(public) if (ROOT / p).resolve() == ROOT / "esp32/include" else p for p in includes]
        command = [*compiler, *flags, *["-I" + str(ROOT / p) for p in includes],
                   *["-D" + p for p in defines], *[str(ROOT / p) for p in sources], "-o", str(binary)]
        result = subprocess.run(command, cwd=folder, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (folder / "build.log").write_text(result.stdout)
        if result.returncode:
            raise AssertionError(result.stdout)
        result = subprocess.run([str(binary), *map(str, arguments)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        (folder / "run.log").write_text(result.stdout)
        print(result.stdout, end="", flush=True)
        if result.returncode:
            raise AssertionError(f"{name} exited {result.returncode}:\n{result.stdout}")


def json_include():
    return os.environ.get("ARDUINOJSON_INCLUDE", str(ROOT / "esp32/.pio/libdeps/esp32s2/ArduinoJson/src"))


def fixture_secrets():
    folder = ROOT / "build-ci/fixture-secrets"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "secrets.h").write_text('''#pragma once
#define WIFI_SSID "fixture"
#define WIFI_PASSWORD "fixture-password"
#define FIREBASE_DB_URL "https://vault.invalid"
#define FIREBASE_API_KEY "fixture-public-key"
#define FIREBASE_DEVICE_EMAIL "device@example.invalid"
#define FIREBASE_DEVICE_PASSWORD "fixture-password"
#define GEMINI_API_KEY "fixture-gemini-key"
#define GEMINI_MODEL "fixture-model"
''')
    return str(folder)
