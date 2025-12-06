/* Author: kvl@eti.uni-siegen.de
 * Created on July 27, 2025, 7:24 PM
 * Read out the capacitive touch sense pads for the PIC24F Starter Kit
 * CTMU (Charge Time Measurement Unit) uses capacitive touch switches (pads
 * on the PCB), to measure the relative capacitance of switches by AD converter
 */
#include "TouchSense.h"

// CTMU Constants
#define CTMU_OFF                        0x0000
#define CTMU_CONTINUE_IN_IDLE           0x0000
#define CTMU_EDGE_DELAY_DISABLED        0x0000
#define CTMU_EDGES_BLOCKED              0x0000
#define CTMU_NO_EDGE_SEQUENCE           0x0000
#define CTMU_CURRENT_NOT_GROUNDED       0x0000
#define CTMU_TRIGGER_OUT_DISABLED       0x0000
#define CTMU_EDGE2_NEGATIVE             0x0000
#define CTMU_EDGE2_CTED1                0x0060
#define CTMU_EDGE2_CTED2                0x0040
#define CTMU_EDGE1_POSITIVE             0x0010
#define CTMU_EDGE1_CTED1                0x000C
#define CTMU_EDGE1_CTED2                0x0008
#define CTMU_EDGE_MASK                  0x0003
#define CTMU_EDGE2                      0x0002
#define CTMU_EDGE1                      0x0001

#define AVG_DELAY                       64 //1 
#define CHARGE_TIME_COUNT               90 //34 // If optimized, change value

uint8_t buttons[NUM_TOUCHPADS];
uint16_t _potADC;

// global variables used in reading and averaging touch pad's values
uint16_t rawCTMU[NUM_TOUCHPADS];   // raw AD value
uint16_t average[NUM_TOUCHPADS];   // averaged AD value
uint16_t trip   [NUM_TOUCHPADS];   // trip point for touch pad
uint16_t hyst   [NUM_TOUCHPADS];   // hysteresis for touch pad
uint8_t first;          // first variable to 'discard' first N samples
uint8_t buttonInd;      // index of touch pad being checked
uint16_t value, bigVal, smallAvg;  // button's value, bigval, smallavg
uint16_t AvgIndex;

// read potentiometer and store value in global variable
void ReadPotentiometer() {
    AD1CON1 = 0x00E4;   // Off, Auto sample start, auto-convert
    AD1CON2 = 0;        // AVdd, AVss, int every conversion, MUXA only
    AD1CON3 = 0x1F05;   // 31 Tad auto-sample, Tad = 5*Tcy
    AD1CHS  = 0x0;      // MUXA uses AN0
    AD1CSSL = 0;        // No scanned inputs
    AD1CON1bits.ADON = 1;        // turn on ADC module
    while(!AD1CON1bits.DONE);    // wait for conversion to complete
    _potADC = ADC1BUF0;
    AD1CON1bits.ADON = 0;        // turn off ADC module
}

// routine to set up CTMU for capacitive touch sensing
void CTMUInit( void ) {
    TRISB    = 0x1F01;   //RB0, RB8, RB9, RB10, RB11, RB12 in tri-state
    AD1PCFGL &= ~0x1F01;
        CTMUCON = CTMU_OFF | CTMU_CONTINUE_IN_IDLE | CTMU_EDGE_DELAY_DISABLED |
              CTMU_EDGES_BLOCKED | CTMU_NO_EDGE_SEQUENCE |
              CTMU_CURRENT_NOT_GROUNDED | CTMU_TRIGGER_OUT_DISABLED |
              CTMU_EDGE2_NEGATIVE | CTMU_EDGE2_CTED1 | CTMU_EDGE1_POSITIVE |
              CTMU_EDGE1_CTED1;  // Set up the CTMU
    CTMUICONbits.IRNG = 2;   // 5.5uA
    CTMUICONbits.ITRIM = 0;  // 0%
    // Set up the ADC:
    AD1CON1            = 0x0000;
    AD1CHS             = STARTING_ADC_CHANNEL; // set starting analog channel
    AD1CSSL            = 0x0000;
    AD1CON1bits.FORM   = 0x0;                  // unsigned int format
    AD1CON3            = 0x0002;
    AD1CON2            = 0x0000;
    AD1CON1bits.ADON   = 1;           // ADC in continuous mode
    CTMUCONbits.CTMUEN = 1;           // enable CTMU
    for (uint8_t i = 0; i < NUM_TOUCHPADS; i++ ) {
        trip[i] = TRIP_VALUE; hyst[i] = HYSTERESIS_VALUE;
    }
    buttonInd = 0;
    first = 160;  // detection starts here after averaging over enough values
}

