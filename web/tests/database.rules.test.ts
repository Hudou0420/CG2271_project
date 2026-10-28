import { afterAll, beforeAll, beforeEach, describe, it } from "vitest";
import { readFileSync } from "node:fs";
import {
  assertFails,
  assertSucceeds,
  initializeTestEnvironment,
  type RulesTestEnvironment,
} from "@firebase/rules-unit-testing";
import { get, ref, set, remove, update } from "firebase/database";
let env: RulesTestEnvironment;
beforeAll(async () => {
  env = await initializeTestEnvironment({
    projectId: "demo-vault",
    database: {
      host: "127.0.0.1",
      port: 9000,
      rules: readFileSync("database.rules.json", "utf8"),
    },
  });
});
afterAll(async () => {
  await env?.cleanup();
});
beforeEach(async () => {
  await env.clearDatabase();
  await env.withSecurityRulesDisabled(async (context) => {
    await set(ref(context.database()), {
      access: { owners: { team: true }, devices: { esp: true } },
      vault: {
        status: { state: "LOCKED", updatedAt: Date.now() },
        events: { e: { type: "armed", player: "Player", at: 1 } },
        leaderboard: {
          a: { player: "Player", seconds: 12, attempts: 1, at: 1 },
        },
        commands: {
          pending: {
            action: "arm",
            code: "042",
            player: "Player",
            at: Date.now(),
          },
        },
        commandResults: { pending: { result: "sent", at: 1 } },
      },
    });
  });
});
const db = (uid?: string) =>
  uid
    ? env.authenticatedContext(uid).database()
    : env.unauthenticatedContext().database();
const arm = () => ({
  action: "arm",
  code: "007",
  player: "Player",
  at: Date.now(),
});
describe("public viewing and private controls", () => {
  it("allows public status, events and rankings; denies secrets and role lists", async () => {
    for (const path of ["vault/status", "vault/events", "vault/leaderboard"])
      await assertSucceeds(get(ref(db(), path)));
    for (const path of [
      "vault",
      "vault/commands",
      "vault/commands/pending",
      "vault/commandResults",
      "access",
    ])
      await assertFails(get(ref(db(), path)));
    await assertFails(get(ref(db("stranger"), "vault/commands")));
  });
  it("denies public and unrelated authenticated writes", async () => {
    for (const uid of [undefined, "stranger"]) {
      await assertFails(set(ref(db(uid), "vault/commands/new"), arm()));
      await assertFails(
        set(ref(db(uid), "vault/status"), { state: "UNLOCKED", updatedAt: 1 }),
      );
      await assertFails(remove(ref(db(uid), "vault/leaderboard")));
      await assertFails(
        set(ref(db(uid), `access/owners/${uid || "attacker"}`), true),
      );
    }
  });
  it("allows the owner to queue a leading-zero code, but not edit, delete or forge results", async () => {
    await assertSucceeds(set(ref(db("team"), "vault/commands/new"), arm()));
    await assertSucceeds(get(ref(db("team"), "vault/commandResults")));
    await assertFails(
      update(ref(db("team"), "vault/commands/new"), { code: "999" }),
    );
    await assertFails(remove(ref(db("team"), "vault/commands/new")));
    await assertFails(
      set(ref(db("team"), "vault/leaderboard/fake"), {
        player: "Cheat",
        seconds: 0,
        attempts: 0,
        at: 1,
      }),
    );
    await assertFails(
      set(ref(db("team"), "vault/status"), { state: "UNLOCKED", updatedAt: 1 }),
    );
  });
  it("rejects malformed, stale and unexpected command fields", async () => {
    for (const value of [
      { ...arm(), code: "12" },
      { ...arm(), code: "abc" },
      { ...arm(), at: 1 },
      { ...arm(), player: "x".repeat(25) },
      { ...arm(), hidden: "extra" },
      { action: "arm", at: Date.now() },
      { action: "delete", at: Date.now() },
    ]) {
      await assertFails(set(ref(db("team"), "vault/commands/bad"), value));
    }
    await assertSucceeds(
      set(ref(db("team"), "vault/commands/disarm"), {
        action: "disarm",
        at: Date.now(),
      }),
    );
  });
  it("allows the device to consume commands, publish, acknowledge and clear stale queues", async () => {
    const device = db("esp");
    await assertSucceeds(get(ref(device, "vault/commands")));
    await assertSucceeds(
      set(ref(device, "vault/status"), {
        state: "UNLOCKED",
        updatedAt: Date.now(),
      }),
    );
    await assertSucceeds(
      set(ref(device, "vault/commandResults/pending"), {
        action: "arm",
        result: "delivered",
        at: Date.now(),
      }),
    );
    await assertSucceeds(
      set(ref(device, "vault/events/solved"), {
        type: "solved",
        at: Date.now(),
        seconds: 12,
        attempts: 1,
      }),
    );
    await assertSucceeds(
      set(ref(device, "vault/leaderboard/new"), {
        player: "Player",
        seconds: 12,
        attempts: 1,
        at: Date.now(),
      }),
    );
    await assertSucceeds(remove(ref(device, "vault/commands")));
    await assertFails(
      set(ref(device, "vault/events/leak"), {
        type: "armed",
        at: Date.now(),
        code: "007",
      }),
    );
    await assertFails(set(ref(device, "access/owners/esp"), true));
  });
  it("lets accounts check their own role without reading other owner identities", async () => {
    await assertSucceeds(get(ref(db("team"), "access/owners/team")));
    await assertFails(get(ref(db("team"), "access/owners/someone-else")));
    await assertFails(get(ref(db("team"), "access/owners")));
  });
});

