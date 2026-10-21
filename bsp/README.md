# Vault raw BSP - FRDM-MCXC444

This first version supplies MCU-side GPIO, pin interrupts, ADC and hardware
PWM. It intentionally reports electrical levels/edges and ADC counts. It does
not yet interpret knocks, encoder detents, button presses or beam breaks.
No UART driver, game FSM, FreeRTOS task, queue, debounce or tune player is
created here. The application owns those policies and resources.

## MCU connection and configuration table

The header numbers below are **physical numbered header contacts**, not the
Arduino D-number. Locate the pin-1 marking using the NXP header diagram; do
not count pins by assuming a viewing orientation. Supply wiring at the
component end is outside this table.

| Connection to module | MCU header | MCU pin | MCU function | Initial configuration |
| --- | --- | --- | --- | --- |
| Tap signal | J1-5 | PTC0 | GPIO input, PORTC IRQ | ALT1, no pull, IRQ disabled |
| Encoder CLK | J1-7 | PTC4 | GPIO input, PORTC IRQ | ALT1, no pull, IRQ disabled |
| Encoder DT | J1-9 | PTC6 | GPIO input, PORTC IRQ | ALT1, no pull, IRQ disabled |
| Encoder SW | J1-11 | PTC7 | GPIO input, PORTC IRQ | ALT1, no pull, IRQ disabled |
| Photoresistor analog signal | J4-2 / A0 | PTB0 | ADC0_SE8 | ALT0, 12-bit, long sampling |
| External RGB red (observed colour) | J4-10 / A4 | PTC2 | TPM0_CH1 | ALT4, active-low, 1 kHz, initially off |
| External RGB green (observed colour) | J4-12 / A5 | PTC1 | TPM0_CH0 | ALT4, active-low, 1 kHz, initially off |
| External RGB B | J2-4 / D9 | PTD2 | TPM0_CH2 | ALT4, 1 kHz, initially off |
| Passive buzzer signal | J1-8 / D3 | PTA12 | TPM1_CH0 | ALT1 low while silent; ALT3 for PWM |
| Relay control | J2-8 / D11 | PTD6 | GPIO output | ALT1, logically off |
| Optional laser logic control | J2-10 / D12 | PTD7 | GPIO output when enabled | Default is input, no pull |
| **Reserved: Person 3 UART2 TX** | **J4-5** | **PTE22** | **UART2_TX ALT4** | **Not configured by BSP** |
| **Reserved: Person 3 UART2 RX** | **J4-7** | **PTE23** | **UART2_RX ALT4** | **Not configured by BSP** |

Board power contacts: J3-8 is 3V3_OUT, J3-10 is 5V0_OUT, and J3-12/J3-14
are GND. These identify board rails only, not which rail each module needs.
All signal voltages must meet the MCU pin's electrical limits; the ADC input
must stay between VREFL and VREFH (normally 0..3.3 V on this board).

```text
FRDM-MCXC444                       Module-side signal
  J1-5   PTC0 -------------------- Tap
  J1-7   PTC4 -------------------- Encoder CLK
  J1-9   PTC6 -------------------- Encoder DT
  J1-11  PTC7 -------------------- Encoder SW
  J4-2   PTB0 -------------------- Photoresistor analog
  J4-12  PTC1 -------------------- RGB green (observed colour)
  J4-10  PTC2 -------------------- RGB red (observed colour)
  J2-4   PTD2 -------------------- RGB B
  J1-8   PTA12 ------------------- Buzzer control
  J2-8   PTD6 -------------------- Relay control
  J2-10  PTD7 - - - - - - - - - - Optional laser logic control
  GND --------------------------- Common signal ground

  J4-5   PTE22 ================== Person 3: UART2 TX
  J4-7   PTE23 ================== Person 3: UART2 RX
```

PTD7 is a logic output only. If the laser is powered/switched solely through
relay contacts, leave PTD7 unconnected and use bsp_relay_set(). If the module
has a verified logic-enable input, enable PTD7 in BspOutputConfig. This BSP
does not establish which physical module pin is power or signal.

## Resource ownership

- PORTC pins 0/4/6/7: tap and encoder. All support the shared
  PORTC_PORTD interrupt vector. PORTB and PORTE do not have GPIO IRQ vectors
  on this device, so they were not chosen for edge inputs.
- ADC0: exclusive to the light driver. One task owns start/read/cancel.
- TPM0: exclusive to RGB, fixed 1 kHz. TPM1: exclusive to buzzer, variable
  pitch. TPM2 is unused. The shared TPM clock source is IRC48M (48 MHz).
  A future user of TPM2 must keep that shared source selection.
