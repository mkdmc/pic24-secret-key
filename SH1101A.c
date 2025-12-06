/* Author: kvl@eti.uni-siegen.de
 * Created on July 26, 2025, 10:17 AM
 */
#include "SH1101A.h"

uint8_t _color;

// sets page + lower and higher address pointer of display buffer
#define SetAddress(page, lowerAddr, higherAddr) \
	DisplaySetCommand(); DeviceWrite(page); DeviceWrite(lowerAddr); \
    DeviceWrite(higherAddr); DisplaySetData();

#define AssignPageAddress(y) \
    if (y < 8) page = 0xB0; else if (y < 16) page = 0xB1; \
    else if (y < 24) page = 0xB2; else if (y < 32) page = 0xB3; \
    else if (y < 40) page = 0xB4; else if (y < 48) page = 0xB5; \
    else if (y < 56) page = 0xB6; else page = 0xB7

#define PMPWaitBusy()   while(PMMODEbits.BUSY)  // wait for PMP cycle end

// a software delay in intervals of 10 microseconds.
void Delay10us( uint32_t tenMicroSecondCounter ) {
    volatile int32_t cyclesRequiredForDelay;  //7 cycles burned to this point 
    cyclesRequiredForDelay = (int32_t)(CLOCK_FREQ/100000)*tenMicroSecondCounter;
    // subtract all  cycles used up til while loop below, each loop cycle count
    // is subtracted (subtract the 5 cycle function return)
    cyclesRequiredForDelay -= 44; //(29 + 5) + 10 cycles padding
    if(cyclesRequiredForDelay <= 0) {
        // cycle count exceeded, exit function
    } else {   
        while(cyclesRequiredForDelay>0) { //19 cycles used to this point.
            cyclesRequiredForDelay -= 11; 
            // subtract cycles in each delay stage, 12 + 1 as padding
        }
    }
}

// performs a software delay in intervals of 1 millisecond
void DelayMs( uint16_t ms ) {
    volatile uint8_t i;        
    while (ms--) {
        i = 4;
        while (i--) {
            Delay10us( 25 );
        }
    }
}

// write data into controller's RAM, chip select should be enabled
extern inline void __attribute__ ((always_inline)) DeviceWrite(uint8_t data) {
	PMDIN1 = data;
	PMPWaitBusy();
}

// read data from controller's RAM. chip select should be enabled
extern inline uint8_t __attribute__ ((always_inline)) DeviceRead() {
    uint8_t value;
	value = PMDIN1;
	PMPWaitBusy();
	PMCONbits.PMPEN = 0; // disable PMP
	value = PMDIN1;
	PMCONbits.PMPEN = 1; // enable  PMP
	return value;
}

// single read is performed; Useful in issuing one read access only.
extern inline uint8_t __attribute__ ((always_inline)) SingleDeviceRead() {
    uint8_t value;
	value = PMDIN1;
	PMPWaitBusy();
	return value;
}

// Reads a word from the device
extern inline uint16_t __attribute__ ((always_inline)) DeviceReadWord() {
    uint16_t value; uint8_t temp;
    value = PMDIN1;
    value = value << 8;
    PMPWaitBusy();
    temp = PMDIN1;
    value = value & temp;
    PMPWaitBusy();
    return value;
}

// initializes the OLED device
extern inline void __attribute__ ((always_inline)) DriverInterfaceInit(void) { 
    // variable for PMP timing calculation
	// CLOCK_FREQ in MHz => pClockPeriod in nanoseconds
    uint32_t pClockPeriod = (1000000000ul) / CLOCK_FREQ;
	DisplayResetEnable();               // hold in reset by default
    DisplayResetConfig();               // enable RESET line
    DisplayCmdDataConfig();             // enable RS line
    DisplayDisable();                   // not selected by default
    DisplayConfig();                    // enable chip select line
    // PMP setup
    PMMODE = 0; PMAEN = 0; PMCON = 0;
    PMMODEbits.MODE = 2;                // Intel 80 master interface
    PMMODEbits.WAITB = 0;
    #if (PMP_DATA_WAIT_TIME == 0)
        PMMODEbits.WAITM = 0;
    #else    
        if (PMP_DATA_WAIT_TIME <= pClockPeriod)
            PMMODEbits.WAITM = 1;
        else if (PMP_DATA_WAIT_TIME > pClockPeriod)
            PMMODEbits.WAITM = (PMP_DATA_WAIT_TIME / pClockPeriod) + 1;
    #endif
    #if (PMP_DATA_HOLD_TIME == 0)
        PMMODEbits.WAITE = 0;
    #else
        if (PMP_DATA_HOLD_TIME <= pClockPeriod)
            PMMODEbits.WAITE = 0;
        else if (PMP_DATA_HOLD_TIME > pClockPeriod)
            PMMODEbits.WAITE = (PMP_DATA_HOLD_TIME / pClockPeriod) + 1;
    #endif
    PMMODEbits.MODE16 = 0;              // 8 bit mode
    PMCONbits.PTRDEN =  PMCONbits.PTWREN = 1;  // enable WR & RD line
    PMCONbits.PMPEN = 1;                // enable PMP
    DisplayResetDisable();              // release from reset
    Delay10us(20);  // hard delay for devices that need it after reset
}

