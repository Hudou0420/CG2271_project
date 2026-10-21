#ifndef VAULT_BSP_OUTPUTS_H
#define VAULT_BSP_OUTPUTS_H

#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool rgb_active_low;       /* true for the tested common-positive RGB */
    bool relay_active_low;     /* Set after testing; affects logical set(). */
    bool laser_active_low;
    bool enable_laser_signal;  /* Optional PTD7 logic control; default false. */
} BspOutputConfig;

/* Defaults: RGB active-low, relay/laser active-high, laser signal disabled. */
void bsp_outputs_default_config(BspOutputConfig *config);

/* Startup only; call after BOARD_InitBootClocks(). NULL uses defaults.
 * Owns TPM0 and TPM1 exclusively. Selects shared TPM source IRC48M without
 * changing UART clocks. Initializes RGB off, buzzer silent, relay inactive.
 * Disabled laser signal leaves PTD7 as an input with no pulls.
 */
BspStatus bsp_outputs_init(const BspOutputConfig *config);

/* Task context only; one application task owns ALL output writes.
 * No delays, pattern playback, queues or game logic in these functions.
 * RGB: independent 0..255 brightness, 0 off, 255 full; fixed 1 kHz PWM.
 */
BspStatus bsp_rgb_set(uint8_t red, uint8_t green, uint8_t blue);

/* Hardware square wave, 50% duty; zero frequency means silent.
 * Supported request range: 40..20000 Hz (rounded to hardware period).
 * Reapplying an identical request does not restart the timer.
 */
BspStatus bsp_buzzer_set_frequency(uint32_t frequency_hz);

/* Raw PWM option: 0..1000 permille (500 = 50%). freq=0 or duty=0 is silent.
 * Invalid requests return BSP_INVALID_ARGUMENT without changing the output.
 */
BspStatus bsp_buzzer_set_pwm(uint32_t frequency_hz, uint16_t duty_permille);

/* Raw electrical level and polarity-aware logical control are both exposed.
 * true in set() means requested ON, not a physical contact-state readback.
 */
BspStatus bsp_relay_write_level(bool high);
BspStatus bsp_relay_set(bool on);
BspStatus bsp_laser_write_level(bool high);
BspStatus bsp_laser_set(bool on);

/* Logical off for all initialized outputs. Does not reconfigure pin pulls. */
void bsp_outputs_off(void);

#ifdef __cplusplus
}
#endif
#endif
