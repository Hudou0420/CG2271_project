/* Golden wire bytes shared by both production transport test harnesses. */
#ifndef TEST_WIRE_VECTORS_H
#define TEST_WIRE_VECTORS_H
#include <stdint.h>
static const uint8_t golden_solved[] = {0xaa,0x11,0x03,0x04,0x34,0x12,0x30};
static const uint8_t golden_tamper_alarm[] = {0xaa,0x12,0x01,0x01,0x12};
#endif