void ResetDevice(void) {
	DriverInterfaceInit();  // Initialize the device
    DisplayEnable();
	DisplaySetCommand();
    DeviceWrite(0xAE);             // turn off the display (AF=ON, AE=OFF)
    DeviceWrite(0xDB); DeviceWrite(0x23); // set  VCOMH
    DeviceWrite(0xD9); DeviceWrite(0x22); // set  VP    
    DeviceWrite(0xA1);             // [A0]:column address 0 is map to SEG0
                                   // [A1]:column address 131 is map to SEG0
    DeviceWrite(0xC8);             // C0 is COM0 to COMn, C8 is COMn to COM0
    DeviceWrite(0xDA);             // set COM pins hardware configuration
    DeviceWrite(0x12);
    DeviceWrite(0xA8);             // set multiplex ratio
    DeviceWrite(0x3F);             // set to 64 mux
    DeviceWrite(0xD5);             // set display clock divide
    DeviceWrite(0xA0);             // set to 100Hz
    DeviceWrite(0x81);             // Set contrast control
    DeviceWrite(0x60);             // display 0 ~ 127; 2C
    DeviceWrite(0xD3);             // Display Offset: set display offset
    DeviceWrite(0x00);             // no offset
    DeviceWrite(0xA6);             //Normal or Inverse Display: Normal display
    DeviceWrite(0xAD);             // Set DC-DC
    DeviceWrite(0x8B);             // 8B=ON, 8A=OFF
    DeviceWrite(0xAF);             // Display ON/OFF: AF=ON, AE=OFF
    DelayMs(150);
    DeviceWrite(0xA4);             // Entire Display ON/OFF: A4=ON
    DeviceWrite(0x40);             // Set display start line
    DeviceWrite(0x00 + OFFSET);    // Set lower column address
    DeviceWrite(0x10);             // Set higher column address
    DelayMs(1);
    DisplayDisable(); DisplaySetData();
}

// puts pixel
void PutPixel(int16_t x, int16_t y) {
    uint8_t page, add, lAddr, hAddr;
    uint8_t mask, display;
    // Assign a page address
    AssignPageAddress(y);
    add = x + OFFSET;
    lAddr = 0x0F & add; hAddr = 0x10 | (add >> 4);  // low + high address
    // Calculate mask from rows basically do a y%8 and remainder is bit position
    add = y >> 3;                   // Divide by 8
    add <<= 3;                      // Multiply by 8
    add = y - add;                  // Calculate bit position
    mask = 1 << add;                // Left shift 1 by bit position
    DisplayEnable();
    SetAddress(page, lAddr, hAddr); // Set the address (sets the page,
    // lower and higher column address pointers)
    display = SingleDeviceRead();	// initiate Read transaction on PMP  								
    display = SingleDeviceRead();	// for synchronization in the controller
    display = SingleDeviceRead();	// read actual data from display buffer
    if (_color > 0)  display |= mask;   // pixel on -> or in mask
    else display &= ~mask;           //    pixel off -> and with inverted mask
    SetAddress(page, lAddr, hAddr); // Set the address (sets the page,
    // lower and higher column address pointers)
    DeviceWrite(display);             // restore the byte with manipulated bit
    DisplayDisable();
}

// return pixel color at x,y position
uint8_t GetPixel(int16_t x, int16_t y) {
    uint8_t page, add, lAddr, hAddr, mask, temp, display;
    AssignPageAddress(y);
    add = x + OFFSET;
    lAddr = 0x0F & add; hAddr = 0x10 | (add >> 4); // low + high address
    // Calculate mask from rows basically do a y%8 and remainder is bit position
    temp = y >> 3;                  // Divide by 8
    temp <<= 3;                     // Multiply by 8
    temp = y - temp;                // Calculate bit position
    mask = 1 << temp;               // Left shift 1 by bit position
    DisplayEnable();
    SetAddress(page, lAddr, hAddr); // set page, lower, higher column address
    display = SingleDeviceRead();	// Read to initiate Read transaction on PMP
    display = DeviceRead();         // Read data from display buffer
    DisplayDisable();
    return (display & mask);        // mask all other bits and return the result
}

// clears screen with _color
void ClearDevice(void) {
    DisplayEnable();
    for(uint8_t i = 0xB0; i < 0xB8; i++) {  // go through all 8 pages
        SetAddress(i, 0x00, 0x10);
        for(uint8_t j = 0; j < 132; j++) // write to all 132 bytes
            DeviceWrite(_color);
    }
    DisplayDisable();
}
