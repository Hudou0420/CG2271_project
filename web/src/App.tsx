import {
  useEffect,
  useRef,
  useState,
  type FormEvent,
  type ReactNode,
} from "react";
import {
  ArrowDown,
  ArrowRight,
  ArrowUpRight,
  Check,
  ChevronRight,
  CircleHelp,
  Clock3,
  Eye,
  EyeOff,
  Fingerprint,
  KeyRound,
  LockKeyhole,
  Radio,
  RotateCcw,
  Settings2,
  ShieldCheck,
  Shuffle,
  Trophy,
  Volume2,
  VolumeX,
  WifiOff,
  X,
} from "lucide-react";
import { connectVault } from "../vault-api.js";
import Vault from "./components/Vault";
import { LiveDigitTiles, GuessTiles } from "./components/DigitTiles";
import { CONFIG_KEY, getConfig, parseFirebaseConfig } from "./config";
import {
  visibleAiMessage,
  attemptFeedback,
  type AiMessage,
  type AiHealth,
  canControl,
  COPY,
  EMPTY_STATUS,
  formatTime,
  randomCode,
  rankScores,
  roundContext,
  sortedEvents,
  STATES,
  timerValues,
  type GameEvent,
  type Score,
  type State,
  type Status,
} from "./model";
import { previewData } from "./preview";

type Api = Awaited<ReturnType<typeof connectVault>>;
type Modal = "owner" | "scores" | "help" | "connection" | null;
const isPreview = new URLSearchParams(location.search).get("preview") === "1";
const defaultPreview = (new URLSearchParams(location.search).get("state") ||
  "LOCKED") as State;

function ModalShell({
  title,
  eyebrow,
  close,
  children,
}: {
  title: string;
  eyebrow: string;
  close: () => void;
  children: ReactNode;
}) {
  const dialog = useRef<HTMLDialogElement>(null);
  useEffect(() => {
    dialog.current?.showModal();
  }, []);
  return (
    <dialog
      ref={dialog}
      className="modal"
      onCancel={close}
      onClick={(e) => {
        if (e.target === e.currentTarget) close();
      }}
      aria-labelledby="modal-title"
    >
      <div className="modal-header">
        <div>
          <span className="eyebrow">{eyebrow}</span>
          <h2 id="modal-title">{title}</h2>
        </div>
        <button
          className="icon-button"
          aria-label="Close panel"
          onClick={close}
        >
          <X size={22} />
        </button>
      </div>
      {children}
    </dialog>
  );
}
function soundFor(state: State) {
  const Context = window.AudioContext;
  if (!Context) return;
  const context = new Context();
  const notes =
    state === "UNLOCKED"
      ? [523, 659, 784, 1047]
      : state === "ALARM"
        ? [220, 165]
        : [];
  notes.forEach((note, i) => {
    const osc = context.createOscillator();
    const gain = context.createGain();
    osc.type = "triangle";
    osc.frequency.value = note;
    gain.gain.setValueAtTime(0.045, context.currentTime + i * 0.12);
    gain.gain.exponentialRampToValueAtTime(
      0.001,
      context.currentTime + i * 0.12 + 0.18,
    );
    osc.connect(gain);
    gain.connect(context.destination);
    osc.start(context.currentTime + i * 0.12);
    osc.stop(context.currentTime + i * 0.12 + 0.2);
  });
  setTimeout(() => void context.close(), 1000);
}
function errorText(error: unknown) {
  const code = (error as { code?: string })?.code || "";
  if (code.includes("permission-denied") || code.includes("PERMISSION_DENIED"))
    return "Access isn’t ready. Check the published database rules and owner account.";
  if (
    /auth\/(invalid-credential|wrong-password|user-not-found|invalid-email)/.test(
      code,
    )
  )
    return "That email and password didn’t match an account.";
  if (code.includes("too-many-requests"))
    return "Too many sign-in attempts. Wait a moment and try again.";
  if (code.includes("network"))
    return "Connection interrupted. Check your internet connection and retry.";
  return "Couldn’t complete that request. Check the connection and try again.";
}

