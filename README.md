# Pattern Lock Security System for PIC24F

## About The Project 
This project demonstrates a smartphone-style Pattern Lock Security System implemented on the Microchip MPLAB Starter Kit for PIC24F. It utilizes the board's integrated capacitive touch pads to record and validate unlock patterns, providing visual feedback via an OLED display and RGB LEDs.
Capacitive touch sensors are inherently sensitive to **environmental noise** (humidity, skin moisture, electrical interference).  
To ensure reliable input, this project implements a robust input qualification system tailored for a pattern-lock interface.

## Operation
1. Startup: The board will power on and display a splash screen. The LED will briefly glow Cyan.

2. Set Password:
- The LED turns Magenta.
- Touch the pads in any sequence to create your pattern. Lines will connect the nodes on the screen.
- Release your finger to save the pattern. The screen will confirm "Password set successfully."

3. Unlock Device:
- The LED turns Solid Blue and the grid clears.
- Re-enter your pattern.

4. Result:
- Success: A large checkmark appears, and the LED turns Green.
- Fail: A large "X" appears, and the LED turns Red.
- The system automatically resets to "Locked" mode after a few seconds.


## Report a bug

You can report a bug by [opening an issue](https://github.com/mkdmc/pic24-secret-key/issues/new).

#### How to report a bug
* A detailed description of the bug
* Make sure there are no similar bug reports already

## Download & Build

1. Clone the project:
``` bash
$ git clone git://github.com/mkdmc/pic24-secret-key.git
```
2. Open the project in MPLAB X IDE.
3. Ensure you have the XC16 Compiler installed.
4. Connect the Starter Kit to your PC via USB (J1 - Debugger side).
5. Build and program the device.

## File Structure
main.c – Application logic (pattern lock state machine, graphics, noise filtering)
TouchSense.c – Handles low-level CTMU initialization, calibration, and reading of the 5 capacitive touch pads.
SH1101A.c – Driver for the OLED display, managing PMP communication and screen buffer updates.
RGBLeds.c – Controls the RGB LED color mixing using Output Compare (PWM) timers.
PIC24FStarter.h - Configuration bits and hardware definitions for the specific starter kit board.
