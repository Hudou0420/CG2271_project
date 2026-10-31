# CG2271 Vault MCU project

The MCU project is now at this repository root. It combines remote main
(commit `82dfc54`) with the locally tested hardware BSP. The original nested
`cg2271_project_mcu/` project was moved outside the repository into local backups.

## Open and build

In MCUXpresso, use **Import > General > Existing Projects into Workspace**
and select this repository's root directory (leave "Copy projects into
workspace" unticked). The imported project is named
**CG2271_Project**. Refresh/reload after importing if needed.
The local MCU/compiler/board settings were reused; Debug and Release include
`bsp/` as a source and include directory.

Validation: the merged **CG2271_Project/Debug** build compiled and linked
with **0 errors and 0 warnings** on 21 October 2026. All 12 BSP files and
the saved bench-test source were verified byte-for-byte against the backup;
remote game/RTOS interface struct layouts and FSM source were verified preserved.
Runtime behaviour on the physical board has not been tested by this merge.

## What was preserved

- Remote `source/CG2271_Project.c` remains the application entry point with
  its FreeRTOS task setup. `VAULT_SELF_TEST=0` now selects real sensor input;
  setting it to 1 restores the synthetic FSM demo.
- Remote event/struct layouts, shared queue names and FSM API are preserved.
  EV_DIAL still carries +1/-1, now per calibrated round-index change.
  `vault_fsm.h/.c` and `vault_rtos.h` remain unchanged.
- Every file under `bsp/` was copied byte-for-byte from the local project.
  Public headers, pin assignments, polarities and return codes are unchanged.
  See [the BSP reference](bsp/README.md) for those interfaces and wiring.
- The latest complete standalone component test, including its current
  enable/disable switches and encoder decoder, is preserved verbatim in
  [examples/bsp_bench_test.c.example](examples/bsp_bench_test.c.example).
  Its `.example` extension prevents a second `main()` in the normal build.
- The 124 shared SDK, device, board and RTOS files matched exactly after
  normalizing line endings. The remote copies are used in the merged project.

## Sensor service integration

The merged project's sensor services are now connected to `xInputQueue`:

- Tap -> `EV_KNOCK` with the capture timestamp.
- Encoder -> `EV_DIAL(+1/-1)` per integer calibrated round-index change.
- Debounced encoder button -> `EV_PRESS`.
- LDR -> `EV_BEAM_BROKEN` after filtering, when armed and calibrated/enabled.

Edit **source/vault_sensor_config.h**. `VAULT_ENCODER_STEPS_PER_ROUND=4`
sets the accepted half-cycle steps per digit change, not a physical revolution. LDR ADC polling runs,
but `VAULT_LDR_BEAM_DETECTION_ENABLED=0` until its threshold/polarity are
measured. The existing game task logs incoming events. It remains IDLE until
an ARM command arrives from the ESP32. The actuator task now drives RGB and
buzzer feedback; see [the actuator guide](docs/actuators.md) for behaviour,
configuration, and board checks.

See [the sensor-service guide](source/README_sensors.md) for event semantics,
round-boundary reversal examples, calibration, task ownership and diagnostics.
BSP files remain unchanged. Regression tests of
the production sensor logic and existing FSM contract are in `tests/`.

The preserved bench test is an alternative application, not another task.
To run it in this project, exclude `source/CG2271_Project.c` from that build
and compile a `.c` copy of the example as the sole main. Restore the remote
entry point for normal game integration. The external backup also retains
the original working bench-test build.

## ESP32 link

`tComms` is `source/vault_comms.c`: LPUART1 at 115200 baud on **PTE0 (J2-18,
TX) -> ESP32 GPIO5** and **PTE1 (J2-20, RX) <- ESP32 GPIO4**, plus a common GND.
The frame format and message types are shared with the ESP32 firmware in
`common/vault_protocol/` (compiled into both builds). ARM, DISARM and TAMPER
from the ESP32 become `EV_ARM`, `EV_DISARM` and `EV_TAMPER` on `xInputQueue`;
tGame's ATTEMPT, SOLVED and ALARM packets on `xUartTxQueue` are sent with
ACK/NAK and up to 3 resends. STATUS (state, dial digit, knocks, attempts,
light level, encoder steps) goes out every second, and within ~100 ms of a change.

The ESP32 firmware (PlatformIO) is in `esp32/`; see `esp32/README.md`. It also
connects to the web dashboard through Firebase; the database layout the dashboard
should use (and `vault-api.js`, its JavaScript interface) is in `web/`.

## Recovery copy

A complete copy of the original local project, including its build settings,
BSP and current main, was saved before updating main at:

`../local_backups/2026-10-21-before-main-integration-af1197f0/cg2271_project_mcu/`

The backups are outside this Git repository and are not included in the push.
The latest old-project folder was archived at
`../local_backups/removed-bsp-only-project-2026-10-21/`.
