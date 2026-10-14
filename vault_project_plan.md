# The Vault — CG2271 Assignment 4 Project Plan

> An AI-refereed safe-cracking game. A player must knock a secret rhythm, dial the right combination and avoid tripping the vault's defences, while an LLM game master watches every attempt and taunts or hints accordingly.

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
| S3 | Photo Resistor | MCXC444 | ADC | **Polled** | Receives the laser beam; a drop in light means the lid is open or someone has reached in |
| S4 | GY-521 MPU-6050 | ESP32 | I2C | **Polled** | Detects the box being shaken or lifted |

### 2.2 Actuators (3)

| # | Part (kit label) | Board | Interface | Purpose |
|---|---|---|---|---|
| A1 | Relay | MCXC444 | Digital GPIO | Acts as the lock: it switches power to the laser tripwire, so the tripwire is on when armed and off when cracked (or drives a latch in the upgrade) |
| A2 | Passive Buzzer | MCXC444 | PWM (timer) | Beeps, victory tune, alarm siren |
| A3 | RGB LED | MCXC444 | PWM (3 channels) | Status: red when locked, a warm/cold gradient for hints, green when open, flashing when in alarm |

### 2.3 Supporting parts

| Part | Source | Purpose |
|---|---|---|
| Laser Emit module | Kit | Beam source for the tripwire (powered through the relay) |
| MB102 breadboard power module | Kit | Backup 5 V / 3.3 V supply if the relay or laser loads the boards too much |
| 2 × mini breadboards, jumper wires | Provided | Wiring |
| FRDM-MCXC444, ESP32 + USB cable | Provided | Controllers |
| Box with lift-off lid (shoebox / plastic container) | Own | Enclosure |
| Small prize (e.g. sweets) | Own | Something to win |
| Tape / Blu-Tack, phone hotspot, laptop or phone | Own | Mounting, internet, web page display |

### 2.4 Things to verify early

- [ ] **Relay drive voltage**: kit relays are often 5 V modules. Confirm a 3.3 V GPIO from the MCXC444 can trigger it; if not, power the relay module from 5 V (or the MB102) and check whether the input pin still triggers reliably.
- [ ] **Photoresistor contrast**: measure ADC readings with the beam on vs. blocked under lab lighting, and pick a threshold with a wide margin.
- [ ] **Encoder bounce**: check whether the CLK edges are noisy and whether software debouncing (ignoring edges within a few ms) is needed.
- [ ] **Tap module sensitivity**: confirm it fires reliably through the lid material, and adjust the knock strength or mounting accordingly.

---

## 3. Physical Layout

```
                 ┌─────────────── LID ───────────────┐
   Tap sensor →  │   [TAP]  (taped on top of lid)     │
                 └────────────────────────────────────┘
                 ┌────────────────────────────────────┐
                 │ LASER ───────────────────► PHOTO-R │  ← tripwire, just under lid
                 │                                    │
                 │   [Relay] [Buzzer]     [ Prize ]   │
                 │                                    │
                 │  [MCXC444]  [breadboards] [ESP32]  │
                 │            [MPU-6050 flat on floor]│
  FRONT FACE →   │   (DIAL)   (RGB LED)               │ → USB/power cable out the back
                 └────────────────────────────────────┘
```

**Outside**
- **Front face**: the rotary encoder knob through a hole, with the RGB LED beside it.
- **Lid**: the tap sensor, where the player knocks.
- **Back**: the power/USB cable exits.
- **Beside the box**: a laptop or phone showing the web dashboard (AI messages, attempt history, leaderboard).

**Inside**
- **Laser and photoresistor**: on opposite walls just below the lid line, aligned so the beam hits the sensor. Shield the photoresistor with a small paper tube to reduce ambient light.
- **MPU-6050**: fixed flat to the floor, so it moves with the box.
- **Boards and breadboards**: taped down, with wires kept below the beam path.
- **Prize**: in a corner, clear of the wiring.

---

## 4. Gameplay

### 4.1 Game states (MCXC444 FSM)

```
        ARM (from web)               correct knock
 IDLE ─────────────────► LOCKED ─────────────────► DIALING
  ▲                        ▲  ▲                      │  │
  │                        │  │ wrong code           │  │ correct code
  │ re-arm (web)           │  └──────────────────────┘  ▼
  │                        │                         UNLOCKED ── re-arm ──► LOCKED
  │              lockout   │
  │              expires   │
  │                        │
  └─────────────── ALARM ◄─┴── beam broken / tamper (from LOCKED or DIALING)
```

