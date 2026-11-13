# CG2271 Vault MCU project

FreeRTOS firmware for the FRDM-MCXC444 side of the vault: game FSM, sensor
services, actuator feedback and the UART link to the ESP32.

## Open and build

In MCUXpresso, use **Import > General > Existing Projects into Workspace**
and select this repository's root directory (leave "Copy projects into
workspace" unticked). The imported project is named **CG2271_Project**.
Debug and Release include `bsp/` as a source and include directory.
Refresh/reload the project after importing if needed.

`source/CG2271_Project.c` is the application entry point and creates the
FreeRTOS tasks. `VAULT_SELF_TEST=0` selects real sensor input; setting it
to 1 runs a synthetic FSM demo instead.

See [the BSP reference](bsp/README.md) for pin assignments, polarities and
return codes.

## Sensor service integration

The sensor services feed `xInputQueue`:

- Tap -> `EV_KNOCK` with the capture timestamp.
- Encoder -> `EV_DIAL(+1/-1)` per integer calibrated round-index change.
- Debounced encoder button -> `EV_PRESS`.
- LDR -> lid open/closed events after filtering (see the sensor guide).

Edit **source/vault_sensor_config.h** to tune them.
`VAULT_ENCODER_STEPS_PER_ROUND` sets the accepted half-cycle steps per digit
change, not a physical revolution. The game stays IDLE until an ARM command
arrives from the ESP32. The actuator task drives the RGB LED, buzzer and
laser signal; see [the actuator guide](docs/actuators.md).

See [the sensor-service guide](source/README_sensors.md) for event semantics,
calibration, task ownership and diagnostics. Host tests are in `tests/`.

`examples/bsp_bench_test.c.example` is a standalone component test, not part
of the normal build. To run it, exclude `source/CG2271_Project.c` from the
build and compile a `.c` copy of the example as the sole `main()`.

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
