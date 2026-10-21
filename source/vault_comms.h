/*
 * vault_comms.h  --  Person 3 (MCU <-> ESP32 link)
 *
 * Reliable packet link to the ESP32 over LPUART1 (PTE0 TX / PTE1 RX).
 * Frame format and message types: common/vault_protocol/vault_protocol.h.
 *
 *   LPUART1 RX ISR -> xUartRxQueue (raw bytes)
 *   tComms: assembles frames, answers ACK/NAK, posts EV_ARM / EV_DISARM /
 *           EV_TAMPER into xInputQueue, sends tGame's TxMsg packets from
 *           xUartTxQueue one at a time (resent on NAK or timeout), and sends
 *           STATUS (game progress + sensor snapshot) every
 *           VAULT_COMMS_STATUS_PERIOD_MS, or sooner when a reading changes.
 */
#ifndef VAULT_COMMS_H
#define VAULT_COMMS_H

#include <stdbool.h>

#define VAULT_COMMS_STACK_WORDS     384  /* PRINTF needs the headroom */
#define VAULT_COMMS_STATUS_PERIOD_MS 1000

/* Call after the queues/mutex are created, before starting the scheduler.
 * Sets up the LPUART1 pins, the UART and its RX interrupt. */
bool vault_comms_init(void);
void vault_comms_task(void *argument);

/* True while the ESP32 is acknowledging our STATUS heartbeats. */
bool vault_comms_link_up(void);

#endif