export default function App() {
  const [config] = useState(getConfig);
  const [api, setApi] = useState<Api | null>(null);
  const [status, setStatus] = useState<Status>(EMPTY_STATUS);
  const [aiMessage, setAiMessage] = useState<AiMessage | null>(null);
  const [events, setEvents] = useState<GameEvent[]>([]);
  const [scores, setScores] = useState<Score[]>([]);
  const [owner, setOwner] = useState(false);
  const [signedIn, setSignedIn] = useState(false);
  const [checkingOwner, setCheckingOwner] = useState(false);
  const [modal, setModal] = useState<Modal>(null);
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [busy, setBusy] = useState(false);
  const [sound, setSound] = useState(false);
  const [now, setNow] = useState(Date.now());
  const [previewState, setPreviewState] = useState<State>(
    [...STATES, "UNKNOWN"].includes(defaultPreview) ? defaultPreview : "LOCKED",
  );
  const [celebrate, setCelebrate] = useState(false);
  const statusRef = useRef(status);
  statusRef.current = status;
  const soundRef = useRef(sound);
  soundRef.current = sound;
  const pending = useRef<{
    expected: State;
    after: number;
    timer: ReturnType<typeof setTimeout>;
  } | null>(null);

  useEffect(() => {
    if (isPreview) {
      const data = previewData(previewState);
      setStatus(data.status);
      setEvents(data.events);
      setScores(data.scores);
      return;
    }
    if (!config) return;
    let disposed = false;
    let connection: Api | null = null;
    const stops: (() => void)[] = [];
    const failed = (e: unknown) => {
      if (!disposed) setError(errorText(e));
    };
    connectVault(config, {
      emulator:
        import.meta.env.DEV && import.meta.env.VITE_USE_EMULATORS === "true",
    })
      .then((client) => {
        if (disposed) {
          client.destroy();
          return;
        }
        connection = client;
        setApi(client);
        stops.push(
          client.onStatus((s: Status) => {
            setStatus(s);
          }, failed),
        );
        stops.push(client.onAiMessage(setAiMessage, failed));
        stops.push(
          client.onEventHistory(
            (rows: GameEvent[]) => setEvents(sortedEvents(rows)),
            failed,
          ),
        );
        stops.push(
          client.onLeaderboard((rows: Score[]) => setScores(rankScores(rows)), {
            limit: 100,
            onError: failed,
          }),
        );
        stops.push(
          client.onOwner(
            (s: { user: unknown; owner: boolean; checking: boolean }) => {
              setSignedIn(!!s.user);
              setOwner(s.owner);
              setCheckingOwner(s.checking);
            },
            failed,
          ),
        );
      })
      .catch(failed);
    return () => {
      disposed = true;
      stops.forEach((stop) => stop());
      connection?.destroy();
    };
  }, [config, previewState]);
  useEffect(() => {
    const tick = setInterval(
      () => setNow(api?.serverNow() ?? Date.now()),
      1000,
    );
    return () => clearInterval(tick);
  }, [api]);
  useEffect(
    () => () => {
      if (pending.current) clearTimeout(pending.current.timer);
    },
    [],
  );
  useEffect(() => {
    const command = pending.current;
    if (
      command &&
      status.state === command.expected &&
      (status.updatedAt || 0) > command.after &&
      status.espOnline &&
      status.mcuOnline
    ) {
      clearTimeout(command.timer);
      pending.current = null;
      setBusy(false);
      setNotice(
        command.expected === "LOCKED"
          ? "Round armed. Ready for the first knock."
          : "Vault is on standby.",
      );
    }
  }, [status]);
  const previous = useRef<State | null>(null);
  useEffect(() => {
    const before = previous.current;
    previous.current = status.state;
    if (
      before &&
      before !== "UNKNOWN" &&
      before !== status.state &&
      status.espOnline &&
      status.mcuOnline
    ) {
      if (status.state === "UNLOCKED" || status.state === "ALARM") {
        setCelebrate(true);
        if (soundRef.current) soundFor(status.state);
        const timer = setTimeout(() => setCelebrate(false), 1800);
        return () => clearTimeout(timer);
      }
    }
  }, [status.state, status.espOnline, status.mcuOnline]);

  const online = status.espOnline && status.mcuOnline;
  const state: State = online
    ? COPY[status.state]
      ? status.state
      : "UNKNOWN"
    : "UNKNOWN";
  const copy = COPY[state];
  const reaction = visibleAiMessage(aiMessage, status, events);
  const round = roundContext(events);
  const timers = timerValues(events, state, now);
  const controls = canControl(status, busy, owner);
  const attempts =
    state === "IDLE" || state === "UNKNOWN"
      ? []
      : [...round.attempts].reverse();
  const top = scores.slice(0, 3);

  async function command(action: "arm" | "disarm", code = "", player = "") {
    if (!api || !canControl(statusRef.current, busy, owner)) return;
    setBusy(true);
    setError("");
    setNotice("Sending to the vault…");
    const after = statusRef.current.updatedAt || 0;
    try {
      const onUpdate = () =>
        setNotice("Delivered to the link. Waiting for the vault…");
      const result =
        action === "arm"
          ? await api.arm(code, { player, onUpdate })
          : await api.disarm({ onUpdate });
      if (!result.ok) {
        setBusy(false);
        setNotice(result.result);
        return;
      }
      const expected = action === "arm" ? "LOCKED" : "IDLE";
      const current = statusRef.current;
      if (
        current.state === expected &&
        (current.updatedAt || 0) > after &&
        current.espOnline &&
        current.mcuOnline
      ) {
        setNotice(
          action === "arm"
            ? "Round armed. Ready for the first knock."
            : "Vault is on standby.",
        );
        setBusy(false);
        return;
      }
      setNotice("Command delivered. Waiting for the state to update…");
      const timer = setTimeout(() => {
        pending.current = null;
        setBusy(false);
        setNotice(
          "Delivered, but the state wasn’t confirmed. Check the vault before trying again.",
        );
      }, 12000);
      pending.current = { expected, after, timer };
    } catch (e) {
      setError(errorText(e));
      setBusy(false);
      setNotice("");
    }
  }

  return (
    <div className={`app theme-${state.toLowerCase()}`}>
      {isPreview && (
        <div className="preview-bar">
          <span>DESIGN PREVIEW · SAMPLE DATA · NO DEVICE COMMANDS</span>
          <label>
            Scene{" "}
            <select
              value={previewState}
              onChange={(e) => setPreviewState(e.target.value as State)}
            >
              {[...STATES, "UNKNOWN"].map((s) => (
                <option key={s}>{s}</option>
              ))}
            </select>
          </label>
          <a href="/">
            Exit preview <ArrowUpRight size={13} />
          </a>
        </div>
      )}
      <header className="site-header">
        <a className="brand" href="/" aria-label="The Vault home">
          <span className="brand-icon">
            <LockKeyhole size={21} strokeWidth={2.5} />
          </span>
          <span>
            THE VAULT<span className="brand-period">.</span>
          </span>
        </a>
        <div className="header-centre">
          <span className="tiny-star">✳</span> A LITTLE NERVE. A LITTLE LUCK.
        </div>
        <nav aria-label="Dashboard controls">
          <span className={`connection-badge ${online ? "connected" : ""}`}>
            <i />
            {online
              ? "VAULT ONLINE"
              : !config && !isPreview
                ? "NOT CONNECTED"
                : "VAULT OFFLINE"}
          </span>
          <button
            className="icon-button sound-button"
            aria-label={sound ? "Mute sound" : "Enable sound"}
            aria-pressed={sound}
            onClick={() => setSound(!sound)}
          >
            {sound ? <Volume2 size={19} /> : <VolumeX size={19} />}
          </button>
          <button
            className="owner-button"
            aria-label={owner ? "Owner panel" : "Owner access"}
            onClick={() => setModal("owner")}
          >
            <Settings2 size={16} />
            <span>{owner ? "Owner panel" : "Owner access"}</span>
          </button>
        </nav>
      </header>

      <main>
        {!config && !isPreview && (
          <div className="setup-banner">
            <Radio size={18} />
            <span>
              Connect this screen to your vault to follow the live game.
            </span>
            <button onClick={() => setModal("connection")}>
              Set up connection <ArrowRight size={15} />
            </button>
          </div>
        )}
        {error && (
          <div className="error-banner" role="alert">
            <WifiOff size={17} />
            <span>{error}</span>
            <button aria-label="Dismiss error" onClick={() => setError("")}>
              <X size={16} />
            </button>
          </div>
        )}
        {notice && (
          <div className="notice-banner" role="status">
            <Check size={17} />
            <span>{notice}</span>
            <button
              aria-label="Dismiss notification"
              onClick={() => setNotice("")}
            >
              <X size={16} />
            </button>
          </div>
        )}
        <div className="page-heading">
          <div>
            <p className="eyebrow">
              <span className="mini-line" /> THE LIVE HEIST
            </p>
            <h1>
              Small vault.
              <br />
              <span>Big stakes.</span>
            </h1>
          </div>
          <button
            className="text-button how-to"
            onClick={() => setModal("help")}
          >
            <CircleHelp size={17} /> How to play <ArrowUpRight size={16} />
          </button>
        </div>
        <div className="dashboard-grid">
          <section className="game-panel" aria-label="Live game">
            <div className="game-topline">
              <span className={`state-tag tag-${state.toLowerCase()}`}>
                <span className="status-dot" />
                {copy.label}
              </span>
              <span className="round-tag">
                {state === "IDLE" || state === "UNKNOWN"
                  ? "AWAITING CHALLENGER"
                  : "THREE DIGITS. ONE SHOT AT GLORY."}
              </span>
            </div>
            <div className="stage">
              <div className="stage-grid" />
              <div className="stage-heading">
                <p className="eyebrow">
                  {state === "ALARM"
                    ? "SECURITY HAS ENTERED THE CHAT"
                    : state === "UNLOCKED"
                      ? "CONSIDER THIS A COMPLIMENT"
                      : "THE VAULT IS WATCHING"}
                </p>
                <h2>{copy.title}</h2>
              </div>
              <div className="speech-bubble" aria-live="polite">
                <span className="bubble-dot" />
                <span>{reaction?.message ?? copy.bubble}</span>
                {reaction && (
                  <small className="ai-source">
                    {reaction.source === "gemini" ? "Gemini" : "Local"}
                  </small>
                )}
              </div>
              <Vault
                state={state}
                dial={status.dial || 0}
                celebrate={celebrate}
              />
              <span className="stage-sticker">
                {state === "UNLOCKED"
                  ? "NICE\nWORK!"
                  : state === "ALARM"
                    ? "UH\nOH!"
                    : "CRACK\nME."}
              </span>
              <div className="stage-instruction">
                <span className="instruction-icon">
                  {state === "DIALING" ? (
                    <RotateCcw size={18} />
                  ) : state === "UNLOCKED" ? (
                    <Check size={18} />
                  ) : (
                    <Fingerprint size={18} />
                  )}
                </span>
                <p>{copy.hint}</p>
              </div>
            </div>
            <div className="game-console">
              <div className="console-digit">
                <span className="eyebrow">ON THE DIAL</span>
                <strong>
                  {state === "DIALING" ? (status.dial ?? "—") : "—"}
                </strong>
              </div>
              <div className="combination">
                <span className="eyebrow">YOUR COMBINATION</span>
                <LiveDigitTiles state={state} entered={status.digitsEntered} dial={status.dial} />
              </div>
              <div className="knock-progress">
                <span className="eyebrow">SECRET KNOCK</span>
                <div
                  aria-label={`${state === "LOCKED" ? status.knocks || 0 : 0} knocks captured`}
                >
                  {[0, 1, 2].map((i) => (
                    <i
                      key={i}
                      className={
                        state === "LOCKED" && i < (status.knocks || 0)
                          ? "captured"
                          : state === "DIALING" || state === "UNLOCKED"
                            ? "captured"
                            : ""
                      }
                    />
                  ))}
                </div>
                <small>
                  {state === "DIALING" || state === "UNLOCKED"
                    ? "Rhythm accepted"
                    : "Make it count"}
                </small>
              </div>
            </div>
            <div className="round-stats">
              <div>
                <span className="eyebrow">AT THE VAULT</span>
                <strong>
                  {state !== "IDLE" && state !== "UNKNOWN"
                    ? round.armed?.player || "Challenger"
                    : "Your name here"}
                </strong>
              </div>
              <div>
                <span className="eyebrow">ATTEMPTS</span>
                <strong>
                  {online && state !== "IDLE"
                    ? String(status.attempts || 0).padStart(2, "0")
                    : "—"}
                </strong>
              </div>
              <div>
                <span className="eyebrow">
                  <Clock3 size={12} />
                  {state === "ALARM"
                    ? "LOCKOUT · APPROX."
                    : state === "UNLOCKED"
                      ? "FINAL TIME"
                      : "ELAPSED · APPROX."}
                </span>
                <strong>
                  {formatTime(
                    state === "ALARM" ? timers.lockout : timers.elapsed,
                  )}
                </strong>
                {state === "ALARM" && timers.lockout === 0 && (
                  <small>Waiting for the vault</small>
                )}
              </div>
            </div>
          </section>

          <aside className="side-rail">
            <section className="scores-panel">
              <div className="section-top">
                <span className="section-number">01 / THE FASTEST HANDS</span>
                <Trophy size={19} />
              </div>
              <h2>
                The clean
                <br />
                <span>getaways.</span>
              </h2>
              <p className="section-description">
                Quick fingers. Permanent bragging rights.
              </p>
              {top.length ? (
                <ol className="podium-list">
                  {top.map((score, i) => (
                    <li key={score.id}>
                      <span className={`rank rank-${i}`}>
                        {String(i + 1).padStart(2, "0")}
                      </span>
                      <div>
                        <strong>{score.player}</strong>
                        <small>
                          {score.attempts}{" "}
                          {score.attempts === 1 ? "attempt" : "attempts"}
                        </small>
                      </div>
                      <span className="score-time">
                        {formatTime(score.seconds)}
                      </span>
                      {i === 0 && <span className="best-tag">TO BEAT</span>}
                    </li>
                  ))}
                </ol>
              ) : (
                <div className="empty-scores">
                  <Trophy size={30} />
                  <strong>First place is wide open.</strong>
                  <span>Crack the vault to claim it.</span>
                </div>
              )}
              <button
                className="rankings-button"
                onClick={() => setModal("scores")}
              >
                The full leaderboard <ArrowUpRight size={18} />
              </button>
            </section>
            <section className="attempts-panel">
              <div className="section-top">
                <span className="section-number">02 / LEAVING A TRACE</span>
                <Fingerprint size={19} />
              </div>
              <div className="attempt-heading">
                <h2>Close calls.</h2>
                <span className="count-pill">{attempts.length}</span>
              </div>
              <p className="section-description">
                Green: right spot. Blue: elsewhere. Red: absent.
              </p>
              {attempts.length ? (
                <ol className="attempt-list">
                  {attempts.slice(0, 4).map((e) => {
                    const heat = attemptFeedback(e.closeness);
                    return (
                      <li key={e.id}>
                        <span className="attempt-index">
                          #{String(e.attempt || 0).padStart(2, "0")}
                        </span>
                        <GuessTiles guess={e.guess} closeness={e.closeness} feedback={e.feedback} />
                        <div className="heat-score">
                          <span>
                            {heat.band.toUpperCase()}{" "}
                            <b>{heat.score}%</b>
                          </span>
                          <i>
                            <i
                              style={{
                                width: `${heat.score}%`,
                              }}
                            />
                          </i>
                        </div>
                      </li>
                    );
                  })}
                </ol>
              ) : (
                <div className="empty-attempts">
                  <span>— — —</span>
                  <p>
                    No guesses yet.
                    <br />
                    Every good heist starts somewhere.
                  </p>
                </div>
              )}
              <div className="rail-note">
                <span>↳</span> The lock makes the call. We keep the receipts.
              </div>
            </section>
            <div className="physical-note">
              <span className="tiny-star">✳</span>
              <p>
                Real dial. Real knocks.
                <br />
                <strong>All the action happens at the box.</strong>
              </p>
              <ArrowDown size={20} />
            </div>
          </aside>
        </div>
        <footer>
          <span>
            THE VAULT <span className="footer-dot">/</span> CG2271
          </span>
          <span>BUILT FOR THE HEIST. PLAYED IN REAL LIFE.</span>
          <button onClick={() => setModal("connection")}>
            Connection <ArrowUpRight size={12} />
          </button>
        </footer>
      </main>
      {modal === "owner" && (
        <ModalShell
          title={owner ? "Set the next heist." : "Owner access."}
          eyebrow="BEHIND THE DOOR"
          close={() => setModal(null)}
        >
          <OwnerPanel
            api={api}
            owner={owner}
            signedIn={signedIn}
            checking={checkingOwner}
            enabled={controls}
            status={status}
            busy={busy}
            run={command}
            report={setError}
          />
        </ModalShell>
      )}
      {modal === "scores" && (
        <ModalShell
          title="The clean getaways."
          eyebrow="HALL OF FAME"
          close={() => setModal(null)}
        >
          <p className="modal-intro">
            Fastest time wins. Fewer attempts break a tie.
          </p>
          {scores.length ? (
            <ol className="full-rankings">
              {scores.map((s, i) => (
                <li key={s.id}>
                  <span>{String(i + 1).padStart(2, "0")}</span>
                  <strong>{s.player}</strong>
                  <small>{s.attempts} attempts</small>
                  <b>{formatTime(s.seconds)}</b>
                </li>
              ))}
            </ol>
          ) : (
            <div className="modal-empty">
              No scores yet. The first spot could be yours.
            </div>
          )}
        </ModalShell>
      )}
      {modal === "help" && (
        <ModalShell
          title="A little nerve helps."
          eyebrow="HOW TO PLAY"
          close={() => setModal(null)}
        >
          <ol className="help-steps">
            <li>
              <span>01</span>
              <div>
                <h3>Wait for your round.</h3>
                <p>The owner sets your name and the secret code.</p>
              </div>
            </li>
            <li>
              <span>02</span>
              <div>
                <h3>Knock your way in.</h3>
                <p>
                  Find the secret rhythm. When the vault accepts it, you can
                  start dialing.
                </p>
              </div>
            </li>
            <li>
              <span>03</span>
              <div>
                <h3>Turn. Press. Repeat.</h3>
                <p>
                  Turn the physical dial to choose a digit. Press it to confirm.
                  Enter three digits to submit your guess.
                </p>
              </div>
            </li>
            <li>
              <span>04</span>
              <div>
                <h3>Keep your hands clean.</h3>
                <p>
                  A wrong guess sends you back to the knock. Disturb the vault
                  and you could trigger a 30-second lockout.
                </p>
              </div>
            </li>
          </ol>
          <p className="muted-note">
            The game runs on the physical vault. This screen follows along.
          </p>
        </ModalShell>
      )}
      {modal === "connection" && (
        <ModalShell
          title="Connect your vault."
          eyebrow="SCREEN SETTINGS"
          close={() => setModal(null)}
        >
          <ConnectionPanel config={config} preview={isPreview} />
        </ModalShell>
      )}
    </div>
  );
}

