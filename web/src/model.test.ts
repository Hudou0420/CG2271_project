import { describe, expect, it } from "vitest";
import {
  visibleAiMessage,
  visibleKnockHint,
  guessDigits,
  combinationTiles,
  submittedFeedback,
  canControl,
  formatTime,
  rankScores,
  roundContext,
  sortedEvents,
  timerValues,
  type GameEvent,
  type Score,
} from "./model";

describe("round recovery and public game display", () => {
  const events: GameEvent[] = [
    { id: "a", type: "armed", at: 1000, player: "First" },
    { id: "b", type: "attempt", at: 2000, guess: "001", closeness: 40 },
    { id: "c", type: "armed", at: 5000, player: "Second" },
    { id: "d", type: "attempt", at: 7000, guess: "042", closeness: 85 },
    { id: "e", type: "alarm", at: 8000 },
  ];
  it("recovers only the latest round after a reload and deduplicates snapshots", () => {
    const context = roundContext([...events].reverse().concat(events[3]));
    expect(context.armed?.player).toBe("Second");
    expect(context.attempts.map((e) => e.guess)).toEqual(["042"]);
    expect(sortedEvents([...events, events[0]])).toHaveLength(5);
  });
  it("uses shared event timestamps and never decides lockout completion", () => {
    expect(timerValues(events, "ALARM", 18000)).toEqual({
      elapsed: 13,
      lockout: 0,
    });
    expect(timerValues(events, "ALARM", 50000).lockout).toBe(0);
    expect(timerValues([], "ALARM", 50000)).toEqual({
      elapsed: null,
      lockout: null,
    });
  });
  it("uses authoritative solve seconds and stops timing disarmed rounds", () => {
    expect(
      timerValues(
        [...events, { id: "f", type: "solved", seconds: 9, at: 30000 }],
        "UNLOCKED",
        999999,
      ).elapsed,
    ).toBe(9);
    expect(
      timerValues(
        [...events, { id: "f", type: "disarmed", at: 30000 }],
        "LOCKED",
        999999,
      ).elapsed,
    ).toBeNull();
    expect(formatTime(65)).toBe("01:05");
    expect(formatTime(null)).toBe("—:—");
  });
  it("sorts equal-time scores by attempts without changing source data", () => {
    const rows = [
      { id: "a", seconds: 30, attempts: 4, at: 0 },
      { id: "b", seconds: 30, attempts: 2, at: 1 },
    ] as Score[];
    expect(rankScores(rows).map((r) => r.id)).toEqual(["b", "a"]);
    expect(rows[0].id).toBe("a");
  });
  it("blocks all controls for spectators, stale links, dialing, alarms and pending commands", () => {
    const live = { state: "LOCKED" as const, espOnline: true, mcuOnline: true };
    expect(canControl(live, false, true)).toBe(true);
    expect(canControl(live, false, false)).toBe(false);
    expect(canControl(live, true, true)).toBe(false);
    expect(canControl({ ...live, espOnline: false }, false, true)).toBe(false);
    expect(canControl({ ...live, mcuOnline: false }, false, true)).toBe(false);
    expect(canControl({ ...live, state: "ALARM" }, false, true)).toBe(false);
    expect(canControl({ ...live, state: "DIALING" }, false, true)).toBe(false);
  });
});

describe("game master message lifetime", () => {
  const status = {
    state: "LOCKED" as const,
    espOnline: true,
    mcuOnline: true,
    roundId: "r",
    actionRevision: 3,
    knocks: 0,
  };
  const message = {
    roundId: "r",
    eventId: "e",
    actionRevision: 3,
    message: "Still locked.",
    source: "gemini" as const,
    at: 100,
  };
  const source = [
    { id: "e", type: "attempt", roundId: "r", actionRevision: 3, at: 99 },
  ];
  it("shows the matching reaction, including after refresh", () => {
    expect(visibleAiMessage(message, status, source)).toEqual(message);
    expect(
      visibleAiMessage({ ...message, source: "local" }, status, source)?.source,
    ).toBe("local");
  });
  it("keeps failed-rhythm feedback out of AI reactions and code guesses", () => {
    const failed: GameEvent[] = [{ ...source[0], type: "knock_failed", reason: "pattern", gaps: "13" }];
    expect(visibleAiMessage(message, status, failed)).toBeNull();
    expect(visibleAiMessage(message, { ...status, knocks: 1 }, failed)).toBeNull();
    expect(visibleAiMessage(message, { ...status, actionRevision: 4 }, failed)).toBeNull();
    expect(roundContext([{ id: "armed", type: "armed", at: 0 }, ...failed]).attempts).toEqual([]);
  });
  it("rejects previous rounds, later actions, missing source events and offline status", () => {
    for (const s of [
      { ...status, roundId: "new" },
      { ...status, actionRevision: 4 },
      { ...status, mcuOnline: false },
      { ...status, espOnline: false },
      { ...status, knocks: 1 },
      { ...status, state: "DIALING" as const },
      { ...status, state: "IDLE" as const },
    ])
      expect(visibleAiMessage(message, s, source)).toBeNull();
    expect(visibleAiMessage(message, status, [])).toBeNull();
    expect(
      visibleAiMessage(message, status, [{ ...source[0], actionRevision: 2 }]),
    ).toBeNull();
  });
  it("matches alarm and solve reactions only to their respective states", () => {
    expect(
      visibleAiMessage(message, { ...status, state: "ALARM" }, [
        { ...source[0], type: "alarm" },
      ]),
    ).toEqual(message);
    expect(
      visibleAiMessage(message, { ...status, state: "UNLOCKED" }, [
        { ...source[0], type: "solved" },
      ]),
    ).toEqual(message);
    expect(
      visibleAiMessage(message, status, [{ ...source[0], type: "solved" }]),
    ).toBeNull();
  });
});

