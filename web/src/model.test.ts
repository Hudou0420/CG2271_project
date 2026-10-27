import { describe, expect, it } from "vitest";
import {
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
      lockout: 20,
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
