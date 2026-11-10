# Game master hints and alarm tuning

The alarm locks the MCU for **10 seconds**. The dashboard counts down the same
duration but waits for the MCU to report that lockout has ended. Disarm and
additional tamper events cannot shorten or restart the alarm.

Tamper detection still accepts acceleration **or** rotation as evidence. Its
initial thresholds are **0.45 g** deviation from the calibrated resting magnitude
and **120 degrees/second**, sustained for **four samples** at 20 Hz. A normal sample,
failed read, or disarmed sample breaks the streak. The existing three-second
packet cooldown remains. These values need a physical check on the assembled box;
use the ESP32 `imu` console command to inspect readings during normal play and
intentional disturbance.

## Failure hints

Every completed wrong code guess gets a local hint immediately. Gemini chooses
among valid directional, odd/even, and repetition clues, then writes a message
containing its chosen clue. It never receives the raw secret code. A confirmed
round is required for code clues; disarm then arm if diagnostics say code context
is unavailable.

Every rejected knock rhythm also gets help. A try fails when all taps have been
captured with incorrect spacing, or an unfinished sequence exceeds the existing
two-second inactivity timeout. Hints coach the opening or final pause without
showing exact target timings. Failed rhythms do not count as code attempts or
leaderboard attempts, and do not create digit rows in Close calls.

The new MCU-to-ESP32 `KNOCK_RESULT` frame is type **0x14**, length **3**:

| Byte | Meaning |
| --- | --- |
| 0 | Failure reason: 0 = completed incorrect pattern, 1 = timeout |
| 1 | Opening gap: 0 = unavailable, 1 = too short, 2 = accepted, 3 = too long |
| 2 | Final gap: same classifications |

Firebase publishes these as `knock_failed` events with `reason` (`pattern` or
`timeout`) and `gaps` (a two-character classification string). Actual target
timings and secret code digits are not included. Each event uses the existing
round ID, event ID and action revision to prevent stale hints from reappearing.

Gemini replies internally use `{clueId, message}`. The selected clue must be one
of the current job's truthful options, with its wording included exactly once.
Messages are limited to 25 words, two sentences, and 191 characters. Extra
reaction text is checked for numeric disclosures and additional clue language.
Invalid responses leave the local hint active. Alarm/solve reactions use clue ID
`-1`, without a useful clue.

There is no normal 15-second request spacing. Current failures request Gemini
once event/status/local-hint publication completes and HTTPS is available. Only
the latest pending job is retained. Network failures, rejected responses, quota
backoff and low memory can still leave a local hint active. Starting another try,
changing rounds, disabling AI or losing the MCU connection invalidates old work.

## Update and verify

1. Close serial monitors and upload the updated ESP32 firmware first. It continues
   to accept existing MCU messages and adds support for `KNOCK_RESULT`.
2. Build and flash `CG2271_Project` through MCUXpresso, then resume execution.
   Use the normal board image, not the supplementary GNU Arm CI artifact.
3. In `Project/web`, run `npm run build`, then
   `npx firebase deploy --only hosting,database`.
4. Arm a fresh round. Submit an incorrect rhythm and check for
   `[mcu] KNOCK_RESULT reason=... gaps=...` in the ESP32 console. Confirm a local
   hint, then a Gemini hint when the provider succeeds. Test a partial-sequence
   timeout too.
5. Complete a wrong code guess. Verify its coloured tiles and a useful hint.
   Quickly start a new try and confirm the previous hint clears.
6. Confirm ordinary taps and dial handling avoid tamper alarms, while deliberate
   disturbance still triggers them. Observe the ten-second countdown and MCU
   recovery. Keep the box still during the startup IMU calibration.

Host regressions exercise timing boundaries, timer rollover, queued capture
ordering, UART validation, hint selection/provider failures, Firebase permissions,
and desktop/phone message lifetime. They do not replace physical sensor tuning
or a live Gemini check.
