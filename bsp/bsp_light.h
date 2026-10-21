#ifndef VAULT_BSP_LIGHT_H
#define VAULT_BSP_LIGHT_H

#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Startup only. Configures ADC0 exclusively for PTB0 / ADC0_SE8.
 * 12-bit, VREFH/VREFL reference, long sample time, no runtime averaging.
 * Performs bounded electrical ADC self-calibration (not light calibration).
 */
BspStatus bsp_light_init(void);

/* Single task owns ADC0. These calls do not take a mutex.
 * start: BSP_BUSY if a previous sample has not been collected/cancelled.
 * try_read: BSP_BUSY if converting; BSP_NOT_READY if none was started.
 * A successful read writes 0..4095; errors leave *raw unchanged.
 * No light thresholds or beam-state interpretation.
 */
BspStatus bsp_light_start(void);
BspStatus bsp_light_try_read(uint16_t *raw);
void bsp_light_cancel(void);

/* Convenience for individual hardware tests: start and bounded CPU poll.
 * BSP_TIMEOUT aborts the conversion. Budget is BSP_ADC_CONVERSION_POLL_LIMIT
 * loop iterations, NOT milliseconds. For RTOS use start/try_read with delays.
 */
BspStatus bsp_light_read_raw(uint16_t *raw);

#ifdef __cplusplus
}
#endif
#endif
