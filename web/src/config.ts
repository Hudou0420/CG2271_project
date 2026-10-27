import JSON5 from "json5";

export interface WebConfig {
  apiKey: string;
  authDomain: string;
  databaseURL: string;
  projectId: string;
}
export const CONFIG_KEY = "vault-public-firebase-config";
export function getConfig(): WebConfig | null {
  const env = import.meta.env;
  if (
    env.VITE_FIREBASE_API_KEY &&
    env.VITE_FIREBASE_DATABASE_URL &&
    env.VITE_FIREBASE_PROJECT_ID
  ) {
    return {
      apiKey: env.VITE_FIREBASE_API_KEY,
      authDomain:
        env.VITE_FIREBASE_AUTH_DOMAIN ||
        `${env.VITE_FIREBASE_PROJECT_ID}.firebaseapp.com`,
      databaseURL: env.VITE_FIREBASE_DATABASE_URL,
      projectId: env.VITE_FIREBASE_PROJECT_ID,
    };
  }
  try {
    const value = JSON.parse(localStorage.getItem(CONFIG_KEY) || "null");
    return validConfig(value) ? value : null;
  } catch {
    return null;
  }
}
export function validConfig(config: unknown): config is WebConfig {
  if (!config || typeof config !== "object") return false;
  const c = config as Record<string, unknown>;
  return (
    ["apiKey", "projectId", "authDomain"].every(
      (k) => typeof c[k] === "string" && !!c[k],
    ) &&
    typeof c.databaseURL === "string" &&
    /^https:\/\/[^/]+\.(firebasedatabase\.app|firebaseio\.com)\/?$/.test(
      c.databaseURL,
    )
  );
}

/** Parse configuration data only. Never evaluate pasted JavaScript. */
export function parseFirebaseConfig(text: string): WebConfig {
  let source = text.trim();
  const assignment = /\b(?:const|let|var)\s+firebaseConfig\s*=\s*/.exec(source);
  if (assignment)
    source = source.slice(assignment.index + assignment[0].length);
  source = source.replace(/;\s*$/, "");
  let parsed: unknown;
  try {
    parsed = JSON5.parse(source);
  } catch {
    throw new Error(
      "Paste the firebaseConfig object, or its const firebaseConfig = { ... }; snippet. Leave out imports and initializeApp(...).",
    );
  }
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
    throw new Error("The configuration must be an object inside { and }.");
  }
  const fields = parsed as Record<string, unknown>;
  const missing = ["apiKey", "authDomain", "databaseURL", "projectId"].filter(
    (key) => typeof fields[key] !== "string" || !(fields[key] as string).trim(),
  );
  if (missing.length) {
    throw new Error(
      `Missing ${missing.join(", ")}. ${missing.includes("databaseURL") ? "Firebase may omit databaseURL from its snippet. Add the URL from Realtime Database → Data." : "Copy these fields from Firebase Project settings → your Web app."}`,
    );
  }
  const config = Object.fromEntries(
    ["apiKey", "authDomain", "databaseURL", "projectId"].map((key) => [
      key,
      (fields[key] as string).trim(),
    ]),
  ) as unknown as WebConfig;
  if (!validConfig(config)) {
    throw new Error(
      "databaseURL must be the HTTPS URL from Realtime Database → Data, ending in firebasedatabase.app or firebaseio.com. Don’t use the Firebase Console page URL.",
    );
  }
  return { ...config, databaseURL: config.databaseURL.replace(/\/$/, "") };
}
