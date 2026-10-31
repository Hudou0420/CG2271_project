# The Vault: ESP32 firmware

Board: **ESP32-S2-DevKitM-1** (built with PlatformIO's `esp32-s2-saola-1` definition).

This version implements the UART link to the MCXC444, tamper detection with the MPU-6050,
and the link to the web dashboard through Firebase Realtime Database (see [web/README.md](../web/README.md)).
The LLM game master comes later.

## Wiring

| ESP32-S2 pin | Goes to | Notes |
|---|---|---|
| GPIO4 (TX) | MCXC444 UART RX | |
| GPIO5 (RX) | MCXC444 UART TX | |
| GND | MCXC444 GND | **Required:** the boards must share a ground |
| 3V3 | GY-521 VCC | |
| GND | GY-521 GND | |
| GPIO8 | GY-521 SDA | |
| GPIO9 | GY-521 SCL | |

You can change the pins in `include/config.h`. Avoid GPIO18, which drives the on-board RGB LED.

Our GY-521 actually carries an **MPU-6500** (WHO_AM_I `0x70`), not a genuine MPU-6050 (`0x68`).
Its accelerometer and gyro registers are the same, so `imu.cpp` drives the chip directly over I2C instead of using the Adafruit library, which rejects anything except `0x68`.

## Build, flash, monitor

`pio` lives at `~/.platformio/penv/bin/pio`. You can also use the PlatformIO buttons in VS Code.

```sh
pio run -t upload        # build and flash
pio device monitor       # debug console, 115200 baud
pio test -e native       # protocol unit tests on the laptop, no board needed
```

## Debug console

Type commands into the serial monitor:

```
arm 427 [pattern]   send ARM (code 4-2-7, knock pattern 0 by default)
disarm              send DISARM
tamper              send TAMPER now
send 10 04 02 07 01 03   send any frame: type, then payload bytes (hex)
imu                 stream IMU samples (for tuning thresholds)
raw                 hex-dump every UART byte
sensors             toggle printing the MCU's sensor snapshot when it changes
stats               link counters, vault state, MCU sensors, latest IMU sample
cloud               Wi-Fi / Firebase status, request counters, last error
```

## Web dashboard link (Firebase)

1. Set up the Firebase project once, as described in [web/README.md](../web/README.md#firebase-project-setup-once).
2. Copy `include/secrets.example.h` to `../Project_Secrets/secrets.h` relative to the repository root and fill in Wi-Fi, database URL, Web API key, and the dedicated device account credentials. See `web/README.md` for the owner/device UID migration before publishing protected rules.
   `secrets.h` is in `.gitignore`: never commit it or put it in the submission zip. Without it the firmware still builds, with the cloud link off.
3. Flash and open the monitor. Within a few seconds you should see `Wi-Fi connected`, `signed in to Firebase` and `connected to the database`.
   `cloud` shows the counters and the last error if something fails.

The production dashboard is in `web/`. `tools/dashboard_sim.py` remains a command-line alternative using the same paths (standard-library Python, reads the external `Project_Secrets/secrets.h`). Public watch/status commands need no login after the protected rules are published; arm/disarm prompt for the shared owner email and password:

```sh
python3 tools/dashboard_sim.py watch                   # live status + new events
python3 tools/dashboard_sim.py arm 427 --player Alice  # arm from the "web"
python3 tools/dashboard_sim.py disarm
python3 tools/dashboard_sim.py events -n 20
python3 tools/dashboard_sim.py leaderboard
```

Wi-Fi, Firebase and Gemini HTTPS share the lowest-priority task (`tCloud`) and one secure client. A slow network never delays the UART link, tamper detection or USB console. Gemini can briefly delay dashboard commands; it starts only after the local reaction and event have been published. Its memory guard uses measured HTTPS heap usage plus a reserve; see `web/README.md` for diagnostics and board verification.

The ESP32 build flags also apply a Wi-Fi memory profile before driver startup:
two static RX buffers, eight dynamic RX/TX buffers, and frame aggregation off.
This limits peak throughput to leave more RAM for HTTPS. Keep the
`--wrap=esp_wifi_init` linker flag; `[wifi] memory profile` confirms it at boot.

## Testing without the MCU (loopback)

Connect **GPIO4 to GPIO5** with a jumper wire. The ESP32 then receives its own frames:

- `arm 123` prints `received ARM`, the ESP32 ACKs its own frame, and then prints `ARM delivered`.
- `send 13 01 00 00 00 00 00 00 00 00 00 00 00` makes the ESP32 receive a fake STATUS = LOCKED, so the vault counts as armed. Shaking the IMU then sends TAMPER.
- `send 10 02 01 05 01 56 01 01 00` simulates guess 215 with blue/blue/red feedback.
  The legacy five-byte ATTEMPT format is also accepted without digit feedback.
- Remove the jumper and run `arm 123`: after 3 retries the console prints `not acknowledged`, which shows retransmission works.

## Protocol

The single source of truth is `../common/vault_protocol/vault_protocol.h`, which is plain C.
See [digit feedback](../docs/digit-feedback.md) for colour meanings and the firmware/hosting rollout order.
The MCXC444 project should compile the same `vault_protocol.c` and `vault_protocol.h` so both sides use the same message IDs and parser.

```
| 0xAA | TYPE | LEN | PAYLOAD | CHK = TYPE ^ LEN ^ payload... |
```

| Type | ID | Direction | Payload |
|---|---|---|---|
| ARM | 0x01 | ESP → MCU | d0 d1 d2 pattern_id |
| DISARM | 0x02 | ESP → MCU | none |
| TAMPER | 0x03 | ESP → MCU | peak \|a\| in mg (u16 LE) |
| ATTEMPT | 0x10 | MCU → ESP | d0 d1 d2 attempt_no closeness feedback0 feedback1 feedback2 (8 B; legacy 5 B accepted) |
| SOLVED | 0x11 | MCU → ESP | attempts, seconds (u16 LE) |
| ALARM | 0x12 | MCU → ESP | cause (0 = beam, 1 = tamper) |
| STATUS | 0x13 | MCU → ESP | 12 B: state (0 IDLE, 1 LOCKED, 2 DIALING, 3 UNLOCKED, 4 ALARM), digits entered, dial digit, knocks, attempts, closeness, light (u16 LE), encoder steps (i16 LE), sensor events, flags. See `VP_ST_*` |
| ACK | 0x20 | both | echoed TYPE |
| NAK | 0x21 | both | echoed TYPE |

- Every good frame except ACK/NAK gets an ACK. A bad checksum, bad length or unknown type gets a NAK.
- The sender keeps one message in flight. It resends on a NAK or after 100 ms without an ACK, up to 3 times.
- The MCU sends STATUS once per second, and within ~100 ms when a sensor reading or the game progress changes. If none arrives for 3 s, the ESP32 shows the MCU as offline.
