#!/usr/bin/env python3
"""Compile online/offline firmware without opening any real credentials."""
import argparse
import configparser
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from native_support import fixture_secrets

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--profile', choices=['online', 'offline'], default='online')
args = parser.parse_args()
output = ROOT / 'build-ci/esp32' / args.profile
include = output / 'include'
include.mkdir(parents=True, exist_ok=True)
for header in (ROOT / 'esp32/include').glob('*.h'):
    if header.name != 'secrets.h':
        shutil.copy2(header, include / header.name)
# The copied directory is used instead of the production include directory.
config = configparser.ConfigParser(interpolation=None)
config.read(ROOT / 'esp32/platformio.ini')
config['platformio'] = {
    'default_envs': 'esp32s2', 'src_dir': str(ROOT / 'esp32/src'),
    'include_dir': str(include), 'build_dir': str(output / 'build'),
    'libdeps_dir': str(ROOT / 'build-ci/esp32/libdeps'),
}
env = config['env:esp32s2']
flags = [line for line in env['build_flags'].splitlines() if line.strip() and 'Project_Secrets' not in line]
if args.profile == 'online':
    flags.append('-I"' + fixture_secrets() + '"')
flags.append('-fstack-usage')
env['build_flags'] = '\n' + '\n'.join(flags)
env['lib_deps'] = '\n' + '\n'.join([
    'symlink://' + str(ROOT / 'common/vault_protocol'), 'bblanchon/ArduinoJson@7.4.2'])
path = output / 'platformio.ini'
with path.open('w') as stream:
    config.write(stream)
result = subprocess.run(['pio', 'run', '-d', str(ROOT / 'esp32'), '-c', str(path)],
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(output / 'build.log').write_text(result.stdout)
print(result.stdout)
result.check_returncode()
# Confirm the linker wrapper is present and actually called in the linked image.
packages = Path(os.environ.get('PLATFORMIO_CORE_DIR', str(Path.home() / '.platformio'))) / 'packages'
objdump_patterns = (
    'toolchain-xtensa-esp-elf/bin/*-objdump',
    'toolchain-xtensa-esp32s2/bin/*-objdump',
)
objdump = next((tool for pattern in objdump_patterns
                for tool in sorted(packages.glob(pattern)) if tool.is_file()), None)
if objdump is None:
    sys.exit(f'ESP32 objdump tool not found in {packages}; searched: '
             + ', '.join(objdump_patterns))
elf = output / 'build/esp32s2/firmware.elf'
disassembly = subprocess.check_output([str(objdump), '-d', str(elf)], text=True)
assert args.profile == 'offline' or '<__wrap_esp_wifi_init>' in disassembly, 'Wi-Fi memory wrapper missing'
assert args.profile == 'offline' or any('call' in line and '<__wrap_esp_wifi_init>' in line for line in disassembly.splitlines()), 'Wi-Fi init bypasses wrapper'
(output / 'wrapper-check.txt').write_text('PASS: online linked code calls __wrap_esp_wifi_init\n' if args.profile == 'online' else 'PASS: offline build (Wi-Fi code may be removed by linker)\n')
