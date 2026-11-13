# The Vault — CG2271 Assignment 4 Project Plan

> An AI-refereed safe-cracking game. A player must knock a secret rhythm, dial the right combination and avoid tripping the vault's defences, while an LLM game master watches every attempt and taunts or hints accordingly.

This document describes the system as built. Detailed behaviour lives in the guides it links to.

---

## 1. Problem Statement

Build a tamper-aware smart vault that lets an owner arm it remotely with a secret combination, lets a player attempt to crack it through physical inputs, and uses a cloud-based AI to give adaptive hints, log attempts to an online leaderboard, and detect cheating.

---

## 2. Bill of Materials

All core parts come from the provided kit and boards.

### 2.1 Sensors (4)

| # | Part (kit label) | Board | Interface | Read method | Purpose |
|---|---|---|---|---|---|
| S1 | Tap Module | MCXC444 | Digital GPIO | **Interrupt** | Detects the secret knock that wakes the vault |
| S2 | Rotary Encoder (CLK, DT, SW) | MCXC444 | Digital GPIO | **Interrupt** | The safe dial; the push button (SW) confirms each digit |
| S3 | Photo Resistor | MCXC444 | ADC | **Polled** | Detects the lid being opened: ambient light inside the box drops when the lid lifts |
| S4 | GY-521 (MPU-6500 chip) | ESP32 | I2C | **Polled** | Detects the box being shaken or lifted |

### 2.2 Actuators (3)

| # | Part (kit label) | Board | Interface | Purpose |
|---|---|---|---|---|
| A1 | Laser Emit module | MCXC444 | Digital GPIO (PTD7, logic control) | Switched on while the lid is open during an alarm |
| A2 | Passive Buzzer | MCXC444 | PWM (timer) | Beeps, dial ticks, victory tune, alarm siren |
| A3 | RGB LED | MCXC444 | PWM (3 channels) | Status: red when locked, a warm/cold gradient for hints, green when open, flashing red in alarm |

The laser's GPIO is a logic control, not a power source. See [the BSP notes](bsp/README.md) before wiring it.

### 2.3 Supporting parts

| Part | Source | Purpose |
|---|---|---|
| 2 × mini breadboards, jumper wires | Provided | Wiring |
| FRDM-MCXC444, ESP32 + USB cable | Provided | Controllers |
| Box with lift-off lid (shoebox / plastic container) | Own | Enclosure |
| Small prize (e.g. sweets) | Own | Something to win |
| Tape / Blu-Tack, phone hotspot, laptop or phone | Own | Mounting, internet, web page display |

### 2.4 Calibration notes

- **Photoresistor**: on the assembled box the reading is about 225–238 with the lid closed and 94–102 with it open. Three consecutive readings at or below 80 confirm an opening; a reading at or above 180 confirms closure. Remeasure if the sensor placement, wiring or ambient light changes.
- **Encoder**: `VAULT_ENCODER_STEPS_PER_ROUND` sets how many accepted half-cycle steps make one digit change. Edges are debounced in the sequence decoder.
- **Tap module**: debounced at 40 ms; the active edge is configurable in `source/vault_sensor_config.h`.
- **Tamper (MPU)**: thresholds are in [the hint guide](docs/game-master-hints.md) and need a physical check on the assembled box.

---

## 3. Physical Layout

```
                 ┌─────────────── LID ───────────────┐
   Tap sensor →  │   [TAP]  (taped on top of lid)     │
                 └────────────────────────────────────┘
                 ┌────────────────────────────────────┐
                 │ [LDR]  [Laser]       [Buzzer]      │
                 │                                    │
                 │  [MCXC444]  [breadboards] [ESP32]  │
                 │            [IMU flat on floor]     │
  FRONT FACE →   │   (DIAL)   (RGB LED)   [ Prize ]   │ → USB/power cable out the back
                 └────────────────────────────────────┘
```

**Outside**
- **Front face**: the rotary encoder knob through a hole, with the RGB LED beside it.
- **Lid**: the tap sensor, where the player knocks.
- **Back**: the power/USB cable exits.
- **Beside the box**: a laptop or phone showing the web dashboard (AI messages, attempt history, leaderboard).