// Capacitive touch sensing service routine for CTMU:  Measure, determine if 
// button under test is pressed or not, set flag accordingly, then average.
// The Starter Kit's potentiometer is also read here.
void ReadCTMU() {
    uint16_t current_ipl;
    volatile unsigned int tempADch;
    tempADch            = AD1CHS;  // store the current A/D mux channel selected
    AD1CON1             = 0x0000;  // unsigned integer format
    AD1CSSL             = 0x0000;
    AD1CON3             = 0x0002;
    AD1CON2             = 0x0000;
    AD1CON1bits.ADON    = 1;            // Start A/D in continuous mode
    for(uint8_t i=0; i<NUM_TOUCHPADS; i++) {
        // Get the raw sensor reading:
        AD1CHS = STARTING_ADC_CHANNEL + buttonInd; //select A/D channel
        IFS0bits.AD1IF = 0;  // ensure touch circuit is discharged
        AD1CON1bits.DONE = 0;
        AD1CON1bits.SAMP = 1;        // manually sample
        // wait for ADC to begin sampling
        Nop(); Nop(); Nop(); Nop(); Nop(); Nop(); Nop(); Nop();
        CTMUCONbits.IDISSEN = 1;  // drain any charge on circuit
        Nop(); Nop(); Nop(); Nop(); Nop();
        CTMUCONbits.IDISSEN = 0;
        Nop(); Nop(); Nop(); Nop(); Nop();
        IFS0bits.AD1IF = 0;
        AD1CON1bits.SAMP = 0;  // manually start conversion
        while(!IFS0bits.AD1IF);  // ADC to drain CTMU charge

        SET_AND_SAVE_CPU_IPL( current_ipl, 7 );  // turn off interrupts
        IFS0bits.AD1IF = 0;
        AD1CON1bits.SAMP = 1;      // manually start sampling
        CTMUCONbits.EDG2STAT = 0;  // make sure edge2 is 0
        CTMUCONbits.EDG1STAT = 1;   // set edge1 - start charge
        for (uint8_t j=0; j<CHARGE_TIME_COUNT; j++); // CTMU charge time delay
        CTMUCONbits.EDG1STAT = 0;  // Clear edge1 - Stop Charge
        RESTORE_CPU_IPL( current_ipl );  // re-enable interrupts

        IFS0bits.AD1IF = 0;
        AD1CON1bits.SAMP = 0;
        while(!IFS0bits.AD1IF);  // wait for ADC
        value = ADC1BUF0;
        
        IFS0bits.AD1IF = 0;    // discharge touch circuit
        AD1CON1bits.SAMP = 1;  // manually start sampling

        // wait for A/D conversion to begin
        Nop(); Nop(); Nop(); Nop(); Nop(); Nop(); Nop(); Nop();
        CTMUCONbits.IDISSEN = 1;        // drain any charge on circuit
        Nop(); Nop(); Nop(); Nop(); Nop(); 
        CTMUCONbits.IDISSEN = 0;        // end charge drain
        Nop(); Nop(); Nop(); Nop();
        IFS0bits.AD1IF = 0;
        AD1CON1bits.SAMP = 0;    // perform conversion
        while(!IFS0bits.AD1IF);  // wait for ADC
        IFS0bits.AD1IF = 0;
        AD1CON1bits.DONE = 0;  // ADC to drain CTMU charge
        
        bigVal = value  * 16; // *16 for greater sensitivity
        
        smallAvg = average[buttonInd]/16;  // smallAvg = average >> 4 bits
        rawCTMU[buttonInd] = bigVal;       // raw array = most recent bigVal
        if (first > 0) {  // on power-up, reach steady-state readings first
            first--;
            average[buttonInd] = bigVal;
            if (++buttonInd == NUM_TOUCHPADS) buttonInd = 0;
            break;
        }
        // is keypad pressed or released?
        if (bigVal > (average[buttonInd]-trip[buttonInd]+hyst[buttonInd])) {
            buttons[buttonInd] = 0;
        } else if (bigVal < (average[buttonInd] - trip[buttonInd])) {
            buttons[buttonInd] = 1;
        }
        // implement quick-release for released button
        if (bigVal > average[buttonInd]) {  // if raw above average,
            average[buttonInd] = bigVal;    // then reset to high average
        }
        // average in the new value:
        if(buttonInd == 0) {
            if (AvgIndex < AVG_DELAY) AvgIndex++; else AvgIndex = 0;
        }
        if (AvgIndex == AVG_DELAY) {  // average raw value
            average[buttonInd] = average[buttonInd] + (value - smallAvg);
        }
        if (++buttonInd == NUM_TOUCHPADS) buttonInd = 0;  // move to next pad
    }
    ReadPotentiometer();  // read potentiometer in _potADC
    AD1CHS = tempADch;    // restore A/D channel select
}