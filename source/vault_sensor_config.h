#ifndef VAULT_SENSOR_CONFIG_H
#define VAULT_SENSOR_CONFIG_H

/* Application/service configuration only. Existing BSP and VaultEvent APIs
 * are unchanged. Values marked provisional require physical measurements.
 */
#ifndef VAULT_ENCODER_STEPS_PER_ROUND
#define VAULT_ENCODER_STEPS_PER_ROUND 40 /* PROVISIONAL: accepted half-cycle steps. */
#endif
#define VAULT_ENCODER_DIRECTION       1 /* Set -1 to reverse the reported sign. */
#define VAULT_INPUT_TASK_PRIORITY     3
#define VAULT_RAW_INPUT_QUEUE_LENGTH  64U
#define VAULT_TAP_ACTIVE_LOW          1 /* Provisional active edge; adjust if needed. */
#define VAULT_TAP_DEBOUNCE_MS         40U
#define VAULT_BUTTON_DEBOUNCE_MS      20U
#define VAULT_INPUT_WAIT_MS           5U

#define VAULT_LDR_SAMPLE_MS           50U
#define VAULT_LDR_CONVERSION_MS       10U
#ifndef VAULT_LDR_BEAM_DETECTION_ENABLED
#define VAULT_LDR_BEAM_DETECTION_ENABLED 0 /* Enable AFTER measuring the threshold. */
#endif
#define VAULT_LDR_BLOCKED_WHEN_LOW    1 /* Provisional: invert if blockage raises ADC. */
#define VAULT_LDR_THRESHOLD           2000U /* PROVISIONAL raw ADC count, not calibration. */
#define VAULT_LDR_HYSTERESIS          100U
#define VAULT_LDR_BLOCKED_SAMPLES     3U

/* Real hardware and fake-event self-test are mutually exclusive in main. */
#ifndef VAULT_LOG_INPUT_EVENTS
#define VAULT_LOG_INPUT_EVENTS        1
#endif

#endif