describe("untrusted snapshots and timer boundaries", () => {
  it("rejects malformed AI sources, non-string text and overlong text", () => {
    const status = { state: "LOCKED" as const, espOnline: true, mcuOnline: true, roundId: "r", actionRevision: 1 };
    const events = [{ id: "e", type: "attempt", roundId: "r", actionRevision: 1, at: 1 }];
    const valid = { roundId: "r", eventId: "e", actionRevision: 1, message: "x".repeat(191), source: "local" as const, at: 1 };
    expect(visibleAiMessage(valid, status, events)).toEqual(valid);
    for (const patch of [{ source: "unknown" }, { message: 42 }, { message: "x".repeat(192) }]) {
      expect(visibleAiMessage({ ...valid, ...patch } as unknown as typeof valid, status, events)).toBeNull();
    }
  });
  it("orders equal timestamps deterministically and replaces duplicate snapshots", () => {
    const input = [{ id: "b", type: "attempt", at: 1 }, { id: "a", type: "armed", at: 1 }, { id: "b", type: "attempt", at: 1, guess: "007" }];
    expect(sortedEvents(input).map(e => e.id)).toEqual(["a", "b"]);
    expect(roundContext(input).attempts[0].guess).toBe("007");
    expect(input).toHaveLength(3);
  });
  it("clamps future starts and observes the exact lockout deadline", () => {
    const events = [{ id: "a", type: "armed", at: 1000 }, { id: "b", type: "alarm", at: 2000 }];
    expect(timerValues(events, "LOCKED", 999).elapsed).toBe(0);
    expect(timerValues(events, "ALARM", 11999).lockout).toBe(1);
    expect(timerValues(events, "ALARM", 12000).lockout).toBe(0);
    expect(formatTime(Number.NaN)).toBe("—:—");
    expect(formatTime(Number.POSITIVE_INFINITY)).toBe("—:—");
  });
});


describe("digit tiles", () => {
  it("preserves leading zeroes and rejects incomplete or malformed guesses", () => {
    expect(guessDigits("007")).toEqual(["0", "0", "7"]);
    for (const value of [undefined, null, 7, "7", "12", "1234", "1x3", "123\n"]) {
      expect(guessDigits(value)).toBeNull();
    }
  });
  it("shows the active digit while keeping confirmations masked", () => {
    expect(combinationTiles("DIALING", 1, 0)).toEqual({
      confirmed: 1,
      tiles: [{ kind: "filled", text: "•" }, { kind: "current", text: "0" }, { kind: "empty", text: "" }],
    });
    expect(combinationTiles("DIALING", 0, 7).tiles[0].text).toBe("7");
    expect(combinationTiles("DIALING", 2, 9).tiles[2].kind).toBe("current");
    for (const dial of [undefined, -1, 10, 1.5, "7"]) {
      expect(combinationTiles("DIALING", 0, dial).tiles[0].text).toBe("—");
    }
    expect(combinationTiles("DIALING", 99, 7).confirmed).toBe(3);
    expect(combinationTiles("DIALING", -1, 7).confirmed).toBe(0);
    expect(combinationTiles("DIALING", NaN, 7).confirmed).toBe(0);
  });
  it("clears live digits outside dialing and reserves solved tiles for unlock", () => {
    for (const state of ["IDLE", "LOCKED", "ALARM", "UNKNOWN"] as const) {
      expect(combinationTiles(state, 2, 7).tiles.every(tile => tile.kind === "empty" && tile.text === "")).toBe(true);
    }
    expect(combinationTiles("UNLOCKED", 0, 7).tiles.every(tile => tile.kind === "solved")).toBe(true);
  });
});


describe("submitted per-digit feedback", () => {
  it("decodes MCU feedback for the requested examples", () => {
    expect(submittedFeedback("110")).toEqual(["present", "present", "absent"]);
    expect(submittedFeedback("220")).toEqual(["correct", "correct", "absent"]);
    expect(submittedFeedback("222")).toEqual(["correct", "correct", "correct"]);
  });
  it("keeps old or malformed feedback neutral instead of guessing correctness", () => {
    for (const value of [undefined, null, 110, "", "12", "123", "0000", [1, 1, 0]]) {
      expect(submittedFeedback(value)).toBeNull();
    }
  });
});