- GPIOC 1/2 and GPIOD 2/6/7, GPIOA 12: BSP outputs as listed above.
- PTE22/PTE23 and UART2 are untouched. PTA1/PTA2 debug LPUART0 and
  PTA0/PTA3 SWD are untouched. Existing on-board RGB pins PTE29/PTE31/PTD5
  and board button pins PTA4/PTC3 are not used.
- Some selected pins also have LCD or expansion-header functions. Do not
  initialize SLCD or another expansion peripheral on these same pins.

The BSP enables port/peripheral clock gates and selects only its own pin
muxes. It does not change core/bus dividers or UART source selection.
Do not run generated pin/peripheral initialization again after bsp_init().

## Files and build

| File | Purpose |
| --- | --- |
| bsp.h / bsp.c | Aggregate startup initialization |
| bsp_board.h | Fixed MCU pin/resource allocation and polling budgets |
| bsp_types.h | Shared return codes |
| bsp_inputs.h / .c | Raw digital reads, configurable pulls/edges, ISR callback |
| bsp_light.h / .c | Raw ADC counts and bounded startup self-calibration |
| bsp_outputs.h / .c | RGB PWM, buzzer PWM, raw/logical relay and laser writes |
| examples/raw_input_queue.c.example | Optional application-side FreeRTOS adapter |

Debug and Release .cproject source/include paths include bsp/. The example
has a .example suffix so it is not compiled or run automatically. The current
application main runs independently selected tests, currently relay and LDR,
with tap/encoder handling retained. If MCUXpresso was
already open, refresh/reload the project so it sees the updated build settings.

Validation on 20 October 2026: full MCUXpresso Debug compile/link passed with
0 errors and 0 warnings. All four BSP translation units also passed direct
Cortex-M0+ compilation with -Wall -Wextra -Werror -Wconversion at -O2. The
FreeRTOS adapter example passed a syntax check against this project's kernel
headers. These checks do not execute the drivers on hardware. The application
is a component test; its image size is not the size of a fully integrated vault.

## Current main: relay test

A relay is an electrically operated switch. The module's signal input
controls its coil/driver, and the contacts switch a separate circuit.
For the vault, those contacts can switch laser power independently of the
MCU's GPIO current capability.

`RELAY_TEST_ENABLED=1` currently enables a repeated logical OFF/ON cycle.
`RELAY_TEST_ACTIVE_LOW=0` assumes HIGH energizes the module; change it to 1
if testing shows that LOW energizes it. The GPIO signal is on
**PTD6 / J2 pin 8**. Ground connects to **J3 pin 12 or 14 / GND**.
Power the module's verified VCC pin at its specified module supply voltage.
J3-8 provides 3.3 V and J3-10 provides 5 V; a module that needs 5 V coil power
must still have an input circuit suitable for the MCU's 3.3 V logic. Do not
connect a bare relay coil directly to the GPIO.

First test with **nothing attached to the relay contact terminals**:

1. Power off the board, connect the module's signal, ground and verified
   supply pins, then power it on and flash the Debug image.
2. At 115200 baud, 8N1, watch the relay messages alternate approximately
   every 2 seconds. Listen for a click at each state change. An indicator
   LED alone does not establish that the coil or contacts have switched.
3. For a module labelled COM/NO/NC, use a continuity meter on its unloaded
   contacts: with the coil de-energized COM-NC is closed and COM-NO is open;
   energizing the coil reverses these connections. Determine terminal
   positions from their actual labels, not a guessed left-to-right order.
4. Confirm that logical ON corresponds to the coil being energized; update
   `RELAY_TEST_ACTIVE_LOW` if necessary. Test a low-voltage load only after
   the unloaded contact test succeeds.

```text
RELAY requested=OFF, signal=LOW, active_low=0
RELAY requested=ON, signal=HIGH, active_low=0
```

The messages report commands, not physical contact feedback. No load or
laser supply is switched by this test unless you wire it to the contacts.
`bsp_relay_set(bool on)` is the polarity-aware API; it returns `BspStatus`.
`bsp_relay_write_level(bool high)` instead requests a raw electrical level.

Existing settings are preserved: laser test 0, LDR test 1, buzzer/RGB tests 0,
sensor prints 0. Set `LDR_TEST_ENABLED=0` if you want a relay-only console.
Set `RELAY_TEST_ENABLED=0` to disable relay cycling on the next build/reset;
it is initialized logically inactive if another output test initializes the
output driver. Hold periods use 2000 main-loop samples, so actual time
includes loop and console overhead.

## Independent laser control and LDR tests

`LASER_TEST_ENABLED` and `LDR_TEST_ENABLED` independently select the tests.
Current settings are **laser 0, LDR 1**. Existing user settings
`BUZZER_TEST_ENABLED=0`, `RGB_TEST_ENABLED=0` and `SENSOR_TEST_PRINTS=0` are
preserved. Tap/encoder processing remains active; buzzer and RGB test code
remain available but do not run.

