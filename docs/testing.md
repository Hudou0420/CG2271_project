CI runs separate jobs for MCXC444, ESP32, native firmware tests, the existing Unity protocol tests, the dashboard, and Firebase emulators. A failure in one job does not cancel the other matrix entries. Every job uploads its available diagnostics, including on failure.

## Coverage

| Area | Automated checks | Limits |
| --- | --- | --- |
| MCXC444 build | All committed MCU C sources compile and link in Debug, Release, self-test, and beam-enabled profiles. Vector address, flash configuration bytes, strong IRQ handlers, reset vector, flash size, and RAM overlap are checked. | The GNU Arm/newlib image is supplementary validation. Continue using MCUXpresso/Redlib for the board. |
| Game logic | Complete leading-zero round, wrong guess, solve, rearm, knock tolerance boundaries, lockout deadline, tick rollover, state/event restrictions, dial wrap, and solve-time saturation. | Host shims do not simulate interrupt priority or actual RTOS scheduling. |
| MCU input service | Actual task code with scripted tap/button capture, debounce boundaries, capture timestamps, queue overflow, encoder resynchronization, startup/ADC failures, and beam filtering/retry/cancellation. Both beam configurations run. | Electrical levels, sensor sensitivity, and calibration require the physical board. |
| UART | Both production implementations: literal golden frames, endian/cause mapping, ACK/NAK, retry limits, queue pressure, corrupt/partial frames, rollover, parser recovery, and MCU ISR diagnostics. Existing Unity packet tests also run. | No measurement of physical UART voltage, baud accuracy, or interrupt latency. |
| ESP32 cloud | Online and offline builds. Actual runtime code with fake authentication/HTTP failures, token refresh, command gates, uncertain writes, stable event IDs, and upload retries. Online ELF must call the Wi-Fi initializer wrapper. | Fake responses do not establish live Firebase connectivity or real TLS memory consumption. |
| AI and HTTPS | Actual AI runtime, policy/parser, provider errors, stale/in-flight responses, cooldowns, low/fragmented memory, lease contention, cleanup, and Wi-Fi memory wrapper. ASan/UBSan under GCC and Clang. | Heap telemetry is scripted. Board tests remain necessary for TLS, stack headroom, and Wi-Fi buffers. |
| Web | TypeScript, production build, model/config tests, all screen states, phone layout, preview permissions, and production-bundle smoke tests. | Preview data cannot verify the physical game. |
| Firebase | Local database rules and Auth/Database browser flows, including atomic escalation rejection, role revocation, sign-out, reconnection, command timeout, rapid submissions, and AI message lifetime. | All tests use the `demo-vault` emulators. Production project configuration is checked manually. |

Pull requests run 1,000 fixed-seed parser/game scenarios. Nightly and manually dispatched runs add ten seeds with 10,000 scenarios each. Each scenario exercises a frame round trip, checksum corruption, random parser bytes, and a sequence of game events. These are sampled invariants, not an exhaustive proof.

## Local commands

Run from `Project/`. Python 3.12+, GCC/G++, Clang, Node 22, and Java 21 are used in CI. On EndeavourOS, install the corresponding packages with your normal package manager. PlatformIO is pinned to 6.2.0; use a virtual environment if your distribution prevents system-wide pip installs.

```sh
python3 -m venv build-ci/venv
. build-ci/venv/bin/activate
python -m pip install platformio==6.2.0 gcovr==8.3
export ARDUINOJSON_INCLUDE="$(python3 ci/setup_tools.py json)"
python3 ci/run_tests.py
CC=clang CXX=clang++ python3 ci/run_tests.py
python3 ci/run_tests.py --stress
```

Downloads are SHA-256 checked by `ci/setup_tools.py`. The GNU Arm download is for Linux x86-64. With an already installed compatible toolchain, set `ARM_TOOLCHAIN_PREFIX` yourself.

```sh
export ARM_TOOLCHAIN_PREFIX="$(python3 ci/setup_tools.py arm)"
python3 ci/mcx_build.py --profile debug
python3 ci/mcx_build.py --profile release
python3 ci/mcx_build.py --profile selftest
python3 ci/mcx_build.py --profile beam
python3 ci/esp32_build.py --profile online
python3 ci/esp32_build.py --profile offline
pio test -d esp32 -c "$PWD/esp32/platformio.ci.ini" -e native --junit-output-path "$PWD/test-reports/protocol.xml"
```

The ESP32 scripts copy public include headers and generate a placeholder `secrets.h` inside `build-ci/`. They exclude every real `secrets.h` by filename. The web runner clears inherited `VITE_*` values and overrides Vite's environment directory with an empty fixture directory. These commands do not open `../Project_Secrets/` or contact the real database or Gemini.

```sh
npm --prefix web ci
(cd web && npx --no-install playwright install chromium)
python3 ci/web_checks.py --suite dashboard
python3 ci/web_checks.py --suite firebase
```

