#include "vault_sensor_logic.h"
#include "vault_fsm.h"
#include <assert.h>
#include <stdio.h>

/* Inject actual captured Gray-code states, not pre-decoded directions. */
static int32_t physical_step(VaultEncoder *encoder, int direction)
{
    uint8_t start = encoder->previous;
    assert(start == 0U || start == 3U);
    uint8_t middle = direction > 0 ? (start == 3U ? 1U : 2U) :
                                       (start == 3U ? 2U : 1U);
    uint8_t end = start == 3U ? 0U : 3U;
    int32_t first = vault_encoder_update(encoder, (middle & 2U) != 0U, (middle & 1U) != 0U);
    assert(first == 0);
    /* Duplicate snapshot from the other flagged input is harmless. */
    assert(vault_encoder_update(encoder, (middle & 2U) != 0U, (middle & 1U) != 0U) == 0);
    return vault_encoder_update(encoder, (end & 2U) != 0U, (end & 1U) != 0U);
}

static void test_round_boundaries(void)
{
    VaultEncoder encoder;
    assert(vault_encoder_init(&encoder, true, true, 40U, 1));
    for (int i = 0; i < 39; ++i) assert(physical_step(&encoder, 1) == 0);
    assert(encoder.steps == 39 && encoder.rounds == 0);
    assert(physical_step(&encoder, 1) == 1);
    assert(encoder.steps == 40 && encoder.rounds == 1);
    assert(physical_step(&encoder, 1) == 0);
    assert(physical_step(&encoder, -1) == 0); /* 41 -> 40 stays in round 1. */
    assert(physical_step(&encoder, -1) == -1); /* 40 -> 39 crosses back to 0. */
    for (int i = 0; i < 78; ++i) assert(physical_step(&encoder, -1) == 0);
    assert(encoder.steps == -39 && encoder.rounds == 0);
    assert(physical_step(&encoder, -1) == -1);
    assert(encoder.rounds == -1);
    assert(physical_step(&encoder, 1) == 1); /* -40 -> -39 returns to 0. */
}

static void test_decoder_noise_and_bounds(void)
{
    VaultEncoder encoder;
    assert(!vault_encoder_init(&encoder, true, true, 0U, 1));
    assert(!vault_encoder_init(&encoder, true, true, 40U, 0));
    assert(vault_encoder_init(&encoder, true, true, 1U, 1));
    for (int i = 0; i < 10; ++i) {
        assert(vault_encoder_update(&encoder, false, true) == 0);
        assert(vault_encoder_update(&encoder, true, true) == 0);
    }
    assert(encoder.steps == 0 && encoder.bounce_returns == 10U);
    assert(vault_encoder_update(&encoder, false, false) == 0); /* Ambiguous 11->00. */
    assert(encoder.invalid == 1U && encoder.steps == 0);
    assert(physical_step(&encoder, 1) == 1);
    vault_encoder_resync(&encoder, true, true);
    assert(encoder.steps == 1 && encoder.rounds == 1);
    encoder.steps = 2147483647;
    encoder.rounds = 2147483647;
    assert(physical_step(&encoder, 1) == 0);
    encoder.steps = (-2147483647 - 1);
    encoder.rounds = (-2147483647 - 1);
    assert(physical_step(&encoder, -1) == 0);
    assert(vault_encoder_init(&encoder, true, true, 1U, -1));
    assert(physical_step(&encoder, 1) == -1);
    assert(physical_step(&encoder, -1) == 1);
}

static void test_existing_game_contract(void)
{
    VaultEncoder encoder;
    GameState game;
    vault_init(&game);
    game.state = ST_DIALING;
    assert(vault_encoder_init(&encoder, true, true, 40U, 1));
    int events = 0;
    for (int i = 0; i < 120; ++i) {
        int32_t change = physical_step(&encoder, 1);
        if (change != 0) {
            assert(change == 1);
            VaultEvent event = {EV_DIAL, change, 1000U};
            (void)vault_handle_event(&game, &event);
            ++events;
        }
    }
    assert(events == 3 && encoder.rounds == 3 && game.current_digit == 3U);
    assert(physical_step(&encoder, -1) == -1);
    VaultEvent reverse = {EV_DIAL, -1, 2000U};
    (void)vault_handle_event(&game, &reverse);
    assert(game.current_digit == 2U);
}

static void test_beam_filter(void)
{
    VaultBeamFilter filter = {2000U, 100U, 3U, 0U, true, false};
    assert(!vault_beam_update(&filter, 1900U, true));
    assert(!vault_beam_update(&filter, 1900U, true));
    assert(!vault_beam_update(&filter, 2050U, true)); /* Not consecutive. */
    assert(!vault_beam_update(&filter, 1900U, true));
    assert(!vault_beam_update(&filter, 1900U, true));
    assert(vault_beam_update(&filter, 1900U, true));
    assert(!vault_beam_update(&filter, 1900U, true)); /* Latch prevents flood. */
    assert(!vault_beam_update(&filter, 2050U, true)); /* Inside hysteresis. */
    assert(filter.latched);
    assert(!vault_beam_update(&filter, 2100U, true));
    assert(!filter.latched);
    assert(!vault_beam_update(&filter, 0U, false));
    assert(filter.count == 0U && !filter.latched);
    filter.blocked_when_low = false;
    assert(!vault_beam_update(&filter, 2500U, true));
    assert(!vault_beam_update(&filter, 2500U, true));
    assert(vault_beam_update(&filter, 2500U, true));
    assert(!vault_beam_update(&filter, 1950U, true) && filter.latched);
    assert(!vault_beam_update(&filter, 1900U, true) && !filter.latched);
}

int main(void)
{
    test_round_boundaries();
    test_decoder_noise_and_bounds();
    test_existing_game_contract();
    test_beam_filter();
    puts("PASS: round boundaries/reversal, bounce, invalid states, bounds, sign, existing FSM contract, beam filtering/hysteresis.");
    return 0;
}
