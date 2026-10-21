#ifndef VAULT_BSP_TYPES_H
#define VAULT_BSP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_OK = 0,
    BSP_INVALID_ARGUMENT,
    BSP_NOT_READY,
    BSP_BUSY,
    BSP_TIMEOUT,
    BSP_CALIBRATION_FAILED
} BspStatus;

#endif
