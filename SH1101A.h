/* Author: kvl@eti.uni-siegen.de
 * Created on July 26, 2025, 10:17 AM
 * The SH1101A OLED Display is connected to the PIC24F through:
 *  - PMD7:PMD0    -> PMP Data port     - PMRD (D5)    -> PMP Read
 *  - PMWR (D4)    -> PMP Write         - PMCS1 (D11)  -> Chip Select
 *  - PMA0 (B15)   -> A0                - RESET (D2)   -> reset              */
#ifndef SH1101A__H
#define	SH1101A__H

#include <xc.h>

#define CLOCK_FREQ 12000000

#define DISP_HOR_RESOLUTION 128
#define DISP_VER_RESOLUTION 64
#define DISP_ORIENTATION    0
         
// minimum pulse width requirement of CS controlled RD/WR access in SH1101A 
// is 100 ns,  + 1 cycle in setup and 1 cycle hold (minimum):
#define PMP_DATA_WAIT_TIME  102 // 
#define PMP_DATA_HOLD_TIME  15  // based on SH1101A data hold requirement  
// IOS FOR THE DISPLAY CONTROLLER
#define DisplayResetConfig()    TRISDbits.TRISD2 = 0   // reset pin
#define DisplayResetEnable()    LATDbits.LATD2 = 0
#define DisplayResetDisable()   LATDbits.LATD2 = 1
#define DisplayCmdDataConfig()	TRISBbits.TRISB15 = 0  // RS pin
#define DisplaySetCommand()     LATBbits.LATB15 = 0
#define DisplaySetData()        LATBbits.LATB15 = 1
#define DisplayConfig()         TRISDbits.TRISD11 = 0  // CS pin         
#define DisplayEnable()         LATDbits.LATD11 = 0
#define DisplayDisable()        LATDbits.LATD11 = 1
#define OFFSET  2  // display offset in x direction

#define BLACK (uint16_t)0b00000000
#define WHITE (uint16_t)0b11111111

extern uint8_t _color;
#define SetColor(color) _color = (color)

void Delay10us( uint32_t tenMicroSecondCounter );
void DelayMs( uint16_t ms );

void ResetDevice(void);
void ClearDevice(void);
void PutPixel(int16_t x, int16_t y);
uint8_t GetPixel(int16_t x, int16_t y);

#endif	/* SH1101A__H */
