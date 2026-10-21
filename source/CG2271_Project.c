/*
 * CG2271_Project.c  --  Person 1 (MCU core + game logic)
 *
 * FreeRTOS skeleton for The Vault.
 *   - tGame      : REAL (Person 1). Owns the FSM.
 *   - tInput     : sensor adapter; calibrated encoder rounds, taps, button
 *   - tTripwire  : polled LDR service; threshold gated by configuration
 *   - tComms     : STUB (Person 3 fills in)
 *   - tActuator  : STUB (Person 2 fills in)
 *   - tSelfTest  : TEMPORARY. Injects fake events so you can watch the FSM
 *                  run on the board before anyone else's code exists.
 *                  Set VAULT_SELF_TEST to 0 when real inputs are hooked up.
 *
 * SETUP CHECKLIST (do these in MCUXpresso, see the instructions):
 *   - SDK Components: Operating Systems -> RTOS, Others -> RTOS -> Heap -> heap 4 ONLY
 *   - Project Template -> RTOS ticked
 *   - FreeRTOSConfig_Gen.h: configUSE_PREEMPTION 1, configUSE_TIME_SLICING 1
 *   - Add vault_types.h, vault_fsm.h, vault_fsm.c, vault_rtos.h to the project's source folder
 */
#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "bsp.h"

#include "vault_rtos.h"
#include "vault_fsm.h"
#include "vault_sensors.h"
#include "vault_sensor_config.h"

#ifndef VAULT_SELF_TEST
#define VAULT_SELF_TEST  0     /* 0 = real sensors, 1 = synthetic FSM demo only */
#endif

/* ------------------------------------------------------------------ */
/* Shared objects                                                      */
/* ------------------------------------------------------------------ */
QueueHandle_t     xInputQueue;
QueueHandle_t     xUartTxQueue;
QueueHandle_t     xUartRxQueue;
SemaphoreHandle_t xStateMutex;
GameState         gGame;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

#if VAULT_LOG_INPUT_EVENTS
static const char *event_name(EventType type)
{
    switch (type) {
    case EV_ARM: return "EV_ARM";
    case EV_DISARM: return "EV_DISARM";
    case EV_KNOCK: return "EV_KNOCK";
    case EV_DIAL: return "EV_DIAL";
    case EV_PRESS: return "EV_PRESS";
    case EV_BEAM_BROKEN: return "EV_BEAM_BROKEN";
    case EV_TAMPER: return "EV_TAMPER";
    case EV_LOCKOUT_DONE: return "EV_LOCKOUT_DONE";
    default: return "EV_NONE";
    }
}
#endif

