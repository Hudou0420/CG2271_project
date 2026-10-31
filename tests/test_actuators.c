/* Exercise the actual actuator service with real FSM events and a fake clock. */
#include "../source/vault_actuators.c"
#include "vault_fsm.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>

static uint8_t output_red, output_green, output_blue;
static uint32_t output_hz;
static unsigned rgb_calls, buzzer_calls, rgb_failures, buzzer_failures;
static GameState game;
BspStatus bsp_rgb_set(uint8_t red, uint8_t green, uint8_t blue)
{
    ++rgb_calls;
    if (rgb_failures) { --rgb_failures; return BSP_NOT_READY; }
    output_red = red; output_green = green; output_blue = blue;
    return BSP_OK;
}
BspStatus bsp_buzzer_set_frequency(uint32_t hz)
{
    ++buzzer_calls;
    if (buzzer_failures) { --buzzer_failures; return BSP_NOT_READY; }
    output_hz = hz;
    return BSP_OK;
}
static void expect_output(unsigned red, unsigned green, unsigned blue, unsigned hz)
{
    assert(output_red == (VAULT_LED_ENABLED ? red : 0U));
    assert(output_green == (VAULT_LED_ENABLED ? green : 0U));
    assert(output_blue == (VAULT_LED_ENABLED ? blue : 0U));
    assert(output_hz == (VAULT_BUZZER_ENABLED ? hz : 0U));
}
static void reset(void)
{
    test_runtime_reset();
    rgb_calls = buzzer_calls = rgb_failures = buzzer_failures = 0U;
    output_red = output_green = output_blue = 0U; output_hz = 0U;
    assert(vault_actuators_init());
    vault_init(&game);
    vault_actuators_step(0U);
    expect_output(0, 0, 0, 0);
}
static void event(EventType type, int32_t value, uint32_t captured)
{
    VaultEvent input = {type, value, captured};
    GameState before = game;
    VaultActions actions = vault_handle_event(&game, &input);
    vault_actuators_publish(&before, &game, &input, &actions);
}
static void dialing(void)
{
    event(EV_ARM, 123, 1);
    event(EV_KNOCK, 0, 100);
    event(EV_KNOCK, 0, 400);
    event(EV_KNOCK, 0, 1300);
    assert(game.state == ST_DIALING);
}
static void wrong_hint(uint8_t closeness)
{
    GameState before = game;
    before.state = ST_DIALING;
    game.state = ST_LOCKED; game.closeness = closeness;
    VaultEvent input = {EV_PRESS, 0, 1};
    VaultActions actions = {true, false, false, false};
    vault_actuators_publish(&before, &game, &input, &actions);
}
static void hints_and_states(void)
{
    reset(); event(EV_ARM, 123, 1); vault_actuators_step(10);
    expect_output(64, 0, 0, 0);
    event(EV_KNOCK, 0, 100); vault_actuators_step(20);
    expect_output(64, 0, 0, 0); /* individual knocks remain silent */
    wrong_hint(0); vault_actuators_step(100);
    expect_output(0, 0, 64, 880);
    vault_actuators_step(180); expect_output(0, 0, 64, 0);
    vault_actuators_step(220); expect_output(0, 0, 64, 440);
    vault_actuators_step(340); expect_output(0, 0, 64, 0);
    vault_actuators_step(5099); expect_output(0, 0, 64, 0);
    vault_actuators_step(5100); expect_output(64, 0, 0, 0);
    wrong_hint(25); vault_actuators_step(5200); expect_output(32, 32, 32, 880);
    wrong_hint(50); vault_actuators_step(5300); expect_output(64, 64, 0, 880);
    wrong_hint(75); vault_actuators_step(5400); expect_output(64, 44, 0, 880);
    wrong_hint(100); vault_actuators_step(5500); expect_output(64, 24, 0, 880);
    wrong_hint(255); vault_actuators_step(5600); expect_output(64, 24, 0, 880);
    event(EV_ARM, 123, 2); vault_actuators_step(5610); expect_output(64, 0, 0, 0);
    wrong_hint(50); vault_actuators_step(5700);
    event(EV_DISARM, 0, 3); vault_actuators_step(5710); expect_output(0, 0, 0, 0);
    puts("PASS LED state colours, zero-score hints, interpolation, five-second expiry and rearm/disarm cancellation");
}
static void inputs_and_win(void)
{
    reset(); dialing(); vault_actuators_step(0); expect_output(0, 0, 16, 1047);
    vault_actuators_step(80); expect_output(0, 0, 16, 0);
    event(EV_DIAL, 1, 1400); vault_actuators_step(100); expect_output(0, 0, 16, 2000);
    vault_actuators_step(120); expect_output(0, 0, 16, 0);
    event(EV_DIAL, 1, 1401); vault_actuators_step(140); expect_output(0, 0, 16, 0);
    event(EV_DIAL, -1, 1402); vault_actuators_step(160); expect_output(0, 0, 16, 2000);
    event(EV_PRESS, 0, 1500); vault_actuators_step(180); expect_output(0, 0, 16, 1320);
    event(EV_DIAL, 1, 1501); vault_actuators_step(200); expect_output(0, 0, 16, 1320); /* confirm beats tick */
    vault_actuators_step(240); expect_output(0, 0, 16, 0);
    event(EV_DIAL, 1, 1502); event(EV_PRESS, 0, 1600); vault_actuators_step(260);
    expect_output(0, 0, 16, 1320);
    event(EV_DIAL, 3, 1601); event(EV_PRESS, 0, 1700);
    assert(game.state == ST_UNLOCKED);
    vault_actuators_step(300); expect_output(0, 64, 0, 523);
    vault_actuators_step(420); expect_output(0, 64, 0, 0);
    vault_actuators_step(460); expect_output(0, 64, 0, 659);
    vault_actuators_step(620); expect_output(0, 64, 0, 784);
    vault_actuators_step(780); expect_output(0, 64, 0, 1047);
    vault_actuators_step(900); expect_output(0, 64, 0, 0);
    event(EV_PRESS, 0, 2000); vault_actuators_step(1000); expect_output(0, 64, 0, 0);
    event(EV_ARM, 123, 2001); vault_actuators_step(1020); expect_output(64, 0, 0, 0);
    dialing(); vault_actuators_step(1100);
    event(EV_PRESS, 0, 2002); event(EV_PRESS, 0, 2003); event(EV_PRESS, 0, 2004);
    assert(game.state == ST_LOCKED);
    vault_actuators_step(1120); assert(tone == CUE_WRONG && hint_active);
    dialing(); vault_actuators_step(1140); expect_output(0, 0, 16, 1047);
    assert(!hint_active);
    puts("PASS actual FSM knock acceptance, dial throttling, confirmations, wrong result and one-shot victory");
}
static void alarm_and_rollover(void)
{
    reset(); dialing(); wrong_hint(75); vault_actuators_step(UINT32_MAX - 100U);
    event(EV_TAMPER, 0, 1400);
    uint32_t start = UINT32_MAX - 50U;
    vault_actuators_step(start); expect_output(64, 0, 0, 880); assert(!hint_active);
    vault_actuators_step(start + 125U); expect_output(64, 0, 0, 1320);
    vault_actuators_step(start + 250U); expect_output(0, 0, 0, 880);
    vault_actuators_step(start + 1000U); expect_output(64, 0, 0, 0);
    event(EV_DISARM, 0, 2000); event(EV_TAMPER, 0, 2001);
    vault_actuators_step(start + 2999U); expect_output(0, 0, 0, 0);
    vault_actuators_step(start + 3000U); expect_output(64, 0, 0, 880);
    assert(state_since == start); /* ignored events do not restart cadence */
    vault_actuators_step(start + 30000U); assert(game.state == ST_ALARM);
    event(EV_LOCKOUT_DONE, 0, 32000); vault_actuators_step(start + 30020U);
    expect_output(64, 0, 0, 0);
    wrong_hint(0); vault_actuators_step(UINT32_MAX - 20U);
    vault_actuators_step(UINT32_MAX - 20U + 4999U); expect_output(0, 0, 64, 0);
    vault_actuators_step(UINT32_MAX - 20U + 5000U); expect_output(64, 0, 0, 0);
    published.epoch = UINT32_MAX;
    event(EV_ARM, 123, 1); vault_actuators_step(6000); expect_output(64, 0, 0, 0);
    assert(current.epoch == 0U);
    puts("PASS intermittent alarm, priority, unchanged lockout authority, timer and epoch rollover");
}
static void queues_and_failures(void)
{
    reset(); dialing(); vault_actuators_step(0); vault_actuators_step(80);
    for (unsigned i = 0; i < 1000; ++i) event(EV_DIAL, 1, 1400 + i);
    assert(uxQueueMessagesWaiting(cues) == 16U);
    wrong_hint(50); assert(uxQueueMessagesWaiting(cues) == 1U);
    vault_actuators_step(100); expect_output(64, 64, 0, 880);
    for (unsigned i = 0; i < 100; ++i) event(EV_DIAL, 1, 2500 + i);
    assert(!uxQueueMessagesWaiting(cues)); /* rejected actions do not beep */
    dialing(); event(EV_TAMPER, 0, 3000); vault_actuators_step(120);
    expect_output(64, 0, 0, 880); /* queued old-epoch knock is ignored */
    unsigned writes = rgb_calls + buzzer_calls;
    vault_actuators_step(121); assert(rgb_calls + buzzer_calls == writes);
    /* A confirm can replace a full tick backlog before the task observes
     * dialing. The state-entry knock cue must still take priority. */
    reset(); dialing();
    for (unsigned i = 0; i < 16; ++i) event(EV_DIAL, 1, 1400 + i);
    event(EV_PRESS, 0, 1500);
    vault_actuators_step(20); expect_output(0, 0, 16, 1047);
    reset(); rgb_failures = 1; event(EV_ARM, 123, 1);
    /* Disabled builds stay off, so force the first write to test errors too. */
    led_written = false;
    vault_actuators_step(20); assert(led_failed && output_red == 0);
    unsigned saved_rgb = rgb_calls;
    buzzer_failures = 1; buzzer_written = false;
    dialing(); vault_actuators_step(40); assert(buzzer_failed && output_hz == 0);
    unsigned saved_buzzer = buzzer_calls;
    event(EV_TAMPER, 0, 1400); vault_actuators_step(60); vault_actuators_step(200);
    assert(rgb_calls == saved_rgb && buzzer_calls == saved_buzzer);
    test_runtime_reset(); test_allocation_failure = true;
    assert(!vault_actuators_init()); vault_actuators_step(0);
    puts("PASS queue saturation, stale cue cancellation, independent driver failures and allocation failure");
}
static bool preempt_pending;
static int preempt_receive(QueueHandle_t queue, void *item, TickType_t wait)
{
    (void)item; (void)wait;
    if (queue == cues && preempt_pending) {
        preempt_pending = false;
        event(EV_TAMPER, 0, 1600);
    }
    return -1;
}
static void preemption(void)
{
    reset(); dialing(); vault_actuators_step(0); vault_actuators_step(80);
    event(EV_DIAL, 1, 1500);
    preempt_pending = true; test_receive_hook = preempt_receive;
    vault_actuators_step(100);
    test_receive_hook = NULL;
    expect_output(64, 0, 0, 880);
    assert(!preempt_pending && current.state == ST_ALARM);
    puts("PASS a game-state change between snapshot and cue receive supersedes queued dial feedback");
}
static jmp_buf task_stop;
static unsigned task_cycles;
static void after_delay(void)
{
    assert(test_now == VAULT_ACTUATOR_PERIOD_MS * ++task_cycles);
    if (task_cycles == 3) longjmp(task_stop, 1);
}
static void actual_task(void)
{
    reset(); task_cycles = 0; test_delay_hook = after_delay;
    if (setjmp(task_stop) == 0) vault_actuators_task(NULL);
    test_delay_hook = NULL;
    assert(task_cycles == 3);
}
int main(void)
{
    hints_and_states(); inputs_and_win(); alarm_and_rollover(); queues_and_failures(); preemption(); actual_task();
    test_runtime_reset();
    puts("PASS real actuator task runs at 20 ms; enabled/disabled outputs respect configuration");
}
