// The dashboard's only interface to Firebase. Passwords never enter the database.
import { initializeApp, getApps } from "firebase/app";
import {
  getAuth,
  onAuthStateChanged,
  signInWithEmailAndPassword,
  signOut,
  setPersistence,
  browserSessionPersistence,
  connectAuthEmulator,
} from "firebase/auth";
import {
  getDatabase,
  onValue,
  onChildAdded,
  query,
  orderByChild,
  limitToLast,
  ref,
  push,
  set,
  serverTimestamp,
  connectDatabaseEmulator,
} from "firebase/database";

export const OFFLINE_AFTER_MS = 30000;
export const STATES = [
  "IDLE",
  "LOCKED",
  "DIALING",
  "UNLOCKED",
  "ALARM",
  "UNKNOWN",
];

export async function connectVault(config, { emulator = false } = {}) {
  const app =
    config?.options && config?.name
      ? config
      : getApps()[0] || initializeApp(config);
  const auth = getAuth(app);
  const db = getDatabase(app);
  if (emulator && !app.__vaultEmulators) {
    connectAuthEmulator(auth, "http://127.0.0.1:9099", {
      disableWarnings: true,
    });
    connectDatabaseEmulator(db, "127.0.0.1", 9000);
    app.__vaultEmulators = true;
  }
  await setPersistence(auth, browserSessionPersistence);
  let offset = 0;
  let connected = false;
  const cleanup = [
    onValue(ref(db, ".info/serverTimeOffset"), (snap) => {
      offset = snap.val() || 0;
    }),
    onValue(ref(db, ".info/connected"), (snap) => {
      connected = snap.val() === true;
    }),
  ];
  const serverNow = () => Date.now() + offset;
  let owner = false;

  function onOwner(cb, onError = () => {}) {
    let stopRole = () => {};
    const stopAuth = onAuthStateChanged(
      auth,
      (user) => {
        stopRole();
        owner = false;
        cb({ user, owner: false, checking: !!user });
        if (user)
          stopRole = onValue(
            ref(db, `access/owners/${user.uid}`),
            (snap) => {
              owner = snap.val() === true;
              cb({ user, owner, checking: false });
            },
            (error) => {
              owner = false;
              cb({ user, owner: false, checking: false });
              onError(error);
            },
          );
      },
      onError,
    );
    return () => {
      stopRole();
      stopAuth();
    };
  }
  function onStatus(cb, onError = () => {}) {
    let raw = null;
    const emit = () => {
      const ageMs = raw
        ? Math.max(0, serverNow() - (raw.updatedAt || 0))
        : null;
      cb(
        raw
          ? { ...raw, ageMs, espOnline: ageMs < OFFLINE_AFTER_MS }
          : {
              state: "UNKNOWN",
              espOnline: false,
              mcuOnline: false,
              ageMs: null,
            },
      );
    };
    const stop = onValue(
      ref(db, "vault/status"),
      (snap) => {
        raw = snap.val();
        emit();
      },
      onError,
    );
    const timer = setInterval(emit, 1000);
    return () => {
      stop();
      clearInterval(timer);
    };
  }
  function onEvents(cb, { limit = 100, onError = () => {} } = {}) {
    return onChildAdded(
      query(ref(db, "vault/events"), orderByChild("at"), limitToLast(limit)),
      (snap) => cb({ id: snap.key, ...snap.val() }),
      onError,
    );
  }
  // Snapshot subscription marks the initial history as a single batch, so it
  // cannot replay celebrations/sounds when a spectator opens the page.
  function onEventHistory(cb, onError = () => {}) {
    return onValue(
      query(ref(db, "vault/events"), orderByChild("at"), limitToLast(200)),
      (snap) => {
        const events = [];
        snap.forEach((child) => {
          events.push({ id: child.key, ...child.val() });
        });
        cb(events);
      },
      onError,
    );
  }
  function onLeaderboard(cb, { limit = 100, onError = () => {} } = {}) {
    return onValue(
      query(ref(db, "vault/leaderboard"), orderByChild("seconds")),
      (snap) => {
        const rows = [];
        snap.forEach((child) => {
          rows.push({ id: child.key, ...child.val() });
        });
        rows.sort(
          (a, b) =>
            a.seconds - b.seconds || a.attempts - b.attempts || a.at - b.at,
        );
        cb(rows.slice(0, limit));
      },
      onError,
    );
  }
  function onAiMessage(cb, onError = () => {}) {
    return onValue(
      ref(db, "vault/aiMessage"),
      (snap) => cb(snap.val()),
      onError,
    );
  }
  function onAiOwner(cb, onError = () => {}) {
    let enabled = true,
      health = null,
      loaded = false;
    const emit = () => cb({ enabled, health, loaded });
    const stops = [
      onValue(
        ref(db, "vault/aiSettings/enabled"),
        (snap) => {
          enabled = snap.val() !== false;
          loaded = true;
          emit();
        },
        onError,
      ),
      onValue(
        ref(db, "vault/aiStatus"),
        (snap) => {
          health = snap.val();
          emit();
        },
        onError,
      ),
    ];
    return () => stops.forEach((stop) => stop());
  }
  async function setAiEnabled(enabled) {
    if (!owner || !auth.currentUser) throw new Error("Owner access required.");
    if (!connected) throw new Error("The database connection is offline.");
    // Checkboxes use the server subscription, never an optimistic local value.
    let timer;
    try {
      await Promise.race([
        set(ref(db, "vault/aiSettings/enabled"), !!enabled),
        new Promise((_, reject) => {
          timer = setTimeout(
            () => reject(new Error("Setting update timed out.")),
            10000,
          );
        }),
      ]);
    } finally {
      clearTimeout(timer);
    }
  }
  async function sendCommand(command, { onUpdate, timeoutMs = 10000 } = {}) {
    if (!owner || !auth.currentUser)
      throw new Error("Sign in with the owner account to control the vault.");
    if (!connected) throw new Error("The database connection is offline.");
    return new Promise((resolve, reject) => {
      let done = false,
        stop = () => {},
        id = null;
      const finish = (result) => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        stop();
        resolve({ ok: result === "delivered", result, id });
      };
      // Includes the database write and acknowledgement. Never auto-resend.
      const timer = setTimeout(
        () =>
          finish(
            "timeout: the vault did not answer; check its state before trying again",
          ),
        timeoutMs,
      );
      push(ref(db, "vault/commands"), { ...command, at: serverTimestamp() })
        .then((cmdRef) => {
          id = cmdRef.key;
          if (done) return;
          stop = onValue(
            ref(db, `vault/commandResults/${cmdRef.key}`),
            (snap) => {
              const result = snap.val()?.result;
              if (!result || done) return;
              onUpdate?.(result);
              if (result !== "sent") finish(result);
            },
            (error) => {
              if (!done) {
                done = true;
                clearTimeout(timer);
                stop();
                reject(error);
              }
            },
          );
          if (done) stop();
        })
        .catch((error) => {
          if (!done) {
            done = true;
            clearTimeout(timer);
            stop();
            reject(error);
          }
        });
    });
  }

  function arm(code, { player = "anonymous", pattern, ...options } = {}) {
    const digits =
      typeof code === "number"
        ? String(code).padStart(3, "0")
        : String(code).trim();
    if (!/^\d{3}$/.test(digits))
      return Promise.resolve({
        ok: false,
        result: "Enter exactly three digits.",
        id: null,
      });
    const command = {
      action: "arm",
      code: digits,
      player: String(player).trim().slice(0, 24) || "anonymous",
    };
    if (pattern !== undefined) command.pattern = pattern;
    return sendCommand(command, options);
  }
  return {
    onStatus,
    onAiMessage,
    onAiOwner,
    setAiEnabled,
    onEvents,
    onEventHistory,
    onLeaderboard,
    onOwner,
    serverNow,
    arm,
    disarm: (options) => sendCommand({ action: "disarm" }, options),
    signIn: (email, password) =>
      signInWithEmailAndPassword(auth, email, password),
    signOut: () => signOut(auth),
    destroy: () => cleanup.forEach((stop) => stop()),
  };
}
