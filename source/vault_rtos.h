/*
 * vault_rtos.h  --  Person 1
 *
 * The shared RTOS objects. Person 2 and Person 3 include this to post events
 * and to protect/read the game state.
 */
#ifndef VAULT_RTOS_H
#define VAULT_RTOS_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "vault_types.h"

/* Task priorities. configMAX_PRIORITIES is 5, so the highest usable is 4. */
#define PRIO_TRIPWIRE   4   /* a break-in must be noticed first            */
#define PRIO_GAME       3   /* dial/knock should feel responsive           */
#define PRIO_COMMS      2   /* can tolerate a few ms of delay              */
#define PRIO_ACTUATOR   1   /* LED/buzzer patterns are least time-critical */

/* Queue sizes */
#define INPUT_Q_LEN     10  /* VaultEvent items  -> tGame                  */
#define TX_Q_LEN        5   /* TxMsg items       -> tComms                 */
#define RX_Q_LEN        64  /* raw UART bytes    -> tComms (Person 3)      */

/* Shared objects (defined in CG2271_Project.c) */
extern QueueHandle_t     xInputQueue;   /* element: VaultEvent  (many senders, tGame receives) */
extern QueueHandle_t     xUartTxQueue;  /* element: TxMsg       (tGame sends, tComms receives) */
extern QueueHandle_t     xUartRxQueue;  /* element: uint8_t     (UART RX ISR sends, tComms receives) */
extern SemaphoreHandle_t xStateMutex;   /* protects gGame                                      */
extern GameState         gGame;         /* ONLY tGame writes; others read while holding mutex  */

/*
 * HOW TO POST AN EVENT
 *
 *   From an ISR (Person 2: tap + encoder):
 *       VaultEvent ev = { EV_KNOCK, 0, xTaskGetTickCountFromISR() * portTICK_PERIOD_MS };
 *       BaseType_t hpw = pdFALSE;
 *       xQueueSendFromISR(xInputQueue, &ev, &hpw);
 *       portYIELD_FROM_ISR(hpw);
 *
 *   From a task (Person 2 tripwire, Person 3 tComms):
 *       VaultEvent ev = { EV_BEAM_BROKEN, 0, 0 };     // tick_ms 0 = tGame fills it in
 *       xQueueSend(xInputQueue, &ev, 0);              // 0 = never block
 *
 *   ARM from tComms: value = d0*100 + d1*10 + d2, e.g. code 4-5-6 -> value 456.
 */

#endif /* VAULT_RTOS_H */
