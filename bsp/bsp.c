#include "bsp.h"

BspStatus bsp_init(const BspOutputConfig *output_config)
{
    BspStatus status = bsp_outputs_init(output_config);
    if (status != BSP_OK) return status;
    bsp_inputs_init();
    return bsp_light_init();
}