| `LASER_TEST_ENABLED` | `LDR_TEST_ENABLED` | Behaviour |
| --- | --- | --- |
| `0` | `0` | Neither test runs; no ADC initialization or laser output enabled |
| `1` | `0` | Toggle laser control only; no ADC initialization or LDR prints |
| `0` | `1` | Sample LDR only; laser is not controlled by this test |
| `1` | `1` | Toggle laser and sample LDR together |

When laser testing is disabled, LDR messages report
`laser_requested=UNCONTROLLED (laser test disabled)` rather than claiming
the physical laser is OFF. You can use ambient light or an independently
powered beam. If other output tests initialize the output driver, the
disabled laser pin is configured as an input; otherwise it is untouched.
Both switches can also be overridden with compiler `-D` definitions.

| Connection | Physical MCU header | Configuration |
| --- | --- | --- |
| LDR analog output (`S` / AO) | **J4 pin 2 / PTB0 / A0** | ADC0_SE8, 12-bit, ALT0 |
| LDR VCC | **J3 pin 8 / 3.3 V**, for a compatible module | Keeps its analog output within the board's ADC supply range |
| LDR ground | **J3 pin 12 or 14 / GND** | Common ground |
| Laser logic-enable input, if present | **J2 pin 10 / PTD7** | GPIO output, ALT1, initially inactive |
| Laser ground | **J3 pin 12 or 14 / GND** | Common signal ground |

The laser GPIO is a **logic control**, not a laser power source. Establish
the laser module's power/control pin functions separately; if its `S` pin
is the power input, switch that supply through suitable relay/driver hardware
instead of powering it from PTD7. The laser's supply voltage must match the
actual module. LDR analog input must remain within VREFL..VREFH, normally
0..3.3 V; do not feed a 5 V analog output into PTB0.

Laser testing initializes the output driver with laser GPIO enabled. LDR
testing independently initializes and self-calibrates the ADC.
`LASER_TEST_ACTIVE_LOW=0` currently
assumes HIGH means laser enabled; set it to 1 if the verified control input
is active-low. The laser is requested ON initially, then alternates ON/OFF
every 3000 loop samples (approximately 3 seconds plus loop/console overhead).

The LDR starts a nonblocking ADC conversion approximately every 50 loop
samples and checks completion on subsequent iterations. A conversion still
pending after 10 checks is cancelled and reported; sample errors are not
reported as zero readings. Samples spanning a laser toggle are discarded.
Each successful sample prints a raw count and the requested laser state:

```text
LASER requested=ON, active_low=0
LDR raw=1234/4095, laser_requested=ON
...
LASER requested=OFF
```

The number shown is illustrative, not a measured threshold. `requested`
records the GPIO command, not optical feedback or proof that the laser is
on. Align the beam with the LDR and compare readings with the beam present,
blocked and requested OFF. No brighter/darker direction, beam threshold or
alarm decision is assumed. ADC startup failures are printed and, if laser
testing is enabled, the laser is requested OFF. Serial settings remain
**115200 baud, 8N1**. All four switch combinations passed strict ARM syntax
checks; the default both-enabled Debug build was also checked.

## Retained passive buzzer test

Current switches in `source/cg2271_project_mcu.c`:

| Switch | Value | Effect |
| --- | --- | --- |
| `RELAY_TEST_ENABLED` | `1` | Toggle relay control independently |
| `LASER_TEST_ENABLED` | `0` | Laser control test retained, currently disabled |
| `LDR_TEST_ENABLED` | `1` | Initialize ADC and report raw LDR readings independently |
| `BUZZER_TEST_ENABLED` | `0` | Buzzer test retained, currently silent |
| `RGB_TEST_ENABLED` | `0` | Temporarily compile out RGB cycling and its prints; code is retained |
| `SENSOR_TEST_PRINTS` | `0` | Keep tap/encoder handling active with periodic sensor prints disabled |

MCU-side buzzer connections:

| Buzzer connection | Board connection |
| --- | --- |
| Signal/control (`S` if confirmed) | **PTA12 / J1 pin 8**, TPM1_CH0 ALT3 |
| Ground | **GND / J3 pin 12 or 14** |
| Module VCC, if required | **3.3 V / J3 pin 8**, if the module supports 3.3 V |

Identify the module's actual power pins separately; the unlabelled pin is
not established as VCC by this software. Header pin numbers are physical
contacts, not Arduino D-numbers.

When enabled, the buzzer pattern repeats: **500 Hz for ~500 ms -> silence ~250 ms -> 1000 Hz
for ~500 ms -> silence ~250 ms -> 2000 Hz for ~500 ms -> silence ~1000 ms**.
Each tone uses 50% duty through `bsp_buzzer_set_frequency()`; zero frequency
silences it and drives the signal LOW. Hold times count main-loop samples,
so actual times include console and loop overhead. Console settings remain
**115200 baud, 8N1**. Expected output includes:

