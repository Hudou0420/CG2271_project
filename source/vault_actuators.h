#ifndef VAULT_ACTUATORS_H
#define VAULT_ACTUATORS_H
#include "vault_types.h"

/* Startup after BSP initialization; allocates a latest-state mailbox and a
 * bounded feedback queue. No output writes until the actuator task runs. */
bool vault_actuators_init(void);

/* tGame only, after releasing xStateMutex. before/after are private snapshots;
 * nothing here writes gGame, waits for queue space, or touches hardware. */
void vault_actuators_publish(const GameState *before, const GameState *after,
                             const VaultEvent *event, const VaultActions *actions);

/* One owner of RGB/buzzer writes. step is also used by deterministic tests. */
void vault_actuators_step(uint32_t now_ms);
void vault_actuators_task(void *argument);
#endif
