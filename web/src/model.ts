export type State =
  | "IDLE"
  | "LOCKED"
  | "DIALING"
  | "UNLOCKED"
  | "ALARM"
  | "UNKNOWN";
export interface Status {
  state: State;
  espOnline: boolean;
  mcuOnline: boolean;
  ageMs?: number | null;
  digitsEntered?: number;
  dial?: number;
  knocks?: number;
  attempts?: number;
  closeness?: number;
  light?: number;
  lightValid?: boolean;
  beamEnabled?: boolean;
  inputsReady?: boolean;
  roundId?: string;
  actionRevision?: number;
  updatedAt?: number;
  uptimeS?: number;
}
export interface AiMessage {
  roundId: string;
  eventId: string;
  actionRevision: number;
  message: string;
  source: "gemini" | "local";
  at: number;
}
export interface AiHealth {
  configured: boolean;
  hasCode: boolean;
  health: string;
  cooldownSeconds: number;
  lastSuccessUptimeS: number;
  freeHeap: number;
  updatedAt: number;
}
export interface GameEvent {
  id: string;
  type: string;
  at: number;
  player?: string;
  guess?: string;
  feedback?: string;
  attempt?: number;
  closeness?: number;
  seconds?: number;
  attempts?: number;
  cause?: string;
  roundId?: string;
  actionRevision?: number;
}
export interface Score {
  id: string;
  player: string;
  seconds: number;
  attempts: number;
  at: number;
}
export const EMPTY_STATUS: Status = {
  state: "UNKNOWN",
  espOnline: false,
  mcuOnline: false,
};
export const STATES: State[] = [
  "IDLE",
  "LOCKED",
  "DIALING",
  "UNLOCKED",
  "ALARM",
];
export const COPY: Record<
  State,
  { title: string; hint: string; bubble: string; label: string }
> = {
  IDLE: {
    title: "Who’s up next?",
    hint: "The owner will set up your round. Get your game face on.",
    bubble: "I’ll be right here.",
    label: "ON STANDBY",
  },
  LOCKED: {
    title: "Make your move.",
    hint: "Knock the secret rhythm on the vault to begin.",
    bubble: "Let’s hear your opening move.",
    label: "VAULT LOCKED",
  },
  DIALING: {
    title: "Give it a spin.",
    hint: "Turn the dial. Press to confirm each of your three digits.",
    bubble: "Careful. I’m keeping score.",
    label: "DIAL IT IN",
  },
  UNLOCKED: {
    title: "Nicely cracked.",
    hint: "Your score is in. The next heist is yours to start.",
    bubble: "Fine. You earned this one.",
    label: "VAULT CRACKED",
  },
  ALARM: {
    title: "Caught in the act.",
    hint: "Hands off for a moment. Wait for the vault to reset.",
    bubble: "I felt that.",
    label: "LOCKOUT ACTIVE",
  },
  UNKNOWN: {
    title: "Waiting for the vault.",
    hint: "The live game will appear when the vault connects.",
    bubble: "Hold that thought.",
    label: "CONNECTING",
  },
};
export function sortedEvents(events: GameEvent[]) {
  return [...new Map(events.map((e) => [e.id, e])).values()].sort(
    (a, b) => a.at - b.at || a.id.localeCompare(b.id),
  );
}
export function roundContext(events: GameEvent[]) {
  const ordered = sortedEvents(events);
  const armed = ordered.findLast((e) => e.type === "armed");
  const startIndex = armed ? ordered.findIndex((e) => e.id === armed.id) : -1;
  const current = startIndex < 0 ? [] : ordered.slice(startIndex);
  return {
    armed,
    attempts: current.filter((e) => e.type === "attempt"),
    alarm: current.findLast((e) => e.type === "alarm"),
    solved: current.findLast((e) => e.type === "solved"),
    disarmed: current.some((e) => e.type === "disarmed"),
  };
}
export function timerValues(events: GameEvent[], state: State, now: number) {
  const r = roundContext(events);
  const running =
    state === "LOCKED" || state === "DIALING" || state === "ALARM";
  return {
    elapsed:
      state === "UNLOCKED"
        ? (r.solved?.seconds ?? null)
        : running && r.armed && !r.disarmed
          ? Math.max(0, Math.floor((now - r.armed.at) / 1000))
          : null,
    lockout:
      state === "ALARM" && r.alarm
        ? Math.max(0, Math.ceil((r.alarm.at + 30000 - now) / 1000))
        : null,
  };
}
export function formatTime(seconds: number | null | undefined) {
  if (seconds == null || !Number.isFinite(seconds)) return "—:—";
  return `${Math.floor(seconds / 60)
    .toString()
    .padStart(2, "0")}:${Math.floor(seconds % 60)
    .toString()
    .padStart(2, "0")}`;
}
export function rankScores(scores: Score[]) {
  return [...scores].sort(
    (a, b) => a.seconds - b.seconds || a.attempts - b.attempts || a.at - b.at,
  );
}
export function canControl(status: Status, busy: boolean, owner: boolean) {
  return (
    owner &&
    !busy &&
    status.espOnline &&
    status.mcuOnline &&
    ["IDLE", "LOCKED", "UNLOCKED"].includes(status.state)
  );
}
export function randomCode() {
  const sample = new Uint32Array(1);
  // Rejection sampling avoids modulo bias.
  do {
    crypto.getRandomValues(sample);
  } while (sample[0] >= 4294967000);
  return String(sample[0] % 1000).padStart(3, "0");
}

