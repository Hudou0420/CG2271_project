# Sensor services in the merged project

These services are implemented in the repository-root **CG2271_Project**.
The older nested `cg2271_project_mcu/` bench project was moved to external
local backups, as documented in the repository README. Existing BSP
files and public BSP APIs are unchanged. No test cycling of actuators is
introduced by this service.

## Configuration

Edit `source/vault_sensor_config.h`:

```c
#define VAULT_ENCODER_STEPS_PER_ROUND 40
#define VAULT_ENCODER_DIRECTION       1
#define VAULT_LDR_BEAM_DETECTION_ENABLED 0
#define VAULT_LDR_BLOCKED_WHEN_LOW    1
#define VAULT_LDR_THRESHOLD           2000U
#define VAULT_LDR_HYSTERESIS          100U
#define VAULT_LDR_BLOCKED_SAMPLES     3U
```

**40 is a provisional accepted-step count, not a measured calibration.**
The ADC threshold, polarity and hysteresis are also provisional. LDR polling
is active, but beam-break event generation defaults OFF until the threshold
and light/dark direction are measured. Set BEAM_DETECTION_ENABLED to 1 only
after choosing those values. This service does not automatically power or
align the laser. The remaining actuator stub does not provide beam readiness
or a laser/relay settling handshake yet.

The tap defaults to a falling active edge with a pull-up and a 40 ms duplicate
rejection interval. Change `VAULT_TAP_ACTIVE_LOW` if the measured pulse is
active-high. Encoder CLK/DT/SW use pull-ups and both-edge capture; SW is
active-low and is confirmed after a 20 ms debounce deadline. The existing
kernel tick is 5 ms; captures are converted to milliseconds accordingly.

## Fixed steps per round

The pure-C sequence decoder counts accepted half-cycles between CLK/DT
states 00 and 11. The startup position is zero. The service computes:

```text
round_index = net_accepted_steps / VAULT_ENCODER_STEPS_PER_ROUND
```

Division truncates toward zero. For a setting of 40:

| Net steps | Round index | New event at this boundary |
| --- | --- | --- |
| 0..39 | 0 | None |
| 40 | 1 | EV_DIAL, value +1 |
| 80 | 2 | EV_DIAL, value +1 |
| 79, after reversing from 80 | 1 | EV_DIAL, value -1 |
| -1..-39 | 0 | None |
| -40 | -1 | EV_DIAL, value -1 |
| -39, after reversing from -40 | 0 | EV_DIAL, value +1 |

An event represents **a change in the integer round index**, not necessarily
another complete revolution since the preceding event. Reversing one step
back across a boundary reverses its event. The index is signed, not an
unsigned accumulated travel distance. There is no automatic reset at a
button press or game-state transition; the reference remains startup.

To measure calibration, inspect `encoder_steps` in a diagnostic snapshot:

```c
VaultSensorStats snapshot;
vault_sensors_get_stats(&snapshot); /* From a running task, not an ISR. */
```

Record its value, make one deliberate physical revolution, and use the
absolute difference as a candidate STEPS_PER_ROUND. Repeat both directions
and across several revolutions before settling on a value. Because the
encoder has been observed to miss changes, this is an estimate; equal
physical clockwise/anticlockwise revolutions may still leave an offset.
The macro scales accepted counts; it cannot reconstruct missed movement.

## Event routing

All processed sensor events use the **existing `xInputQueue`** and unchanged
`VaultEvent` layout:

| Sensor | Event | value | tick_ms |
| --- | --- | --- | --- |
| Tap | EV_KNOCK | 0 | Original active-edge capture time |
| Encoder | EV_DIAL | +1 or -1 round-index change | Capture completing the boundary crossing |
| Encoder button | EV_PRESS | 0 | Edge beginning the confirmed pressed level |
| LDR | EV_BEAM_BROKEN | 0 | Confirmed detection time |

