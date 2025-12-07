#include "PIC24FStarter.h"

// Debounce Threshold: Button must be stable for this many cycles to register
#define DEBOUNCE_THRESH  2

// Gap Timeout: How long to wait (in 10ms loops) before deciding the swipe is finished.
// 50 * 10ms = 500ms grace period between buttons.
#define SWIPE_GAP_LIMIT  50

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

// --- PASSWORD CONFIGURATION ---
uint8_t PASSWORD[10];
uint8_t PASS_LEN = 0;

// Application States
enum AppState {
    STATE_SET_PATTERN = 0, // Recording new password
    STATE_CONFIRM_SET,     // Visual feedback that pass is saved
    STATE_INPUT,           // Locked: Waiting for unlock attempt
    STATE_SUCCESS,         // Unlocked
    STATE_FAIL             // Wrong password
};

uint8_t current_state = STATE_SET_PATTERN;

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

// Check if a button ID is already in the current path
// Returns 1 if present, 0 if not
uint8_t isNodeInPath(uint8_t id, uint8_t path[], uint8_t len) {
    uint8_t i;
    for(i=0; i<len; i++) {
        if (path[i] == id) return 1;
    }
    return 0;
}

// Compare entered path with password
uint8_t checkPassword(uint8_t path[], uint8_t len) {
    if (len != PASS_LEN) return 0;
    
    uint8_t i;
    for(i=0; i<len; i++) {
        if (path[i] != PASSWORD[i]) return 0;
    }
    return 1;
}

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
    
    // Pattern Logic Variables
    uint8_t current_path[10]; // Stores the user's sequence
    uint8_t path_idx = 0;
    uint16_t gap_timer = 0; // Timer to track "no touch" duration
    uint16_t state_timer = 0; // Generic timer for state transitions
    
    // Visual State Management
    // 0 = Hollow (Inactive), 1 = Filled (Active)
    uint8_t node_visual_state[5] = {0,0,0,0,0}; 
    uint8_t prev_visual_state[5] = {255,255,255,255,255}; // Force initial draw
    
    // Result feedback timer
    uint16_t result_timer = 0;
    uint8_t lock_state = 0; // 0: Input, 1: Success Show, 2: Fail Show
    
    while(1) { 
        // Read touch sensors; updates the global 'buttons' array
        ReadCTMU();
        
        uint8_t any_pressed_now = 0;
        
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
            if (current_btn_state[i]) any_pressed_now = 1;
        }
        
if (current_state == STATE_SET_PATTERN) {
            // INDICATOR: Pulse Magenta to indicate "Recording Mode"
            SetRGBs(100, 0, 100); 
            
            if (any_pressed_now) {
                gap_timer = 0;
                for(uint8_t i = 0; i < 5; i++) {
                    if (current_btn_state[i] && !isNodeInPath(i, current_path, path_idx)) {
                        if (path_idx < 10) {
                            current_path[path_idx++] = i;
                            node_visual_state[i] = 1; // Visual feedback
                        }
                    }
                }
            } 
            else if (path_idx > 0) {
                // User lifted finger after starting a pattern
                gap_timer++;
                if (gap_timer > SWIPE_GAP_LIMIT) {
                    // Pattern complete. Save it.
                    for(uint8_t k=0; k<path_idx; k++) PASSWORD[k] = current_path[k];
                    PASS_LEN = path_idx;
                    
                    // Reset and move to confirmation
                    gap_timer = 0;
                    path_idx = 0;
                    for(uint8_t k=0; k<5; k++) node_visual_state[k] = 0;
                    
                    current_state = STATE_CONFIRM_SET;
                    state_timer = 100; // 1 second confirmation
                }
            }
        }
        else if (current_state == STATE_CONFIRM_SET) {
            // INDICATOR: Rapid Cyan Flash to confirm "Password Set"
            if ((state_timer / 10) % 2 == 0) SetRGBs(0, 255, 255);
            else SetRGBs(0, 0, 0);
            
            if (state_timer > 0) state_timer--;
            else current_state = STATE_INPUT; // Go to locked mode
        }
        else if (current_state == STATE_INPUT) {
            // INDICATOR: Solid Blue (Locked)
            SetRGBs(0, 0, 255);
            
            if (any_pressed_now) {
                gap_timer = 0;
                for(uint8_t i = 0; i < 5; i++) {
                    if (current_btn_state[i] && !isNodeInPath(i, current_path, path_idx)) {
                        if (path_idx < 10) {
                            current_path[path_idx++] = i;
                            node_visual_state[i] = 1;
                        }
                    }
                }
            } 
            else if (path_idx > 0) {
                gap_timer++;
                if (gap_timer > SWIPE_GAP_LIMIT) {
                    // Validate
                    if (checkPassword(current_path, path_idx)) {
                        current_state = STATE_SUCCESS;
                    } else {
                        current_state = STATE_FAIL;
                    }
                    
                    // Reset input buffers
                    gap_timer = 0;
                    path_idx = 0;
                    state_timer = 150; // Show result for 1.5s
                }
            }
        }
        else if (current_state == STATE_SUCCESS) {
            SetRGBs(0, 255, 0); // Green
            if (state_timer > 0) state_timer--;
            else {
                // Clear visual state and return to lock
                for(uint8_t k=0; k<5; k++) node_visual_state[k] = 0;
                current_state = STATE_INPUT; 
            }
        }
        else if (current_state == STATE_FAIL) {
            SetRGBs(255, 0, 0); // Red
            if (state_timer > 0) state_timer--;
            else {
                // Clear visual state and return to lock
                for(uint8_t k=0; k<5; k++) node_visual_state[k] = 0;
                current_state = STATE_INPUT;
            }
        }
        
        // Loop through all 5 buttons
        for (uint8_t i = 0; i < 5; i++) {
            
            // Only redraw if the state of the button has changed
            if (node_visual_state[i] != prev_visual_state[i]) {
                
                // 1. Erase the old shape
                // We draw a black filled circle to clear the area completely
                SetColor(BLACK);
                drawFilledCircle(btnX[i], btnY[i], RADIUS);
                
                // 2. Draw the new shape
                SetColor(WHITE);
                
                if (node_visual_state[i] == 1) {
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
                prev_visual_state[i] = node_visual_state[i];
            }
        }
        delay(10); // Short delay to debounce and limit refresh rate
    }
    
    RGBTurnOffLED();
    return 0;
}