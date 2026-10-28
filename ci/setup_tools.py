#!/usr/bin/env python3
"""Fetch checksum-pinned CI dependencies. Cache downloads, never build outputs."""
import argparse
import hashlib
from pathlib import Path
import tarfile
import urllib.request
import zipfile

TOOLS = {
    'actionlint': ('https://github.com/rhysd/actionlint/releases/download/v1.7.12/actionlint_1.7.12_linux_amd64.tar.gz',
                   '8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8', 'actionlint'),
    'arm': ('https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz',
            '95c011cee430e64dd6087c75c800f04b9c49832cc1000127a92a97f9c8d83af4',
            'arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-'),
    'json': ('https://downloads.arduino.cc/libraries/github.com/bblanchon/ArduinoJson-7.4.2.zip',
             '5848dbc2a2eca7eb469710f64106195e90e6788a2365859f88c491214a108230',
             'ArduinoJson-7.4.2/src'),
}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('tool', choices=TOOLS)
parser.add_argument('--cache', type=Path, default=Path.home() / '.cache/vault-ci-downloads')
parser.add_argument('--output', type=Path, default=Path('build-ci/tools'))
args = parser.parse_args()
url, digest, target = TOOLS[args.tool]
args.cache.mkdir(parents=True, exist_ok=True)
archive = args.cache / url.rsplit('/', 1)[1]
if not archive.exists():
    urllib.request.urlretrieve(url, archive)
if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
    archive.unlink()
    raise SystemExit('Checksum mismatch: download deleted; rerun to fetch a clean archive')
args.output.mkdir(parents=True, exist_ok=True)
if archive.suffix == '.zip':
    with zipfile.ZipFile(archive) as source:
        source.extractall(args.output)
else:
    with tarfile.open(archive) as source:
        source.extractall(args.output, filter='data')
print((args.output / target).resolve())
