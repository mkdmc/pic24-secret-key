#include "PIC24FStarter.h"
#include <stdlib.h>
#include <stdbool.h>

// --- Configuration ---
#define DEBOUNCE_THRESH  4      // Cycles a touch must be stable to register
#define TOUCH_TIMEOUT    5000  // Cycles to wait after release before submitting pattern
#define RESULT_DELAY     20000   // Cycles to hold result display
#define PATTERN_MAX      10     // Max nodes in a pattern

// Screen Resolution is 128x64. Center is (64, 32).
// Button Mapping:
// Index 0: Up
// Index 1: Right
// Index 2: Down
// Index 3: Left
// Index 4: Center (Pin AN12/RB12)

const uint8_t btnX[5] = {64, 104, 64, 24, 64}; // X positions
const uint8_t btnY[5] = {12, 32, 52, 32, 32};  // Y positions

// --- Global Variables ---

// State Machine States
enum AppState {
    STATE_SET_PATTERN = 0, // Recording new password
    STATE_CONFIRM_SET,     // Visual feedback that pass is saved
    STATE_INPUT,           // Locked: Waiting for unlock attempt
    STATE_SUCCESS,         // Unlocked
    STATE_FAIL             // Wrong password
};

uint8_t current_state = STATE_SET_PATTERN;

// Pattern Storage
uint8_t PASSWORD[PATTERN_MAX];
uint8_t PASS_LEN = 0;

// Current Entry Variables
uint8_t patternBuf[PATTERN_MAX];
uint8_t patternIdx = 0;
bool visitedMask[5]; // Tracks which nodes are already used in current stroke

// Debounce / Input Globals
int8_t lastSeenButton = -1;
uint8_t stableCount = 0;
uint32_t idleTimer = 0;

// --- Graphic Helpers ---

