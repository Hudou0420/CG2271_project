#ifndef VAULT_BSP_BOARD_H
#define VAULT_BSP_BOARD_H

/* FRDM-MCXC444 (MCXC444VLH). This is the MCU-side allocation only.
 * Sources: board/pin_mux.c header mapping and NXP MCXC444QSG / datasheet.
 * ESP32 link LPUART1 PTE0/J2-18 (TX) and PTE1/J2-20 (RX, source/vault_comms.c),
 * debug LPUART0 PTA1/PTA2, and
 * SWD PTA0/PTA3 are reserved. Do not reconfigure these from this BSP.
 */
#include "fsl_device_registers.h"

/* All four raw input lines share the PORTC_PORTD interrupt vector. */
#define BSP_INPUT_PORT           PORTC
#define BSP_INPUT_GPIO           GPIOC
#define BSP_TAP_PIN              0U  /* J1-5 */
#define BSP_ENCODER_CLK_PIN      4U  /* J1-7 */
#define BSP_ENCODER_DT_PIN       6U  /* J1-9 */
#define BSP_ENCODER_SW_PIN       7U  /* J1-11 */
#define BSP_INPUT_MASK ((1UL << BSP_TAP_PIN) | (1UL << BSP_ENCODER_CLK_PIN) | \
                        (1UL << BSP_ENCODER_DT_PIN) | (1UL << BSP_ENCODER_SW_PIN))

/* Photoresistor: J4-2 / Arduino A0, ADC0_SE8, analog mux (ALT0). */
#define BSP_LIGHT_PORT           PORTB
#define BSP_LIGHT_GPIO           GPIOB
#define BSP_LIGHT_PIN            0U
#define BSP_LIGHT_ADC_CHANNEL    8U
#define BSP_LIGHT_RAW_MAX        4095U /* 12-bit single-ended */

/* External common-anode RGB, mapped to observed colours of the current
 * wiring. R/G are swapped from the initial allocation; do not rewire again.
 * None uses an on-board LED pin.
 */
#define BSP_RGB_RED_PORT         PORTC
#define BSP_RGB_RED_GPIO         GPIOC
#define BSP_RGB_RED_PIN          2U  /* J4-10, TPM0_CH1 ALT4 */
#define BSP_RGB_GREEN_PORT       PORTC
#define BSP_RGB_GREEN_GPIO       GPIOC
#define BSP_RGB_GREEN_PIN        1U  /* J4-12, TPM0_CH0 ALT4 */
#define BSP_RGB_BLUE_PORT        PORTD
#define BSP_RGB_BLUE_GPIO        GPIOD
#define BSP_RGB_BLUE_PIN         2U  /* J2-4, TPM0_CH2 ALT4 */
#define BSP_RGB_TPM              TPM0
#define BSP_RGB_RED_CHANNEL      1U
#define BSP_RGB_GREEN_CHANNEL    0U
#define BSP_RGB_BLUE_CHANNEL     2U
#define BSP_RGB_MUX              4U
#define BSP_RGB_FREQUENCY_HZ      1000U

/* Buzzer has its own timer, so pitch changes do not affect RGB PWM. */
#define BSP_BUZZER_PORT          PORTA
#define BSP_BUZZER_GPIO          GPIOA
#define BSP_BUZZER_PIN           12U /* J1-8, TPM1_CH0 ALT3 */
#define BSP_BUZZER_TPM           TPM1
#define BSP_BUZZER_CHANNEL       0U
#define BSP_BUZZER_MUX           3U
#define BSP_BUZZER_MIN_HZ        40U
#define BSP_BUZZER_MAX_HZ        20000U

#define BSP_RELAY_PORT           PORTD
#define BSP_RELAY_GPIO           GPIOD
#define BSP_RELAY_PIN            6U  /* J2-8 */
#define BSP_LASER_PORT           PORTD
#define BSP_LASER_GPIO           GPIOD
#define BSP_LASER_PIN            7U  /* J2-10, optional logic control only */

/* Logical CMSIS priority, not a shifted NVIC register value (0..3 here). */
#ifndef BSP_INPUT_IRQ_PRIORITY
#define BSP_INPUT_IRQ_PRIORITY   3U
#endif

/* Set to 0 if an application supplies the shared interrupt handler.
 * That handler must call bsp_inputs_irq_dispatch() and handle its own flags.
 */
#ifndef BSP_DEFINE_PORT_IRQ_HANDLER
#define BSP_DEFINE_PORT_IRQ_HANDLER 1
#endif

/* Bounded CPU polling budgets, not milliseconds. Calibration is startup-only.
 * RTOS tasks should prefer the nonblocking ADC start/try_read pair.
 */
#ifndef BSP_ADC_CALIBRATION_POLL_LIMIT
#define BSP_ADC_CALIBRATION_POLL_LIMIT 200000UL
#endif
#ifndef BSP_ADC_CONVERSION_POLL_LIMIT
#define BSP_ADC_CONVERSION_POLL_LIMIT 10000UL
#endif

#endif
