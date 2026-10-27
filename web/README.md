# The Vault dashboard

A public live game screen with an animated mechanical vault, round progress,
recent guesses, rankings, and a protected owner panel. React + TypeScript + Vite,
using Firebase Realtime Database through `vault-api.js`.

## Run it

From `Project/web/`:

```sh
npm ci
npm run dev
```

Open the URL printed by Vite. For a phone on the same network, use the laptop's
LAN address and the same port. `npm run dev` binds to the network interface;
allow that port through the laptop firewall if needed.

Open `http://localhost:5173/?preview=1` to review the design without Firebase or
hardware. Its scene selector covers IDLE, LOCKED, DIALING, UNLOCKED, ALARM and
UNKNOWN. Preview records are labelled sample data and never sent to Firebase.

The real page has empty rankings until actual scores arrive. Preview is not a
substitute for validating a physical round.

## Public Firebase configuration

Copy `.env.example` to `.env.local` and fill the four `VITE_FIREBASE_*` fields
from Firebase Project settings → your Web app. Restart Vite after changing it.
These fields are intentionally public browser configuration. Never place owner,
device or Wi-Fi passwords, or a service-account private key, in `VITE_*` values.

Alternatively, use **Connection** at the bottom of the dashboard and paste a
JSON object containing `apiKey`, `authDomain`, `databaseURL`, and `projectId`.
Only those fields are retained in local browser storage. The hosted production
build should supply the configuration through `.env.local` at build time so
visitors do not need to configure their own browsers.

External `Project_Secrets/secrets.h` is used only by firmware and the CLI
simulator. The web app never imports it or needs access to it.

## Migrate from the original demo rules

Do this in order so the existing ESP32 connection keeps working during setup.
Do not publish the protected rules while the board still relies on anonymous
sign-in. No rules or Hosting configuration are published by local builds.

1. Enable **Email/Password** under Firebase Authentication → Sign-in method.
2. Under Authentication → Users, manually create two different accounts:
   - One shared owner account for the project team.
   - One dedicated device account for the ESP32.
   Use different passwords. The web app has no public registration flow.
3. Copy each user's UID. In Realtime Database → Data, add these entries at the
   root, alongside the existing `vault` node. Use actual UIDs in place of the
   example names; preserve all existing game data:

   ```json
   {
     "access": {
       "owners": { "OWNER_UID": true },
       "devices": { "DEVICE_UID": true }
     }
   }
   ```

4. In the external `../Project_Secrets/secrets.h` relative to `Project/`, add:

   ```cpp
   #define FIREBASE_DEVICE_EMAIL "your-device-account-email"
   #define FIREBASE_DEVICE_PASSWORD "your-device-account-password"
   ```

   Keep the existing Wi-Fi settings, `FIREBASE_API_KEY` and database URL. Never
   use the owner credentials as the device credentials.
5. Rebuild/upload from `Project/esp32/`:

   ```sh
   pio run -e esp32s2 -t upload
   pio device monitor -b 115200
   ```

   Verify `signed in to Firebase (device account)` and continuing status
   updates. Missing device fields retain legacy anonymous sign-in for migration
   only; the protected rules will reject that identity.
6. Review `database.rules.json`, then publish it in Realtime Database → Rules,
   or deploy it with the Firebase CLI as described below. These rules replace
   the broad demo permissions. Do not retain a parent `auth != null` grant:
   parent grants would bypass the restrictions on private children.
7. Open the dashboard and verify public viewing. Open **Owner access** and
   sign in with the shared owner account. Its UID must be in `access/owners`.
   Sign-in uses tab/session persistence; passwords are not saved by this app.
8. Verify owner arm/disarm, status changes and alarms. Check the owner panel's
   diagnostics for disabled beam detection or an unavailable input service.

Anonymous sign-in is no longer needed by the dashboard, device or simulator
once migration is complete. It can be disabled after verification. Public
viewers read only public game data; they cannot read queued codes, send
commands, write results, or grant themselves owner access. The Firebase
Console remains the administrative place to manage roles.

## Controls and timing

The owner can enter or generate a code (000–999), set a player name, arm, and
disarm. A secret is masked by default, cleared after submission, and never
persisted by the page. Confirmed guess digits are masked on the shared screen;
submitted wrong guesses are visible as attempt history. Ranking uses MCU solve
seconds, then attempt count, then event time for stable ordering.

