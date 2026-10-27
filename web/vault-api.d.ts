import type { FirebaseApp, FirebaseOptions } from "firebase/app";
import type { User } from "firebase/auth";
import type { Status, GameEvent, Score } from "./src/model";
export const OFFLINE_AFTER_MS: number;
export const STATES: string[];
export interface CommandOptions {
  onUpdate?: (result: string) => void;
  timeoutMs?: number;
}
export interface CommandResult {
  ok: boolean;
  result: string;
  id: string | null;
}
export interface VaultApi {
  onStatus(
    cb: (status: Status) => void,
    onError?: (error: unknown) => void,
  ): () => void;
  onOwner(
    cb: (state: {
      user: User | null;
      owner: boolean;
      checking: boolean;
    }) => void,
    onError?: (error: unknown) => void,
  ): () => void;
  onEvents(
    cb: (event: GameEvent) => void,
    options?: { limit?: number; onError?: (error: unknown) => void },
  ): () => void;
  onEventHistory(
    cb: (events: GameEvent[]) => void,
    onError?: (error: unknown) => void,
  ): () => void;
  onLeaderboard(
    cb: (scores: Score[]) => void,
    options?: { limit?: number; onError?: (error: unknown) => void },
  ): () => void;
  serverNow(): number;
  arm(
    code: string | number,
    options?: CommandOptions & { player?: string; pattern?: number },
  ): Promise<CommandResult>;
  disarm(options?: CommandOptions): Promise<CommandResult>;
  signIn(email: string, password: string): Promise<unknown>;
  signOut(): Promise<void>;
  destroy(): void;
}
export function connectVault(
  config: FirebaseOptions | FirebaseApp,
  options?: { emulator?: boolean },
): Promise<VaultApi>;
