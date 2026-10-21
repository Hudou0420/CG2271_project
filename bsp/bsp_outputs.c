#include "bsp_outputs.h"
#include "bsp_board.h"
#include "fsl_clock.h"
#include <stddef.h>

static bool initialized;
static BspOutputConfig output_config;
static uint32_t tpm_clock_hz;
static uint32_t rgb_period;
static uint32_t buzzer_frequency;
static uint16_t buzzer_duty;

/* MOD and CnV are 16-bit. Keep period <=65535 so full-on CnV=period
 * is representable (CnV > MOD). No divide by zero or 16-bit overflow.
 */
static bool select_period(uint32_t frequency, uint32_t *period, uint32_t *prescale)
{
    for (uint32_t ps = 0U; ps < 8U; ++ps) {
        uint32_t divider = (1UL << ps) * frequency;
        uint32_t counts = (tpm_clock_hz + divider / 2U) / divider;
        if (counts >= 2U && counts <= 65535U) {
            *period = counts;
            *prescale = ps;
            return true;
        }
    }
    return false;
}

static void gpio_write(GPIO_Type *gpio, uint32_t pin, bool high)
{
    if (high) gpio->PSOR = 1UL << pin;
    else gpio->PCOR = 1UL << pin;
}

static void gpio_output(PORT_Type *port, GPIO_Type *gpio, uint32_t pin, bool high)
{
    /* Establish latch and direction before exposing GPIO on the pad. */
    gpio_write(gpio, pin, high);
    gpio->PDDR |= 1UL << pin;
    port->PCR[pin] = PORT_PCR_MUX(1) | PORT_PCR_ISF_MASK;
}

static uint32_t pwm_mode(bool active_low)
{
    return TPM_CnSC_MSB_MASK |
           (active_low ? TPM_CnSC_ELSA_MASK : TPM_CnSC_ELSB_MASK);
}

void bsp_outputs_default_config(BspOutputConfig *config)
{
    if (config != NULL) {
        config->rgb_active_low = true; /* Observed common-positive LED. */
        config->relay_active_low = false;
        config->laser_active_low = false;
        config->enable_laser_signal = false;
    }
}

BspStatus bsp_outputs_init(const BspOutputConfig *config)
{
    BspOutputConfig requested;
    bsp_outputs_default_config(&requested);
    if (config != NULL) requested = *config;
    tpm_clock_hz = CLOCK_GetPeriphClkFreq();
    if (tpm_clock_hz == 0U) return BSP_NOT_READY;
    uint32_t prescale;
    if (!select_period(BSP_RGB_FREQUENCY_HZ, &rgb_period, &prescale)) {
        return BSP_INVALID_ARGUMENT;
    }
    output_config = requested;
    initialized = false;
    SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK | SIM_SCGC5_PORTC_MASK | SIM_SCGC5_PORTD_MASK;
    SIM->SCGC6 |= SIM_SCGC6_TPM0_MASK | SIM_SCGC6_TPM1_MASK;

    gpio_output(BSP_RELAY_PORT, BSP_RELAY_GPIO, BSP_RELAY_PIN, output_config.relay_active_low);
    if (output_config.enable_laser_signal) {
        gpio_output(BSP_LASER_PORT, BSP_LASER_GPIO, BSP_LASER_PIN, output_config.laser_active_low);
    } else {
        BSP_LASER_GPIO->PDDR &= ~(1UL << BSP_LASER_PIN);
        BSP_LASER_PORT->PCR[BSP_LASER_PIN] = PORT_PCR_MUX(1) | PORT_PCR_ISF_MASK;
    }
    gpio_output(BSP_RGB_RED_PORT, BSP_RGB_RED_GPIO, BSP_RGB_RED_PIN, output_config.rgb_active_low);
    gpio_output(BSP_RGB_GREEN_PORT, BSP_RGB_GREEN_GPIO, BSP_RGB_GREEN_PIN, output_config.rgb_active_low);
    gpio_output(BSP_RGB_BLUE_PORT, BSP_RGB_BLUE_GPIO, BSP_RGB_BLUE_PIN, output_config.rgb_active_low);
    gpio_output(BSP_BUZZER_PORT, BSP_BUZZER_GPIO, BSP_BUZZER_PIN, false);

    CLOCK_SetTpmClock(1U); /* Shared source: IRC48M, enabled by board RUN clock. */
    BSP_RGB_TPM->SC = 0U;
    BSP_RGB_TPM->CONF = 0U;
    BSP_RGB_TPM->POL = 0U;
    BSP_RGB_TPM->CNT = 0U;
    BSP_RGB_TPM->MOD = rgb_period - 1U;
    for (uint32_t ch = 0U; ch < 6U; ++ch) {
        BSP_RGB_TPM->CONTROLS[ch].CnSC = 0U;
        BSP_RGB_TPM->CONTROLS[ch].CnV = 0U;
    }
    const uint32_t channels[] = {BSP_RGB_RED_CHANNEL, BSP_RGB_GREEN_CHANNEL, BSP_RGB_BLUE_CHANNEL};
    for (uint32_t i = 0U; i < 3U; ++i) {
        BSP_RGB_TPM->CONTROLS[channels[i]].CnSC = pwm_mode(output_config.rgb_active_low);
    }
    BSP_RGB_TPM->SC = TPM_SC_CMOD(1U) | TPM_SC_PS(prescale);
    BSP_RGB_RED_PORT->PCR[BSP_RGB_RED_PIN] = PORT_PCR_MUX(BSP_RGB_MUX);
    BSP_RGB_GREEN_PORT->PCR[BSP_RGB_GREEN_PIN] = PORT_PCR_MUX(BSP_RGB_MUX);
    BSP_RGB_BLUE_PORT->PCR[BSP_RGB_BLUE_PIN] = PORT_PCR_MUX(BSP_RGB_MUX);

    BSP_BUZZER_TPM->SC = 0U;
    BSP_BUZZER_TPM->CONF = 0U;
    BSP_BUZZER_TPM->POL = 0U;
    BSP_BUZZER_TPM->CNT = 0U;
    BSP_BUZZER_TPM->MOD = 0U;
    for (uint32_t ch = 0U; ch < 2U; ++ch) {
        BSP_BUZZER_TPM->CONTROLS[ch].CnSC = 0U;
        BSP_BUZZER_TPM->CONTROLS[ch].CnV = 0U;
    }
    buzzer_frequency = 0U;
    buzzer_duty = 0U;
    initialized = true;
    return BSP_OK;
}