**Inside**
- **Photoresistor**: positioned so that it sees much more light with the lid closed than with it open, as measured in §2.4.
- **IMU**: fixed flat to the floor, so it moves with the box.
- **Boards and breadboards**: taped down, with wires kept clear.
- **Prize**: in a corner, clear of the wiring.

---

## 4. Gameplay

### 4.1 Game states (MCXC444 FSM)

```
        ARM (from web)               correct knock
 IDLE ─────────────────► LOCKED ─────────────────► DIALING
  ▲                        ▲  ▲                      │  │
  │                        │  │ wrong code           │  │ correct code
  │ DISARM (web)           │  └──────────────────────┘  ▼
  │                        │                         UNLOCKED ── re-arm ──► LOCKED
  │              lockout   │
  │              expires   │
  │                        │
  └─────────────── ALARM ◄─┴── lid open / tamper (from LOCKED or DIALING)
```

| State | LED | Buzzer | Laser | Accepts |
|---|---|---|---|---|
| IDLE | Off | Silent | Off | ARM from web |
| LOCKED | Solid red; the hint colour for 5 s after a wrong guess | Silent | Off | Knocks |
| DIALING | Dim blue | Tick per dial step | Off | Dial turns, button presses |
| UNLOCKED | Green | Victory tune (once) | Off | Re-arm from web |
| ALARM (tamper) | Flashing red | Siren, repeating 1 s on / 2 s off | On only if the lid is also open | Nothing; 10 s lockout (held longer while the lid is open) |
| ALARM (lid open) | Flashing red | Silent | **On** while the lid is open | Nothing; no timed lockout, ends when the lid closes |

DISARM is ignored during ALARM. The exact colours and sounds are in [the actuator guide](docs/actuators.md).

### 4.2 A round

1. **Arm**: the owner enters a 3-digit combination (each digit 0–9) on the web page. The ESP32 sends it to the MCXC444, which moves to LOCKED.
2. **Wake**: the player knocks the secret rhythm: three knocks with gaps of about 300 ms then 900 ms, each within ±30%. On a match the vault beeps and moves to DIALING. A wrong rhythm is rejected, and the MCU reports which gap was too short or too long (`KNOCK_RESULT`) for the dashboard's rhythm card.
3. **Dial**: turning the knob changes the current digit (wrapping 0–9), with a tick on each step. Pressing the knob confirms the digit, and after 3 digits the guess is submitted.
4. **Check (local)**: the MCXC444 compares the guess with the stored code, so the lock decision never depends on the internet.
   - **Wrong**: the LED shows a closeness colour (blue = cold, through yellow, to orange = hot) for 5 s. An ATTEMPT packet goes to the ESP32 with per-digit feedback (correct / elsewhere / absent, see [digit feedback](docs/digit-feedback.md)), and the vault returns to LOCKED, so the player must knock again.
   - **Right**: the vault moves to UNLOCKED, the LED turns green and the victory tune plays. A SOLVED packet goes to the ESP32.
5. **Cloud**: the ESP32 publishes events to Firebase. For each wrong guess it also asks Gemini for a short reaction built around a truthful clue (higher/lower, odd/even); the model never sees the secret code. On a solve, the time and attempt count go to the leaderboard.
6. **Cheating**: opening the lid, or a tamper report from the IMU via the ESP32, while armed moves the vault to ALARM. A tamper alarm sounds the siren and locks out for 10 s. A lid alarm flashes red, lights the laser and lasts until the lid is closed. The event is logged, and the LLM can comment on it.

### 4.3 Knock detection

- The tap ISR callback records the capture time of each tap.
- The game task compares the intervals between taps with the stored pattern, within the tolerance, so the rhythm matters rather than the absolute speed.
- A gap longer than 2 s resets the knock buffer.

---

## 5. Hardware Architecture

### 5.1 Connection overview

```
                          UART (TX/RX, common GND)
   ┌────────────────┐  ◄────────────────────────►  ┌────────────────┐
   │   FRDM-MCXC444 │                              │     ESP32-S2   │
   │                │                              │                │
   │ GPIO-IRQ ◄ Tap │                              │ I2C ◄ IMU      │
   │ GPIO-IRQ ◄ Enc │                              │                │
   │ ADC      ◄ LDR │                              │ Wi-Fi ─► Cloud │
   │ GPIO     ► Lsr │                              │  • Gemini API  │
   │ PWM      ► Bzr │                              │  • Firebase    │
   │ PWM×3    ► RGB │                              │                │
   └────────────────┘                              └────────────────┘
```

