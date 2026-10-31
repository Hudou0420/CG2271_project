#include "vault_actuators.h"
#include "vault_actuator_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "bsp_outputs.h"
#include "fsl_debug_console.h"

#define CUE_QUEUE_LENGTH 16U
_Static_assert(VAULT_LED_BRIGHTNESS <= 255U && VAULT_LED_DIAL_BLUE <= VAULT_LED_BRIGHTNESS,
               "RGB brightness must fit 8 bits and dialing must respect the brightness cap");
_Static_assert(VAULT_ACTUATOR_PERIOD_MS > 0U && pdMS_TO_TICKS(VAULT_ACTUATOR_PERIOD_MS) > 0U,
               "Actuator period must span at least one RTOS tick");
_Static_assert(VAULT_ALARM_FLASH_MS > 0U && VAULT_ALARM_NOTE_MS > 0U &&
               VAULT_ALARM_SOUND_MS + VAULT_ALARM_SILENT_MS > 0U && VAULT_VICTORY_NOTE_MS > 0U,
               "Pattern periods must be nonzero");
/* Numeric order is playback priority; alarm is handled directly from state. */
typedef enum { CUE_NONE, CUE_TICK, CUE_CONFIRM, CUE_KNOCK, CUE_WRONG, CUE_VICTORY } CueKind;
typedef struct { VaultState state; uint32_t epoch; } OutputState;
typedef struct { uint32_t epoch; CueKind kind; uint8_t closeness; } Cue;
static QueueHandle_t states, cues;
static OutputState published, current;
static bool have_state, hint_active, tick_played;
static uint32_t state_since, hint_since, tone_since, last_tick;
static uint8_t hint_closeness;
static CueKind tone;
static bool led_failed, buzzer_failed, led_written, buzzer_written;
static uint8_t previous_red, previous_green, previous_blue;
static uint32_t previous_frequency;

bool vault_actuators_init(void)
{
    states = xQueueCreate(1U, sizeof(OutputState));
    cues = xQueueCreate(CUE_QUEUE_LENGTH, sizeof(Cue));
    if (!states || !cues) return false;
    published = (OutputState){ST_IDLE, 0U};
    current = published;
    have_state = hint_active = tick_played = false;
    led_failed = buzzer_failed = led_written = buzzer_written = false;
    tone = CUE_NONE;
    xQueueOverwrite(states, &published);
    return true;
}

void vault_actuators_publish(const GameState *before, const GameState *after,
                             const VaultEvent *event, const VaultActions *actions)
{
    if (!states || !cues) return;
    bool rearm = event->type == EV_ARM &&
        (before->state == ST_IDLE || before->state == ST_LOCKED || before->state == ST_UNLOCKED);
    bool disarm = event->type == EV_DISARM && before->state != ST_ALARM;
    if (rearm || disarm || before->state != after->state) ++published.epoch;
    published.state = after->state;
    xQueueOverwrite(states, &published);

    Cue cue = {published.epoch, CUE_NONE, after->closeness};
    if (actions->send_attempt) cue.kind = CUE_WRONG;
    else if (before->state == ST_LOCKED && after->state == ST_DIALING) cue.kind = CUE_KNOCK;
    else if (before->state == ST_DIALING && after->state == ST_DIALING) {
        if (event->type == EV_PRESS) cue.kind = CUE_CONFIRM;
        else if (event->type == EV_DIAL && before->current_digit != after->current_digit) cue.kind = CUE_TICK;
    }
    if (cue.kind != CUE_NONE && xQueueSend(cues, &cue, 0) != pdTRUE && cue.kind != CUE_TICK) {
        /* One producer. Queue APIs serialize reset against the consumer.
         * Important cues replace a saturated backlog instead of blocking. */
        xQueueReset(cues);
        (void)xQueueSend(cues, &cue, 0);
    }
}

static void synchronize_state(uint32_t now)
{
    OutputState latest;
    if (xQueuePeek(states, &latest, 0) != pdTRUE) return;
    if (!have_state || latest.epoch != current.epoch || latest.state != current.state) {
        current = latest;
        have_state = true;
        state_since = now;
        hint_active = tick_played = false;
        /* State-entry cues bypass even a saturated event queue. */
        tone = current.state == ST_UNLOCKED ? CUE_VICTORY :
               current.state == ST_DIALING ? CUE_KNOCK : CUE_NONE;
        tone_since = now;
    }
}

static uint32_t frequency(uint32_t now)
{
    uint32_t elapsed = now - tone_since;
    switch (tone) {
    case CUE_TICK: return elapsed < VAULT_DIAL_TICK_MS ? VAULT_DIAL_TICK_HZ : 0U;
    case CUE_CONFIRM: return elapsed < VAULT_CONFIRM_MS ? VAULT_CONFIRM_HZ : 0U;
    case CUE_KNOCK: return elapsed < VAULT_KNOCK_ACCEPTED_MS ? VAULT_KNOCK_ACCEPTED_HZ : 0U;
    case CUE_WRONG:
        if (elapsed < VAULT_WRONG_FIRST_MS) return VAULT_WRONG_HIGH_HZ;
        if (elapsed < VAULT_WRONG_FIRST_MS + VAULT_WRONG_GAP_MS) return 0U;
        if (elapsed < VAULT_WRONG_FIRST_MS + VAULT_WRONG_GAP_MS + VAULT_WRONG_LAST_MS) return VAULT_WRONG_LOW_HZ;
        return 0U;
    case CUE_VICTORY: {
        static const uint32_t notes[] = VAULT_VICTORY_NOTES;
        uint32_t span = VAULT_VICTORY_NOTE_MS + VAULT_VICTORY_GAP_MS;
        uint32_t index = elapsed / span;
        return index < sizeof(notes) / sizeof(notes[0]) && elapsed % span < VAULT_VICTORY_NOTE_MS ? notes[index] : 0U;
    }
    default: return 0U;
    }
}

