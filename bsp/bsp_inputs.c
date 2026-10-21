#include "bsp_inputs.h"
#include "bsp_board.h"
#include <stddef.h>

static const uint8_t input_pins[BSP_INPUT_COUNT] = {
    BSP_TAP_PIN, BSP_ENCODER_CLK_PIN, BSP_ENCODER_DT_PIN, BSP_ENCODER_SW_PIN
};
static bool initialized;
static BspInputCallback input_callback;
static void *callback_context;

static BspInputLevels decode_levels(uint32_t pins)
{
    BspInputLevels result = {
        (pins & (1UL << BSP_TAP_PIN)) != 0U,
        (pins & (1UL << BSP_ENCODER_CLK_PIN)) != 0U,
        (pins & (1UL << BSP_ENCODER_DT_PIN)) != 0U,
        (pins & (1UL << BSP_ENCODER_SW_PIN)) != 0U
    };
    return result;
}

void bsp_inputs_init(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTC_MASK;
    input_callback = NULL;
    callback_context = NULL;
    for (uint32_t i = 0U; i < BSP_INPUT_COUNT; ++i) {
        /* ISF is W1C: explicitly clear it; do not RMW stale ISF. */
        BSP_INPUT_PORT->PCR[input_pins[i]] = PORT_PCR_MUX(1) | PORT_PCR_ISF_MASK;
    }
    BSP_INPUT_GPIO->PDDR &= ~BSP_INPUT_MASK;
    BSP_INPUT_PORT->ISFR = BSP_INPUT_MASK;
    NVIC_SetPriority(PORTC_PORTD_IRQn, BSP_INPUT_IRQ_PRIORITY);
    /* The shared NVIC vector is enabled only on the first edge request. */
    initialized = true;
}

BspStatus bsp_inputs_read(BspInput input, bool *high)
{
    if ((uint32_t)input >= BSP_INPUT_COUNT || high == NULL) {
        return BSP_INVALID_ARGUMENT;
    }
    if (!initialized) return BSP_NOT_READY;
    *high = (BSP_INPUT_GPIO->PDIR & (1UL << input_pins[input])) != 0U;
    return BSP_OK;
}

BspStatus bsp_inputs_read_all(BspInputLevels *levels)
{
    if (levels == NULL) return BSP_INVALID_ARGUMENT;
    if (!initialized) return BSP_NOT_READY;
    *levels = decode_levels(BSP_INPUT_GPIO->PDIR);
    return BSP_OK;
}

void bsp_inputs_set_callback(BspInputCallback callback, void *context)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (callback == NULL && initialized) {
        for (uint32_t i = 0U; i < BSP_INPUT_COUNT; ++i) {
            uint32_t pcr = BSP_INPUT_PORT->PCR[input_pins[i]];
            BSP_INPUT_PORT->PCR[input_pins[i]] =
                (pcr & ~(PORT_PCR_IRQC_MASK | PORT_PCR_ISF_MASK)) | PORT_PCR_ISF_MASK;
        }
    }
    callback_context = context;
    input_callback = callback;
    __DMB();
    __set_PRIMASK(mask);
}

BspStatus bsp_inputs_configure(BspInput input, BspPull pull, BspEdge edge)
{
    static const uint8_t irq_codes[] = {0U, 9U, 10U, 11U};
    if ((uint32_t)input >= BSP_INPUT_COUNT || (uint32_t)pull > BSP_PULL_DOWN ||
        (uint32_t)edge > BSP_EDGE_BOTH) return BSP_INVALID_ARGUMENT;
    if (!initialized) return BSP_NOT_READY;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (edge != BSP_EDGE_DISABLED && input_callback == NULL) {
        __set_PRIMASK(mask);
        return BSP_NOT_READY;
    }
    uint32_t pcr = PORT_PCR_MUX(1) | PORT_PCR_IRQC(irq_codes[edge]) | PORT_PCR_ISF_MASK;
    if (pull != BSP_PULL_NONE) pcr |= PORT_PCR_PE_MASK;
    if (pull == BSP_PULL_UP) pcr |= PORT_PCR_PS_MASK;
    BSP_INPUT_PORT->PCR[input_pins[input]] = pcr;
    if (edge != BSP_EDGE_DISABLED) NVIC_EnableIRQ(PORTC_PORTD_IRQn);
    __DSB();
    __set_PRIMASK(mask);
    return BSP_OK;
}

void bsp_inputs_irq_dispatch(void)
{
    if (!initialized) return;
    uint32_t pending = BSP_INPUT_PORT->ISFR & BSP_INPUT_MASK;
    BspInputLevels levels = decode_levels(BSP_INPUT_GPIO->PDIR);
    BSP_INPUT_PORT->ISFR = pending; /* Clear before callback, preserving new edges. */
    BspInputCallback callback = input_callback;
    if (callback != NULL) {
        for (uint32_t i = 0U; i < BSP_INPUT_COUNT; ++i) {
            if ((pending & (1UL << input_pins[i])) != 0U) {
                callback((BspInput)i, &levels, callback_context);
            }
        }
    }
    __DSB();
}

#if BSP_DEFINE_PORT_IRQ_HANDLER
void PORTC_PORTD_IRQHandler(void)
{
    bsp_inputs_irq_dispatch();
}
#endif