The web dashboard (`web/`) is hosted on Firebase and talks to the ESP32 only through the database.

### 5.2 Pin table

| Signal | Board | Pin | Peripheral | Notes |
|---|---|---|---|---|
| Tap | MCXC444 | PTC0 (J1-5) | PORT IRQ | |
| Encoder CLK | MCXC444 | PTC4 (J1-7) | PORT IRQ | |
| Encoder DT | MCXC444 | PTC6 (J1-9) | PORT IRQ | Decoded with CLK |
| Encoder SW | MCXC444 | PTC7 (J1-11) | PORT IRQ | Debounced in software |
| Photoresistor | MCXC444 | PTB0 (J4-2) | ADC0_SE8 | 12-bit |
| Buzzer | MCXC444 | PTA12 (J1-8) | TPM1_CH0 | Frequency sets the pitch |
| RGB R / G / B | MCXC444 | PTC2 / PTC1 / PTD2 | TPM0 channels | Active-low (common-positive LED) |
| Laser logic control | MCXC444 | PTD7 (J2-10) | GPIO output | Optional logic input only |
| UART TX / RX | MCXC444 | PTE0 (J2-18) / PTE1 (J2-20) | LPUART1 | Crossed to the ESP32, common ground |
| IMU SDA / SCL | ESP32 | GPIO8 / GPIO9 | I2C | |
| UART TX / RX | ESP32 | GPIO4 / GPIO5 | UART | GPIO4 to MCU RX, GPIO5 from MCU TX |

The full table with pin configuration is in [the BSP reference](bsp/README.md). ESP32 wiring is in [the ESP32 README](esp32/README.md).

### 5.3 Engineering justification for placement and read methods

- **Encoder → interrupt on the MCXC444**: quadrature edges are short and frequent, so polling risks missing steps, while an edge interrupt captures every one.
- **Tap → interrupt**: a tap is a brief pulse at an unpredictable time, and its timestamp needs to be accurate for rhythm matching.
- **Photoresistor → polled ADC**: light level changes relatively slowly, so sampling every 50 ms is fast enough to catch a lid opening and wastes little CPU.
- **IMU → ESP32 over I2C**: sampled at 20 Hz. The ESP32 drives the chip's registers directly, because the GY-521 carries an MPU-6500 that the usual library rejects. Tamper is less time-critical than the lid sensor, so the UART hop is acceptable.
- **All actuators on the MCXC444**: the lock decision and alarm are local and real-time, and still work if Wi-Fi drops.

---

## 6. Software Architecture (MCXC444, FreeRTOS)

### 6.1 Design principles

- ISRs only capture the event (with a timestamp), push it to a queue with `xQueueSendFromISR`, request a context switch if needed, and return. They do no waiting, no UART transmission and no game logic.
- The game task is the single owner of state transitions; other tasks only read the state or post events.
- Every task blocks (on a queue or with `vTaskDelay`) rather than busy-waiting.
- A single task owns each output: only `tActuator` writes the LED, buzzer and laser during gameplay.

### 6.2 Tasks

| Task | Priority | Blocks on | Responsibility |
|---|---|---|---|
| `tTripwire` | 4 (highest) | `vTaskDelay` (50 ms period) | Reads the ADC; after three consecutive low readings posts the lid-open event, and posts lid-closed on recovery |
| `tGame` | 3 | `xInputQueue` | Runs the FSM: knock matching, digit entry, code check, alarm and lockout timing |
| `tInput` | 3 | Raw edge queue | Decodes tap, encoder and button edges, debounces them, and posts `EV_KNOCK`, `EV_DIAL` and `EV_PRESS` |
| `tComms` | 2 | `xUartRxQueue` | Assembles incoming bytes into packets, verifies the checksum, sends ACK/NAK, turns packets into events for `tGame`, and sends outgoing packets and STATUS |
| `tActuator` | 1 (lowest) | `vTaskDelay` (20 ms) | Drives the LED colour, buzzer patterns and laser from the latest published state and cue queue |

