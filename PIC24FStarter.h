/* Author: kvl@eti.uni-siegen.de
 * Created on July 26, 2025, 10:17 AM */
#ifndef P24FS__H
#define	P24FS__H

#include <xc.h>
#include <p24Fxxxx.h>

// Configuration bits -- from PIC24F data sheet and Starter Kit docs
#pragma config WPFP = WPFP511    // Write Protection Flash Page 
#pragma config WPDIS = WPDIS     // Segment Write Protection disabled
#pragma config WPCFG = WPCFGDIS  // Configuration Word Code Page: not protected
#pragma config WPEND = WPENDMEM  // Write Protect WPFP - memory's last page
#pragma config POSCMOD = HS      // Primary Oscillator: HS oscillator mode
#pragma config DISUVREG = OFF    // Internal USB 3.3V Regulator disabled
#pragma config IOL1WAY = ON      // Write RP Registers Once
#pragma config OSCIOFNC = ON     // OSCO functions as port I/O (RC15)
#pragma config FCKSM = CSECMD    // Clock switching enabled, Monitor disabled
#pragma config FNOSC = PRIPLL    // Oscillator Select->PRIPLL /FRC
#pragma config PLL_96MHZ = ON    // 96MHz PLL Disable->Enabled
#pragma config PLLDIV = DIV3     // Oscillator input divided by 3 (12MHz input)
#pragma config IESO = OFF        // IESO mode (Two-speed start-up)disabled
#pragma config FWDTEN = OFF      // Watchdog Timer disabled
#pragma config ICS = PGx2        // Emulator functions shared with PGEC1/PGED1
#pragma config BKBUG = OFF       // Background Debug: resets in Operational mode
#pragma config GWRP = OFF        // Writes to program memory allowed
#pragma config GCP = OFF         // General Code Segment Code Protect disabled
#pragma config JTAGEN = OFF      // JTAG Port disabled

#include "SH1101A.h"
#include "TouchSense.h"
#include "RGBLeds.h"

#define INIT_CLOCK() OSCCON = 0x3302; CLKDIV = 0x0000;

#endif	/* P24FS__H */
