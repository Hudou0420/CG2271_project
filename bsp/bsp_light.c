#include "bsp_light.h"
#include "bsp_board.h"
#include "fsl_clock.h"
#include <stddef.h>

static bool initialized;
static bool sample_pending;

BspStatus bsp_light_init(void)
{
    initialized = false;
    sample_pending = false;
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK;
    SIM->SCGC6 |= SIM_SCGC6_ADC0_MASK;
    BSP_LIGHT_PORT->PCR[BSP_LIGHT_PIN] = PORT_PCR_ISF_MASK; /* ALT0 analog */
    BSP_LIGHT_GPIO->PDDR &= ~(1UL << BSP_LIGHT_PIN);
    ADC0->SC1[0] = ADC_SC1_ADCH(31U); /* Disable conversions and ADC IRQ. */
    ADC0->SC1[1] = ADC_SC1_ADCH(31U);

    /* bus/8 = 3 MHz with BOARD_BootClockRUN (bus 24 MHz).
     * Calibration uses ADCK <= 4 MHz; do not silently use a new clock tree.
     */
    uint32_t bus_hz = CLOCK_GetBusClkFreq();
    if (bus_hz < 8000000U || bus_hz > 32000000U) return BSP_INVALID_ARGUMENT;
    ADC0->CFG1 = ADC_CFG1_ADICLK(0U) | ADC_CFG1_ADIV(3U) |
                 ADC_CFG1_MODE(1U) | ADC_CFG1_ADLSMP_MASK;
    ADC0->CFG2 = 0U; /* Mux A, longest sample, normal conversion speed. */
    ADC0->SC2 = 0U;  /* Software trigger, VREFH/VREFL, no compare or DMA. */
    ADC0->SC3 = ADC_SC3_CALF_MASK; /* W1C previous calibration failure. */
    ADC0->SC3 = ADC_SC3_CAL_MASK | ADC_SC3_AVGE_MASK | ADC_SC3_AVGS(3U);
    uint32_t remaining = BSP_ADC_CALIBRATION_POLL_LIMIT;
    while ((ADC0->SC3 & ADC_SC3_CAL_MASK) != 0U && remaining != 0U) --remaining;
    if ((ADC0->SC3 & ADC_SC3_CAL_MASK) != 0U) {
        ADC0->SC3 = 0U;
        ADC0->SC1[0] = ADC_SC1_ADCH(31U);
        return BSP_TIMEOUT;
    }
    if ((ADC0->SC3 & ADC_SC3_CALF_MASK) != 0U) return BSP_CALIBRATION_FAILED;

    uint32_t gain = ADC0->CLP0 + ADC0->CLP1 + ADC0->CLP2 + ADC0->CLP3 +
                    ADC0->CLP4 + ADC0->CLPS;
    ADC0->PG = (gain / 2U) | 0x8000U;
    gain = ADC0->CLM0 + ADC0->CLM1 + ADC0->CLM2 + ADC0->CLM3 +
           ADC0->CLM4 + ADC0->CLMS;
    ADC0->MG = (gain / 2U) | 0x8000U;
    ADC0->SC3 = 0U; /* Raw single conversion: no hardware average. */
    ADC0->SC1[0] = ADC_SC1_ADCH(31U);
    (void)ADC0->R[0];
    initialized = true;
    return BSP_OK;
}

BspStatus bsp_light_start(void)
{
    if (!initialized) return BSP_NOT_READY;
    if (sample_pending) return BSP_BUSY;
    sample_pending = true;
    ADC0->SC1[0] = ADC_SC1_ADCH(BSP_LIGHT_ADC_CHANNEL);
    return BSP_OK;
}

BspStatus bsp_light_try_read(uint16_t *raw)
{
    if (raw == NULL) return BSP_INVALID_ARGUMENT;
    if (!initialized || !sample_pending) return BSP_NOT_READY;
    if ((ADC0->SC1[0] & ADC_SC1_COCO_MASK) == 0U) return BSP_BUSY;
    *raw = (uint16_t)ADC0->R[0];
    sample_pending = false;
    return BSP_OK;
}

void bsp_light_cancel(void)
{
    if (!initialized) return;
    ADC0->SC1[0] = ADC_SC1_ADCH(31U);
    (void)ADC0->R[0];
    sample_pending = false;
}

BspStatus bsp_light_read_raw(uint16_t *raw)
{
    if (raw == NULL) return BSP_INVALID_ARGUMENT;
    BspStatus status = bsp_light_start();
    if (status != BSP_OK) return status;
    uint32_t remaining = BSP_ADC_CONVERSION_POLL_LIMIT;
    while (remaining-- != 0U) {
        status = bsp_light_try_read(raw);
        if (status != BSP_BUSY) return status;
    }
    bsp_light_cancel();
    return BSP_TIMEOUT;
}