function OwnerPanel({
  api,
  owner,
  signedIn,
  checking,
  enabled,
  status,
  busy,
  run,
  report,
}: {
  api: Api | null;
  owner: boolean;
  signedIn: boolean;
  checking: boolean;
  enabled: boolean;
  status: Status;
  busy: boolean;
  run: (
    action: "arm" | "disarm",
    code?: string,
    player?: string,
  ) => Promise<void>;
  report: (message: string) => void;
}) {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [player, setPlayer] = useState("");
  const [code, setCode] = useState("");
  const [reveal, setReveal] = useState(false);
  const [localBusy, setLocalBusy] = useState(false);
  const [localError, setLocalError] = useState("");
  const [replace, setReplace] = useState(false);
  async function login(e: FormEvent) {
    e.preventDefault();
    if (!api) return;
    setLocalBusy(true);
    setLocalError("");
    try {
      await api.signIn(email, password);
      setPassword("");
    } catch (error) {
      setLocalError(errorText(error));
      setPassword("");
    } finally {
      setLocalBusy(false);
    }
  }
  async function arm(e: FormEvent) {
    e.preventDefault();
    if (!/^\d{3}$/.test(code)) {
      setLocalError("Enter exactly three digits.");
      return;
    }
    const submitted = code;
    setCode("");
    setReveal(false);
    setReplace(false);
    await run("arm", submitted, player);
  }
  if (isPreview)
    return (
      <p className="modal-intro">
        This is a design preview with sample data. Open the live dashboard to
        sign in and control the physical vault.
      </p>
    );
  if (!api)
    return (
      <p className="modal-intro">
        Set up this screen’s Firebase connection first, then come back to sign
        in.
      </p>
    );
  if (checking)
    return (
      <p className="modal-intro" role="status">
        Checking owner access…
      </p>
    );
  if (signedIn && !owner)
    return (
      <>
        <p className="modal-intro">
          This account doesn’t have owner access. Ask the project team to check
          the owner UID in Firebase.
        </p>
        <button
          className="primary-button"
          onClick={() => void api.signOut().catch((e) => report(errorText(e)))}
        >
          Sign out
        </button>
      </>
    );
  if (!owner)
    return (
      <form onSubmit={login} className="owner-form">
        <p className="modal-intro">
          For the project team. Everyone else can watch the game without signing
          in.
        </p>
        <label>
          Email
          <input
            type="email"
            autoComplete="username"
            value={email}
            onChange={(e) => setEmail(e.target.value)}
            required
          />
        </label>
        <label>
          Password
          <input
            type="password"
            autoComplete="current-password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            required
          />
        </label>
        {localError && (
          <p className="form-error" role="alert">
            {localError}
          </p>
        )}
        <button className="primary-button" disabled={localBusy}>
          {localBusy ? "Signing in…" : "Sign in"}
          <ArrowRight size={18} />
        </button>
        <p className="muted-note">
          <ShieldCheck size={14} /> Access is checked by Firebase, not just this
          screen.
        </p>
      </form>
    );
  return (
    <>
      <form className="owner-form" onSubmit={arm}>
        <p className="modal-intro">
          Set a challenger and a secret code. You can run this panel from your
          phone while the audience watches the main screen.
        </p>
        <label>
          Challenger
          <input
            value={player}
            onChange={(e) => setPlayer(e.target.value)}
            placeholder="Player name"
            maxLength={24}
            required
            autoComplete="off"
          />
        </label>
        <label>
          Secret code
          <div className="code-input">
            <input
              aria-label="Secret code"
              type={reveal ? "text" : "password"}
              inputMode="numeric"
              pattern="[0-9]{3}"
              maxLength={3}
              value={code}
              onChange={(e) =>
                setCode(e.target.value.replace(/\D/g, "").slice(0, 3))
              }
              placeholder="•••"
              autoComplete="off"
              required
            />
            <button
              type="button"
              aria-label={reveal ? "Hide code" : "Show code"}
              onClick={() => setReveal(!reveal)}
            >
              {reveal ? <EyeOff size={18} /> : <Eye size={18} />}
            </button>
            <button
              className="random-button"
              type="button"
              onClick={() => setCode(randomCode())}
            >
              <Shuffle size={16} /> Random
            </button>
          </div>
        </label>
        {status.state === "LOCKED" && (
          <label className="checkbox-label">
            <input
              type="checkbox"
              checked={replace}
              onChange={(e) => setReplace(e.target.checked)}
            />{" "}
            Replace the current round and reset its progress
          </label>
        )}
        {localError && (
          <p className="form-error" role="alert">
            {localError}
          </p>
        )}
        <button
          className="primary-button"
          disabled={
            !enabled ||
            !player.trim() ||
            !/^\d{3}$/.test(code) ||
            (status.state === "LOCKED" && !replace)
          }
        >
          {busy ? "Waiting for the vault…" : "Arm the vault"}
          <KeyRound size={18} />
        </button>
        <button
          className="secondary-button"
          type="button"
          disabled={!enabled || status.state === "IDLE"}
          onClick={() => void run("disarm")}
        >
          Disarm and return to standby
        </button>
        {!enabled && !busy && (
          <p className="muted-note">
            {!status.espOnline || !status.mcuOnline
              ? "Controls are paused while the vault is offline."
              : "Controls are paused during dialing or alarm lockout."}
          </p>
        )}
      </form>
      <GameMasterControls api={api} online={status.espOnline} />
      <details className="diagnostics">
        <summary>
          Device diagnostics <ChevronRight size={15} />
        </summary>
        <dl>
          <dt>ESP32</dt>
          <dd>{status.espOnline ? "Online" : "Offline"}</dd>
          <dt>MCU link</dt>
          <dd>{status.mcuOnline ? "Online" : "Offline"}</dd>
          <dt>Input service</dt>
          <dd>
            {status.inputsReady
              ? "Ready · sensor operation unverified"
              : "Not ready"}
          </dd>
          <dt>Beam detection</dt>
          <dd>
            {status.beamEnabled ? "Enabled" : "Disabled · calibration required"}
          </dd>
          <dt>Light reading</dt>
          <dd>{status.lightValid ? status.light : "Unavailable"}</dd>
        </dl>
      </details>
      <button
        className="text-button signout"
        onClick={() => void api.signOut().catch((e) => report(errorText(e)))}
      >
        Sign out of owner account <ArrowUpRight size={15} />
      </button>
    </>
  );
}
function ConnectionPanel({
  config,
  preview,
}: {
  config: ReturnType<typeof getConfig>;
  preview: boolean;
}) {
  const [text, setText] = useState("");
  const [error, setError] = useState("");
  function save(e: FormEvent) {
    e.preventDefault();
    try {
      const safe = parseFirebaseConfig(text);
      localStorage.setItem(CONFIG_KEY, JSON.stringify(safe));
      location.href = "/";
    } catch (error) {
      setError(
        error instanceof Error
          ? error.message
          : "Couldn’t save the connection. Check browser storage permissions.",
      );
    }
  }
  return (
    <form className="owner-form" onSubmit={save}>
      <p className="modal-intro">
        Paste the firebaseConfig object from Firebase Project settings. JSON and
        the console’s JavaScript object format both work. Only the four public
        fields below are saved in this browser.
      </p>
      <label>
        Firebase web configuration
        <textarea
          value={text}
          onChange={(e) => setText(e.target.value)}
          rows={7}
          placeholder={
            '{\n  "apiKey": "…",\n  "authDomain": "…firebaseapp.com",\n  "databaseURL": "https://…firebasedatabase.app",\n  "projectId": "…"\n}'
          }
          spellCheck={false}
          required
        />
      </label>
      <p className="muted-note">
        Never paste Wi-Fi passwords, owner passwords, device passwords, or a
        service-account key here.
      </p>
      {config && (
        <p className="muted-note">
          Current project: {config.projectId}. Hosted deployments normally use
          build-time public configuration.
        </p>
      )}
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
      <button className="primary-button" disabled={preview}>
        Save connection <ArrowRight size={18} />
      </button>
    </form>
  );
}