| State | LED | Buzzer | Relay / Laser | Accepts |
|---|---|---|---|---|
| IDLE | Off / dim white | Silent | Laser off | ARM from web |
| LOCKED | Solid red | Silent | Laser **on** | Knocks |
| DIALING | Warm/cold hint colour | Tick per dial step | Laser **on** | Dial turns, button presses |
| UNLOCKED | Green | Victory tune | Laser **off** | Re-arm from web |
| ALARM | Flashing red | Siren | Laser on | Nothing (30 s lockout) |

### 4.2 A round

1. **Arm**: the owner enters a 3-digit combination (each digit 0–9) on the web page. The ESP32 sends it to the MCXC444, which moves to LOCKED.
2. **Wake**: the player knocks the secret rhythm (e.g. knock–knock–pause–knock). On a match the vault beeps and moves to DIALING; a wrong rhythm is ignored.
3. **Dial**: turning the knob changes the current digit, with a tick on each step. Pressing the knob confirms the digit, and after 3 digits the guess is submitted.
4. **Check (local)**: the MCXC444 compares the guess with the stored code, so the lock decision never depends on the internet.
   - **Wrong**: the LED shows a closeness score (e.g. total digit distance mapped from blue = cold to orange = hot). An ATTEMPT packet goes to the ESP32, and the vault returns to LOCKED, so the player must knock again.
   - **Right**: the vault moves to UNLOCKED, the relay switches the laser off, and the victory tune plays. A SOLVED packet goes to the ESP32.
5. **Cloud**: the ESP32 sends the attempt history to the LLM, which returns a short hint or taunt for the web page; on a solve, the ESP32 writes the time and attempt count to the leaderboard database.
6. **Cheating**: a beam break (from the photoresistor) or tamper (from the MPU-6050 via the ESP32) while armed moves the vault to ALARM, with a siren and a 30 s lockout. The event is logged, and the LLM is told so it can comment.

### 4.3 Knock detection

- The tap ISR records the tick count of each tap.
- The game task compares the intervals between taps with the stored pattern, within a tolerance (e.g. ±30%), so the rhythm matters rather than the absolute speed.
- A gap longer than about 2 s resets the knock buffer.

---

## 5. Hardware Architecture

### 5.1 Connection overview

```
                          UART (TX/RX, common GND)
   ┌────────────────┐  ◄────────────────────────►  ┌────────────────┐
   │   FRDM-MCXC444 │                              │     ESP32      │
   │                │                              │                │
   │ GPIO-IRQ ◄ Tap │                              │ I2C ◄ MPU-6050 │
   │ GPIO-IRQ ◄ Enc │                              │                │
   │ ADC      ◄ LDR │                              │ Wi-Fi ─► Cloud │
   │ GPIO     ► Rly │──► Laser                     │  • LLM API     │
   │ PWM      ► Bzr │                              │  • Database    │
   │ PWM×3    ► RGB │                              │  • Web page    │
   └────────────────┘                              └────────────────┘
```

### 5.2 Pin table (fill in during build)

| Signal | Board | Pin | Peripheral | Notes |
|---|---|---|---|---|
| Tap OUT | MCXC444 | TBD | PORT IRQ (falling/rising edge) | |
| Encoder CLK | MCXC444 | TBD | PORT IRQ (either edge) | |
| Encoder DT | MCXC444 | TBD | GPIO input | Read inside the CLK ISR to get direction |
| Encoder SW | MCXC444 | TBD | PORT IRQ (falling edge) | Pull-up needed |
| Photoresistor | MCXC444 | TBD | ADC channel | |
| Relay IN | MCXC444 | TBD | GPIO output | Check the voltage (§2.4) |
| Buzzer | MCXC444 | TBD | TPM channel (PWM) | Frequency sets the pitch |
| RGB R/G/B | MCXC444 | TBD ×3 | TPM channels (PWM) | |
| UART TX/RX | MCXC444 | TBD | UART | Cross-connect to the ESP32, common ground |
| MPU-6050 SDA/SCL | ESP32 | Default I2C pins | I2C | |
| UART TX/RX | ESP32 | TBD | Serial2 | |

### 5.3 Engineering justification for placement and read methods

- **Encoder → interrupt on the MCXC444**: quadrature edges are short and frequent, so polling risks missing steps, while an edge interrupt captures every one.
- **Tap → interrupt**: a tap is a brief pulse at an unpredictable time, and its timestamp needs to be accurate for rhythm matching.
- **Photoresistor → polled ADC**: light level changes relatively slowly, so sampling every ~50 ms is fast enough to catch a lid opening and wastes little CPU.
- **MPU-6050 → ESP32 over I2C**: it needs a library and register-level setup that the ESP32's Arduino ecosystem handles well. Tamper is less time-critical than the tripwire, so the UART hop is acceptable.
- **All actuators on the MCXC444**: the lock decision and alarm are local and real-time, and still work if Wi-Fi drops.