```text
Component test boot: debug console ready.
Output initialization complete; RGB starts OFF.
BUZZER 500 Hz, 50% duty (~500 ms)
BUZZER OFF (~250 ms)
BUZZER 1000 Hz, 50% duty (~500 ms)
```

TPM1 generates tones in hardware while the main loop continues sampling the
button and servicing tap/encoder interrupts. No FreeRTOS scheduler is started.
Output initialization also establishes the tested common-positive RGB's OFF
state and relay inactive; the laser is controlled by its separate test switch. RGB cycling does
not run. Restore `RGB_TEST_ENABLED=1` to enable its saved test. Set
`BUZZER_TEST_ENABLED=0` to disable the buzzer test on the next build/reset.

## Retained tap, rotary encoder and RGB LED tests

`source/cg2271_project_mcu.c` initializes the board/debug console, digital
inputs and the selected RGB test mode. Run the Debug image and open the MCU-Link debug serial port at
**115200 baud, 8 data bits, no parity, 1 stop bit**.

Connect the tap signal to **PTC0 / J1 pin 5**. The tap input uses no internal
pull. Encoder **CLK = PTC4 / J1-7**, **DT = PTC6 / J1-9**, and
**SW = PTC7 / J1-11** use internal pull-ups. The loop samples inputs with a
1 ms busy delay and, when `SENSOR_TEST_PRINTS=1`, prints both devices every 100 iterations (approximately
100 ms plus console/sampling overhead):

```text
TAP S=0 (LOW), raw edges=0
ENC position=+0, switch=RELEASED
```

RGB connections below describe the **existing wiring**, kept unchanged after
testing revealed common-positive operation and reversed red/green channels:

| Existing module lead | MCU connection | Observed function |
| --- | --- | --- |
| Common lead labelled `-` | 3.3 V / J3-8 | Common-positive supply |
| Lead originally connected as `R` | PTC1 / J4-12 | Green |
| Lead originally connected as `G` | PTC2 / J4-10 | Red |
| `B` | PTD2 / J2-4 | Blue |

The software now maps logical red to PTC2/TPM0_CH1 and logical green to
PTC1/TPM0_CH0 in both GPIO and PWM modes. Keep the current physical wires;
do not also swap them, or the colours will reverse again. This mapping
records the observed assembly; it does not establish whether the mismatch
originated in module labels or wiring.

The common lead is shared by all three colours; its polarity depends on the
module type. Each colour needs current limiting;
use the module's built-in resistors if present, otherwise add series
resistors. The current test uses **active-low** colour outputs based on the
observed common-positive operation: a LOW output lights the corresponding
colour and a HIGH output turns it off.
Use only 3.3 V for this common-anode GPIO test, and do not connect board GND
to the positive supply.

When `RGB_TEST_ENABLED=1`, `RGB_TEST_USE_GPIO=1` bypasses the PWM driver and drives each pad
directly HIGH/LOW. `RGB_TEST_ACTIVE_LOW=1` selects common-anode behaviour:
LOW turns a colour on and HIGH turns it off. Set ACTIVE_LOW to 0 for a
confirmed common-cathode module with its common lead grounded. Restore
USE_GPIO to 0 after testing to run the PWM path with the same polarity.

The LED repeats **RED -> GREEN -> BLUE -> YELLOW -> CYAN -> MAGENTA ->
WHITE -> OFF**. It holds each colour for 1000 main-loop iterations
(approximately one second plus console/sampling overhead). Each enabled
channel uses brightness **64/255** in PWM mode, controlled by
`RGB_TEST_BRIGHTNESS`. GPIO mode treats any nonzero value as fully on,
so it does not provide brightness control. Its console output includes the
colour name and measured pad levels, for example active-low red should show
`RGB GPIO RED: pad_R=0 pad_G=1 pad_B=1 (active_low=1)`.
For the current component testing session, `SENSOR_TEST_PRINTS` in main is **0**:
tap/encoder startup banners and periodic TAP/ENC/DBG lines are temporarily
disabled. Set it to **1** to restore them. Sensor handling remains active,
and initialization/read errors are still reported. Two boot messages show
whether execution reaches the debug console and completes the selected init:
`Component test boot: debug console ready.`, followed by the output init
message when buzzer/PWM is enabled, or the GPIO diagnostic banner when that
RGB test is enabled.
TPM0 generates the PWM in hardware when PWM mode is selected. The colour cycle adds no long delay,
so tap interrupts, encoder decoding and switch sampling keep running.
If the observed primary colours do not match their console labels, check
the R/G/B signal connections. RGB GPIO-only mode with the buzzer test disabled
configures only RGB outputs. Enabling buzzer or RGB PWM initializes the
output drivers together through bsp_outputs_init().