describe("game master permissions", () => {
  const message = {
    roundId: "round",
    eventId: "event",
    actionRevision: 3,
    message: "Still locked.",
    source: "local",
    at: 1,
  };
  const health = {
    enabled: true,
    configured: true,
    hasCode: false,
    health: "ready",
    cooldownSeconds: 0,
    lastSuccessUptimeS: 1,
    freeHeap: 100000,
    updatedAt: 1,
  };
  it("publishes reactions publicly, but keeps health and settings private", async () => {
    await assertSucceeds(set(ref(db("esp"), "vault/aiMessage"), message));
    await assertSucceeds(get(ref(db(), "vault/aiMessage")));
    await assertSucceeds(set(ref(db("esp"), "vault/aiStatus"), health));
    await assertSucceeds(get(ref(db("team"), "vault/aiStatus")));
    for (const uid of [undefined, "stranger"]) {
      await assertFails(get(ref(db(uid), "vault/aiStatus")));
      await assertFails(get(ref(db(uid), "vault/aiSettings")));
      await assertFails(set(ref(db(uid), "vault/aiMessage"), message));
      await assertFails(set(ref(db(uid), "vault/aiSettings/enabled"), false));
    }
    await assertFails(set(ref(db("team"), "vault/aiMessage"), message));
    await assertFails(set(ref(db("team"), "vault/aiStatus"), health));
  });
  it("allows only owners to toggle a boolean; the device may read the setting", async () => {
    await assertSucceeds(
      set(ref(db("team"), "vault/aiSettings/enabled"), false),
    );
    await assertSucceeds(get(ref(db("esp"), "vault/aiSettings/enabled")));
    await assertFails(set(ref(db("esp"), "vault/aiSettings/enabled"), true));
    await assertFails(set(ref(db("team"), "vault/aiSettings/enabled"), "true"));
    await assertFails(set(ref(db("team"), "vault/aiSettings/key"), "secret"));
  });
  it("rejects extra secret fields, oversized messages and arbitrary health errors", async () => {
    for (const value of [
      { ...message, code: "042" },
      { ...message, apiKey: "secret" },
      { ...message, source: "other" },
      { ...message, message: "x".repeat(192) },
      { ...message, actionRevision: -1 },
    ])
      await assertFails(set(ref(db("esp"), "vault/aiMessage"), value));
    await assertFails(
      set(ref(db("esp"), "vault/aiStatus"), { ...health, prompt: "secret" }),
    );
    await assertFails(
      set(ref(db("esp"), "vault/aiStatus"), {
        ...health,
        health: "raw provider error",
      }),
    );
    await assertSucceeds(remove(ref(db("esp"), "vault/aiMessage")));
  });
});

describe("atomic updates and role revocation", () => {
  it("rejects parent replacement and atomic privilege escalation without partial writes", async () => {
    await assertFails(set(ref(db("team"), "vault"), { commands: { bulk: arm() } }));
    await assertFails(update(ref(db("team")), {
      "vault/commands/atomic": arm(),
      "access/owners/attacker": true,
    }));
    await assertFails(update(ref(db("esp")), {
      "vault/status": { state: "UNLOCKED", updatedAt: Date.now() },
      "access/owners/esp": true,
    }));
    await env.withSecurityRulesDisabled(async context => {
      const database = context.database();
      const command = await get(ref(database, "vault/commands/atomic"));
      const status = await get(ref(database, "vault/status/state"));
      if (command.exists() || status.val() !== "LOCKED") throw new Error("Denied atomic write partially committed");
    });
  });
  it("revokes permissions for existing owner and device sessions immediately", async () => {
    const owner = db("team"), device = db("esp");
    await assertSucceeds(get(ref(owner, "vault/commands")));
    await assertSucceeds(get(ref(device, "vault/commands")));
    await env.withSecurityRulesDisabled(async context => {
      await remove(ref(context.database(), "access"));
    });
    await assertFails(set(ref(owner, "vault/commands/revoked"), arm()));
    await assertFails(set(ref(owner, "vault/aiSettings/enabled"), false));
    await assertFails(get(ref(owner, "vault/commands")));
    await assertFails(set(ref(device, "vault/status"), { state: "UNLOCKED", updatedAt: Date.now() }));
    await assertSucceeds(get(ref(db(), "vault/status")));
  });
});