Controls are disabled during dialing, lockout, disconnected links, and pending
commands. Replacing a LOCKED round requires an explicit checkbox. Delivery
acknowledgement means the packet arrived; the page waits for a subsequent
matching MCU status before reporting success. Existing firmware does not echo
a command ID in STATUS, so this is observed state confirmation rather than
transaction-level proof of acceptance.

Elapsed and lockout timers use server-timestamped armed/alarm events, corrected
with Firebase's server clock offset. They are approximate: cloud delivery adds
latency. If history has no start, the timer displays unavailable. A zero lockout
countdown does not re-enable controls: MCU state must leave ALARM. Solve time
comes directly from the MCU. If the ESP32 reboots during a round, the firmware's
existing player association may reset to `console`; there is no durable round
ID in the current protocol.

The vault bubble currently shows short state guidance. Real LLM hints are a
separate phase. Sound starts muted. Reduced-motion preferences disable motion.

## Hosting

From `Project/web/`, after configuring and verifying the accounts/device:

```sh
npx firebase login
npm run build
npx firebase deploy --project YOUR_PROJECT_ID --only hosting,database
```

The build outputs `dist/`. `firebase.json` deploys it as a single-page site
with HTTPS, security headers, and long-lived asset caching. Use the explicit
project ID to avoid selecting the wrong Firebase project. Add the deployed
host to Authentication → Settings → Authorized domains if needed. For local
sign-in, add `localhost` to that list if Firebase rejects it.

Review the build and rules before running deploy. There is no automatic deploy
hook. For a Hosting-only visual update after migration:

```sh
npm run build
npx firebase deploy --project YOUR_PROJECT_ID --only hosting
```

## Validation

```sh
npm run build
npm test
npx playwright install chromium
npm run test:browser
npm run test:rules
npm run test:live
```

Database/auth emulator checks require Java 21+ and use only `demo-vault` on
loopback. Rules tests cover public reads, private command data, owner-only
commands, malformed requests, protected results, device publishing and roles.
Live browser tests cover owner sign-in, leading-zero codes, command rejection,
state confirmation, session reload, stale status, and historical solve display.
UI tests cover all states, phone layout, dialogs, muted sound and reduced motion.

Use `VITE_USE_EMULATORS=true` only with `npm run dev` for a manually seeded local
emulator. Production builds ignore this flag and never connect to emulators.
No physical sensor is simulated by changing Firebase status. Hardware acceptance
still needs the tap sensor fixed, then a wrong guess, solve and leaderboard write.

## Interface and database paths

`connectVault(publicConfig)` returns public subscriptions and owner operations:

| Method | Behaviour |
| --- | --- |
| `onStatus(cb, onError)` | State, dial, digit progress, sensor snapshot and online flags; refreshes staleness every second |
| `onEvents(cb, options)` | Incremental recent events, retaining the original helper contract |
| `onEventHistory(cb, onError)` | Snapshot of the last 200 events for round recovery without replaying effects |
| `onLeaderboard(cb, options)` | Fastest solves, sorted by time and attempts; dashboard limit 100 |
| `onOwner(cb, onError)` | Session identity, role lookup and checking state |
| `signIn(email, password)` / `signOut()` | Shared owner email/password session |
| `arm(code, options)` / `disarm(options)` | Owner-only command submission; existing payloads unchanged |
| `serverNow()` | Estimated Firebase server time |
| `destroy()` | Stops the connection's clock subscription; stop each UI subscription separately |

Existing `/vault` paths remain:

- `status`: public live snapshot, written only by the device.
- `events/<id>`: public armed, disarmed, attempt, solved, alarm and link events.
- `leaderboard/<id>`: public `{player, seconds, attempts, at}` scores.
- `commands/<id>`: private `{action, code?, player?, pattern?, at}` queue;
  owners create entries, the device consumes/deletes them.
- `commandResults/<id>`: private `{result, action?, at}` acknowledgements,
  written by the device and readable by the owner.

Player names and historical submitted guesses are public on this shared game
screen. The current secret code is private in the pending command queue and is
removed when the ESP32 consumes it. Anyone with owner credentials can operate
the vault; Firebase roles, not hidden buttons, enforce this distinction.