Both-edge interrupts count signal transitions between prints, so a short
pulse can increase `raw edges` even when every printed sample shows the
idle level. The count includes bounce and is not a count of valid knocks.

The application-level encoder decoder interrupts on **both edges of CLK and
DT** and validates the order of adjacent quadrature states. A completed
half-cycle from 11 to 00 or from 00 to 11 produces one signed step:

```text
11 -> 01 -> 00  : +1       00 -> 10 -> 11 : +1
11 -> 10 -> 00  : -1       00 -> 01 -> 11 : -1
11 -> 01 -> 11  :  0       (partial movement/contact bounce cancels)
```

State notation is `(CLK, DT)`. Duplicate snapshots are ignored. A jump
where both bits change at once is ambiguous, so it is discarded and the
decoder resynchronizes at 00 or 11. There is no fixed time lockout or PIT
use in this version. A complete valid sequence caused by noise can still
look like real movement; physical testing remains necessary. The startup
banner identifies this as **Encoder v2**. The knob's position at startup is the arbitrary
center, assigned `ENCODER_CENTER_POSITION` (default 0). `position` is a signed
step count relative to that reference, not an absolute angle. Equal numbers
of accepted positive and negative steps cancel, but equal physical rotations
are not guaranteed to produce equal accepted counts. Pressing SW does not
reset the reference. Verify which physical direction gives `+`; change the
sign later if required. The count is one valid half-cycle; verify the
relationship to physical detents. Encoders whose detents occur only at 11
can report two such steps per detent.

### Observed encoder accuracy and suggested gameplay

Physical testing shows that the rotary encoder is **not entirely accurate**:
some movement is not recorded, so a physical click does not always change
the reported position. The reported count is the game's input value, not
a precise measurement of the knob's physical angle or number of turns.

Suggested gameplay:

- Prefer threshold objectives such as **rotate until position > x** or
  **rotate until position < x**. The player can continue turning until the
  reported value meets the condition.
- An exact-value objective such as **rotate to position == x** is also
  possible. Show the current reported value so the player can adjust and
  optionally press SW to confirm; do not require a fixed number of clicks
  or a particular physical knob orientation to reach it.
- Do not expect one clockwise revolution followed by one anticlockwise
  revolution to restore the original reported value. Missed changes can
  differ between the two directions, leaving a count offset even when the
  knob returns to its original physical orientation.

`balance=OK` verifies the arithmetic of accepted software steps only. It
does not prove that all physical movement was captured.

SW is assumed active-low. The loop accepts a new button level after 20
consecutive stable samples. This is a sample-count debounce, not an exact
20 ms timer. The displayed `switch=PRESSED/RELEASED` is the debounced state.
The `ENC` line shows position and switch state. An additional `DBG` line
prints current raw CLK/DT/SW, `clk_edges`/`dt_edges` (raw notifications),
`accepted_plus`/`accepted_minus` (completed signed steps), `invalid`
(ambiguous two-bit jumps), `bounce` (returns to an endpoint without a net
step), raw switch edges and last direction. `irq_CLK`/`irq_DT` are the latest
CLK/DT callback snapshot, not the current polled levels. `balance=OK` checks
that position equals center + accepted_plus - accepted_minus, modulo the
32-bit counter range. Counters are cumulative since startup. Compare their
changes during several clicks in each direction; the tap output is unchanged.

Two implementation fixes accompany v2: the Redlib header defines `INT32_MIN`
as `(-0x80000000)`, which is unsigned under this compiler and caused the old
decrement guard to reject nonnegative positions. The test now uses an
explicitly signed lower bound with a compile-time assertion. The minimal
SDK console formatter also mishandles negative `%d`, so position is printed
using an explicit sign and unsigned magnitude, and direction as a string.

No printing occurs in the ISR. This standalone test uses busy delays in
main and does not start FreeRTOS. ADC initialization is selected by
`LDR_TEST_ENABLED`. Enabled test switches
select which output is exercised. Change the
tap's `BSP_PULL_NONE` only when its required input pull is established.
The raw BSP interfaces are unchanged; the decoder is only in the bench test.

The v2 Debug build passed with 0 errors/warnings. Host checks covered both
directions, partial bounce in both directions, partial returns, reversal,
ambiguous jumps, bounds, duplicate snapshots and tap/button handling. A
regression matching the screenshot's 110 positive and 127 negative steps
produces position -17. Physical direction, scaling and noise rejection
still require testing on the connected encoder.

## Startup

Run once, before starting the scheduler. All public hardware functions must
be called after their corresponding initialization succeeds.

