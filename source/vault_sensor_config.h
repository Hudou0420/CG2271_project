#ifndef VAULT_SENSOR_CONFIG_H
#define VAULT_SENSOR_CONFIG_H

/* Application/service configuration only. Existing BSP and VaultEvent APIs
 * are unchanged. Values marked provisional require physical measurements.
 */
#ifndef VAULT_ENCODER_STEPS_PER_ROUND
#define VAULT_ENCODER_STEPS_PER_ROUND 15 /* Accepted half-cycle steps per digit change. */
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
#define VAULT_LDR_CONFIRM_SAMPLES     3U
#ifndef VAULT_LDR_BEAM_DETECTION_ENABLED
#define VAULT_LDR_BEAM_DETECTION_ENABLED 1 /* Lid readings measured on the assembled box. */
#endif
#define VAULT_LDR_THRESHOLD           80U /* Open below this value. */
#define VAULT_LDR_HIGH_THRESHOLD      250U /* Upper open band begins above this value. */
#define VAULT_LDR_CLOSE_ABOVE         350U /* Upper open band ends here, inclusive. */

/* Real hardware and fake-event self-test are mutually exclusive in main. */
#ifndef VAULT_LOG_INPUT_EVENTS
#define VAULT_LOG_INPUT_EVENTS        1
#endif

#endif
