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