```c
#include "bsp.h"

/* After the existing BOARD_InitBootPins(), BOARD_InitBootClocks(),
 * BOARD_InitBootPeripherals() and optional debug-console initialization: */
BspOutputConfig outputs;
bsp_outputs_default_config(&outputs);
/* outputs.relay_active_low = true;   // Only if verified for your module. */
/* outputs.enable_laser_signal = true; // Only for a logic-control input. */
BspStatus status = bsp_init(&outputs);
if (status != BSP_OK) {
    /* Report the init failure; do not assume ADC readings are available. */
}
```

Defaults: tested external RGB active-low (common positive), relay active-high, optional
laser signal disabled. Output init requests RGB off, buzzer silent and relay
off according to the configured polarity. It cannot guarantee a relay's
physical startup state before firmware runs, or if polarity is configured
incorrectly. Logical relay commands do not report contact feedback.

bsp_init() initializes outputs, inputs, then ADC. If ADC self-calibration
fails, the output/input drivers remain usable and ADC calls return
BSP_NOT_READY. Individual init functions are exposed for component tests.
ADC self-calibration is electrical gain calibration, not beam calibration.

## Sensor API reference

Include `bsp.h` to use all sensor functions, or include `bsp_inputs.h` and
`bsp_light.h` separately. Functions return a status separately from the
measurement: for example, `bsp_light_read_raw(&raw)` returns `BspStatus` and
writes the ADC reading into `raw` only on success.

### Tap and encoder - shared digital input API

Tap, encoder CLK, encoder DT and encoder SW use the same functions. Select
the desired signal with a `BspInput` argument:

| `BspInput` value | Device signal | Raw value meaning |
| --- | --- | --- |
| `BSP_INPUT_TAP` | Tap output | `true` = electrical HIGH; `false` = LOW |
| `BSP_INPUT_ENCODER_CLK` | Encoder CLK | Current CLK level |
| `BSP_INPUT_ENCODER_DT` | Encoder DT | Current DT level |
| `BSP_INPUT_ENCODER_SW` | Encoder push-button SW | Current SW level; pressed polarity is not assumed |

| Function | Return type | Arguments | Short explanation |
| --- | --- | --- | --- |
| `bsp_inputs_init` | `void` | `void` (none) | Startup initialization for all four digital inputs. Sets no pulls, disables their edge sources and removes any registered callback. Already called by `bsp_init()`. |
| `bsp_inputs_read` | `BspStatus` | `BspInput input`: signal to read; `bool *high`: non-NULL output pointer | Reads one raw electrical level. Writes `*high` on `BSP_OK`; returns `BSP_INVALID_ARGUMENT` for an invalid input/pointer or `BSP_NOT_READY` before initialization. |
| `bsp_inputs_read_all` | `BspStatus` | `BspInputLevels *levels`: non-NULL output pointer | Reads all four levels in one port snapshot. Writes `tap`, `clk`, `dt` and `sw` on `BSP_OK`; returns `BSP_INVALID_ARGUMENT` for NULL or `BSP_NOT_READY` before initialization. |
| `bsp_inputs_set_callback` | `void` | `BspInputCallback callback`: ISR callback or NULL; `void *context`: caller-owned pointer forwarded to the callback | Registers one callback shared by all four inputs. Register after initialization and before enabling edges. Passing NULL disables all four edge sources and unregisters the callback. Task context only. |
| `bsp_inputs_configure` | `BspStatus` | `BspInput input`: signal; `BspPull pull`: pull configuration; `BspEdge edge`: edge configuration | Sets one input's pull and interrupt edge, clearing its pending flag. Returns `BSP_OK`, `BSP_INVALID_ARGUMENT`, or `BSP_NOT_READY` if uninitialized or an enabled edge has no callback. Task context only. |
| `bsp_inputs_irq_dispatch` | `void` | `void` (none) | ISR integration function: clears this BSP's pending input flags and invokes the registered callback for each flagged signal. The default IRQ handler already calls it; application tasks should not call it. |

`BspInputLevels` contains four `bool` fields: `tap`, `clk`, `dt` and `sw`.
No field represents a decoded knock, direction, detent or debounced press.
Read functions leave their output arguments unchanged on failure.

Configuration argument values:

| Argument type | Allowed values | Meaning |
| --- | --- | --- |
| `BspPull` | `BSP_PULL_NONE`, `BSP_PULL_UP`, `BSP_PULL_DOWN` | Disable the internal pull, enable pull-up, or enable pull-down |
| `BspEdge` | `BSP_EDGE_DISABLED`, `BSP_EDGE_RISING`, `BSP_EDGE_FALLING`, `BSP_EDGE_BOTH` | Disable the input's interrupt, capture LOW-to-HIGH, capture HIGH-to-LOW, or capture both transitions |

