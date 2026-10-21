# The Vault: web dashboard ↔ ESP32 interface

**Dashboard developers: use [`vault-api.js`](vault-api.js).** It is the whole
interface to the vault. You never deal with the ESP32, the UART link or the
database paths yourself.

```html
<script type="module">
  import { connectVault } from "./vault-api.js";

  const vault = await connectVault({          // your Firebase web config
    apiKey: "...", authDomain: "...", projectId: "...",
    databaseURL: "https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app",
  });

  vault.onStatus((s) => {                     // live, on every change
    // s.state: IDLE | LOCKED | DIALING | UNLOCKED | ALARM | UNKNOWN
    // s.espOnline: false if the vault has gone quiet for 30 s
    // s.dial, s.digitsEntered, s.knocks, s.attempts, s.closeness (0-100), ...
  });
  vault.onEvents((e) => { /* e.type: armed, attempt, solved, alarm, ... */ });
  vault.onLeaderboard((rows) => { /* [{ player, seconds, attempts }, ...] fastest first */ });

  const r = await vault.arm("427", { player: "Alice", onUpdate: (step) => console.log(step) });
  // r.ok: the MCU received it; otherwise r.result says why ("rejected: MCU offline", ...)
  await vault.disarm();
</script>
```

| Function | What it does |
|---|---|
| `connectVault(config)` | Signs in (anonymously) and returns the interface. Pass the Firebase web config, or an app you already created with `initializeApp()`. |
| `onStatus(cb)` | `cb(status)` on every change, and when the vault goes offline. Fields are in [`/vault/status`](#vaultstatus-esp32-writes-dashboard-reads) below, plus `espOnline` and `ageMs`. |
| `onEvents(cb, { limit })` | The last `limit` (default 50) events oldest first, then each new one. See [`/vault/events`](#vaulteventsid-esp32-appends-dashboard-reads). |
| `onLeaderboard(cb, { limit })` | The `limit` (default 10) fastest solves, re-sent on every new solve. |
| `arm(code, { player, pattern, onUpdate, timeoutMs })` | Arms with a 3-digit code (`"042"` or `42`). Resolves with `{ ok, result, id }` once the vault answers, or with a timeout after 10 s. |
| `disarm({ onUpdate, timeoutMs })` | Back to IDLE. Same result as `arm`. |

Every `on…` function returns a function that stops listening. ES modules have
to be served over HTTP, so test with `python3 -m http.server` in your folder,
not by opening the file directly. With npm and a bundler, change the three
`import` URLs at the top of `vault-api.js` to `"firebase/app"`,
`"firebase/auth"` and `"firebase/database"`.

`ok: true` means the MCU *received* the command. The game still decides
whether it takes effect: for example, `arm` and `disarm` are ignored during an
ALARM lockout. Show the state from `onStatus` rather than assuming.

## How it works

The dashboard and the ESP32 never talk directly. Both read and write the same
**Firebase Realtime Database**, under `/vault`:

```
 Web dashboard  ──writes──►  /vault/commands         ──polled by──►  ESP32 ──UART──► MCU
 (any browser)  ◄──reads───  /vault/status            ◄──written by──  ESP32
                ◄──reads───  /vault/events            ◄──appended by──
                ◄──reads───  /vault/leaderboard       ◄──appended by──
                ◄──reads───  /vault/commandResults    ◄──written by──
```

The ESP32 side is `esp32/src/cloud.cpp`. `esp32/tools/dashboard_sim.py` uses
the same paths from the command line, which is handy for testing without a page.
The rest of this file is the database layout that `vault-api.js` wraps. You
only need it if you change the interface or read the database some other way.

## `/vault/status` (ESP32 writes, dashboard reads)

Overwritten within about 1 s of any change, and at least every 10 s.

```json
{
  "state": "DIALING",
  "mcuOnline": true,
  "digitsEntered": 1,
  "dial": 7,
  "knocks": 0,
  "attempts": 2,
  "closeness": 85,
  "light": 2011,
  "lightValid": true,
  "beamEnabled": false,
  "inputsReady": true,
  "uptimeS": 734,
  "updatedAt": 1792577312345
}
```

| Field | Meaning |
|---|---|
| `state` | `IDLE`, `LOCKED` (armed, waiting for the knock), `DIALING` (knock accepted), `UNLOCKED` (cracked), `ALARM` (30 s lockout), or `UNKNOWN` (no word from the MCU yet) |
| `mcuOnline` | `false` if the MCU has stopped answering the ESP32. The other fields are then stale. |
| `digitsEntered`, `dial` | While DIALING: how many digits are confirmed (0–2), and the digit currently on the dial (0–9) |
| `knocks` | Knocks counted so far towards the secret rhythm (while LOCKED) |
| `attempts`, `closeness` | Guesses since arming, and how close the last one was (0–100, 100 = exact). Use it for a hot/cold hint. |
| `light`, `lightValid`, `beamEnabled` | Raw tripwire light level (0–4095); whether it is a real reading; whether beam-break detection is switched on |
| `inputsReady` | The tap sensor and dial are working |
| `updatedAt` | Server time in ms since 1970. **If it is more than ~30 s old, show the vault as offline**: the ESP32 has lost Wi-Fi or power. |

The secret code is never written to the database.

## `/vault/events/<id>` (ESP32 appends, dashboard reads)

A log, oldest first by key. Every event has `type` and `at` (server time, ms).

| `type` | Extra fields | When |
|---|---|---|
| `armed` | `player` | An ARM reached the MCU (from the dashboard or the ESP32 console) |
| `disarmed` | | A DISARM reached the MCU |
| `attempt` | `guess` ("428"), `attempt` (1, 2, …), `closeness` (0–100) | A wrong 3-digit guess |
| `solved` | `player`, `attempts`, `seconds` | The vault was cracked |
| `alarm` | `cause` (`"beam"` or `"tamper"`) | Beam broken or box shaken while armed. A 30 s lockout follows. |
| `mcu_online` / `mcu_offline` | | The MCU link came up or went down |

These are the events the LLM game master should react to (`attempt`, `alarm`, `solved`).

## `/vault/leaderboard/<id>` (ESP32 appends, dashboard reads)

```json
{ "player": "Alice", "attempts": 3, "seconds": 74, "at": 1792577312345 }
```

One entry per solve. Sort by `seconds` (then `attempts`) on the dashboard, or
with `orderByChild("seconds")`, which needs the `.indexOn` rule below.

## `/vault/commands/<id>` (dashboard writes, ESP32 consumes)

Push a new child for each command, so two quick clicks are both kept:

```json
{ "action": "arm", "code": "427", "player": "Alice", "at": {".sv": "timestamp"} }
{ "action": "disarm", "at": {".sv": "timestamp"} }
```

| Field | Notes |
|---|---|
| `action` | `"arm"` or `"disarm"` |
| `code` | `arm` only: 3 digits, as a string `"042"` or a number `42` |
| `player` | `arm` only, optional: name shown on the leaderboard if this round is solved (max 24 characters, default `"anonymous"`) |
| `pattern` | `arm` only, optional: knock-pattern number (0–255). Currently ignored by the MCU, which has one fixed rhythm. |

The ESP32 checks for commands once a second and handles them oldest first. It
sends each one to the MCU once, then **deletes it**. Commands written while the
ESP32 was off are deleted unhandled when it next connects. That stops a
stale `arm` from firing hours later.

## `/vault/commandResults/<id>` (ESP32 writes, dashboard reads)

Same `<id>` as the command. Watch it to show feedback for a button press:

```json
{ "action": "arm", "result": "delivered", "at": 1792577312345 }
```

`result` goes from `"sent"` (handed to the UART link) to `"delivered"` (the MCU
acknowledged it), or ends with one of:

- `"failed: MCU did not acknowledge"`
- `"rejected: MCU offline"`
- `"rejected: code must be 3 digits"`
- `"rejected: unknown action"`
- `"rejected: link busy"`

`delivered` means the MCU received the command. The game still decides: an
`arm` during an ALARM lockout, or a `disarm` during ALARM, is ignored by the
MCU. `/vault/status` shows the state that actually took effect.

## Firebase project setup (once)

1. Create a Firebase project, then **Build → Realtime Database → Create database**.
   Pick the Singapore region (`asia-southeast1`) and start in *locked mode*.
2. **Build → Authentication → Sign-in method → Anonymous → Enable.** The ESP32
   signs in anonymously. The dashboard should too, with
   `signInAnonymously(auth)` from the Firebase JS SDK.
3. **Realtime Database → Rules**, then paste and publish:

   ```json
   {
     "rules": {
       "vault": {
         ".read": "auth != null",
         ".write": "auth != null",
         "leaderboard": { ".indexOn": ["seconds"] }
       }
     }
   }
   ```

4. Copy the **database URL** (top of the Data tab) and the **Web API key**
   (Project settings → General) into `esp32/include/secrets.h`. For the
   dashboard, **Project settings → General → Your apps → Add app → Web** gives
   the `firebaseConfig` object to pass to `connectVault()`. Add `databaseURL` if
   it's missing.

These rules let anyone signed in, including anonymous users, read and write
`/vault`. That's fine for a class demo, but a public page would need stricter
rules (for example, only the ESP32's user may write `status` and `events`).
