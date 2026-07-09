#include "ch32fun.h"
#include "i2c_slave.h"

#include <stdint.h>

/*
 * Final wiring:
 *
 * CH32 PA2  -> TPIC6B595 RCK / RCLK / latch
 * CH32 PC4  -> TPIC6B595 SRCK / SCLK / shift clock
 * CH32 PD6  -> TPIC6B595 SER / serial data
 *
 * CH32 PC1  -> I2C SDA
 * CH32 PC2  -> I2C SCL
 */

#define HC595_LATCH_PIN  PC4
#define HC595_CLOCK_PIN  PA2
#define HC595_DATA_PIN   PD6

#define I2C_ADDRESS       0x09

/*
 * I2C register layout:
 *
 * register 0: ASCII display character
 * register 1: decimal point flag; non-zero means decimal point on
 */
volatile uint8_t i2c_registers[2] =
{
    ' ',
    0
};

/*
 * Verified physical segment assignment.
 *
 * TPIC output bit:
 *   bit 7 -> A
 *   bit 6 -> B
 *   bit 5 -> C
 *   bit 4 -> D
 *   bit 3 -> E
 *   bit 2 -> F
 *   bit 1 -> G
 *   bit 0 -> DP
 */
#define SEG_DP  0x01
#define SEG_G   0x02
#define SEG_F   0x04
#define SEG_E   0x08
#define SEG_D   0x10
#define SEG_C   0x20
#define SEG_B   0x40
#define SEG_A   0x80

/*
 * Shift one byte LSB first into the TPIC6B595.
 *
 * RCLK remains low while shifting.
 * One rising edge on RCLK copies the completed byte to the outputs.
 */
static void hc595_shiftOut8(uint8_t value)
{
    funDigitalWrite(HC595_LATCH_PIN, 0);

    for (uint8_t i = 0; i < 8; i++)
    {
        funDigitalWrite(HC595_DATA_PIN, value & 0x01);
        value >>= 1;

        funDigitalWrite(HC595_CLOCK_PIN, 1);
        funDigitalWrite(HC595_CLOCK_PIN, 0);
    }

    funDigitalWrite(HC595_LATCH_PIN, 1);
}

/*
 * Convert the incoming ASCII character to a seven-segment mask.
 */
static uint8_t segments_for_character(uint8_t character)
{
    switch (character)
    {
        case '0':
            return SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;

        case '1':
            return SEG_B | SEG_C;

        case '2':
            return SEG_A | SEG_B | SEG_D | SEG_E | SEG_G;

        case '3':
            return SEG_A | SEG_B | SEG_C | SEG_D | SEG_G;

        case '4':
            return SEG_B | SEG_C | SEG_F | SEG_G;

        case '5':
            return SEG_A | SEG_C | SEG_D | SEG_F | SEG_G;

        case '6':
            return SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;

        case '7':
            return SEG_A | SEG_B | SEG_C;

        case '8':
            return SEG_A | SEG_B | SEG_C | SEG_D |
                   SEG_E | SEG_F | SEG_G;

        case '9':
            return SEG_A | SEG_B | SEG_C | SEG_D |
                   SEG_F | SEG_G;

        case '-':
            return SEG_G;

        case ' ':
        default:
            return 0;
    }
}

int main(void)
{
    SystemInit();
    Delay_Ms(100);

    funGpioInitAll();

    /* TPIC6B595 output-control pins */
    funPinMode(HC595_LATCH_PIN, GPIO_CFGLR_OUT_10Mhz_PP);
    funPinMode(HC595_CLOCK_PIN, GPIO_CFGLR_OUT_10Mhz_PP);
    funPinMode(HC595_DATA_PIN,  GPIO_CFGLR_OUT_10Mhz_PP);

    funDigitalWrite(HC595_LATCH_PIN, 0);
    funDigitalWrite(HC595_CLOCK_PIN, 0);
    funDigitalWrite(HC595_DATA_PIN,  0);

    /* I2C1: PC1 = SDA, PC2 = SCL */
    funPinMode(PC1, GPIO_CFGLR_OUT_10Mhz_AF_OD);
    funPinMode(PC2, GPIO_CFGLR_OUT_10Mhz_AF_OD);

    SetupI2CSlave(
        I2C_ADDRESS,
        i2c_registers,
        sizeof(i2c_registers),
        NULL,
        NULL,
        false
    );

    uint8_t last_character = 0xFF;
    uint8_t last_decimal_point = 0xFF;

    /* Start with the display blank. */
    hc595_shiftOut8(0x00);

    while (1)
    {
        uint8_t character = i2c_registers[0];
        uint8_t decimal_point = i2c_registers[1];

        /*
         * Update only after a new character or DP flag arrives.
         * This avoids needlessly clocking the TPIC continuously.
         */
        if ((character != last_character) ||
            (decimal_point != last_decimal_point))
        {
            uint8_t segments = segments_for_character(character);

            if (decimal_point)
            {
                segments |= SEG_DP;
            }

            hc595_shiftOut8(segments);

            last_character = character;
            last_decimal_point = decimal_point;
        }

        Delay_Ms(10);
    }
}