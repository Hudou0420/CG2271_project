#!/usr/bin/env python3
"""Full Cortex-M0+ validation build from the committed MCUXpresso source entries.

No IDE-generated makefiles, Redlib, credentials or physical board are used.
The newlib image is a CI artifact, not a replacement for the IDE's flash image.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=["debug", "release", "selftest", "beam"], default="debug")
    parser.add_argument("--output", type=Path, default=ROOT / "build-ci/mcx")
    args = parser.parse_args()
    output = args.output.resolve() / args.profile
    output.mkdir(parents=True, exist_ok=True)
    prefix = os.environ.get("ARM_TOOLCHAIN_PREFIX", "arm-none-eabi-")
    gcc = shutil.which(prefix + "gcc")
    if not gcc:
        parser.error("Install GNU Arm Embedded GCC/newlib, or set ARM_TOOLCHAIN_PREFIX")
    version = subprocess.check_output([gcc, "--version"], text=True)
    (output / "toolchain.txt").write_text(version)
    project = ET.parse(ROOT / ".cproject")
    configuration = next(c for c in project.findall(".//configuration") if c.get("name") == "Debug")
    compiler = next(t for t in configuration.findall(".//tool") if t.get("name") == "MCU C Compiler")
    includes, defines = ["-I" + str(ROOT / "ci/include")], []
    for option in compiler.findall("option"):
        superclass = option.get("superClass", "")
        for item in option.findall("listOptionValue"):
            value = item.get("value", "")
            if superclass == "gnu.c.compiler.option.include.paths":
                match = re.search(r"\$\{ProjName\}/(.*?)\}", value)
                if match and (ROOT / match[1]).is_dir():
                    includes += ["-I" + str(ROOT / match[1])]
            elif superclass == "gnu.c.compiler.option.preprocessor.def.symbols":
                if value not in ("__REDLIB__", "DEBUG"):
                    defines.append("-D" + value)
    if not includes or "-DCPU_MCXC444VLH" not in defines:
        raise RuntimeError("MCUXpresso configuration changed; review CI build extraction")
    source_roots = [ROOT / e.get("name") for e in configuration.findall("./sourceEntries/entry")]
    sources = sorted({p for folder in source_roots for p in folder.rglob("*.c")})
    if not sources or ROOT / "source/CG2271_Project.c" not in sources:
        raise RuntimeError("Application entry point missing from source manifest")
    # Always rebuild every source. No ignored objects or stale dependency files.
    flags = ["-mcpu=cortex-m0plus", "-mthumb", "-std=gnu11", "-Wall", "-g3",
             "-ffunction-sections", "-fdata-sections", "-fno-common", "-fno-builtin", "-fstack-usage"]
    flags += ["-Os"] if args.profile == "release" else ["-O0", "-DDEBUG"]
    defines += ["-DVAULT_SELF_TEST=" + str(int(args.profile == "selftest"))]
    if args.profile == "beam":
        defines.append("-DVAULT_LDR_BEAM_DETECTION_ENABLED=1")
    objects = []
    commands = []
    for source in sources:
        obj = output / source.relative_to(ROOT).with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        objects.append(obj)
        commands.append([gcc, *flags, *defines, *includes, "-c", str(source), "-o", str(obj)])
    (output / "manifest.json").write_text(json.dumps({
        "profile": args.profile, "sources": [str(s.relative_to(ROOT)) for s in sources],
        "defines": defines, "toolchain": version,
    }, indent=2))

    def compile_one(command):
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return result.returncode, result.stdout

    with ThreadPoolExecutor(max_workers=min(os.cpu_count() or 2, 8)) as executor:
        results = list(executor.map(compile_one, commands))
    log = "".join(text for _, text in results)
    (output / "build.log").write_text(log)
    if any(code for code, _ in results):
        print(log)
        raise SystemExit(1)
    elf = output / "vault-validation.elf"
    command = [gcc, "-mcpu=cortex-m0plus", "-mthumb", "-nostartfiles", "--specs=nano.specs", "--specs=nosys.specs",
               "-T", str(ROOT / "ci/mcx.ld"), "-Wl,--gc-sections,--print-memory-usage,-Map=" + str(output / "vault.map"),
               *map(str, objects), "-Wl,--start-group", "-lc", "-lm", "-lgcc", "-lnosys", "-Wl,--end-group",
               "-o", str(elf)]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log += result.stdout
    (output / "build.log").write_text(log)
    print(log)
    result.check_returncode()
    symbols_text = subprocess.check_output([prefix + "nm", "-n", str(elf)], text=True)
    (output / "symbols.txt").write_text(symbols_text)
    symbols = {}
    for line in symbols_text.splitlines():
        match = re.match(r"([\da-fA-F]+)\s+(\w)\s+(\S+)$", line)
        if match:
            symbols[match[3]] = (int(match[1], 16), match[2])
    assert symbols["g_pfnVectors"][0] == 0, "Vector table moved"
    assert symbols["Flash_Config"][0] == 0x400, "Flash configuration moved"
    for handler in ("ResetISR", "LPUART1_IRQHandler", "PORTC_PORTD_IRQHandler", "SysTick_Handler", "PendSV_Handler", "SVC_Handler"):
        assert symbols[handler][1] == "T", f"Missing strong handler: {handler}"
    binary = output / "vault-validation.bin"
    subprocess.run([prefix + "objcopy", "-O", "binary", str(elf), str(binary)], check=True)
    image = binary.read_bytes()
    assert len(image) <= 256 * 1024
    assert struct.unpack_from("<I", image, 0)[0] == 0x20006000
    assert struct.unpack_from("<I", image, 4)[0] == symbols["ResetISR"][0] | 1
    assert image[0x400:0x410] == bytes.fromhex("fffffffffffffffffffffffffe3dffff")
    size = subprocess.check_output([prefix + "size", "-A", str(elf)], text=True)
    (output / "size.txt").write_text(size)
    gap = symbols["_vStackBase"][0] - symbols["_pvHeapLimit"][0]
    print(f"RAM: {32768-gap} bytes reserved (including RTOS storage, libc heap and main stack); {gap} bytes unallocated")
    print("Validation image only: production MCUXpresso/Redlib configuration is unchanged.")
    print(f"PASS: {args.profile}; compiled/linked {len(sources)} sources; vectors, IRQs and memory layout verified")


if __name__ == "__main__":
    main()