The callback type is:

```c
typedef void (*BspInputCallback)(BspInput input,
                                 const BspInputLevels *levels,
                                 void *context);
```

| Callback item | Type | Meaning |
| --- | --- | --- |
| Return value | `void` | The application callback returns nothing. |
| `input` | `BspInput` | Identifies the signal whose pending flag is being reported. |
| `levels` | `const BspInputLevels *` | Current snapshot of all four inputs. Copy the data if needed; the pointer is valid only during the callback. |
| `context` | `void *` | The same pointer passed to `bsp_inputs_set_callback()`. Its pointed-to data must remain valid while registered. |

The BSP invokes this callback in **ISR context**. Capture timestamps and
queue copies there using ISR-safe APIs; do not block or perform debounce
delays. The callback does not receive a built-in timestamp or decoded event.

### Photoresistor - raw ADC API

The ADC interface returns a **12-bit raw count, 0..4095**. It does not return
volts, lux or a beam-broken flag. Higher/lower readings must be interpreted
after measuring the actual sensor circuit.

| Function | Return type | Arguments | Short explanation |
| --- | --- | --- | --- |
| `bsp_light_init` | `BspStatus` | `void` (none) | Startup initialization of ADC0 for the photoresistor, including bounded ADC self-calibration. Already called by `bsp_init()`. Returns `BSP_OK`, `BSP_INVALID_ARGUMENT` for an unsupported bus clock, `BSP_TIMEOUT`, or `BSP_CALIBRATION_FAILED`. |
| `bsp_light_start` | `BspStatus` | `void` (none) | Starts one conversion and returns immediately. Returns `BSP_OK`, `BSP_BUSY` if an earlier sample is outstanding, or `BSP_NOT_READY` if unavailable. |
| `bsp_light_try_read` | `BspStatus` | `uint16_t *raw`: non-NULL output pointer | Checks an outstanding conversion without waiting. On `BSP_OK`, writes the count and consumes the sample. Returns `BSP_BUSY` while converting, `BSP_NOT_READY` if unavailable/no sample was started, or `BSP_INVALID_ARGUMENT` for NULL. |
| `bsp_light_cancel` | `void` | `void` (none) | Aborts/discards an outstanding sample, allowing another start. Does nothing if the driver is unavailable. |
| `bsp_light_read_raw` | `BspStatus` | `uint16_t *raw`: non-NULL output pointer | Convenience start-and-read function for individual hardware tests. Uses bounded CPU polling. Returns `BSP_OK`, `BSP_INVALID_ARGUMENT`, `BSP_NOT_READY`, `BSP_BUSY`, or `BSP_TIMEOUT`; a timeout cancels the conversion. |

Both read functions leave `*raw` unchanged on failure. All ADC functions
are startup/task-context functions, not ISR functions. Use one ADC owner
task. For RTOS sampling, prefer `bsp_light_start()` followed by delayed
`bsp_light_try_read()` checks and an application deadline; call
`bsp_light_cancel()` if that deadline expires. The convenience read's
polling budget is in CPU loop iterations, not milliseconds.

## Raw sensor reads

```c
BspInputLevels levels;
if (bsp_inputs_read_all(&levels) == BSP_OK) {
    /* levels.tap, levels.clk, levels.dt, levels.sw are electrical HIGH/LOW. */
}
bool high;
BspStatus s = bsp_inputs_read(BSP_INPUT_TAP, &high);

uint16_t raw;
s = bsp_light_read_raw(&raw); /* Short, bounded busy-poll for bench tests. */
if (s == BSP_OK) {
    /* raw is 0..4095. Neither "light present" nor "beam broken" is assumed. */
}
```

For a periodic RTOS tripwire task, prefer the nonblocking pair:

```c
BspStatus s = bsp_light_start();
if (s == BSP_OK) {
    vTaskDelay(pdMS_TO_TICKS(1) ? pdMS_TO_TICKS(1) : 1);
    s = bsp_light_try_read(&raw);
    if (s == BSP_BUSY) {
        /* Retry with a task delay until an application deadline, then cancel. */
    }
}
```

Do not start again before collecting/cancelling an outstanding conversion.
try_read() leaves its destination unchanged on every error. The BSP does not
lock ADC0: use one owner task, or add a task-level mutex around the complete
start/read/cancel transaction. Never call ADC functions from an ISR.

## Raw input interrupts

1. Create any application queue before enabling interrupts.
2. Register a BspInputCallback with bsp_inputs_set_callback().
3. From a running task, configure each line's pull and desired edge.
4. Capture a timestamp and queue a copy of the raw snapshot in the callback.

