import type { GameEvent, Score, State, Status } from "./model";
// Explicit design preview only. These records are never written to Firebase.
export function previewData(state: State) {
  const now = Date.now();
  const events: GameEvent[] = [
    {
      id: "preview-arm",
      type: "armed",
      player: "The next challenger",
      at: now - 86000,
    },
  ];
  if (state !== "IDLE" && state !== "UNKNOWN")
    events.push(
      {
        id: "preview-1",
        type: "attempt",
        guess: "428",
        attempt: 1,
        closeness: 67,
        at: now - 55000,
      },
      {
        id: "preview-2",
        type: "attempt",
        guess: "735",
        attempt: 2,
        closeness: 85,
        at: now - 28000,
      },
    );
  if (state === "ALARM")
    events.push({
      id: "preview-alarm",
      type: "alarm",
      cause: "tamper",
      at: now - 8000,
    });
  if (state === "UNLOCKED")
    events.push({
      id: "preview-solved",
      type: "solved",
      player: "The next challenger",
      seconds: 86,
      attempts: 3,
      at: now,
    });
  const status: Status = {
    state,
    espOnline: state !== "UNKNOWN",
    mcuOnline: state !== "UNKNOWN",
    dial: 7,
    digitsEntered: 1,
    knocks: 1,
    attempts: state === "IDLE" ? 0 : 2,
    closeness: 85,
    inputsReady: true,
    beamEnabled: false,
    lightValid: true,
    light: 194,
    updatedAt: now,
  };
  const scores: Score[] = [
    {
      id: "p1",
      player: "The getaway driver",
      seconds: 42,
      attempts: 2,
      at: now,
    },
    { id: "p2", player: "Inside job", seconds: 68, attempts: 3, at: now },
    {
      id: "p3",
      player: "Lucky number three",
      seconds: 91,
      attempts: 4,
      at: now,
    },
  ];
  return { status, events, scores };
}
