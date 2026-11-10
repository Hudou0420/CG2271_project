# Game master hints and alarm tuning

The alarm locks the MCU for **10 seconds**. The dashboard counts down the same
duration but waits for the MCU to report that lockout has ended. Disarm and
additional tamper events cannot shorten or restart the alarm.

Tamper detection still accepts acceleration **or** rotation as evidence. Its
initial thresholds are **0.30 g** deviation from the calibrated resting magnitude
and **80 degrees/second**, sustained for **four samples** at 20 Hz. A normal sample,
failed read, or disarmed sample breaks the streak. The existing three-second
packet cooldown remains. These values need a physical check on the assembled box;
use the ESP32 `imu` console command to inspect readings during normal play and
intentional disturbance.

## Failure hints

Every completed wrong code guess gets a local hint immediately. Gemini chooses
among higher/lower and odd/even clues for positions that are incorrect in the
latest submitted guess, then writes a message containing its chosen clue.
Correct positions and whole-code repetition clues are excluded. For code `123`
and guess `423`, only “Left digit: go lower” and “Left digit is odd” are eligible. It never receives the raw secret code. A confirmed
round is required for code clues; disarm then arm if diagnostics say code context
is unavailable.

Rejected knock rhythms use fixed feedback in the dashboard’s “Rhythm check” card,
independent of the Game master toggle. A three-knock diagram labels both pauses:
“Too short · Wait longer,” “Too long · Tap sooner,” “Timing good,” or “Not captured.”
No target timings are shown. Incomplete sequences show “Sequence timed out” after
the existing two-second inactivity timeout, preserving any captured gap result.

Knock failures do not request Gemini or appear as AI speech-bubble reactions.
They still publish the existing events and round/action tags. The card clears
when another sequence starts, the state changes, the connection goes offline,
or its round/action becomes stale. Failed rhythms do not count as code attempts
or leaderboard attempts, and do not create digit rows in Close calls.

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

There is no normal 15-second request spacing. Wrong code guesses request Gemini
once event/status/local-hint publication completes and HTTPS is available. Only
the latest pending job is retained. Network failures, rejected responses, quota
backoff and low memory can still leave a local hint active. Starting another try,
changing rounds, disabling AI or losing the MCU connection invalidates old work.

## Update and verify

1. Close serial monitors and upload the updated ESP32 firmware. Knock-failure
   publication remains compatible with the existing MCU firmware; no MCU update
   is needed for the fixed dashboard hints.
2. Build and redeploy the website hosting. Firebase rules and schemas are unchanged.
3. Arm a fresh round. Submit an incorrect rhythm and check for
   `[mcu] KNOCK_RESULT reason=... gaps=...` in the ESP32 console. Confirm the
   dashboard shows both pause results, with no Gemini request for that failure.
4. Turn Game master off and repeat. Check a partial-sequence timeout, reload,
   and the start of a new sequence. Test both phone and desktop layouts.
5. Complete a wrong code guess. Verify its coloured tiles and a useful hint.
   Quickly start a new try and confirm the previous hint clears.
6. Confirm ordinary taps and dial handling avoid tamper alarms, while deliberate
   disturbance still triggers them. Observe the ten-second countdown and MCU
   recovery. Keep the box still during the startup IMU calibration.

Host regressions exercise timing boundaries, timer rollover, queued capture
ordering, UART validation, hint selection/provider failures, Firebase permissions,
and desktop/phone message lifetime. They do not replace physical sensor tuning
or a live Gemini check.