/* ------------------------------------------------------------------ */
/* tGame  (Person 1)  -- the ONLY task that changes gGame              */
/*                                                                     */
/* Blocks on xInputQueue. Wakes when an event arrives, runs the FSM,   */
/* then queues any packets for tComms. During ALARM it waits only for  */
/* the remaining lockout time, so no software timer is needed.         */
/* ------------------------------------------------------------------ */
static void tGame(void *p)
{
    TickType_t alarmDeadline = 0;

    while (1) {
        /* How long may we sleep? Forever, unless an alarm lockout is running. */
        TickType_t wait = portMAX_DELAY;
        if (gGame.state == ST_ALARM) {      /* safe: tGame is the only writer */
            TickType_t nowT = xTaskGetTickCount();
            wait = ((int32_t)(alarmDeadline - nowT) > 0) ? (alarmDeadline - nowT) : 0;
        }

        VaultEvent ev;
        if (xQueueReceive(xInputQueue, &ev, wait) != pdTRUE) {
            /* Timed out: only possible during ALARM -> lockout is over */
            if (gGame.state != ST_ALARM) {
                continue;
            }
            ev.type    = EV_LOCKOUT_DONE;
            ev.value   = 0;
            ev.tick_ms = 0;
        }

        if (ev.tick_ms == 0) {              /* sender didn't timestamp it */
            ev.tick_ms = now_ms();
        }
#if VAULT_LOG_INPUT_EVENTS
        /* Explicit sign avoids the minimal SDK formatter's negative %d issue. */
        uint32_t magnitude = ev.value < 0 ? 0U - (uint32_t)ev.value : (uint32_t)ev.value;
        PRINTF("[input] %s value=%s%u captured_ms=%u\r\n", event_name(ev.type),
               ev.value < 0 ? "-" : "+", (unsigned int)magnitude, (unsigned int)ev.tick_ms);
#endif

        /* --- run the FSM while holding the mutex --- */
        TxMsg        tx[3];
        int          ntx = 0;
        VaultActions act;

        if (xSemaphoreTake(xStateMutex, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        act = vault_handle_event(&gGame, &ev);
        if (act.send_attempt) vault_make_packet(&gGame, PKT_ATTEMPT, &tx[ntx++]);
        if (act.send_solved)  vault_make_packet(&gGame, PKT_SOLVED,  &tx[ntx++]);
        if (act.send_alarm)   vault_make_packet(&gGame, PKT_ALARM,   &tx[ntx++]);
        xSemaphoreGive(xStateMutex);

        /* --- after releasing the mutex --- */
        if (act.start_lockout) {
            alarmDeadline = xTaskGetTickCount() + pdMS_TO_TICKS(LOCKOUT_MS);
        }
        for (int i = 0; i < ntx; i++) {
            /* 0 = never block. If tComms is behind, drop rather than stall the game. */
            xQueueSend(xUartTxQueue, &tx[i], 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* tTripwire  (Person 2 sensor service)                                */
/* ------------------------------------------------------------------ */
static void tTripwire(void *p)
{
#if VAULT_SELF_TEST
    (void)p;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
#else
    vault_tripwire_task(p);
#endif
}

/* ------------------------------------------------------------------ */
/* tComms  (STUB - Person 3)                                           */
/* Right now it just prints whatever tGame wants to send.              */
/* ------------------------------------------------------------------ */
static void tComms(void *p)
{
    TxMsg m;
    while (1) {
        if (xQueueReceive(xUartTxQueue, &m, portMAX_DELAY) == pdTRUE) {
            PRINTF("[tComms stub] would send packet type 0x%02X, %d payload bytes\r\n",
                   (unsigned)m.type, (int)m.len);
        }
        /* Person 3: also build/parse real packets, handle ACK/NAK, and post
         * EV_ARM / EV_DISARM / EV_TAMPER into xInputQueue. */
    }
}

/* ------------------------------------------------------------------ */
/* tActuator  (STUB - Person 2)                                        */
/* Right now it just prints the state whenever it changes.             */
/* ------------------------------------------------------------------ */
static void tActuator(void *p)
{
    int lastState = -1;
    while (1) {
        VaultState s;
        uint8_t    closeness;

        if (xSemaphoreTake(xStateMutex, portMAX_DELAY) == pdTRUE) {
            s         = gGame.state;
            closeness = gGame.closeness;
            xSemaphoreGive(xStateMutex);

            if ((int)s != lastState) {
                PRINTF("[tActuator stub] state -> %s (closeness %d)\r\n",
                       vault_state_name(s), (int)closeness);
                lastState = (int)s;
            }
            /* Person 2: here drive setLED / playTone / setRelay from s and closeness. */
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ------------------------------------------------------------------ */
/* tSelfTest  (TEMPORARY)  -- fake inputs so you can see the FSM work  */
/* ------------------------------------------------------------------ */
#if VAULT_SELF_TEST
static void post(EventType t, int32_t v, uint32_t tick)
{
    VaultEvent e = { t, v, tick };
    xQueueSend(xInputQueue, &e, portMAX_DELAY);
}

static void enterDigit(int steps)
{
    for (int i = 0; i < steps; i++) post(EV_DIAL, +1, 0);
    post(EV_PRESS, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void tSelfTest(void *p)
{
    vTaskDelay(pdMS_TO_TICKS(1000));

    PRINTF("\r\n=== SELF TEST round 1: arm 1-2-3, knock, enter 1-2-3 (should UNLOCK) ===\r\n");
    post(EV_ARM, 123, 0);
    vTaskDelay(pdMS_TO_TICKS(300));
    post(EV_KNOCK, 0, 1000);            /* synthetic timestamps: gaps 300 ms, 900 ms */
    post(EV_KNOCK, 0, 1300);
    post(EV_KNOCK, 0, 2200);
    vTaskDelay(pdMS_TO_TICKS(300));
    enterDigit(1);
    enterDigit(2);
    enterDigit(3);
    vTaskDelay(pdMS_TO_TICKS(1000));

    PRINTF("\r\n=== SELF TEST round 2: arm, knock, enter 1-2-2 (wrong), then beam broken ===\r\n");
    post(EV_ARM, 123, 0);
    vTaskDelay(pdMS_TO_TICKS(300));
    post(EV_KNOCK, 0, 10000);
    post(EV_KNOCK, 0, 10300);
    post(EV_KNOCK, 0, 11200);
    vTaskDelay(pdMS_TO_TICKS(300));
    enterDigit(1);
    enterDigit(2);
    enterDigit(2);
    vTaskDelay(pdMS_TO_TICKS(1000));
    post(EV_BEAM_BROKEN, 0, 0);
    PRINTF("(alarm lockout is %d s, wait for it to return to LOCKED)\r\n", LOCKOUT_MS / 1000);

    vTaskSuspend(NULL);                 /* done: park this task forever */
}
#endif

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    BOARD_InitDebugConsole();
#endif

    PRINTF("The Vault - core skeleton starting\r\n");

    /* Local hardware BSP: keep its existing public interfaces and pin map.
     * Inputs start with IRQ sources disabled; the hardware task/ISR adapters
     * enables them after the scheduler and queues are ready. An individual
     * device init failure is logged; the sensor services report availability.
     */
    BspStatus bspStatus = bsp_init(NULL);
    if (bspStatus != BSP_OK) {
        PRINTF("BSP initialization reported status %u; inspect sensor diagnostics.\r\n",
               (unsigned int)bspStatus);
    }

    vault_init(&gGame);

    xInputQueue  = xQueueCreate(INPUT_Q_LEN, sizeof(VaultEvent));
    xUartTxQueue = xQueueCreate(TX_Q_LEN,    sizeof(TxMsg));
    xUartRxQueue = xQueueCreate(RX_Q_LEN,    sizeof(uint8_t));
    xStateMutex  = xSemaphoreCreateMutex();

    if (!xInputQueue || !xUartTxQueue || !xUartRxQueue || !xStateMutex) {
        PRINTF("ERROR: could not create queues/mutex (check heap 4 is selected)\r\n");
        while (1) { }
    }

#if !VAULT_SELF_TEST
    if (!vault_sensors_init() ||
        xTaskCreate(vault_input_task, "inputs", configMINIMAL_STACK_SIZE + 180,
                    NULL, VAULT_INPUT_TASK_PRIORITY, NULL) != pdPASS) {
        PRINTF("ERROR: could not create sensor service (check heap).\r\n");
        while (1) { }
    }
#endif
    if (xTaskCreate(tTripwire, "tripwire", configMINIMAL_STACK_SIZE + 160, NULL, PRIO_TRIPWIRE, NULL) != pdPASS ||
        xTaskCreate(tGame, "game", configMINIMAL_STACK_SIZE + 150, NULL, PRIO_GAME, NULL) != pdPASS ||
        xTaskCreate(tComms, "comms", configMINIMAL_STACK_SIZE + 100, NULL, PRIO_COMMS, NULL) != pdPASS ||
        xTaskCreate(tActuator, "actuator", configMINIMAL_STACK_SIZE + 100, NULL, PRIO_ACTUATOR, NULL) != pdPASS) {
        PRINTF("ERROR: could not create game tasks (check heap).\r\n");
        while (1) { }
    }
#if VAULT_SELF_TEST
    if (xTaskCreate(tSelfTest, "selftest", configMINIMAL_STACK_SIZE + 100,
                    NULL, PRIO_ACTUATOR, NULL) != pdPASS) {
        PRINTF("ERROR: could not create self-test task.\r\n");
        while (1) { }
    }
#endif

    vTaskStartScheduler();

    /* Only reached if the scheduler failed to start (usually not enough heap). */
    volatile static int i = 0;
    while (1) {
        i++;
        __asm volatile ("nop");
    }
    return 0;
}