---

## 6. Software Architecture (MCXC444, FreeRTOS)

### 6.1 Design principles

- ISRs only capture the event (with a timestamp or direction), push it to a queue with `xQueueSendFromISR`, request a context switch if needed, and return. They do no waiting, no UART transmission and no game logic.
- The game task is the single owner of state transitions; other tasks only read the state or post events.
- Every task blocks (on a queue or with `vTaskDelay`) rather than busy-waiting.

### 6.2 Tasks

| Task | Priority | Blocks on | Responsibility |
|---|---|---|---|
| `tTripwire` | Highest | `vTaskDelay` (~50 ms period) | Reads the ADC; if armed and the light falls below the threshold for N consecutive samples, posts `EV_BEAM_BROKEN` |
| `tGame` | High | `xInputQueue` | Runs the FSM: knock matching, digit entry, code check, alarm and lockout timing |
| `tComms` | Medium | `xUartRxQueue` | Assembles incoming bytes into packets, verifies the checksum, sends ACK/NAK, turns packets into events for `tGame`, and sends outgoing packets |
| `tActuator` | Low / Medium | `vTaskDelay` (~20 ms) | Reads the current state (under the mutex) and drives the LED colour, buzzer patterns and relay |

**Why this order**: a tamper-safety response (tripwire) must pre-empt everything; player input should feel responsive; comms can tolerate a few ms of delay; the visual and audio patterns are the least time-critical.

### 6.3 ISRs

| ISR | Trigger | Action |
|---|---|---|
| Tap ISR | Tap module edge | Push `EV_KNOCK{tick}` to `xInputQueue` |
| Encoder ISR (PORT) | CLK edge / SW press | Read DT; push `EV_DIAL{+1/-1}` or `EV_PRESS` |
| UART RX ISR | Byte received | Push the byte to `xUartRxQueue` |

### 6.4 Synchronisation and resource protection

| Primitive | Name | Used for |
|---|---|---|
| Queue | `xInputQueue` | ISRs, tripwire task and comms task → game task (all events, one consumer) |
| Queue | `xUartRxQueue` | UART RX ISR → comms task (raw bytes) |
| Queue | `xUartTxQueue` | Game task → comms task (packets to send) |
| Mutex | `xStateMutex` | Protects the shared `gameState` struct (state, current guess, attempt count, hint colour) read by the actuator and comms tasks and written by the game task |

### 6.5 RTOS configuration

- `configUSE_PREEMPTION = 1`
- `configUSE_TIME_SLICING = 1`
- Disable features you don't use (e.g. unused timers, trace or co-routines) to meet the "remove unnecessary components" requirement, and note each one in the report.

---

## 7. MCXC444 ↔ ESP32 Communication Protocol

UART at a fixed baud rate (e.g. 115200), with packets so that the boundaries between messages are unambiguous and corruption is detected.

### 7.1 Frame format

```
| START (0xAA) | TYPE (1B) | LEN (1B) | PAYLOAD (LEN bytes) | CHECKSUM (1B) |
```

- **CHECKSUM** = XOR of TYPE, LEN and every payload byte.
- The receiver uses a small state machine (wait for START → TYPE → LEN → payload → checksum).
- On a good frame the receiver replies ACK; on a bad checksum or a timeout it replies NAK, and the sender retransmits up to 3 times.

### 7.2 Message types

| Type | Direction | Payload | Meaning |
|---|---|---|---|
| `ARM` | ESP32 → MCU | 3 code digits + knock-pattern ID | Arm the vault with a new code |
| `DISARM` | ESP32 → MCU | — | Return to IDLE |
| `TAMPER` | ESP32 → MCU | Acceleration magnitude | The box was shaken; trigger the alarm |
| `ATTEMPT` | MCU → ESP32 | Guess digits, attempt #, closeness score | A wrong guess, for the AI and the log |
| `SOLVED` | MCU → ESP32 | Attempt count, time taken (s) | For the leaderboard |
| `ALARM` | MCU → ESP32 | Cause (beam / tamper) | For logging and an AI taunt |
| `STATUS` | MCU → ESP32 | Current state | Periodic heartbeat for the web page |
| `ACK` / `NAK` | Both | Echoed TYPE | Flow control |

This covers the rubric's "beyond lab 3 UART" items: packetisation, checksums and ACK/NAK flow control.

---

## 8. ESP32 and Cloud

### 8.1 ESP32 responsibilities