static uint32_t tone_duration(void)
{
    switch (tone) {
    case CUE_TICK: return VAULT_DIAL_TICK_MS;
    case CUE_CONFIRM: return VAULT_CONFIRM_MS;
    case CUE_KNOCK: return VAULT_KNOCK_ACCEPTED_MS;
    case CUE_WRONG: return VAULT_WRONG_FIRST_MS + VAULT_WRONG_GAP_MS + VAULT_WRONG_LAST_MS;
    case CUE_VICTORY: {
        static const uint32_t notes[] = VAULT_VICTORY_NOTES;
        return (sizeof(notes) / sizeof(notes[0])) * (VAULT_VICTORY_NOTE_MS + VAULT_VICTORY_GAP_MS) - VAULT_VICTORY_GAP_MS;
    }
    default: return 0U;
    }
}

static void write_outputs(uint8_t red, uint8_t green, uint8_t blue, uint32_t hz)
{
    if (!VAULT_LED_ENABLED) red = green = blue = 0U;
    if (!VAULT_BUZZER_ENABLED) hz = 0U;
    if (!led_failed && (!led_written || red != previous_red || green != previous_green || blue != previous_blue)) {
        if (bsp_rgb_set(red, green, blue) != BSP_OK) {
            PRINTF("[actuator] RGB write failed; LED disabled until reboot\r\n");
            (void)bsp_rgb_set(0U, 0U, 0U);
            led_failed = true;
        } else {
            previous_red = red; previous_green = green; previous_blue = blue; led_written = true;
        }
    }
    if (!buzzer_failed && (!buzzer_written || hz != previous_frequency)) {
        if (bsp_buzzer_set_frequency(hz) != BSP_OK) {
            PRINTF("[actuator] buzzer write failed; sound disabled until reboot\r\n");
            (void)bsp_buzzer_set_frequency(0U);
            buzzer_failed = true;
        } else {
            previous_frequency = hz; buzzer_written = true;
        }
    }
}

void vault_actuators_step(uint32_t now)
{
    if (!states || !cues) return;
    synchronize_state(now);
    if (now - tone_since >= tone_duration()) tone = CUE_NONE;
    Cue cue;
    for (unsigned i = 0; i < CUE_QUEUE_LENGTH && xQueueReceive(cues, &cue, 0) == pdTRUE; ++i) {
        /* Re-read after receiving: a preempting publisher may have advanced
         * the epoch between the initial snapshot and this queue receive. */
        synchronize_state(now);
        if (cue.epoch != current.epoch || current.state == ST_ALARM || current.state == ST_UNLOCKED) continue;
        if (cue.kind == CUE_WRONG && current.state == ST_LOCKED) {
            hint_active = true; hint_since = now; hint_closeness = cue.closeness;
        }
        if (cue.kind == CUE_TICK && tick_played && now - last_tick < VAULT_DIAL_TICK_GAP_MS) continue;
        if (cue.kind >= tone) {
            /* Identical ticks in this batch collapse into the same short tone. */
            tone = cue.kind; tone_since = now;
            if (cue.kind == CUE_TICK) { last_tick = now; tick_played = true; }
        }
    }
    uint8_t red = 0U, green = 0U, blue = 0U;
    uint32_t hz = frequency(now);
    switch (current.state) {
    case ST_LOCKED:
        if (hint_active && now - hint_since < VAULT_HINT_MS) {
            unsigned score = hint_closeness > 100U ? 100U : hint_closeness;
            if (score <= 50U) {
                red = green = (uint8_t)(VAULT_LED_BRIGHTNESS * score / 50U);
                blue = (uint8_t)(VAULT_LED_BRIGHTNESS * (50U - score) / 50U);
            } else {
                red = VAULT_LED_BRIGHTNESS;
                green = (uint8_t)(VAULT_LED_BRIGHTNESS -
                    (VAULT_LED_BRIGHTNESS - VAULT_LED_BRIGHTNESS * 3U / 8U) * (score - 50U) / 50U);
            }
        } else { hint_active = false; red = VAULT_LED_BRIGHTNESS; }
        break;
    case ST_DIALING: blue = VAULT_LED_DIAL_BLUE; break;
    case ST_UNLOCKED: green = VAULT_LED_BRIGHTNESS; break;
    case ST_ALARM: {
        uint32_t elapsed = now - state_since;
        red = (elapsed / VAULT_ALARM_FLASH_MS) % 2U == 0U ? VAULT_LED_BRIGHTNESS : 0U;
        uint32_t phase = elapsed % (VAULT_ALARM_SOUND_MS + VAULT_ALARM_SILENT_MS);
        hz = phase < VAULT_ALARM_SOUND_MS ?
            ((phase / VAULT_ALARM_NOTE_MS) % 2U == 0U ? VAULT_ALARM_LOW_HZ : VAULT_ALARM_HIGH_HZ) : 0U;
        break;
    }
    default: hz = 0U; break;
    }
    write_outputs(red, green, blue, hz);
}

void vault_actuators_task(void *argument)
{
    (void)argument;
    TickType_t previous = xTaskGetTickCount();
    for (;;) {
        vault_actuators_step((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
        vTaskDelayUntil(&previous, pdMS_TO_TICKS(VAULT_ACTUATOR_PERIOD_MS));
    }
}