function GameMasterControls({ api, online }: { api: Api; online: boolean }) {
  const [settings, setSettings] = useState<{
    enabled: boolean;
    loaded: boolean;
    health: AiHealth | null;
  }>({ enabled: true, loaded: false, health: null });
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState("");
  useEffect(
    () => api.onAiOwner(setSettings, (e) => setError(errorText(e))),
    [api],
  );
  const health = settings.health;
  const reasons: Record<string, string> = {
    "not-configured":
      "Add the Gemini key to the ESP32 to enable generated reactions.",
    "waiting-settings": "Waiting for the device to load its settings.",
    requesting: "Asking Gemini…",
    ready: "Ready for Gemini reactions.",
    offline: "Gemini is offline. Local reactions are available.",
    "rate-limited": "Keeping reactions spaced out. Local reaction active.",
    "quota-cooldown": "Free quota reached. Using local reactions.",
    "key-rejected":
      "Gemini rejected the key. Check its project and permissions.",
    "model-unavailable": "The configured Gemini model is unavailable.",
    "invalid-response": "Response skipped. Using a local reaction.",
    "network-timeout": "Gemini did not respond. Using local reactions.",
    "low-memory": "Local reactions active while device memory is low.",
    "worker-unavailable":
      "AI worker could not start. Local reactions are active.",
  };
  async function toggle(value: boolean) {
    setSaving(true);
    setError("");
    try {
      await api.setAiEnabled(value);
    } catch (e) {
      setError(errorText(e));
    } finally {
      setSaving(false);
    }
  }
  const stale = !online || !health || Date.now() - health.updatedAt > 30000;
  return (
    <section
      className="game-master-controls"
      aria-labelledby="game-master-heading"
    >
      <h3 id="game-master-heading">Game master</h3>
      <label className="checkbox-label">
        <input
          type="checkbox"
          checked={settings.enabled}
          disabled={!settings.loaded || saving}
          onChange={(e) => void toggle(e.target.checked)}
        />
        Enable reactions and hints
      </label>
      <p className="muted-note">
        {!settings.enabled
          ? "Game master is off."
          : stale
            ? "Waiting for device diagnostics."
            : (reasons[health.health] ?? "Local reactions are ready.")}
      </p>
      {settings.enabled && !stale && !health.hasCode && (
        <p className="muted-note">
          Useful hints need a confirmed round. Disarm, then arm the vault to
          restore them.
        </p>
      )}
      {settings.enabled && !stale && health.cooldownSeconds > 0 && (
        <p className="muted-note">
          Gemini retry available in about {health.cooldownSeconds} seconds.
        </p>
      )}
      {!stale && (
        <details className="diagnostics">
          <summary>
            AI diagnostics <ChevronRight size={15} />
          </summary>
          <dl>
            <dt>Last Gemini success</dt>
            <dd>
              {health.lastSuccessUptimeS
                ? `Device uptime ${health.lastSuccessUptimeS}s`
                : "None this boot"}
            </dd>
            <dt>Free heap</dt>
            <dd>{Math.round(health.freeHeap / 1024)} KiB</dd>
          </dl>
        </details>
      )}
      {error && (
        <p className="form-error" role="alert">
          {error}
        </p>
      )}
    </section>
  );
}