1. Connect to Wi-Fi (phone hotspot).
2. Poll the MPU-6050 (~20 Hz) and compute the acceleration magnitude; if it exceeds a threshold while armed, send `TAMPER`.
3. Run the UART packet parser and builder (mirroring the MCU side).
4. On `ATTEMPT` / `ALARM`, call the LLM API and push the reply to the web page.
5. On `SOLVED`, write the leaderboard entry to the online database.
6. Serve or connect to the web dashboard: an arm/disarm form, the live status, the AI messages and the leaderboard.

### 8.2 Libraries (beyond the template)

- `Wire.h` + an MPU-6050 library (e.g. Adafruit MPU6050) for I2C.
- `HTTPClient.h` + `Arduino_JSON.h` for REST calls (in the template, extended).
- `WebServer.h` for a dashboard hosted on the ESP32, **or** a Firebase client library if the dashboard is hosted online.
- `HardwareSerial` (Serial2) for the MCU link.

### 8.3 Cloud services

| Service | Option | Use |
|---|---|---|
| Generative AI | OpenAI API (or similar) | The game master's hints and taunts |
| Online database | Firebase Realtime Database / Supabase | Leaderboard and attempt log |
| Web app | Page hosted on the ESP32, or a static page reading the database | Arm/disarm, live status, AI messages, leaderboard |

### 8.4 LLM game master

- **System prompt (sketch)**: "You are a sarcastic vault guarding a 3-digit code. Given the true code and the player's past guesses, reply in under 25 words with either a cryptic hint or a taunt. Never reveal more than one digit's direction per reply."
- **Input sent**: the code, the guess history, the closeness scores and any alarm events.
- **Output expected**: JSON `{ "message": "...", "mood": "taunt|hint" }`, parsed with `Arduino_JSON`.
- **Rate limiting**: call at most once per attempt, with a timeout. If the call fails, the game continues and the web page shows a fallback message.
- **Keep API keys out of the submitted code**; put them in a separate `secrets.h` that is excluded from the zip.

---

## 9. Rubric Mapping

| Requirement | How it's met |
|---|---|
| 4 sensors, 3 actuators | Tap, encoder, photoresistor, MPU-6050 / relay, buzzer, RGB LED |
| ≥1 sensor on the MCXC444 | Three of them |
| ≥1 polled, ≥1 interrupt-driven | Photoresistor and MPU polled; tap and encoder interrupt-driven |
| ≥1 ISR, ≥3 tasks | 3 ISRs, 4 tasks |
| Good ISR design | ISRs only capture and queue; nothing blocks or waits |
| ≥1 mutex/semaphore + ≥1 queue | `xStateMutex`, 3 queues |
| Pre-emption + time-slicing | Both enabled; unused features disabled |
| Bi-directional MCU ↔ ESP32 | ARM/TAMPER down to the MCU, ATTEMPT/SOLVED/ALARM up to the ESP32 |
| Comms bonus | Packetisation, checksum, ACK/NAK retransmission |
| ESP32 used meaningfully | Hosts a sensor (I2C), the protocol, the web app and the cloud logic |
| Online function + bonus | LLM inference, online database, web app |
| Coherence | Every part serves the vault game |

---

## 10. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| The relay won't trigger from 3.3 V | Power the module from 5 V or the MB102; test in week 1 |
| Ambient light confuses the tripwire | Shield the photoresistor with a paper tube; calibrate the threshold at startup |
| Laser misalignment when the box is moved | Hot-glue or tape both ends firmly; add a "beam OK" check before arming |
| Encoder bounce causes double steps | Software debounce in the ISR (ignore edges within a few ms) |
| Hotspot or API failure during the demo | The lock logic is local; cache the last leaderboard; show fallback messages |
| The MPU triggers when someone bumps the table | Tune the threshold; require it to be exceeded for several samples |

---

## 11. Timeline

| When | Milestone |
|---|---|
| Week 9 (now – 18 Oct) | Test every part individually; confirm the relay and laser work; agree the protocol |
| By 23 Oct (Interim) | UART link working both ways with basic packets; the FreeRTOS task skeleton running; box layout sketched; **interim report + code submitted** |
| Week 11 | Full FSM on the MCXC444; tripwire and tamper alarm; actuators finished |
| Week 12 | LLM game master, database, web page; mount everything in the box; end-to-end testing |
| Week 13 lab | Demo (rehearse a 5–7 min script: arm → knock → wrong guess + AI hint → cheat attempt → solve → leaderboard) |
| By 13 Nov | **Final report + code submitted** |

---

## 12. Possible Upgrades

- A real latch driven by a servo or a DC motor (ask the TA) instead of the symbolic lock.
- Difficulty levels set from the web page (longer code, a stricter knock tolerance).
- A "heist mode" where two players race, with the leaderboard split by mode.
