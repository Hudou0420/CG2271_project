#include "vault_sensor_logic.h"
#include <stddef.h>
#include <string.h>

/* Avoid Redlib's unsigned INT32_MIN expression. */
#define POSITION_MIN (-2147483647 - 1)
#define POSITION_MAX 2147483647

static uint8_t encoder_state(bool clk, bool dt)
{
    return (uint8_t)((clk ? 2U : 0U) | (dt ? 1U : 0U));
}

static bool endpoint(uint8_t state)
{
    return state == 0U || state == 3U;
}

void vault_encoder_resync(VaultEncoder *encoder, bool clk, bool dt)
{
    encoder->previous = encoder_state(clk, dt);
    encoder->partial_step = 0;
    encoder->synchronized = endpoint(encoder->previous);
}

bool vault_encoder_init(VaultEncoder *encoder, bool clk, bool dt,
                        uint32_t steps_per_round, int8_t direction)
{
    if (encoder == NULL || steps_per_round == 0U || steps_per_round > 2147483647U ||
        (direction != 1 && direction != -1)) return false;
    memset(encoder, 0, sizeof(*encoder));
    encoder->steps_per_round = (int32_t)steps_per_round;
    encoder->direction = direction;
    vault_encoder_resync(encoder, clk, dt);
    return true;
}

int32_t vault_encoder_update(VaultEncoder *encoder, bool clk, bool dt)
{
    static const int8_t transition[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };
    uint8_t current = encoder_state(clk, dt);
    uint8_t previous = encoder->previous;
    encoder->previous = current;
    if (current == previous) return 0;
    if ((current ^ previous) == 3U) {
        ++encoder->invalid;
        vault_encoder_resync(encoder, clk, dt);
        return 0;
    }
    if (!encoder->synchronized) {
        encoder->synchronized = endpoint(current);
        encoder->partial_step = 0;
        return 0;
    }
    encoder->partial_step = (int8_t)(encoder->partial_step +
                                     transition[(previous << 2U) | current]);
    if (!endpoint(current)) return 0;

    int32_t step = 0;
    if (encoder->partial_step == 2) step = encoder->direction;
    else if (encoder->partial_step == -2) step = -encoder->direction;
    else ++encoder->bounce_returns;
    encoder->partial_step = 0;

    if (step > 0 && encoder->steps < POSITION_MAX) ++encoder->steps;
    if (step < 0 && encoder->steps > POSITION_MIN) --encoder->steps;
    int32_t new_rounds = encoder->steps / encoder->steps_per_round;
    int32_t change = new_rounds - encoder->rounds; /* At most one per accepted step. */
    encoder->rounds = new_rounds;
    return change;
}

void vault_beam_reset(VaultBeamFilter *filter)
{
    filter->count = 0U;
    filter->latched = false;
}

bool vault_beam_update(VaultBeamFilter *filter, uint16_t raw, bool armed)
{
    if (!armed) {
        vault_beam_reset(filter);
        return false;
    }
    bool blocked = filter->blocked_when_low ? raw <= filter->threshold : raw >= filter->threshold;
    bool clear = filter->blocked_when_low ?
        (uint32_t)raw >= (uint32_t)filter->threshold + filter->hysteresis :
        (uint32_t)raw + filter->hysteresis <= filter->threshold;
    if (filter->latched) {
        if (clear) vault_beam_reset(filter);
        return false;
    }
    if (!blocked) {
        filter->count = 0U;
        return false;
    }
    if (filter->count < filter->blocked_samples) ++filter->count;
    if (filter->count >= filter->blocked_samples) {
        filter->latched = true;
        return true;
    }
    return false;
}
