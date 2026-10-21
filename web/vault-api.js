// vault-api.js - the web dashboard's interface to the vault.
//
// The dashboard never talks to the ESP32 directly: both sides use the same
// Firebase Realtime Database (layout in README.md). This module hides those
// paths, so the page only calls functions:
//
//   import { connectVault } from "./vault-api.js";
//
//   const vault = await connectVault(firebaseConfig);   // from the Firebase console
//   vault.onStatus((s) => showState(s.state, s.espOnline));
//   vault.onEvents((e) => addToLog(e));
//   vault.onLeaderboard((rows) => showLeaderboard(rows));
//
//   const r = await vault.arm("427", { player: "Alice", onUpdate: showProgress });
//   if (!r.ok) alert(r.result);
//
// Plain ES module: load the page with <script type="module"> from a web server
// (e.g. `python3 -m http.server` in this folder), not by opening it as file://.
// With npm + a bundler, change the three import URLs to "firebase/app",
// "firebase/auth" and "firebase/database".
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.14.1/firebase-app.js";
import { getAuth, signInAnonymously } from "https://www.gstatic.com/firebasejs/10.14.1/firebase-auth.js";
import {
  getDatabase,
  limitToFirst,
  limitToLast,
  onChildAdded,
  onValue,
  orderByChild,
  orderByKey,
  push,
  query,
  ref,
  serverTimestamp,
} from "https://www.gstatic.com/firebasejs/10.14.1/firebase-database.js";

const ROOT = "vault";

/** The ESP32 rewrites status at least every 10 s; older than this = offline. */
export const OFFLINE_AFTER_MS = 30000;

/** Values of status.state. */
export const STATES = ["IDLE", "LOCKED", "DIALING", "UNLOCKED", "ALARM", "UNKNOWN"];

/**
 * Sign in (anonymously) and return the vault interface.
 *
 * @param {object} config  Firebase web config ({ apiKey, authDomain, databaseURL,
 *                         projectId, ... }), or an app from initializeApp().
 *                         databaseURL is required for non-US databases.
 */
export async function connectVault(config) {
  const app = config && config.options && config.name ? config : initializeApp(config);
  await signInAnonymously(getAuth(app)); // needs Anonymous sign-in enabled
  const db = getDatabase(app);

  // Server clock minus ours, so "is the ESP32 online?" survives a wrong PC clock.
  let serverOffset = 0;
  onValue(ref(db, ".info/serverTimeOffset"), (snap) => {
    serverOffset = snap.val() || 0;
  });
  const serverNow = () => Date.now() + serverOffset;

  function withOnline(raw) {
    if (!raw) {
      return { state: "UNKNOWN", espOnline: false, mcuOnline: false, ageMs: null };
    }
    const ageMs = serverNow() - (raw.updatedAt || 0);
    return { ...raw, espOnline: ageMs < OFFLINE_AFTER_MS, ageMs };
  }

  /**
   * Live vault status. cb(status) runs on every change, and also when the
   * ESP32 goes offline (no update for OFFLINE_AFTER_MS).
   * status = { state, espOnline, mcuOnline, digitsEntered, dial, knocks,
   *            attempts, closeness, light, lightValid, beamEnabled,
   *            inputsReady, uptimeS, updatedAt, ageMs }
   * Returns a function that stops listening.
   */
  function onStatus(cb) {
    let raw = null;
    let lastOnline = null;
    const emit = () => {
      const s = withOnline(raw);
      lastOnline = s.espOnline;
      cb(s);
    };
    const stop = onValue(ref(db, `${ROOT}/status`), (snap) => {
      raw = snap.val();
      emit();
    });
    const timer = setInterval(() => {
      if (withOnline(raw).espOnline !== lastOnline) emit();
    }, 5000);
    return () => {
      stop();
      clearInterval(timer);
    };
  }

  /**
   * Game events, oldest first: the last `limit` existing ones, then each new one.
   * cb(event), event = { id, type, at, ...fields }, type is one of
   * armed, disarmed, attempt, solved, alarm, mcu_online, mcu_offline.
   * Returns a function that stops listening.
   */
  function onEvents(cb, { limit = 50 } = {}) {
    const q = query(ref(db, `${ROOT}/events`), orderByKey(), limitToLast(limit));
    return onChildAdded(q, (snap) => cb({ id: snap.key, ...snap.val() }));
  }

  /**
   * Fastest solves. cb(rows) with rows sorted by seconds, then attempts:
   * [{ id, player, attempts, seconds, at }, ...]. Runs again on every new solve.
   * Returns a function that stops listening.
   */
  function onLeaderboard(cb, { limit = 10 } = {}) {
    const q = query(ref(db, `${ROOT}/leaderboard`), orderByChild("seconds"), limitToFirst(limit));
    return onValue(q, (snap) => {
      const rows = [];
      snap.forEach((child) => {
        rows.push({ id: child.key, ...child.val() });
      });
      rows.sort((a, b) => a.seconds - b.seconds || a.attempts - b.attempts);
      cb(rows);
    });
  }

  // Push a command and wait for the ESP32's verdict in commandResults.
  function sendCommand(command, { onUpdate, timeoutMs = 10000 } = {}) {
    return new Promise((resolve, reject) => {
      push(ref(db, `${ROOT}/commands`), { ...command, at: serverTimestamp() })
        .then((cmdRef) => {
          let done = false;
          let stop = null;
          const finish = (result) => {
            if (done) return;
            done = true;
            clearTimeout(timer);
            if (stop) stop();
            resolve({ ok: result === "delivered", result, id: cmdRef.key });
          };
          const timer = setTimeout(
            () => finish("timeout: no answer from the vault (is the ESP32 online?)"),
            timeoutMs,
          );
          stop = onValue(ref(db, `${ROOT}/commandResults/${cmdRef.key}`), (snap) => {
            const res = snap.val();
            if (!res || done) return;
            if (onUpdate) onUpdate(res.result);
            if (res.result !== "sent") finish(res.result);
          });
          if (done) stop(); // answered before onValue() returned
        })
        .catch(reject);
    });
  }

  /**
   * Arm the vault with a 3-digit code. Resolves with { ok, result, id }:
   * ok is true once the MCU acknowledged it ("delivered"); otherwise result
   * explains why (see README.md). onUpdate(result) also sees "sent" first.
   * Delivered is not the same as accepted: watch onStatus for LOCKED.
   */
  function arm(code, { player = "anonymous", pattern, onUpdate, timeoutMs } = {}) {
    const digits = typeof code === "number" ? String(code).padStart(3, "0") : String(code).trim();
    if (!/^\d{3}$/.test(digits)) {
      return Promise.resolve({ ok: false, result: "rejected: code must be 3 digits", id: null });
    }
    const command = { action: "arm", code: digits, player: String(player).slice(0, 24) };
    if (pattern !== undefined) command.pattern = pattern;
    return sendCommand(command, { onUpdate, timeoutMs });
  }

  /** Disarm (back to IDLE). Same result as arm(). Ignored by the MCU during ALARM. */
  function disarm({ onUpdate, timeoutMs } = {}) {
    return sendCommand({ action: "disarm" }, { onUpdate, timeoutMs });
  }

  return { onStatus, onEvents, onLeaderboard, arm, disarm, db };
}
