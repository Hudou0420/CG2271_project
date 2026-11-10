import type { KnockHint } from "../model";

export default function KnockFeedback({ hint }: { hint: KnockHint }) {
  return (
    <section className="knock-feedback" aria-label="Knock timing feedback" aria-live="polite">
      <div className="knock-feedback-heading">
        <span className="eyebrow">RHYTHM CHECK</span>
      </div>
      <div className="knock-diagram" aria-hidden="true">
        <span>1</span><i className={hint.pauses[0].kind} />
        <span>2</span><i className={hint.pauses[1].kind} /><span>3</span>
      </div>
      <div className="knock-pause-grid">
        {hint.pauses.map((pause, index) => (
          <div className={`knock-pause ${pause.kind}`} key={index}>
            <div className="knock-pause-label">
              <span className="eyebrow">PAUSE {index + 1}</span>
              <strong>{pause.verdict}</strong>
            </div>
            <span>{pause.advice}</span>
          </div>
        ))}
      </div>
      {hint.title === "Sequence timed out." && (
        <p>{hint.title} {hint.instruction}</p>
      )}
    </section>
  );
}
