/* Author: kvl@eti.uni-siegen.de
 * Created on July 27, 2025, 7:24 PM
 * 
 */
#include "RGBLeds.h"

// set new PWM output 
void SetRGBs( uint8_t satR, uint8_t satG, uint8_t satB ) {
    OC1RS = (satR==0)? 0x100: CONVERT_TO_COLOR( satR );
    OC2RS = (satG==0)? 0x100: CONVERT_TO_COLOR( satG );
    OC3RS = (satB==0)? 0x100: CONVERT_TO_COLOR( satB );
}

void RGBMapColorPins() {
    // Configure red, pins 31 (RP10), 32 (RP17) for OutputCompare1 (function 18)
    RPOR5bits.RP10R = RPOR8bits.RP17R  = 18;
    // Configure green, pins 6 (RP19), 8 (RP27) for OutputCompare2 (function 19)
    RPOR9bits.RP19R = RPOR13bits.RP27R = 19;
    // Configure blue, pins 4 (RP21), 5 (RP26) for OutputCompare3 (function 20)
    RPOR10bits.RP21R = RPOR13bits.RP26R = 20;
    RPOR4 = 0;   // AN8 and AN9 PPS, leave analog
}

// turns off the LED by turning off the timers, PWMs, and setting pins to inputs
void RGBTurnOffLED() {
    T2CON   = 0x0000;
    OC1CON1 = OC2CON1 = OC3CON1 = PWM_OFF;
    TRISFbits.TRISF4 = 1; TRISFbits.TRISF5 = 1;  // TRIS_INPUT (1))
    TRISGbits.TRISG8 = 1; TRISGbits.TRISG9 = 1;
    TRISGbits.TRISG6 = 1; TRISGbits.TRISG7 = 1;
}

// turns on the LEDs by turning on timers, PWMs, and setting pins to outputs
void RGBTurnOnLED() {
    T2CON = 0x0030;  // Initialize the timer for the PWMs
    PR2   = 0x00FF;
    // Initialize the PWMs
    OC1RS = 0x100; 
    OC1R  = 0; OC1CON2 = PWM_CONFIGURATION_2; OC1CON1 = PWM_CONFIGURATION_1;
    OC2RS = 0x100; 
    OC2R  = 0; OC2CON2 = PWM_CONFIGURATION_2; OC2CON1 = PWM_CONFIGURATION_1;
    OC3RS = 0x100; 
    OC3R  = 0; OC3CON2 = PWM_CONFIGURATION_2; OC3CON1 = PWM_CONFIGURATION_1;
    // Configure the PWM pins for output
    TRISFbits.TRISF4 = 0; TRISFbits.TRISF5 = 0;   // Red, TRIS_OUTPUT (0)
    TRISGbits.TRISG8 = 0; TRISGbits.TRISG9 = 0;   // Green, TRIS_OUTPUT
    TRISGbits.TRISG6 = 0; TRISGbits.TRISG7 = 0;   // Blue, TRIS_OUTPUT
    ODCF = 0x0030;       // Enable open drain, red
    ODCG = 0x03C0;       // green and blue    
    T2CON = 0x8000;  // turn on timer
}
