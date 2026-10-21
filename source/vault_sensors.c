#include "vault_sensors.h"
#include "vault_sensor_config.h"
#include "vault_sensor_logic.h"
#include "vault_rtos.h"
#include "vault_fsm.h"
#include "bsp.h"
#include "fsl_debug_console.h"
#include <stddef.h>
#include <string.h>

_Static_assert(VAULT_ENCODER_STEPS_PER_ROUND > 0 &&
               VAULT_ENCODER_STEPS_PER_ROUND <= 2147483647,
               "Encoder steps per round must be a positive signed 32-bit value");
_Static_assert(VAULT_ENCODER_DIRECTION == 1 || VAULT_ENCODER_DIRECTION == -1,
               "Encoder direction must be +1 or -1");
_Static_assert(VAULT_LDR_THRESHOLD >= VAULT_LDR_HYSTERESIS &&
               VAULT_LDR_THRESHOLD + VAULT_LDR_HYSTERESIS <= 4095U &&
               VAULT_LDR_BLOCKED_SAMPLES > 0U, "Invalid beam-filter configuration");

typedef struct {
    BspInput input;
    BspInputLevels levels;
    TickType_t tick;
    uint32_t generation;
} RawInputEvent;

static QueueHandle_t raw_queue;
static volatile uint32_t raw_drops;
static VaultSensorStats stats;

static TickType_t ms_ticks(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks == 0U ? 1U : ticks;
}

static uint32_t tick_ms(TickType_t tick)
{
    return (uint32_t)(((uint64_t)tick * 1000U) / configTICK_RATE_HZ);
}

static void count_sent_event(void)
{
    taskENTER_CRITICAL();
    ++stats.game_events_sent;
    taskEXIT_CRITICAL();
}

/* This callback is deliberately independent of the game state. */
static void capture_edge(BspInput input, const BspInputLevels *levels, void *context)
{
    (void)context;
    RawInputEvent raw = {input, *levels, xTaskGetTickCountFromISR(), raw_drops};
    BaseType_t task_woken = pdFALSE;
    if (xQueueSendFromISR(raw_queue, &raw, &task_woken) != pdPASS) ++raw_drops;
    portYIELD_FROM_ISR(task_woken);
}

/* Preserve ordering and original capture times. Blocking here wakes tGame
 * to drain its queue; raw ISR capture can continue into the private queue.
 */
static void send_input(EventType type, int32_t value, TickType_t captured)
{
    VaultEvent event = {type, value, tick_ms(captured)};
    if (xQueueSend(xInputQueue, &event, portMAX_DELAY) == pdPASS) count_sent_event();
}

bool vault_sensors_init(void)
{
    if (raw_queue != NULL || xInputQueue == NULL || xStateMutex == NULL) return false;
    memset(&stats, 0, sizeof(stats));
    raw_drops = 0U;
    stats.beam_detection_enabled = VAULT_LDR_BEAM_DETECTION_ENABLED != 0;
    raw_queue = xQueueCreate(VAULT_RAW_INPUT_QUEUE_LENGTH, sizeof(RawInputEvent));
    return raw_queue != NULL;
}

void vault_sensors_get_stats(VaultSensorStats *out)
{
    if (out == NULL) return;
    taskENTER_CRITICAL();
    *out = stats;
    out->raw_queue_drops = raw_drops;
    taskEXIT_CRITICAL();
}

