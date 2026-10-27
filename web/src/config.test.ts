import { describe, expect, it } from "vitest";
import { parseFirebaseConfig } from "./config";

const config = {
  apiKey: "public-example-key",
  authDomain: "example.firebaseapp.com",
  databaseURL:
    "https://example-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "example",
};

describe("Firebase connection configuration", () => {
  it("accepts strict JSON and keeps only public connection fields", () => {
    expect(
      parseFirebaseConfig(JSON.stringify({ ...config, appId: "extra" })),
    ).toEqual(config);
  });
  it("accepts Firebase console syntax with unquoted keys, comments, single quotes and trailing commas", () => {
    expect(
      parseFirebaseConfig(`const firebaseConfig = {
      // Public configuration
      apiKey: 'public-example-key',
      authDomain: 'example.firebaseapp.com',
      databaseURL: '${config.databaseURL}/',
      projectId: 'example',
    };`),
    ).toEqual(config);
  });
  it("explains the missing database URL separately from a syntax error", () => {
    expect(() =>
      parseFirebaseConfig('{apiKey:"a",authDomain:"b",projectId:"c"}'),
    ).toThrow(/Missing databaseURL.*Realtime Database/);
  });
  it("rejects console page URLs with an actionable message", () => {
    expect(() =>
      parseFirebaseConfig(
        JSON.stringify({
          ...config,
          databaseURL:
            "https://console.firebase.google.com/project/example/database",
        }),
      ),
    ).toThrow(/Don’t use the Firebase Console/);
  });
  it("never evaluates JavaScript expressions or functions", () => {
    expect(() => parseFirebaseConfig('{apiKey: (() => "key")()}')).toThrow(
      /Leave out imports/,
    );
  });
});
