#ifndef VAULT_SENSOR_LOGIC_H
#define VAULT_SENSOR_LOGIC_H

#include <stdbool.h>
#include <stdint.h>
#include "vault_sensor_config.h"

/* Pure C processing shared by the RTOS service and host regression tests. */
typedef struct {
    uint8_t previous;
    int8_t partial_step;
    bool synchronized;
    int32_t steps;
    int32_t rounds;
    int32_t steps_per_round;
    int8_t direction;
    uint32_t invalid;
    uint32_t bounce_returns;
} VaultEncoder;

bool vault_encoder_init(VaultEncoder *encoder, bool clk, bool dt,
                        uint32_t steps_per_round, int8_t direction);
/* Preserve position, but discard ambiguous history after capture overflow. */
void vault_encoder_resync(VaultEncoder *encoder, bool clk, bool dt);
/* Returns -1/0/+1 when the signed integer round index changes.
 * rounds = steps / steps_per_round, using C truncation toward zero.
 * Origin is startup; reversing across a boundary reverses its event.
 */
int32_t vault_encoder_update(VaultEncoder *encoder, bool clk, bool dt);

typedef struct {
    uint16_t samples[VAULT_LDR_AVERAGE_SAMPLES];
    uint32_t sum;
    uint32_t count;
    uint32_t next;
} VaultLdrAverage;

void vault_ldr_reset(VaultLdrAverage *filter);
/* Average the full 12-bit ADC range; reject only out-of-range values. */
bool vault_ldr_update(VaultLdrAverage *filter, uint16_t raw, uint16_t *average);

typedef struct {
    uint16_t threshold;
    uint16_t hysteresis;
    uint16_t blocked_samples;
    uint16_t count;
    bool blocked_when_low;
    bool latched;
    uint16_t clear_samples;
} VaultBeamFilter;

void vault_beam_reset(VaultBeamFilter *filter);
/* One true result per confirmed opening; consecutive clear samples release latch. */
bool vault_beam_update(VaultBeamFilter *filter, uint16_t raw, bool armed);

#endif
