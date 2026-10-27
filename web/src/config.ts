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
