/*
 * vault_fsm.h  --  Person 1
 * Pure-C game logic. Compiles on a laptop AND on the MCXC444.
 */
#ifndef VAULT_FSM_H
#define VAULT_FSM_H

#include "vault_types.h"

/* Reset everything and go to ST_IDLE. */
void vault_init(GameState *g);

/* THE FSM: feed it one event, it updates *g and says what to do next. */
VaultActions vault_handle_event(GameState *g, const VaultEvent *e);

/* True when the tripwire should be watching (LOCKED or DIALING).
 * Person 2's tTripwire can call this (while holding the mutex). */
bool vault_is_armed(const GameState *g);

/* 100 = exact, 0 = as far away as possible. Used for the hint colour. */
uint8_t vault_closeness(const uint8_t code[CODE_LEN], const uint8_t guess[CODE_LEN]);

/* True if the knock timestamps match the secret rhythm (within tolerance). */
bool vault_knock_matches(const uint32_t times[KNOCK_COUNT]);

/* Fill a TxMsg (PKT_ATTEMPT / PKT_SOLVED / PKT_ALARM) from the current state. */
void vault_make_packet(const GameState *g, uint8_t pkt_type, TxMsg *out);

/* "LOCKED", "DIALING", ... handy for PRINTF debugging. */
const char *vault_state_name(VaultState s);

#endif /* VAULT_FSM_H */
