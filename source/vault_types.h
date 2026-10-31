/*
 * vault_types.h  --  Person 1 (MCU core + game logic)
 *
 * Shared types for the Vault. This file is PURE C: no FreeRTOS, no hardware.
 * Everyone includes it, so agree on any change with the whole team.
 */
#ifndef VAULT_TYPES_H
#define VAULT_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Game constants (tune these)                                         */
/* ------------------------------------------------------------------ */
#define CODE_LEN             3      /* digits in the combination (0-9 each)   */
#define KNOCK_COUNT          3      /* knocks in the secret rhythm            */
#define KNOCK_GAPS           (KNOCK_COUNT - 1)
#define KNOCK_TOLERANCE_PCT  30     /* each gap may be +/-30% off             */
#define KNOCK_TIMEOUT_MS     2000   /* gap longer than this resets the knocks */
#define LOCKOUT_MS           30000  /* alarm lockout time                     */

/* ------------------------------------------------------------------ */
/* FSM states                                                          */
/* ------------------------------------------------------------------ */
typedef enum {
    ST_IDLE = 0,    /* not armed                                   */
    ST_LOCKED,      /* armed, waiting for the secret knock         */
    ST_DIALING,     /* knock accepted, player is entering digits   */
    ST_UNLOCKED,    /* code correct                                */
    ST_ALARM        /* beam broken / tamper, locked out for 30 s   */
} VaultState;

/* ------------------------------------------------------------------ */
/* Events (what other people's code sends to tGame)                    */
/* ------------------------------------------------------------------ */
typedef enum {
    EV_NONE = 0,
    EV_ARM,          /* value = code packed as d0*100 + d1*10 + d2   (tComms)  */
    EV_DISARM,       /*                                              (tComms)  */
    EV_KNOCK,        /* tick_ms = captured tap time                   (tInput) */
    EV_DIAL,         /* value = +1/-1 calibrated round-index change   (tInput) */
    EV_PRESS,        /* debounced encoder button: confirm digit       (tInput) */
    EV_BEAM_BROKEN,  /*                                              (tTripwire)*/
    EV_TAMPER,       /*                                              (tComms)  */
    EV_LOCKOUT_DONE  /* generated INSIDE tGame, nobody else sends it           */
} EventType;

typedef struct {
    EventType type;
    int32_t   value;
    uint32_t  tick_ms;   /* time of the event in ms. 0 = "tGame fills it in" */
} VaultEvent;

/* ------------------------------------------------------------------ */
/* Alarm causes                                                        */
/* ------------------------------------------------------------------ */
typedef enum {
    ALARM_NONE   = 0,
    ALARM_BEAM   = 1,
    ALARM_TAMPER = 2
} AlarmCause;

/* ------------------------------------------------------------------ */
/* Shared game state. ONLY tGame writes it. Others read it while       */
/* holding xStateMutex.                                                */
/* ------------------------------------------------------------------ */
typedef struct {
    VaultState state;

    uint8_t  code[CODE_LEN];        /* the real combination               */
    uint8_t  guess[CODE_LEN];       /* digits confirmed so far            */
    uint8_t  digit_index;           /* which digit is being entered (0..) */
    uint8_t  current_digit;         /* digit currently shown on the dial  */

    uint8_t  attempts;              /* wrong + right guesses since arming */
    uint8_t  closeness;             /* 0 = cold ... 100 = exact (hint LED)*/

    uint32_t knock_times[KNOCK_COUNT];
    uint8_t  knock_count;

    uint32_t start_ms;              /* when armed (for "time taken")      */
    uint16_t solve_time_s;          /* seconds from arm to solve          */
    AlarmCause alarm_cause;
} GameState;

/* Per-digit submitted-guess feedback. Exact matches consume occurrences first. */
typedef enum {
    DIGIT_ABSENT = 0,
    DIGIT_PRESENT = 1,
    DIGIT_CORRECT = 2
} DigitFeedback;

/* ------------------------------------------------------------------ */
/* What the FSM wants the caller (tGame) to do after an event.         */
/* The FSM itself never touches queues or hardware.                    */
/* ------------------------------------------------------------------ */
typedef struct {
    bool send_attempt;   /* post an ATTEMPT packet  */
    bool send_solved;    /* post a SOLVED packet    */
    bool send_alarm;     /* post an ALARM packet    */
    bool start_lockout;  /* start the 30 s timer    */
} VaultActions;

/* ------------------------------------------------------------------ */
/* Outgoing packet request (tGame -> xUartTxQueue -> tComms)           */
/* PLACEHOLDER type numbers: agree the real ones with Person 3.        */
/* ------------------------------------------------------------------ */
#define PKT_ATTEMPT   0x10  /* payload: g0 g1 g2 attempts closeness feedback[3] */
#define PKT_SOLVED    0x11  /* payload: attempts time_hi time_lo        */
#define PKT_ALARM     0x12  /* payload: cause                           */

#define TX_MAX_PAYLOAD 8

typedef struct {
    uint8_t type;
    uint8_t len;
    uint8_t payload[TX_MAX_PAYLOAD];
} TxMsg;

#endif /* VAULT_TYPES_H */
