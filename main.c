#include "PIC24FStarter.h"

// Debounce Threshold: Button must be stable for this many cycles to register
#define DEBOUNCE_THRESH  4

// Delay function using Timer1
void delay(unsigned int milliseconds) {
    T1CONbits.TCKPS = 0b11; // Prescale 1:256
    PR1 = 47; TMR1 = 0;     // Reset timer counter
    T1CONbits.TON = 1;      // Turn on Timer1
    unsigned int count = 0;
    while (count < milliseconds) {
        while (!IFS0bits.T1IF); // Wait for Timer1 interrupt flag
        IFS0bits.T1IF = 0;      // Clear Timer1 interrupt flag
        count++;
    }
    T1CONbits.TON = 0;      // Turn off Timer1
}

// Helper function to plot 8 symmetric points of a circle
void plotPoints(uint8_t xctr, uint8_t yctr, uint8_t x, uint8_t y) {
    PutPixel(xctr + x, yctr + y); PutPixel(xctr - x, yctr + y);
    PutPixel(xctr + x, yctr - y); PutPixel(xctr - x, yctr - y);
    PutPixel(xctr + y, yctr + x); PutPixel(xctr - y, yctr + x);
    PutPixel(xctr + y, yctr - x); PutPixel(xctr - y, yctr - x);
}

// Draw a hollow circle at (x,y) with radius r
void drawCircle(uint8_t x1, uint8_t y1, uint8_t r) {
    uint8_t x = 0, y = r;
    int16_t p = 1 - r; 
    plotPoints(x1, y1, x, y);
    while (x < y) {
        x++;
        if (p < 0) p += 2 * x + 1;
        else p += 2 * (x - --y) + 1;
        plotPoints(x1, y1, x, y);
    }
}

// Helper function to draw a filled circle (for pressed state)
void drawFilledCircle(uint8_t x1, uint8_t y1, uint8_t r) {
    uint8_t i;
    for(i = 0; i <= r; i++) {
        drawCircle(x1, y1, i);
    }
}

// Screen Resolution is 128x64. Center is (64, 32).
// Button Mapping:
// Index 0: Up
// Index 1: Right
// Index 2: Down
// Index 3: Left
// Index 4: Center (Pin AN12/RB12)

const uint8_t btnX[5] = {64, 104, 64, 24, 64}; // X positions
const uint8_t btnY[5] = {12, 32, 52, 32, 32};  // Y positions
const uint8_t RADIUS = 6;                      // Circle Radius

int main(void) {
    // Hardware Initialization
    INIT_CLOCK(); 
    CTMUInit(); 
    RGBMapColorPins();
    
    RGBTurnOnLED();
    ResetDevice();
    ClearDevice();
    
    // 2. Debounce State Tracking
    // counters: How many consecutive frames the button has been 'raw' pressed
    uint8_t debounce_counters[5] = {0, 0, 0, 0, 0};
    
    // current_btn_state: The "clean" on/off state used for logic/drawing
    uint8_t current_btn_state[5] = {0, 0, 0, 0, 0};
    
    // Array to track the previous state of buttons to optimize drawing
    // Initialize to 0xFF to force an initial draw
    uint8_t prev_drawn_state[5]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    while(1) { 
        // Read touch sensors; updates the global 'buttons' array
        ReadCTMU(); 
        
        // We filter the raw 'buttons[]' into 'current_btn_state[]'
        for(uint8_t i = 0; i < 5; i++) {
            if (buttons[i] == 1) {
                // Button is physically active
                if (debounce_counters[i] < DEBOUNCE_THRESH) {
                    debounce_counters[i]++;
                }
            } else {
                // Button is physically released
                if (debounce_counters[i] > 0) {
                    debounce_counters[i]--;
                }
            }
            
            // Hysteresis Logic:
            // Switch to 1 only if counter fills up (sustained press)
            // Switch to 0 only if counter empties (sustained release)
            if (debounce_counters[i] == DEBOUNCE_THRESH) {
                current_btn_state[i] = 1;
            } else if (debounce_counters[i] == 0) {
                current_btn_state[i] = 0;
            }
            // If counter is between 0 and THRESH, keep previous state (ignore noise)
        }
        
        // Loop through all 5 buttons
        for (uint8_t i = 0; i < 5; i++) {
            
            // Only redraw if the state of the button has changed
            if (current_btn_state[i] != prev_drawn_state[i]) {
                
                // 1. Erase the old shape
                // We draw a black filled circle to clear the area completely
                SetColor(BLACK);
                drawFilledCircle(btnX[i], btnY[i], RADIUS);
                
                // 2. Draw the new shape
                SetColor(WHITE);
                
                if (current_btn_state[i] == 1) {
                    // STATE: PRESSED
                    // Draw a solid white circle (Active node)
                    drawFilledCircle(btnX[i], btnY[i], RADIUS);
                } else {
                    // STATE: RELEASED
                    // Draw a hollow circle with a center dot (Inactive node)
                    drawCircle(btnX[i], btnY[i], RADIUS);
                    PutPixel(btnX[i], btnY[i]); 
                }
                
                // Update state tracker
                prev_drawn_state[i] = current_btn_state[i];
            }
        }
        
        // Light up RGB LED Green if any button is pressed
        uint8_t anyPressed = 0;
        for(uint8_t j=0; j<5; j++) { if(current_btn_state[j]) anyPressed = 1; }
        
        if(anyPressed) SetRGBs(0, 255, 0); // Green
        else SetRGBs(0, 0, 0);             // Off

        delay(10); // Short delay to debounce and limit refresh rate
    }
    
    RGBTurnOffLED();
    return 0;
}