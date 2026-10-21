#ifndef VAULT_BSP_H
#define VAULT_BSP_H

#include "bsp_inputs.h"
#include "bsp_light.h"
#include "bsp_outputs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Startup only, once after BOARD_InitBootPins/Clocks/Peripherals.
 * NULL output config selects defaults. Inputs stay interrupt-disabled.
 * Returns the first init error. Outputs/inputs remain usable if ADC init
 * fails; ADC calls return BSP_NOT_READY. No FreeRTOS resources are created.
 */
BspStatus bsp_init(const BspOutputConfig *output_config);

#ifdef __cplusplus
}
#endif
#endif
