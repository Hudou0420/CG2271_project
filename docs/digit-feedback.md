Submitted guesses in **Close calls.** now use per-digit results computed by the MCXC444:

| Colour | Meaning |
| --- | --- |
| Green | Correct digit in the correct position |
| Blue | Digit occurs elsewhere in the code |
| Red | No unmatched occurrence of this digit remains in the code |

For code `123`, guess `215` is blue/blue/red, and `125` is green/green/red. Repeated digits follow Wordle counting: exact matches consume occurrences first, then unmatched guesses consume remaining occurrences from left to right. For code `123`, guess `221` is red/green/blue because the only `2` is already matched in the middle.

Live entry stays neutral until a complete guess is submitted. The existing percentage and heat bar still describe overall closeness. Old guesses without per-digit results display neutral tiles; historical results are not inferred from the percentage.

The MCU computes the results while creating the attempt packet. ATTEMPT now carries eight bytes: the existing three guessed digits, attempt number, closeness, then three feedback bytes. Feedback values are `0` absent, `1` present elsewhere, and `2` correct position. The ESP32 publishes these as a three-character `feedback` string on the existing attempt event. The secret code is not added to the packet or public event. Database rules validate the optional field and continue to permit older events without it.

To enable this on the physical system:

1. Build and upload the updated ESP32 firmware using your normal PlatformIO command. Update the ESP32 first: it accepts both the legacy five-byte and new eight-byte ATTEMPT formats.
2. Rebuild and flash the updated **CG2271_Project** onto the MCXC444 through MCUXpresso.
3. From `Project/web`, rebuild the website and deploy hosting plus database rules:

```sh
npm run build
npx firebase deploy --only hosting,database
```

Start a fresh round and submit a new guess. Updating hosting alone cannot generate the missing MCU feedback. The MCU validation images produced by CI remain supplementary build artifacts; use the normal MCUXpresso image for flashing.

Firmware tests cover both examples, duplicate counts, leading zeroes, golden UART frames, legacy packet support, invalid feedback rejection, and cloud serialization without a code field. Web tests check the corresponding tile classes, accessible descriptions, neutral legacy data, and live-entry behaviour. Physical-board verification requires flashing both boards and submitting real guesses.