The existing EV_DIAL payload remains a delta. Only its physical unit changes
from one encoder step to one calibrated round-index change. The existing
FSM therefore advances/reverses `current_digit` once per round event and
still wraps 0..9. Threshold/other gameplay changes remain Person 1's FSM
responsibility. Sensor processing never writes `gGame`.

```text
Tap / CLK / DT / SW -> BSP callback -> private raw queue -> tInput --+
                                                                  +-> xInputQueue -> tGame
LDR ADC ---------------------------> tTripwire --------------------+
```

The private queue is internal ISR-to-task transport, not a second game
input interface. The ISR copies input identity, pin levels and capture tick;
it does not print, debounce with delays, or access the game mutex. tInput
uses the captured CLK/DT snapshots rather than rereading historical edges.
It blocks while publishing to the game queue to preserve event ordering.
If raw capture overflows, a diagnostic counts the loss and decoding
resynchronizes rather than extending a sequence through missing states.

## Tasks, startup and queue protection

- Main initializes the BSP with interrupts disabled and creates the existing
  queues/mutex, then calls `vault_sensors_init()` to create the raw queue.
- Main creates `vault_input_task` at priority 3 alongside tGame. The input
  task enables edges only after the scheduler is running and the initial
  encoder/button levels have been captured.
- The existing tTripwire entry point delegates to `vault_tripwire_task`,
  priority 4, with `vTaskDelayUntil()` at a 50 ms period. It polls the ADC
  through start/try_read, delaying while conversion completes and cancelling
  after a bounded deadline.
- tTripwire reads armed/session information under the existing xStateMutex,
  releases it before ADC work, then applies the measured threshold and
  hysteresis. It requires consecutive valid blocked samples. ADC errors
  reset the consecutive count and never count as darkness.
- A confirmed break is latched to avoid flooding xInputQueue. If the game
  queue is full, the pending break is retried next period. Disarm/session
  changes discard pending detections; state is rechecked after ADC work.
- tGame remains the sole owner of FSM transitions. No BSP interface,
  UART packet interface, GameState field layout or shared queue name changed.

## Hardware mode versus synthetic self-test

`VAULT_SELF_TEST=0` in `source/CG2271_Project.c` is now the default. Real
sensor services run and the old fake-event task is not created. With
`VAULT_SELF_TEST=1`, the original fake FSM demo runs without physical input
capture or real beam-event generation, so synthetic and physical inputs
do not mix. It can also be selected with a compiler definition.

`VAULT_LOG_INPUT_EVENTS=1` prints events **from tGame**, outside its state
mutex, using explicit signs to avoid the SDK negative-number formatting
issue. Example:

```text
[input] EV_DIAL value=+1 captured_ms=12345
[input] EV_DIAL value=-1 captured_ms=13000
[input] EV_PRESS value=+0 captured_ms=14000
```

Without Person 3's ARM command, the game remains IDLE: events still reach
tGame and can be logged, but the FSM intentionally ignores dial/knock input
outside the applicable states. The remote communication and actuator tasks
are still stubs; no UART implementation or output-pattern service was added.

## Diagnostics and regression tests

`vault_sensors_get_stats()` returns accepted encoder steps/rounds, invalid
states/bounce returns, raw queue losses, total sent events, beam queue retries,
ADC errors, latest raw LDR value/validity and service readiness flags.
Raw ADC data is available here, not as an invented new game event type.

The host test in `tests/test_sensor_logic.c` exercises the production pure-C
logic: positive/negative round boundaries, immediate boundary reversal,
partial bounce, invalid transitions, saturation/sign, unchanged FSM delta
handling, and beam hysteresis/latching. Compile it with a host C compiler
alongside `source/vault_sensor_logic.c` and `source/vault_fsm.c`, using
`-Isource`; do not add its main to the MCU project's source folders.

The saved standalone bench example and external backups are preserved.
Physical calibration, captured-edge loss under load and actual hardware
timing still need board tests.