**Why this order**: a tamper-safety response must pre-empt everything; player input should feel responsive; comms can tolerate a few ms of delay; the visual and audio patterns are the least time-critical.

### 6.3 ISRs

| ISR | Trigger | Action |
|---|---|---|
| GPIO edge callback (PORTC) | Tap, encoder CLK/DT or SW edge | Capture the raw levels and timestamp into a private raw queue for `tInput` |
| UART RX ISR | Byte received | Push the byte to `xUartRxQueue` |

### 6.4 Synchronisation and resource protection

| Primitive | Name | Used for |
|---|---|---|
| Queue | `xInputQueue` | `tInput`, `tTripwire` and `tComms` → `tGame` (all events, one consumer) |
| Queue | `xUartRxQueue` | UART RX ISR → `tComms` (raw bytes) |
| Queue | `xUartTxQueue` | `tGame` → `tComms` (packets to send) |
| Queue | Raw edge queue | GPIO callback → `tInput` (private to the sensor service) |
| Queue + mailbox | Actuator cue queue and state mailbox | `tGame` → `tActuator` (latest state and accepted feedback; writes never block) |
| Mutex | `xStateMutex` | Protects the shared `gGame` struct, read by the actuator and comms tasks and written only by `tGame` |

### 6.5 RTOS configuration

- `configUSE_PREEMPTION = 1`
- `configUSE_TIME_SLICING = 1`
- Heap 4 only. Features that aren't used are disabled to meet the "remove unnecessary components" requirement; note each one in the report.

---

## 7. MCXC444 ↔ ESP32 Communication Protocol

UART at 115200 baud, with packets so that the boundaries between messages are unambiguous and corruption is detected. The single source of truth is `common/vault_protocol/vault_protocol.h`, which both builds compile.

### 7.1 Frame format

```
| START (0xAA) | TYPE (1B) | LEN (1B) | PAYLOAD (LEN bytes) | CHECKSUM (1B) |
```

- **CHECKSUM** = XOR of TYPE, LEN and every payload byte.
- The receiver uses a small state machine (wait for START → TYPE → LEN → payload → checksum), and drops a frame that stalls for 50 ms.
- On a good frame the receiver replies ACK; on a bad checksum, length or unknown type it replies NAK. The sender keeps one message in flight and retransmits on NAK or after 100 ms without an ACK, up to 3 times.

### 7.2 Message types

| Type | ID | Direction | Payload | Meaning |
|---|---|---|---|---|
| `ARM` | 0x01 | ESP32 → MCU | 3 code digits + knock-pattern ID | Arm the vault with a new code |
| `DISARM` | 0x02 | ESP32 → MCU | none | Return to IDLE |
| `TAMPER` | 0x03 | ESP32 → MCU | Peak acceleration in mg | The box was shaken; trigger the alarm |
| `ATTEMPT` | 0x10 | MCU → ESP32 | Guess digits, attempt #, closeness, 3 feedback bytes | A wrong guess, for the AI and the log |
| `SOLVED` | 0x11 | MCU → ESP32 | Attempt count, time taken (s) | For the leaderboard |
| `ALARM` | 0x12 | MCU → ESP32 | Cause (0 beam/lid, 1 tamper) | For logging and an AI taunt |
| `STATUS` | 0x13 | MCU → ESP32 | State, progress and a sensor snapshot (12 B) | Sent every second, and within ~100 ms of a change; the ESP32 treats 3 s of silence as the MCU being offline |
| `KNOCK_RESULT` | 0x14 | MCU → ESP32 | Reason and gap classifications | Failed rhythm feedback; no target timings |
| `ACK` / `NAK` | 0x20 / 0x21 | Both | Echoed TYPE | Flow control |

This covers the rubric's "beyond lab 3 UART" items: packetisation, checksums and ACK/NAK flow control.

---

## 8. ESP32 and Cloud

### 8.1 ESP32 responsibilities