// Both streams must agree. A refresh must never revive a previous attempt's reply.
export function visibleAiMessage(
  message: AiMessage | null,
  status: Status,
  events: GameEvent[],
) {
  if (
    !message ||
    !status.espOnline ||
    !status.mcuOnline ||
    status.state === "IDLE" ||
    status.state === "UNKNOWN" ||
    status.state === "DIALING" ||
    (status.knocks ?? 0) > 0 ||
    message.roundId !== status.roundId ||
    message.actionRevision !== status.actionRevision ||
    !["gemini", "local"].includes(message.source) ||
    typeof message.message !== "string" ||
    message.message.length > 191
  )
    return null;
  const source = events.find(
    (e) =>
      e.id === message.eventId &&
      e.roundId === message.roundId &&
      e.actionRevision === message.actionRevision,
  );
  const expected =
    source?.type === "attempt"
      ? "LOCKED"
      : source?.type === "alarm"
        ? "ALARM"
        : source?.type === "solved"
          ? "UNLOCKED"
          : null;
  return expected === status.state ? message : null;
}

export function attemptFeedback(closeness: unknown) {
  const score = typeof closeness === "number" && Number.isFinite(closeness)
    ? Math.max(0, Math.min(100, closeness)) : 0;
  return { score, band: score >= 70 ? "warm" : score >= 40 ? "cool" : "cold" };
}

export function guessDigits(guess: unknown) {
  return typeof guess === "string" && /^[0-9]{3}$/.test(guess)
    ? guess.split("") : null;
}

export function combinationTiles(state: State, entered: unknown, dial: unknown) {
  const confirmed = state === "UNLOCKED" ? 3 : state === "DIALING" &&
    typeof entered === "number" && Number.isInteger(entered)
    ? Math.max(0, Math.min(3, entered)) : 0;
  const digit = typeof dial === "number" && Number.isInteger(dial) && dial >= 0 && dial <= 9
    ? String(dial) : "—";
  return {
    confirmed,
    tiles: [0, 1, 2].map(index => state === "UNLOCKED"
      ? { kind: "solved", text: "" }
      : index < confirmed
        ? { kind: "filled", text: "•" }
        : state === "DIALING" && index === confirmed
          ? { kind: "current", text: digit }
          : { kind: "empty", text: "" }),
  };
}


export type DigitFeedback = "absent" | "present" | "correct";
export function submittedFeedback(feedback: unknown): DigitFeedback[] | null {
  if (typeof feedback !== "string" || !/^[012]{3}$/.test(feedback)) return null;
  const meanings: DigitFeedback[] = ["absent", "present", "correct"];
  return [...feedback].map(mark => meanings[Number(mark)]);
}