```c
bsp_inputs_set_callback(my_isr_callback, my_context);
bsp_inputs_configure(BSP_INPUT_TAP, BSP_PULL_NONE, BSP_EDGE_BOTH);
bsp_inputs_configure(BSP_INPUT_ENCODER_CLK, BSP_PULL_UP, BSP_EDGE_BOTH);
bsp_inputs_configure(BSP_INPUT_ENCODER_DT, BSP_PULL_UP, BSP_EDGE_BOTH);
bsp_inputs_configure(BSP_INPUT_ENCODER_SW, BSP_PULL_UP, BSP_EDGE_BOTH);
```

Those pulls/edges are an **experiment setup**, not measured module facts.
Pulls and edges are independently configurable at runtime. Start with raw
polling if unsure. Both-edge capture permits observing press/release and
quadrature transitions before deciding the final decoder.

Callbacks execute in ISR context, once per flagged input, using one current
level snapshot per dispatch. Multiple edges on one pin can coalesce before
service, and a rapidly changing pin's current level may differ from the
level at the triggering edge. This is GPIO edge reporting, not a lossless
logic analyzer. For precise edge timing beyond RTOS tick resolution, use a
separate timer capture design later.

The callback owns timestamp units, xQueueSendFromISR(), overflow counting,
and portYIELD_FROM_ISR(). It must not block, print, take a mutex, or call
ordinary task-only RTOS APIs. See examples/raw_input_queue.c.example.
Calling bsp_inputs_set_callback(NULL, NULL) disables all BSP edge sources.

The default PORTC_PORTD_IRQHandler dispatches only the four BSP input bits.
If another driver needs PORTC/PORTD interrupts, set
BSP_DEFINE_PORT_IRQ_HANDLER=0 in compiler defines or bsp_board.h, provide
one shared handler, call bsp_inputs_irq_dispatch(), and handle the other
driver's flags there. Do not define two strong handlers for the same vector.
The BSP does not clear unrelated PORTC or any PORTD interrupt flags.

## Raw actuator controls

```c
bsp_rgb_set(255, 0, 0);             /* Red, 0..255 per colour. */
bsp_rgb_set(0, 128, 255);
bsp_buzzer_set_frequency(1000);    /* Hardware PWM, 50% duty. Returns now. */
bsp_buzzer_set_pwm(2000, 250);      /* 25% duty, optional raw control. */
bsp_buzzer_set_frequency(0);       /* Silent, pad driven low. */
bsp_relay_write_level(true);       /* Electrical HIGH regardless of polarity. */
bsp_relay_set(true);               /* Logical ON using configured polarity. */
bsp_relay_set(false);
bsp_outputs_off();
```

No function waits for a tune or flash interval. Assign one application task
to all runtime output changes. Implement patterns as a task state machine
with elapsed time and vTaskDelayUntil(); do not play patterns in an ISR.
RGB and buzzer continue in hardware while the task sleeps. Identical buzzer
requests preserve the running waveform. Frequency changes restart TPM1.

## Return codes

| Result | Meaning |
| --- | --- |
| BSP_OK | Operation completed/request accepted |
| BSP_INVALID_ARGUMENT | Invalid pointer/enum/frequency/duty or unsupported ADC clock |
| BSP_NOT_READY | Driver unavailable, missing callback, disabled laser output, or no ADC sample started |
| BSP_BUSY | Outstanding ADC sample or conversion still running |
| BSP_TIMEOUT | Bounded ADC polling budget exhausted |
| BSP_CALIBRATION_FAILED | ADC hardware reported failed self-calibration |

## First hardware checks

1. Run init with outputs disconnected; inspect status.
2. Read tap/encoder levels and raw ADC counts from a task/debugger.
3. Enable raw interrupts and count captured edges without debounce.
4. Test external RGB channels separately and 0/255 endpoints.
5. Run a buzzer tone, change pitch, verify RGB brightness stays unchanged.
6. Toggle relay electrical levels; establish active polarity, then configure it.
7. Test optional laser logic output only if that interface is applicable.
8. Record bounce, direction, beam-on/blocked values and response timing.

Build checks do not establish electrical behaviour. Debounce, encoder
decoding, beam thresholds, arming readiness, output patterns and game events
are the next layer after these checks.

## References

- Existing board/pin_mux.c includes the generated header and signal mapping.
- [NXP FRDM-MCXC444 Quick Start Guide, header diagram page 3](https://www.nxp.com/docs/en/quick-reference-guide/MCX-C444-QSG.pdf)
- [NXP MCX C44x datasheet, 64-pin signal multiplexing table](https://www.nxp.com/docs/en/data-sheet/MCXC44XP64M48SF6.pdf)
- device/MCXC444_COMMON.h confirms PORTA and shared PORTC/PORTD IRQ vectors.
- device/periph2/PERI_ADC.h, PERI_TPM.h and PERI_SIM.h define registers.