// Bresenham's Line Algorithm
void GFX_DrawLine(int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    for (;;) {
        PutPixel(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Draw Node (Hollow or Filled)
void GFX_DrawNode(uint8_t x, uint8_t y, bool filled) {
    // Draw Node Outline (Cross-like shape)
    PutPixel(x, y-3); PutPixel(x, y+3);
    PutPixel(x-3, y); PutPixel(x+3, y);
    PutPixel(x-1, y-2); PutPixel(x+1, y-2);
    PutPixel(x-2, y-1); PutPixel(x+2, y-1);
    PutPixel(x-2, y+1); PutPixel(x+2, y+1);
    PutPixel(x-1, y+2); PutPixel(x+1, y+2);

    if (filled) {
        PutPixel(x, y);
        PutPixel(x-1, y); PutPixel(x+1, y);
        PutPixel(x, y-1); PutPixel(x, y+1);
    }
}

// Draw a single character from the font array
void UI_DrawChar(int x, int y, char c) {
    if (c < 32 || c > 122) c = 32; // basic safety
    int index = c - 32;
    
    for (int i = 0; i < 5; i++) {
        uint8_t line = Font5x7[index][i];
        for (int j = 0; j < 8; j++) {
            if (line & (1 << j)) {
                PutPixel(x + i, y + j);
            }
        }
    }
}

// Draw a string of text
void UI_DrawString(int x, int y, char* str) {
    while (*str) {
        UI_DrawChar(x, y, *str);
        x += 6; // 5px width + 1px spacing
        str++;
    }
}

// Reset screen grid to initial state
void UI_ResetGrid() {
    SetColor(BLACK); 
    ClearDevice(); 
    SetColor(WHITE);
    
    for(int i=0; i<5; i++) {
        GFX_DrawNode(btnX[i], btnY[i], false);
        visitedMask[i] = false;
    }
    patternIdx = 0;
    idleTimer = 0;
}

// --- Input Processing ---

// Helper to get single stable button press (filters noise where multiple pads trigger)
int8_t GetStableInput() {
    int8_t detected = -1;
    uint8_t pressCount = 0;

    for (uint8_t i = 0; i < 5; i++) {
        if (buttons[i]) {
            detected = i;
            pressCount++;
        }
    }

    // Ignore if multiple pads are triggered (noise reduction)
    if (pressCount > 1) return -1;
    
    return detected;
}

// --- Logic Helpers ---

bool CheckPassword() {
    if (patternIdx != PASS_LEN) return false;
    for (uint8_t k=0; k<PASS_LEN; k++) {
        if (patternBuf[k] != PASSWORD[k]) return false;
    }
    return true;
}

void SavePassword() {
    for (uint8_t k=0; k<patternIdx; k++) {
        PASSWORD[k] = patternBuf[k];
    }
    PASS_LEN = patternIdx;
}

// Blocking Delay
void delay(unsigned int delay_count) {
    T1CON = 0x8030; // Timer 1 ON, Prescale 1:256
    TMR1 = 0;
    while(TMR1 < delay_count);
    T1CON = 0;
}

// --- Main Application ---

int main(void) {
    // Hardware Init
    INIT_CLOCK(); 
    CTMUInit(); 
    RGBMapColorPins(); 
    RGBTurnOnLED();
    ResetDevice(); 

    // --- Splash Screen ---
    SetColor(BLACK);
    ClearDevice();
    SetColor(WHITE);
    
    // Display "Set a new" at (x=37, y=20)
    UI_DrawString(37, 20, "Set a new");
    // Display "password" at (x=40, y=30)
    UI_DrawString(40, 30, "password");
    
    // Set LED to Cyan to indicate Info/Message
    SetRGBs(0, 255, 255);
    
    // Show message for ~2 seconds
    delay(RESULT_DELAY * 3);
    // -------------------------------
    
    // Initial State
    current_state = STATE_SET_PATTERN;
    UI_ResetGrid();
    
    // Set LED to Magenta (Red + Blue) to indicate "Programming Mode"
    SetRGBs(100, 0, 100); 

    while(1) {
        ReadCTMU(); 

        // 1. Debounce Logic
        int8_t rawInput = GetStableInput();
        int8_t validTouch = -1;
        
        if (rawInput == lastSeenButton && rawInput != -1) {
            stableCount++;
            if (stableCount >= DEBOUNCE_THRESH) {
                validTouch = rawInput; 
                stableCount = DEBOUNCE_THRESH; // Clamp
            }
        } else {
            stableCount = 0;
            lastSeenButton = rawInput;
        }

        // 2. State Machine Handling
        
        if (current_state == STATE_CONFIRM_SET || current_state == STATE_SUCCESS || current_state == STATE_FAIL) {
            // These states are handled by blocking delays inside transitions, 
            // but if we loop back here, reset to appropriate interactive state.
            UI_ResetGrid();
            if (current_state == STATE_CONFIRM_SET || current_state == STATE_SUCCESS || current_state == STATE_FAIL) {
                current_state = STATE_INPUT; // Default to Input mode after any result
                SetRGBs(0, 0, 255); // Solid Blue
            }
        }

        // 3. Process Input (Recording Pattern)
        if (validTouch != -1) {
            idleTimer = 0; // Reset timeout because user is touching

            if (patternIdx < PATTERN_MAX) {
                // Only register if node hasn't been visited in this stroke
                if (!visitedMask[validTouch]) {
                    
                    patternBuf[patternIdx] = validTouch;
                    visitedMask[validTouch] = true;

                    // Graphics: Fill Node
                    GFX_DrawNode(btnX[validTouch], btnY[validTouch], true);

                    // Graphics: Draw Line from previous node
                    if (patternIdx > 0) {
                        uint8_t prev = patternBuf[patternIdx - 1];
                        GFX_DrawLine(btnX[prev], btnY[prev], 
                                     btnX[validTouch], btnY[validTouch]);
                    }

                    // Feedback: Flash Yellow briefly
                    SetRGBs(255, 255, 0); 
                    
                    patternIdx++;
                }
            }
        } 
        else {
            // 4. No Touch Detected - Handle Timeout / Submission
            
            // Restore base LED color based on mode if not touching
            if (idleTimer == 1) {
                if (current_state == STATE_SET_PATTERN) SetRGBs(100, 0, 100); // Magenta
                else if (current_state == STATE_INPUT)  SetRGBs(0, 0, 255);   // Blue
            }

            if (patternIdx > 0) {
                // Pattern is in progress, check for timeout (finger lifted)
                idleTimer++;
                
                if (idleTimer > TOUCH_TIMEOUT) {
                    
                    // --- TIMEOUT: Process the Pattern ---
                    
                    if (current_state == STATE_SET_PATTERN) {
                        // Pattern Recording Complete
                        SavePassword();
                        
                        // Transition to Confirmation
                        current_state = STATE_CONFIRM_SET;
                        
                        // --- Success Message ---
                        SetColor(BLACK);
                        ClearDevice();
                        SetColor(WHITE);
                        UI_DrawString(28, 20, "Password set");
                        UI_DrawString(28, 30, "successfully");
                        
                        SetRGBs(0, 255, 0); // Green for success
                        delay(RESULT_DELAY * 2);
                        // ---------------------------------
                        
                        // Move to Input Mode
                        UI_ResetGrid();
                        current_state = STATE_INPUT;
                        SetRGBs(0, 0, 255); // Blue
                    }
                    else if (current_state == STATE_INPUT) {
                        // Pattern Entry Complete - Validate
                        if (CheckPassword()) {
                            // Success
                            SetRGBs(0, 255, 0); // Green
                            // Draw Checkmark
                            GFX_DrawLine(50, 40, 60, 50); 
                            GFX_DrawLine(60, 50, 80, 20);
                            current_state = STATE_SUCCESS;
                        } else {
                            // Fail
                            SetRGBs(255, 0, 0); // Red
                            // Draw X
                            GFX_DrawLine(50, 20, 78, 48); 
                            GFX_DrawLine(78, 20, 50, 48);
                            current_state = STATE_FAIL;
                        }
                        
                        delay(RESULT_DELAY);
                        UI_ResetGrid();
                        current_state = STATE_INPUT; // Return to lock screen
                        SetRGBs(0, 0, 255); // Restore Blue
                    }
                    
                    idleTimer = 0;
                }
            }
        }
    }
    
    return 0;
}