describe("fixed knock timing feedback", () => {
  const status = { state: "LOCKED" as const, espOnline: true, mcuOnline: true,
    roundId: "r", actionRevision: 3, knocks: 0 };
  const failure: GameEvent = { id: "k", type: "knock_failed", roundId: "r",
    actionRevision: 3, at: 100, reason: "pattern", gaps: "13" };
  it("describes both pauses without disclosing the secret timings", () => {
    const hint = visibleKnockHint(status, [failure])!;
    expect(hint.title).toBe("Rhythm needs work.");
    expect(hint.pauses.map(p => p.advice)).toEqual(["Wait longer.", "Tap sooner."]);
    expect(JSON.stringify(hint)).not.toMatch(/300|900/);
    expect(visibleKnockHint(status, [{ ...failure, gaps: "22" }])?.pauses.map(p => p.verdict))
      .toEqual(["Timing good", "Timing good"]);
  });
  it("preserves known gaps on timeout and marks uncaptured gaps honestly", () => {
    const hint = visibleKnockHint(status, [{ ...failure, reason: "timeout", gaps: "20" }])!;
    expect(hint.title).toBe("Sequence timed out.");
    expect(hint.pauses.map(p => p.verdict)).toEqual(["Timing good", "Not captured"]);
    expect(visibleKnockHint(status, [{ ...failure, reason: "timeout", gaps: "00" }])?.pauses.every(p => p.kind === "missing")).toBe(true);
  });
  it("hides failures when the player starts again, changes state, loses connection or changes round", () => {
    for (const change of [{knocks: 1}, {actionRevision: 4}, {roundId: "new"},
      {espOnline: false}, {mcuOnline: false},
      ...(["IDLE", "DIALING", "UNLOCKED", "ALARM", "UNKNOWN"] as const).map(state => ({state}))]) {
      expect(visibleKnockHint({...status, ...change}, [failure])).toBeNull();
    }
    expect(visibleKnockHint({...status, roundId: undefined}, [failure])).toBeNull();
    expect(visibleKnockHint(status, [])).toBeNull();
  });
  it("rejects malformed verdicts and reasons", () => {
    for (const gaps of [undefined, "", "1", "123", "49", "a1", 13 as unknown as string]) {
      expect(visibleKnockHint(status, [{...failure, gaps}])).toBeNull();
    }
    expect(visibleKnockHint(status, [{...failure, reason: undefined}])).toBeNull();
  });
});

describe("duplicate close-call delivery", () => {
  const armed: GameEvent = {id: "armed", type: "armed", at: 0, roundId: "r"};
  const attempt = (id: string, at: number, number: number, guess: string, feedback?: string): GameEvent =>
    ({id, at, type: "attempt", attempt: number, guess, feedback, roundId: "r"});
  it("collapses the reported duplicate before counting or selecting rows", () => {
    const events = [armed, attempt("a",1,1,"123"), attempt("b",2,2,"345"),
      attempt("retry",3,2,"345"), attempt("c",4,3,"127")];
    const rows = roundContext(events.reverse()).attempts;
    expect(rows.map(e => [e.attempt,e.guess])).toEqual([[1,"123"],[2,"345"],[3,"127"]]);
    expect(rows[1].id).toBe("retry");
  });
  it("preserves feedback when a newer duplicate is legacy or malformed", () => {
    for (const feedback of [undefined, "bad"]) {
      const rows = roundContext([armed,attempt("colour",1,2,"345","110"),
        attempt("legacy",2,2,"345",feedback)]).attempts;
      expect(rows).toHaveLength(1);expect(rows[0].feedback).toBe("110");
    }
    const rows = roundContext([armed,attempt("legacy",1,2,"345"),
      attempt("colour",2,2,"345","110"),attempt("new",3,2,"345","220")]).attempts;
    expect(rows).toHaveLength(1);expect(rows[0].id).toBe("new");
  });
  it("keeps real repeat guesses, counter wrap, different rounds and unidentified records", () => {
    const rows = roundContext([armed,attempt("a",1,1,"007"),attempt("b",2,2,"007"),
      attempt("c",3,255,"007"),attempt("d",4,0,"007"),attempt("e",5,1,"007")]).attempts;
    expect(rows).toHaveLength(5);
    const a = attempt("a",1,2,"345");
    for (const change of [{attempt: undefined}, {guess: undefined}, {guess: "bad"},
      {attempt: -1}, {attempt: 256}, {roundId: "other"}]) {
      expect(roundContext([armed,a,{...a,id:"b",at:2,...change}]).attempts).toHaveLength(2);
    }
    expect(roundContext([armed,a,{...armed,id:"new",at:3,roundId:"new"},
      {...a,id:"later",at:4,roundId:"new"}]).attempts.map(e=>e.id)).toEqual(["later"]);
  });
});
