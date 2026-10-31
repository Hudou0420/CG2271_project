# LED and buzzer feedback

The normal MCXC444 application now drives the external RGB LED and passive buzzer from the game. Rebuild and flash the root **CG2271_Project** in MCUXpresso. The ESP32 firmware and hosted dashboard need no update for this feature.

## Hardware

The existing BSP wiring and PWM drivers are used:

| Output | MCU pin | Header |
| --- | --- | --- |
| RGB red | PTC2 | J4-10 / A4 |
| RGB green | PTC1 | J4-12 / A5 |
| RGB blue | PTD2 | J2-4 / D9 |
| Passive buzzer signal | PTA12 | J1-8 / D3 |

The documented RGB LED is common-positive, with active-low PWM. The red and green mapping follows the observed colours recorded in the BSP. Check [the BSP wiring notes](../bsp/README.md) for the existing circuit and module power connections. This change controls RGB and buzzer only; relay and laser operation remains as before.

## What to expect

| Game action or state | RGB LED | Physical buzzer |
| --- | --- | --- |
| Idle/disarmed | Off | Silent |
| Armed/locked | Red | Silent |
| Correct knock rhythm | Dim blue | 1047 Hz beep for 80 ms |
| Dialing | Dim blue | 2000 Hz tick for 20 ms when the digit changes |
| First two digit confirmations | Dim blue | 1320 Hz beep for 60 ms |
| Wrong complete guess | Hint colour for 5 seconds, then red | 880 Hz for 80 ms, 40 ms silence, then 440 Hz for 120 ms |
| Correct complete guess | Green | 523, 659, 784, 1047 Hz ascending tune, once |
| Alarm/lockout | Red flashing every 250 ms | Alternate 880/1320 Hz every 125 ms for 1 second, then 2 seconds silent; repeat until lockout ends |

The victory tune uses four 120 ms notes separated by 40 ms gaps, lasting 600 ms. The final digit confirmation uses the wrong-guess or victory sound instead of another confirmation beep.

Hints use the existing completed-guess closeness score: blue at 0%, yellow at 50%, and orange at 100%, with linear interpolation between anchors. An exact correct guess uses green. The hint remains visible while waiting for knocks, then cancels when dialing begins. Rearm, disarm, and alarm also cancel previous feedback.

RGB channels are capped at 64/255 PWM; dialing blue is 16/255. Dial sounds are limited to one every 60 ms. Under rapid input, ticks coalesce or are dropped rather than building an audible backlog. Alarm overrides other sound, followed by victory, wrong guess, knock acceptance, digit confirmation, and dial tick. Rejected actions and individual knocks are silent.

## Configuration

Edit [source/vault_actuator_config.h](../source/vault_actuator_config.h) to adjust brightness, hint duration, note frequencies, and pattern timings. `VAULT_LED_ENABLED` and `VAULT_BUZZER_ENABLED` default to 1 and also accept compiler definitions; setting either to 0 keeps that output off. The website's sound button controls browser audio independently.

The actuator task updates every 20 ms using hardware PWM between updates. Only this task writes the RGB LED and buzzer during gameplay. `tGame` publishes the latest state and accepted feedback through a one-element state mailbox and a 16-element cue queue after releasing the state mutex. Queue writes never wait for space. State epochs cancel obsolete feedback, including rearming while already locked.

A failed output write logs one diagnostic and attempts to switch that output off. Further writes to that output stop until reboot; the other output continues. A hardware failure can prevent the off request from succeeding. The service never uses the BSP's all-output shutdown routine, which would also alter relay and laser.

## Verify on the board

1. Build and flash the normal MCU application. At boot, expect LED off and buzzer silent. Arm from the owner dashboard; the LED should turn red.
2. Enter the accepted knock rhythm. Expect dim blue and a short beep. Turn the dial and confirm digits: ticks should track digit changes, and the first two confirmations should have a distinct beep.
3. Submit a wrong code. Expect the descending cue and a hint colour for 5 seconds, followed by red. Knock again during the hint to verify entering dialing cancels it.
4. Solve the code. Expect green and the ascending tune once. Additional dial/button input must not replay it. Rearm or disarm during playback to verify cancellation.
5. Trigger tamper while armed. Expect flashing red and the repeating 1-second-on/2-seconds-off siren throughout the existing 30-second lockout. Additional tamper or disarm commands must not restart the cadence or shorten lockout. At lockout completion, expect locked red and silence.

If the tap sensor is unavailable, use the existing `VAULT_SELF_TEST=1` MCU build option temporarily. It injects the accepted rhythm and digit events, solves one round, then submits a wrong guess in a second round and triggers a beam alarm. The wrong-guess pause now allows the full hint to expire before the alarm. This bypasses sensor operation and is not proof that taps or beam detection work. After the demo the MCU remains locked; restore `VAULT_SELF_TEST=0` and rebuild/flash for normal use.

Host tests exercise the real actuator service and FSM with fake time and recording output drivers, including disabled-output builds, saturation, rollover, driver errors, and task cadence. Run `python3 ci/run_tests.py` with the test prerequisites in [the testing guide](testing.md). All four MCU validation profiles also compile the service. Actual colours, brightness, sound, and interference with physical inputs still require the board checks above.
