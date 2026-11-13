#include "bsp_outputs.h"
/* Game-task integration capture; actuator tests supply failure-injection fakes. */
uint8_t test_rgb_red, test_rgb_green, test_rgb_blue;
uint32_t test_buzzer_hz;
void bsp_outputs_default_config(BspOutputConfig *config)
{ *config = (BspOutputConfig){true, false, false}; }
BspStatus bsp_laser_set(bool on)
{ (void)on; return BSP_OK; }
BspStatus bsp_rgb_set(uint8_t red, uint8_t green, uint8_t blue)
{ test_rgb_red = red; test_rgb_green = green; test_rgb_blue = blue; return BSP_OK; }
BspStatus bsp_buzzer_set_frequency(uint32_t hz)
{ test_buzzer_hz = hz; return BSP_OK; }
