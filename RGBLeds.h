/* Author: kvl@eti.uni-siegen.de
 * Created on July 26, 2025, 10:17 AM
 * Functions for the RGB LEDs, used pins are: RG6/RG7, RG8/RG9, RF4/RF5
 * To control the color, write the inverse of the saturation value to the PWM */
#ifndef RGBLEDS__H
#define	RGBLEDS__H

#include <xc.h>

#define CONVERT_TO_COLOR(x)         (~x & 0xFF)
#define PWM_CONFIGURATION_1         0x0007
#define PWM_CONFIGURATION_2         0x000C
#define PWM_OFF                     0x0000

// set new PWM output
void SetRGBs( uint8_t satR, uint8_t satG, uint8_t satB );

void RGBMapColorPins();

// turns off the LED by turning off the timers, PWMs, and setting pins to inputs
void RGBTurnOffLED();

// turns on the LEDs by turning on timers, PWMs, and setting pins to outputs
void RGBTurnOnLED();

#endif	/* RGBLEDS__H */