BspStatus bsp_rgb_set(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!initialized) return BSP_NOT_READY;
    BSP_RGB_TPM->CONTROLS[BSP_RGB_RED_CHANNEL].CnV = (rgb_period * red + 127U) / 255U;
    BSP_RGB_TPM->CONTROLS[BSP_RGB_GREEN_CHANNEL].CnV = (rgb_period * green + 127U) / 255U;
    BSP_RGB_TPM->CONTROLS[BSP_RGB_BLUE_CHANNEL].CnV = (rgb_period * blue + 127U) / 255U;
    return BSP_OK;
}

BspStatus bsp_buzzer_set_pwm(uint32_t frequency_hz, uint16_t duty_permille)
{
    if (!initialized) return BSP_NOT_READY;
    if (duty_permille > 1000U || (frequency_hz != 0U &&
        (frequency_hz < BSP_BUZZER_MIN_HZ || frequency_hz > BSP_BUZZER_MAX_HZ))) {
        return BSP_INVALID_ARGUMENT;
    }
    if (frequency_hz == 0U || duty_permille == 0U) {
        /* GPIO low is deterministic even if the timer is stopped high. */
        gpio_output(BSP_BUZZER_PORT, BSP_BUZZER_GPIO, BSP_BUZZER_PIN, false);
        BSP_BUZZER_TPM->SC = 0U;
        BSP_BUZZER_TPM->CONTROLS[BSP_BUZZER_CHANNEL].CnSC = 0U;
        BSP_BUZZER_TPM->CONTROLS[BSP_BUZZER_CHANNEL].CnV = 0U;
        buzzer_frequency = 0U;
        buzzer_duty = 0U;
        return BSP_OK;
    }
    if (buzzer_frequency == frequency_hz && buzzer_duty == duty_permille) return BSP_OK;
    uint32_t period, prescale;
    if (!select_period(frequency_hz, &period, &prescale)) return BSP_INVALID_ARGUMENT;
    gpio_output(BSP_BUZZER_PORT, BSP_BUZZER_GPIO, BSP_BUZZER_PIN, false);
    BSP_BUZZER_TPM->SC = 0U;
    BSP_BUZZER_TPM->CNT = 0U;
    BSP_BUZZER_TPM->MOD = period - 1U;
    BSP_BUZZER_TPM->CONTROLS[BSP_BUZZER_CHANNEL].CnSC = pwm_mode(false);
    BSP_BUZZER_TPM->CONTROLS[BSP_BUZZER_CHANNEL].CnV = (period * duty_permille + 500U) / 1000U;
    BSP_BUZZER_TPM->SC = TPM_SC_CMOD(1U) | TPM_SC_PS(prescale);
    BSP_BUZZER_PORT->PCR[BSP_BUZZER_PIN] = PORT_PCR_MUX(BSP_BUZZER_MUX);
    buzzer_frequency = frequency_hz;
    buzzer_duty = duty_permille;
    return BSP_OK;
}

BspStatus bsp_buzzer_set_frequency(uint32_t frequency_hz)
{
    return bsp_buzzer_set_pwm(frequency_hz, 500U);
}

BspStatus bsp_relay_write_level(bool high)
{
    if (!initialized) return BSP_NOT_READY;
    gpio_write(BSP_RELAY_GPIO, BSP_RELAY_PIN, high);
    return BSP_OK;
}

BspStatus bsp_relay_set(bool on)
{
    return bsp_relay_write_level(on != output_config.relay_active_low);
}

BspStatus bsp_laser_write_level(bool high)
{
    if (!initialized || !output_config.enable_laser_signal) return BSP_NOT_READY;
    gpio_write(BSP_LASER_GPIO, BSP_LASER_PIN, high);
    return BSP_OK;
}

BspStatus bsp_laser_set(bool on)
{
    return bsp_laser_write_level(on != output_config.laser_active_low);
}

void bsp_outputs_off(void)
{
    if (!initialized) return;
    (void)bsp_rgb_set(0U, 0U, 0U);
    (void)bsp_buzzer_set_frequency(0U);
    (void)bsp_relay_set(false);
    (void)bsp_laser_set(false);
}