On a fresh Ubuntu runner, Playwright uses `install --with-deps chromium`. EndeavourOS uses Playwright's fallback Chromium build; missing system libraries must be installed through the distribution. Ports 4173, 4174, 4175, 9000 and 9099 must be available. The runner refuses to reuse an existing web server. Browser fixtures allow loopback traffic and stub Google Fonts; unexpected external requests fail the test.

## Reading failures

Download the failed job's artifact from GitHub Actions. MCU artifacts include the source manifest, toolchain version, full build log, ELF, map, symbol table, size report, and stack-usage files. The linker reports RAM at 100% because the reserved stack ends at the top of RAM; the script prints the actual reserved bytes and unallocated gap separately. A linker assertion rejects overlaps.

Native JUnit reports and runtime logs are under `test-reports/native/`. Set `VAULT_TEST_OUTPUT` to choose another folder. The GCC job publishes HTML/XML coverage for the production sources exercised by the new runtime harnesses. Legacy temporary-directory tests are outside that coverage report; there is no percentage gate that could encourage superficial tests.

Reproduce a property failure using the seed and iteration count printed in its log:

```sh
VAULT_TEST_SEED=8817 VAULT_TEST_ITERATIONS=1000 python3 ci/run_tests.py
```

Stress artifacts keep separate logs for each seed. Browser failures retain screenshots, video, and Playwright traces under `test-reports/web/`. Open a trace with `web/node_modules/.bin/playwright show-trace /path/to/trace.zip`. The web runner runs every check in its suite and returns failure if any check failed. Emulator debug logs are also uploaded. Expected `permission_denied` warnings are assertions that unauthorized writes were rejected.

The workflow uses Ubuntu 24.04 and immutable action SHAs. PlatformIO, its ESP32 platform, ArduinoJson, the GNU Arm compiler, and Unity have explicit versions; web packages come from `package-lock.json`. Node/Python/Java track the selected supported major/minor versions. Caches hold dependency downloads, not compiled project objects. Fork PRs require no project secrets.

## Known regression found by these tests

`web/tests/live/owner.spec.ts` contains `rapid repeated arm clicks queue one command`. The test sends three synchronous click events to the arm button, then checks the emulator command queue. The current dashboard queues three commands instead of one. `App.tsx` calls `setBusy(true)`, but the handler's captured `busy` value remains false until React renders again. Programmatic synchronous clicks reproduce that window deterministically; physical clicks may be separated by a render.

This test intentionally fails. It is neither skipped nor marked as an expected failure. Production fixes were excluded from this change, so the Firebase CI job will remain red until a separately authorized dashboard fix closes the submission window. The failure trace records the reproduction. No firmware, protocol, database rules, or MCUXpresso project settings were changed for this test expansion.

## Physical-board checklist

Use the existing board wiring and power arrangement documented for the project, and flash the normal board firmware through MCUXpresso and PlatformIO. CI validation binaries are not the board deployment path. Record the firmware revision, board identities, sensor configuration, and console logs for each check.

1. Boot both boards and keep the enclosure still during IMU calibration. Confirm MCU link online, input service ready, and fresh website status. An input service marked ready does not prove the tap sensor works.
2. Verify each input independently: encoder increments in both directions, button confirms once per press, and actual taps produce knock events with plausible timestamps. Tap operation was unresolved in the earlier manual session; this remains a required physical check. Inspect the circuit and signal when no events arrive.
3. With the laser and LDR correctly positioned, calibrate the beam threshold before enabling detection. Verify valid readings, one alarm per beam break, and no false alarm while intact. A zero reading or disabled beam is not evidence of a passing test.
4. Arm with a leading-zero code, enter the knock rhythm, try an incorrect code, then solve. Compare the MCU state, ESP32 console, dashboard digits/attempts, elapsed time, and leaderboard. Confirm the code stays private to owner controls.
5. Trigger tamper and verify the full lockout. Try disarm and additional tamper during lockout; neither should shorten or extend the MCU deadline. Disarm and rearm after recovery.
6. Disconnect and restore Wi-Fi, then briefly interrupt the UART connection. Confirm stale/offline indicators, recovery, and no duplicate commands or solve records. Reconnect wiring with board power handled according to the existing hardware instructions.
7. Enable Gemini and trigger spaced game events. Confirm an HTTP 200/ready response and Gemini-labelled reaction, then test local fallback with AI disabled or unavailable. Record heap, largest block, minimum heap, and stack headroom across repeated rounds. Earlier HTTP 200 output confirms one successful real request; repeated operation still needs observation.
8. Open the hosted dashboard on laptop and phone. Check public access, owner sign-in/out, revoked owner access, and reconnection. Verify deployed rules reject unauthorized writes. Do not use the emulator suite as evidence that the hosted configuration is correct.
