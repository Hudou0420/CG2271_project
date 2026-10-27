import type { State } from "../model";

export default function Vault({
  state,
  dial,
  celebrate,
}: {
  state: State;
  dial: number;
  celebrate: boolean;
}) {
  return (
    <div
      className={`vault-art state-${state.toLowerCase()} ${celebrate ? "celebrate" : ""}`}
    >
      <svg
        viewBox="0 0 640 480"
        role="img"
        aria-label={`The vault is ${state.toLowerCase()}`}
      >
        <defs>
          <pattern
            id="steel-lines"
            width="7"
            height="7"
            patternUnits="userSpaceOnUse"
            patternTransform="rotate(35)"
          >
            <path d="M0 0V7" stroke="#c9cbb0" strokeWidth=".5" opacity=".1" />
          </pattern>
          <pattern
            id="halftone"
            width="9"
            height="9"
            patternUnits="userSpaceOnUse"
          >
            <circle cx="2" cy="2" r="1.5" fill="#141810" opacity=".25" />
          </pattern>
          <linearGradient id="vault-side" x1="0" y1="0" x2="1" y2="1">
            <stop stopColor="#535b42" />
            <stop offset="1" stopColor="#292e25" />
          </linearGradient>
          <linearGradient id="door-metal" x1="0" y1="0" x2="1" y2="1">
            <stop stopColor="#9da680" />
            <stop offset="1" stopColor="#737e5f" />
          </linearGradient>
        </defs>
        <ellipse
          cx="332"
          cy="431"
          rx="214"
          ry="20"
          fill="#11150d"
          opacity=".45"
        />
        <g
          className="vault-rays"
          stroke="#ccf55b"
          strokeWidth="4"
          strokeLinecap="round"
        >
          <path d="M143 116l-19-16m-13 59-29-6m402-41 20-23m17 69 33-5M124 340l-29 12m433-14 25 14" />
        </g>
        <g
          className="vault-body"
          stroke="#151912"
          strokeWidth="5"
          strokeLinejoin="round"
        >
          <path
            d="M193 74L455 49l82 49v306l-78 33-277-25z"
            fill="url(#vault-side)"
          />
          <path d="M193 74L455 49l82 49-275 35z" fill="#6b7655" />
          <path d="M455 91l82 7v306l-78 33z" fill="#333a2b" />
          <path d="M193 74l266 17v346l-277-25z" fill="#555f46" />
          <path d="M211 98l226 14v297l-233-17z" fill="#192016" />
          <path d="M220 112l208 13v267l-215-15z" fill="#0f150e" />
          <g className="vault-treasure" strokeWidth="3">
            <path d="M256 328h115v44H256z" fill="#b3c83f" />
            <path d="M274 307h79v23h-79z" fill="#e5ca56" />
            <path d="M291 284h47v24h-47z" fill="#f8e48b" />
            <path d="M281 335v36m24-64v21m20-44v22m22 25v42" stroke="#736326" />
          </g>
          <g className="vault-door">
            <path d="M213 103l224 14v292l-230-18z" fill="url(#door-metal)" />
            <path
              d="M213 103l224 14v292l-230-18z"
              fill="url(#steel-lines)"
              stroke="none"
            />
            <path
              d="M231 124l186 12v253l-192-15z"
              fill="none"
              stroke="#4c5840"
              strokeWidth="3"
            />
            <path
              d="M240 134l169 11v234l-175-13z"
              fill="url(#halftone)"
              stroke="none"
            />
            <g fill="#343e2b" strokeWidth="3">
              <path d="M197 145l25 2v40l-26-2z" />
              <path d="M195 315l25 2v40l-26-2z" />
            </g>
            <g fill="#d3d9ad" strokeWidth="2">
              <circle cx="236" cy="140" r="4" />
              <circle cx="405" cy="151" r="4" />
              <circle cx="232" cy="359" r="4" />
              <circle cx="403" cy="373" r="4" />
            </g>
            <g
              className="vault-dial"
              style={{ transform: `rotate(${dial * 36}deg)` }}
            >
              <circle cx="326" cy="247" r="65" fill="#424d36" strokeWidth="4" />
              <circle cx="326" cy="247" r="54" fill="#c9d29f" strokeWidth="3" />
              {Array.from({ length: 20 }, (_, i) => (
                <path
                  key={i}
                  d={`M326 198v${i % 2 ? 5 : 9}`}
                  transform={`rotate(${i * 18} 326 247)`}
                  stroke="#3d482f"
                  strokeWidth={i % 2 ? 2 : 3}
                />
              ))}
              <circle cx="326" cy="247" r="33" fill="#323d2a" strokeWidth="3" />
              <path
                d="M326 222v50m-25-25h50"
                stroke="#bded55"
                strokeWidth="9"
                strokeLinecap="round"
              />
              <circle cx="326" cy="247" r="11" fill="#c8f55a" strokeWidth="3" />
            </g>
            <path d="M319 172l7 11 7-10" fill="#e5f0c0" strokeWidth="2" />
            <g strokeWidth="3">
              <path d="M389 219l14 1v61l-14-1z" fill="#36422d" />
              <path d="M398 225l11 1v48l-11-1z" fill="#b6c18d" />
            </g>
            <rect
              x="269"
              y="323"
              width="104"
              height="28"
              rx="3"
              fill="#333e2b"
              strokeWidth="2"
              transform="rotate(4 269 323)"
            />
            <text
              x="281"
              y="343"
              fontFamily="monospace"
              fontSize="13"
              fontWeight="bold"
              fill="#c8d3a4"
              stroke="none"
              transform="rotate(4 269 323)"
            >
              NO. 00—03
            </text>
          </g>
          <path
            d="M471 153l48 4m-48 12 48 4m-48 12 48 4"
            stroke="#20271b"
            strokeWidth="5"
          />
          <g className="vault-light">
            <circle cx="493" cy="338" r="14" fill="#c9f55a" strokeWidth="3" />
            <circle cx="489" cy="334" r="4" fill="#f3ffd1" stroke="none" />
          </g>
          <path d="M201 413v19l42 3v-19m191 17v15l43-8v-11" fill="#252d20" />
        </g>
        <g
          className="comic-spark"
          fill="#d1f65a"
          stroke="#151912"
          strokeWidth="3"
        >
          <path d="M174 171l-12 17-20-2 11 17-8 19 22-5 15 13 1-22 17-12-21-6z" />
          <path d="M489 65l-8-19-9 18-20 2 15 13-4 20 18-10 18 9-4-20 14-13z" />
        </g>
        <g className="alarm-mark" stroke="#171a16" strokeWidth="4">
          <path d="M480 230l-35 60h70z" fill="#ff8b59" />
          <path d="M480 247v19m0 8v2" strokeWidth="5" strokeLinecap="round" />
        </g>
      </svg>
      <div className="art-caption">
        <span>STEEL NERVES REQUIRED</span>
        <span>↗ TRY YOUR LUCK</span>
      </div>
    </div>
  );
}