1. Connect to Wi-Fi (phone hotspot).
2. Poll the IMU at 20 Hz; if acceleration or rotation exceeds its threshold for several consecutive samples while armed, send `TAMPER`.
3. Run the UART packet parser and builder (mirroring the MCU side).
4. Publish game events, status and the leaderboard to Firebase Realtime Database.
5. On a wrong guess or alarm, call the Gemini API and publish the reply for the dashboard.
6. Take arm/disarm commands from the dashboard and forward them to the MCU.

Wi-Fi, Firebase and Gemini HTTPS share the lowest-priority `tCloud` task, so a slow network never delays the UART link or tamper detection.

### 8.2 Cloud services

| Service | Choice | Use |
|---|---|---|
| Generative AI | Gemini (`gemini-2.5-flash-lite`) | The game master's hints and taunts |
| Online database | Firebase Realtime Database | Live state, attempt log and leaderboard |
| Web app | React + TypeScript dashboard on Firebase Hosting | Arm/disarm (owner login), live status, AI messages, rhythm feedback, leaderboard |

See [the web README](web/README.md) for setup, the database layout and the owner/device accounts.

### 8.3 LLM game master

- **Local first**: every wrong guess gets a local hint immediately, so the game never waits on the network.
- **Clues**: Gemini picks one truthful clue (higher/lower or odd/even) for a wrong position, and writes a message around it. It never receives the secret code.
- **Limits**: replies are capped at 25 words, two sentences and 191 characters, and are checked for numeric disclosure. An invalid reply leaves the local hint in place.
- **Failure handling**: network errors, quota backoff and low memory leave the local hint active. Only the latest pending job is kept.
- **Rhythm failures** use fixed dashboard feedback and do not call Gemini.
- **Secrets**: API keys and Wi-Fi credentials live in an external `secrets.h` that is excluded from the repository and the submission zip.

Details are in [the hint guide](docs/game-master-hints.md).

---

## 9. Rubric Mapping

| Requirement | How it's met |
|---|---|
| 4 sensors, 3 actuators | Tap, encoder, photoresistor, IMU / laser, buzzer, RGB LED |
| ≥1 sensor on the MCXC444 | Three of them |
| ≥1 polled, ≥1 interrupt-driven | Photoresistor and IMU polled; tap and encoder interrupt-driven |
| ≥1 ISR, ≥3 tasks | 2 ISRs (GPIO edge, UART RX), 5 tasks |
| Good ISR design | ISRs only capture and queue; nothing blocks or waits |
| ≥1 mutex/semaphore + ≥1 queue | `xStateMutex` and several queues |
| Pre-emption + time-slicing | Both enabled; unused features disabled |
| Bi-directional MCU ↔ ESP32 | ARM/DISARM/TAMPER down to the MCU; ATTEMPT/SOLVED/ALARM/STATUS/KNOCK_RESULT up to the ESP32 |
| Comms bonus | Packetisation, checksum, ACK/NAK retransmission |
| ESP32 used meaningfully | Hosts a sensor (I2C), the protocol, and the cloud logic |
| Online function + bonus | LLM inference, online database, web app |
| Coherence | Every part serves the vault game |

---

## 10. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Ambient light changes the lid threshold | Calibrate against the assembled box (§2.4), use hysteresis and require three consecutive samples |
| Encoder bounce causes double steps | The sequence decoder only accepts valid transitions, and the button is debounced |
| Hotspot or API failure during the demo | The lock logic is local; local hints show immediately; the dashboard shows fallback messages |
| The IMU triggers when someone bumps the table | Tune the thresholds; require them to be exceeded for several samples; keep a cooldown between packets |
| Wi-Fi/HTTPS exhausts ESP32 memory | A reduced Wi-Fi buffer profile and a memory guard before each HTTPS call |
| Tap sensor misses knocks through the lid | Adjust the knock strength or mounting; the ±30% tolerance absorbs timing jitter |

---

## 11. Testing

Host tests run the real FSM, sensor logic, actuator service and comms code against fakes: `python3 ci/run_tests.py`. See [the testing guide](docs/testing.md). Colours, sound, sensor thresholds and physical wiring still need checks on the board, listed in the guides linked above.

---

## 12. Possible Upgrades

- A real latch driven by a servo or a DC motor (ask the TA) instead of the symbolic lock.
- Difficulty levels set from the web page (longer code, a stricter knock tolerance).
- A "heist mode" where two players race, with the leaderboard split by mode.
