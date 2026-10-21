// link.h - reliable packet link to the MCXC444 over Serial1.
//
// One task (tLink) owns the UART: it parses incoming bytes, answers ACK/NAK,
// and sends queued messages one at a time (stop-and-wait), resending on NAK
// or timeout. Other tasks only call link_send(), which never blocks.
#pragma once

#include <stdint.h>
#include "vault_protocol.h"

// Called from the link task for every valid, ACKed message from the MCU.
// Keep it short: log, update state, or post to another task's queue.
typedef void (*link_rx_cb_t)(const vp_frame_t &frame);

// Called from the link task when an outgoing message is ACKed (ok = true)
// or dropped after VP_MAX_RETRIES resends (ok = false).
typedef void (*link_tx_cb_t)(uint8_t type, bool ok);

struct LinkStats {
    uint32_t txFrames;      // messages sent (first attempt)
    uint32_t txRetries;     // resends
    uint32_t txFailed;      // given up after all retries
    uint32_t rxFrames;      // valid frames received (incl. ACK/NAK)
    uint32_t rxBad;         // bad checksum / length / unknown type
    uint32_t rxTimeouts;    // half-received frames dropped
    uint32_t naksSent;
};

void        link_init(link_rx_cb_t onRx, link_tx_cb_t onTx);
bool        link_send(uint8_t type, const uint8_t *payload, uint8_t len);
LinkStats   link_stats();
void        link_set_raw_log(bool on); // hex-dump every byte in and out
const char *link_type_name(uint8_t type);
