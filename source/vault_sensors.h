#ifndef VAULT_SENSORS_H
#define VAULT_SENSORS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t encoder_steps;
    int32_t encoder_rounds;
    uint32_t encoder_invalid;
    uint32_t encoder_bounce_returns;
    uint32_t raw_queue_drops;
    uint32_t game_events_sent;
    uint32_t beam_queue_retries;
    uint32_t adc_errors;
    uint16_t ldr_raw;
    bool ldr_valid;
    bool inputs_ready;
    bool beam_detection_enabled;
} VaultSensorStats;

/* Call after BSP/queues/mutex initialization, before starting the scheduler.
 * Creates the private raw-edge queue; processed events use xInputQueue.
 */
bool vault_sensors_init(void);
void vault_input_task(void *argument);
void vault_tripwire_task(void *argument);
/* Task context, after scheduler start. Short protected diagnostic snapshot. */
void vault_sensors_get_stats(VaultSensorStats *out);

#endif
