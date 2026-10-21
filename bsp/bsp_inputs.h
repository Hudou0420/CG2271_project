#ifndef VAULT_BSP_INPUTS_H
#define VAULT_BSP_INPUTS_H

#include "bsp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BSP_INPUT_TAP = 0,
    BSP_INPUT_ENCODER_CLK,
    BSP_INPUT_ENCODER_DT,
    BSP_INPUT_ENCODER_SW,
    BSP_INPUT_COUNT
} BspInput;

typedef enum { BSP_PULL_NONE = 0, BSP_PULL_UP, BSP_PULL_DOWN } BspPull;
typedef enum {
    BSP_EDGE_DISABLED = 0,
    BSP_EDGE_RISING,
    BSP_EDGE_FALLING,
    BSP_EDGE_BOTH
} BspEdge;

typedef struct {
    bool tap;
    bool clk;
    bool dt;
    bool sw;
} BspInputLevels;

/* Runs in ISR context. levels is a snapshot taken at dispatch, not a
 * historical record of the edge. Copy it before returning; never save
 * the pointer. Capture your timestamp here (e.g. xTaskGetTickCountFromISR).
 * Callback must be short/nonblocking and use only ISR-safe RTOS calls.
 * Raw edges can bounce and are not yet knocks/detents/press events.
 */
typedef void (*BspInputCallback)(BspInput input, const BspInputLevels *levels,
                                 void *context);

/* Startup only, before the scheduler. Default: inputs, no pulls, IRQs off. */
void bsp_inputs_init(void);
BspStatus bsp_inputs_read(BspInput input, bool *high);
BspStatus bsp_inputs_read_all(BspInputLevels *levels);

/* Task context only. Register before enabling any interrupt.
 * Passing NULL disables all four edge sources before unregistering.
 * context must remain valid until callback registration is changed.
 */
void bsp_inputs_set_callback(BspInputCallback callback, void *context);

/* Task context only. Can change pull/edge without recompiling.
 * Enabling an edge requires a callback. Reconfiguration clears that pin's
 * pending flag. No debounce or direction interpretation is performed.
 */
BspStatus bsp_inputs_configure(BspInput input, BspPull pull, BspEdge edge);

/* ISR context only; clears/distributes only this BSP's PORTC flags.
 * PORTD and other PORTC flags are left to their respective owners.
 */
void bsp_inputs_irq_dispatch(void);

#ifdef __cplusplus
}
#endif
#endif