void vault_input_task(void *argument)
{
    (void)argument;
    const BspInput encoder_inputs[] = {
        BSP_INPUT_ENCODER_CLK, BSP_INPUT_ENCODER_DT, BSP_INPUT_ENCODER_SW
    };
    if (raw_queue == NULL) goto input_failed;
    for (uint32_t i = 0U; i < 3U; ++i) {
        if (bsp_inputs_configure(encoder_inputs[i], BSP_PULL_UP, BSP_EDGE_DISABLED) != BSP_OK)
            goto input_failed;
    }
    vTaskDelay(ms_ticks(5U)); /* Allow pull-ups to settle before seeding decoder. */
    BspInputLevels levels;
    if (bsp_inputs_read_all(&levels) != BSP_OK) goto input_failed;
    VaultEncoder encoder;
    if (!vault_encoder_init(&encoder, levels.clk, levels.dt,
                            VAULT_ENCODER_STEPS_PER_ROUND, VAULT_ENCODER_DIRECTION))
        goto input_failed;

    bool stable_button = levels.sw;
    bool button_candidate = levels.sw;
    bool button_pending = false;
    TickType_t button_since = 0U;
    TickType_t last_tap = 0U;
    bool have_tap = false;
    uint32_t generation = 0U;

    bsp_inputs_set_callback(capture_edge, NULL);
    for (uint32_t i = 0U; i < 3U; ++i) {
        if (bsp_inputs_configure(encoder_inputs[i], BSP_PULL_UP, BSP_EDGE_BOTH) != BSP_OK)
            goto input_failed;
    }
    if (bsp_inputs_configure(BSP_INPUT_TAP, BSP_PULL_UP,
                             VAULT_TAP_ACTIVE_LOW ? BSP_EDGE_FALLING : BSP_EDGE_RISING) != BSP_OK)
        goto input_failed;
    stats.inputs_ready = true;
    PRINTF("Sensors ready: %u accepted steps per round (provisional calibration).\r\n",
           (unsigned int)VAULT_ENCODER_STEPS_PER_ROUND);

    for (;;) {
        RawInputEvent raw;
        TickType_t wait = button_pending ? ms_ticks(VAULT_INPUT_WAIT_MS) : portMAX_DELAY;
        if (xQueueReceive(raw_queue, &raw, wait) == pdTRUE) {
            if (raw.generation != generation) {
                /* A gap in captured states cannot safely extend a sequence. */
                generation = raw.generation;
                vault_encoder_resync(&encoder, raw.levels.clk, raw.levels.dt);
                button_candidate = raw.levels.sw;
                button_since = raw.tick;
                button_pending = true;
            }
            if (raw.input == BSP_INPUT_TAP) {
                if (!have_tap || (TickType_t)(raw.tick - last_tap) >= ms_ticks(VAULT_TAP_DEBOUNCE_MS)) {
                    last_tap = raw.tick;
                    have_tap = true;
                    send_input(EV_KNOCK, 0, raw.tick);
                }
            } else if (raw.input == BSP_INPUT_ENCODER_CLK || raw.input == BSP_INPUT_ENCODER_DT) {
                int32_t change = vault_encoder_update(&encoder, raw.levels.clk, raw.levels.dt);
                stats.encoder_steps = encoder.steps;
                stats.encoder_rounds = encoder.rounds;
                stats.encoder_invalid = encoder.invalid;
                stats.encoder_bounce_returns = encoder.bounce_returns;
                if (change != 0) send_input(EV_DIAL, change, raw.tick);
            } else if (raw.input == BSP_INPUT_ENCODER_SW) {
                button_candidate = raw.levels.sw;
                button_since = raw.tick;
                button_pending = true;
            }
        }
        if (button_pending && (TickType_t)(xTaskGetTickCount() - button_since) >=
                               ms_ticks(VAULT_BUTTON_DEBOUNCE_MS)) {
            bool current;
            if (bsp_inputs_read(BSP_INPUT_ENCODER_SW, &current) == BSP_OK) {
                if (current != button_candidate) {
                    button_candidate = current;
                    button_since = xTaskGetTickCount();
                } else {
                    button_pending = false;
                    if (stable_button != current) {
                        stable_button = current;
                        if (!current) send_input(EV_PRESS, 0, button_since);
                    }
                }
            }
        }
    }

input_failed:
    bsp_inputs_set_callback(NULL, NULL);
    PRINTF("Sensor input task failed to initialize; GPIO events disabled.\r\n");
    for (;;) vTaskSuspend(NULL);
}

void vault_tripwire_task(void *argument)
{
    (void)argument;
    VaultBeamFilter filter = {VAULT_LDR_THRESHOLD, VAULT_LDR_HYSTERESIS,
        VAULT_LDR_BLOCKED_SAMPLES, 0U, VAULT_LDR_BLOCKED_WHEN_LOW != 0, false};
    bool pending_break = false;
    uint32_t pending_ms = 0U;
    uint32_t session = 0U;
    TickType_t last_wake = xTaskGetTickCount();
    PRINTF("Tripwire: ADC polling every %u ms; beam events %s (configure threshold).\r\n",
           VAULT_LDR_SAMPLE_MS, VAULT_LDR_BEAM_DETECTION_ENABLED ? "ENABLED" : "DISABLED");
    for (;;) {
        bool armed = false;
        uint32_t current_session = 0U;
        if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE) {
            armed = vault_is_armed(&gGame);
            current_session = gGame.start_ms;
            xSemaphoreGive(xStateMutex);
        }
        if (!armed || current_session != session) {
            vault_beam_reset(&filter);
            pending_break = false;
            session = current_session;
        }

        uint16_t raw = 0U;
        BspStatus status = bsp_light_start();
        if (status == BSP_OK) {
            TickType_t started = xTaskGetTickCount();
            do {
                status = bsp_light_try_read(&raw);
                if (status != BSP_BUSY) break;
                if ((TickType_t)(xTaskGetTickCount() - started) >= ms_ticks(VAULT_LDR_CONVERSION_MS)) {
                    status = BSP_TIMEOUT;
                    break;
                }
                vTaskDelay(1U);
            } while (true);
        }
        if (status == BSP_OK) {
            stats.ldr_raw = raw;
            stats.ldr_valid = true;
            if (VAULT_LDR_BEAM_DETECTION_ENABLED && vault_beam_update(&filter, raw, armed)) {
                pending_break = true;
                pending_ms = tick_ms(xTaskGetTickCount());
            }
        } else {
            bsp_light_cancel();
            stats.ldr_valid = false;
            ++stats.adc_errors;
            filter.count = 0U; /* Errors are not dark samples or consecutive valid samples. */
        }
        if (pending_break) {
            /* State may have changed while the ADC was completing. */
            bool same_session = false;
            if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE) {
                same_session = vault_is_armed(&gGame) && gGame.start_ms == session;
                xSemaphoreGive(xStateMutex);
            }
            VaultEvent event = {EV_BEAM_BROKEN, 0, pending_ms};
            if (!same_session) {
                pending_break = false;
                vault_beam_reset(&filter);
            } else if (xQueueSend(xInputQueue, &event, 0U) == pdPASS) {
                pending_break = false;
                count_sent_event();
            } else {
                ++stats.beam_queue_retries; /* Retry next period; don't silently lose alarm. */
            }
        }
        vTaskDelayUntil(&last_wake, ms_ticks(VAULT_LDR_SAMPLE_MS));
    }
}
