import { Check } from "lucide-react";
import { attemptFeedback, combinationTiles, guessDigits, submittedFeedback, type State } from "../model";

export function LiveDigitTiles({ state, entered, dial }: {
  state: State; entered: unknown; dial: unknown;
}) {
  const { confirmed, tiles } = combinationTiles(state, entered, dial);
  const active = tiles.find(tile => tile.kind === "current");
  return (
    <div className="digit-slots" role="group"
      aria-label={`${confirmed} of 3 digits confirmed`}
      aria-description={active ? `Current digit ${active.text}. Confirmed digits are hidden.` : undefined}>
      {tiles.map((tile, index) => (
        <span key={index} className={tile.kind} aria-hidden="true">
          {tile.kind === "solved" ? <Check size={18} /> : tile.text}
        </span>
      ))}
    </div>
  );
}

export function GuessTiles({ guess, closeness, feedback }: {
  guess: unknown; closeness: unknown; feedback: unknown;
}) {
  const digits = guessDigits(guess);
  const marks = digits ? submittedFeedback(feedback) : null;
  const { band, score } = attemptFeedback(closeness);
  const description = digits && marks
    ? digits.map((digit, index) => `${digit}: ${marks[index] === "correct" ? "correct position" : marks[index] === "present" ? "present elsewhere" : "absent"}`).join("; ")
    : "Per-digit feedback unavailable.";
  return (
    <div className={digits ? "guess-tiles" : "guess-tiles unavailable"}
      role="group" aria-label={`Guess ${digits ? digits.join("") : "unavailable"}, ${band}, ${score}% closeness`}
      aria-description={description}>
      {(digits || ["—", "—", "—"]).map((digit, index) => (
        <span key={index} className={marks ? `feedback-${marks[index]}` : "feedback-unknown"} aria-hidden="true">{digit}</span>
      ))}
    </div>
  );
}
