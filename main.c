#include "PIC24FStarter.h"
#include <stdlib.h>
#include <stdbool.h>

// --- Configuration ---
#define DEBOUNCE_THRESH  4      // Cycles a touch must be stable to register
#define TOUCH_TIMEOUT    5000  // Cycles to wait after release before submitting pattern
#define RESULT_DELAY     20000   // Cycles to hold result display
#define PATTERN_MAX      5     // Max nodes in a pattern
#define MAX_USERS        3     // Max num. of users that can be created
#define MAX_LOGS         15    // Max num. of entris of Logs

// Screen Resolution is 128x64. Center is (64, 32).
// Button Mapping:
// Index 0: Up
// Index 1: Right
// Index 2: Down
// Index 3: Left
// Index 4: Center (Pin AN12/RB12)

const uint8_t btnX[5] = {64, 104, 64, 24, 64}; // X positions
const uint8_t btnY[5] = {12, 32, 52, 32, 32};  // Y positions

// --- Flash Memory Configuration ---
#define FLASH_MAGIC     0xDA7B 
#define FLASH_ROW_SIZE  64 
#define FLASH_PAGE_SIZE 512 

const uint16_t __attribute__((space(prog), aligned(1024))) FlashStorage[FLASH_PAGE_SIZE] = {0xFFFF};

#define BCDToBin(x)     ( (((x) >> 4) * 10) + ((x) & 0x0F) )
#define BinToBCD(x)     ( (((x) / 10) << 4) | ((x) % 10) )

// --- Global Variables ---

enum AppState {
    STATE_BOOT = 0,
    STATE_LANGUAGE_SELECT, 
    STATE_WELCOME,
    STATE_SET_DATE,
    STATE_SET_TIME,
    STATE_TUTORIAL,       
    STATE_MENU,           
    
    STATE_ADVANCED_MENU,
    STATE_SET_PATTERN,    
    STATE_PERMISSIONS,   
    STATE_USER_CONFIG,   
    
    STATE_ADMIN_LOGS,
    STATE_USER_LOGS,      
    
    STATE_DOOR_OPEN_MENU, 
    STATE_LOGIN_SETTINGS, 
    
    STATE_VERIFY_DOOR,    
    STATE_VERIFY_LOGIN,   
    
    STATE_ERROR_MSG       
};

uint8_t current_state = STATE_BOOT;
uint8_t returnState = STATE_MENU;

// Debounce / Input Globals
int8_t lastSeenButton = -1;
uint8_t stableCount = 0;
uint32_t idleTimer = 0;

// --- USER DATA ---
uint8_t PASSWORDS[MAX_USERS][PATTERN_MAX];
uint8_t PASS_LENS[MAX_USERS]; 
uint8_t PERMISSIONS[MAX_USERS]; 
// Access Control
#define ACC_PERMANENT 0
#define ACC_ONETIME   1
#define ACC_MULTI     2
uint8_t ACCESS_TYPE[MAX_USERS];
uint8_t ACCESS_COUNT[MAX_USERS];
// Active Flag
uint8_t USER_ACTIVE[MAX_USERS];

uint8_t numUsers = 0;    
uint8_t currentUser = 0; 
uint8_t targetUserIdx = 0; 

// --- LOG DATA ---
typedef struct {
    uint8_t userIdx; 
    uint8_t type;    
    uint8_t status;  
    uint8_t mon;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
} LogEntry;

LogEntry ADMIN_LOGS[MAX_LOGS];
uint8_t logCount = 0;
uint8_t logScroll = 0; 
uint8_t userLogScroll = 0; 

#define LOG_TYPE_SETTINGS 0
#define LOG_TYPE_DOOR     1
#define LOG_STATUS_FAIL    0
#define LOG_STATUS_SUCCESS 1

// Temp buffers
uint8_t patternBuf[PATTERN_MAX];
uint8_t patternIdx = 0;
bool visitedMask[5];

// Date/Time Editing
uint8_t editY = 24, editM = 1, editD = 1;
uint8_t editH = 12, editMin = 0;
uint8_t cursorIndex = 0;

// Config Buffer
uint8_t cfgActive = 1; 
uint8_t cfgPerm = 0;
uint8_t cfgAccType = 0;
uint8_t cfgAccCount = 2; 
bool cfgIsNewUser = false; 

// Menu Globals
uint8_t menuIndex = 0;
#define MAX_MENU_ITEMS 6
int8_t dynamicMenuMap[MAX_MENU_ITEMS]; 
char dynamicMenuLabels[MAX_MENU_ITEMS][32]; 

// --- MENUS ---
const uint8_t menuItemsAdmin[] = { S_M_CHANGE_PASS, S_M_CREATE_USER, S_M_ADVANCED, S_M_LANG, S_M_EXIT };
#define MENU_COUNT_ADMIN 5

// Removed static menuItemsAdvanced in favor of dynamic generation
const uint8_t menuItemsUserFull[] = { S_M_CHANGE_PASS, S_M_LANG, S_M_LOGIN_SESSIONS, S_M_EXIT };
#define MENU_COUNT_USER_FULL 4

const uint8_t menuItemsUserRestricted[] = { S_M_LANG, S_M_LOGIN_SESSIONS, S_M_EXIT };
#define MENU_COUNT_USER_RESTRICTED 3

// --- Helper Prototypes ---
void NVM_WriteAll(void);
bool NVM_ReadAll(void);
void RTCC_Init(void);
void RTCC_Set(uint8_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t min);
void RTCC_ReadTime(uint8_t* m, uint8_t* d, uint8_t* h, uint8_t* min); 
void Log_Add(uint8_t userIdx, uint8_t type, uint8_t status); 
void UI_DrawString(int x, int y, char* str);
void UI_PrintNum(int x, int y, int num, bool leadingZero);
void UI_ResetGrid(void);
void GFX_DrawLine(int x0, int y0, int x1, int y1);
void GFX_DrawNode(uint8_t x, uint8_t y, bool filled);
void delay(unsigned int delay_count);

// --- FLASH MEMORY FUNCTIONS ---
void NVM_Unlock() { __builtin_write_NVM(); }

void NVM_WriteAll() {
    uint16_t buffer[FLASH_ROW_SIZE];
    uint16_t i;
    
    // Prepare Buffer
    for(i=0; i<FLASH_ROW_SIZE; i++) buffer[i] = 0xFFFF;
    buffer[0] = FLASH_MAGIC;
    buffer[1] = numUsers;
    buffer[2] = logCount; 
    buffer[3] = sysLanguage; 
    
    int offset = 4;
    // PACKED CONFIG DATA (6 words total for 3 users)
    // Word A: [Active:8] [Permissions:8]
    // Word B: [Access Type:8] [Access Count:8]
    for(int u=0; u<MAX_USERS; u++) {
        buffer[offset++] = (USER_ACTIVE[u] << 8) | (PERMISSIONS[u] & 0xFF);
        buffer[offset++] = (ACCESS_TYPE[u] << 8) | (ACCESS_COUNT[u] & 0xFF);
    }
    
    // --- PASSWORD STORAGE ---
    // Structure per user (3 Words):
    // Word 0: [Length] [Pass0]
    // Word 1: [Pass1]  [Pass2]
    // Word 2: [Pass3]  [Pass4]
    for(int u=0; u<MAX_USERS; u++) {
        // High Byte | Low Byte
        buffer[offset++] = (PASS_LENS[u] << 8)       | (PASSWORDS[u][0] & 0xFF);
        buffer[offset++] = (PASSWORDS[u][1] << 8)    | (PASSWORDS[u][2] & 0xFF);
        buffer[offset++] = (PASSWORDS[u][3] << 8)    | (PASSWORDS[u][4] & 0xFF);
    }
    
    // --- LOG STORAGE ---
    for(int k=0; k<logCount; k++) {
        if (offset >= FLASH_ROW_SIZE - 1) break; 
        
        LogEntry l = ADMIN_LOGS[k];
        
        // Word A: [User:2][Type:1][Status:1][Month:4][Day:5]
        uint16_t wordA = 0;
        wordA |= (l.userIdx & 0x03) << 11; 
        wordA |= (l.type & 0x01) << 10;    
        wordA |= (l.status & 0x01) << 9;   
        wordA |= (l.mon & 0x0F) << 5;      
        wordA |= (l.day & 0x1F);           
        
        // Word B: [Hour:5][Min:6]
        uint16_t wordB = 0;
        wordB |= (l.hour & 0x1F) << 6;     
        wordB |= (l.min & 0x3F);           
        
        buffer[offset++] = wordA;
        buffer[offset++] = wordB;
    }
    
    // Erase Page
    NVMCON = 0x4042; 
    TBLPAG = __builtin_tblpage(FlashStorage); 
    __builtin_tblwtl(__builtin_tbloffset(FlashStorage), 0xFFFF); 
    NVM_Unlock(); 
    while(NVMCONbits.WR);

    // Write Row
    NVMCON = 0x4001; 
    TBLPAG = __builtin_tblpage(FlashStorage); 
    uint16_t flashOff = __builtin_tbloffset(FlashStorage);
    
    for(i=0; i<FLASH_ROW_SIZE; i++) 
        __builtin_tblwtl(flashOff + (i*2), buffer[i]);
    
    NVM_Unlock(); 
    while(NVMCONbits.WR);
}

bool NVM_ReadAll() {
    uint16_t offset = __builtin_tbloffset(FlashStorage);
    TBLPAG = __builtin_tblpage(FlashStorage);
    uint16_t magic = __builtin_tblrdl(offset);
    
    if (magic == FLASH_MAGIC) {
        numUsers = __builtin_tblrdl(offset + 2);
        if (numUsers > MAX_USERS)
            numUsers = 0;
        logCount = __builtin_tblrdl(offset + 4); 
        if (logCount > MAX_LOGS)
            logCount = 0;
        sysLanguage = __builtin_tblrdl(offset + 6); 
        if(sysLanguage > 1)
            sysLanguage = 0; 

        int ptr = offset + 8;
        for(int u=0; u<MAX_USERS; u++) {
            uint16_t wA = __builtin_tblrdl(ptr); ptr += 2;
            uint16_t wB = __builtin_tblrdl(ptr); ptr += 2;

            USER_ACTIVE[u] = (wA >> 8) & 0xFF;
            PERMISSIONS[u] = wA & 0xFF;
            ACCESS_TYPE[u] = (wB >> 8) & 0xFF;
            ACCESS_COUNT[u] = wB & 0xFF;
        }
        
        // --- READ PASSWORDS ---
        for(int u=0; u<MAX_USERS; u++) {
            uint16_t w0 = __builtin_tblrdl(ptr); ptr += 2;
            uint16_t w1 = __builtin_tblrdl(ptr); ptr += 2;
            uint16_t w2 = __builtin_tblrdl(ptr); ptr += 2;
            
            PASS_LENS[u]    = (w0 >> 8) & 0xFF;
            PASSWORDS[u][0] = w0 & 0xFF;
            PASSWORDS[u][1] = (w1 >> 8) & 0xFF;
            PASSWORDS[u][2] = w1 & 0xFF;
            PASSWORDS[u][3] = (w2 >> 8) & 0xFF;
            PASSWORDS[u][4] = w2 & 0xFF;

            // Safety check for length
            if (PASS_LENS[u] > PATTERN_MAX)
                PASS_LENS[u] = 0;
        }
        
        // --- READ LOGS ---
        for(int k=0; k<logCount; k++) {
            uint16_t wordA = __builtin_tblrdl(ptr); ptr += 2;
            uint16_t wordB = __builtin_tblrdl(ptr); ptr += 2;
            
            ADMIN_LOGS[k].userIdx = (wordA >> 11) & 0x03;
            ADMIN_LOGS[k].type    = (wordA >> 10) & 0x01;
            ADMIN_LOGS[k].status  = (wordA >> 9)  & 0x01;
            ADMIN_LOGS[k].mon     = (wordA >> 5)  & 0x0F;
            ADMIN_LOGS[k].day     = wordA & 0x1F;
            
            ADMIN_LOGS[k].hour    = (wordB >> 6) & 0x1F;
            ADMIN_LOGS[k].min     = wordB & 0x3F;
        }
        return true; 
    }
    return false; 
}

// --- RTCC & LOG HELPER ---
void RTCC_Init() {
    __builtin_write_OSCCONL(OSCCON | 0x02);
    __builtin_write_RTCWEN();
    RCFGCALbits.RTCEN = 0;
    RCFGCALbits.RTCPTR = 3;
    RTCVAL = 0x0024;
    RTCVAL = 0x0101;
    RTCVAL = 0x0000;
    RTCVAL = 0x0000;
    RCFGCALbits.RTCEN = 1;
    RCFGCALbits.RTCWREN = 0;
}

void RTCC_Set(uint8_t y, uint8_t m, uint8_t d, uint8_t h, uint8_t min) {
    __builtin_write_RTCWEN();
    RCFGCALbits.RTCEN = 0;
    RCFGCALbits.RTCPTR = 3;
    RTCVAL = BinToBCD(y);
    RTCVAL = (BinToBCD(m) << 8) | BinToBCD(d);
    RTCVAL = (1 << 8) | BinToBCD(h);
    RTCVAL = (BinToBCD(min) << 8);
    RCFGCALbits.RTCEN = 1;
    RCFGCALbits.RTCWREN = 0;
}

void RTCC_ReadTime(uint8_t* m, uint8_t* d, uint8_t* h, uint8_t* min) {
    RCFGCALbits.RTCPTR = 3;
    uint16_t rYear = RTCVAL;
    uint16_t rMonDay = RTCVAL;
    uint16_t rWkHr = RTCVAL;
    uint16_t rMinSec = RTCVAL;
    *m = BCDToBin(rMonDay >> 8);
    *d = BCDToBin(rMonDay & 0xFF);
    *h = BCDToBin(rWkHr & 0xFF);
    *min = BCDToBin(rMinSec >> 8);
}

void Log_Add(uint8_t userIdx, uint8_t type, uint8_t status) {
    if (logCount < MAX_LOGS)
        logCount++;
    for (int i = logCount - 1; i > 0; i--)
        ADMIN_LOGS[i] = ADMIN_LOGS[i-1];
    ADMIN_LOGS[0].userIdx = userIdx;
    ADMIN_LOGS[0].type = type;
    ADMIN_LOGS[0].status = status;
    RTCC_ReadTime(&ADMIN_LOGS[0].mon, &ADMIN_LOGS[0].day, &ADMIN_LOGS[0].hour, &ADMIN_LOGS[0].min);
    NVM_WriteAll();
}

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
    // Cast to unsigned to handle extended ASCII (128-255) safely
    uint8_t uc = (uint8_t)c;
    
    // Check range: 32 (Space) to 129 (ß)
    if (uc < 32 || uc > 129) uc = 32; 
    
    int index = uc - 32; 
    
    for (int i = 0; i < 5; i++) { 
        uint8_t line = Font5x7[index][i]; 
        for (int j = 0; j < 8; j++)
            if (line & (1 << j))
                PutPixel(x + i, y + j); 
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

void UI_PrintNum(int x, int y, int num, bool leadingZero) {
    char buf[5];
    if (leadingZero)
        sprintf(buf, "%02d", num);
    else
        sprintf(buf, "%d", num);
    UI_DrawString(x, y, buf);
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

bool CheckPassword(uint8_t userIdx) {
    if (patternIdx != PASS_LENS[userIdx]) return false;
    for (uint8_t k=0; k<PASS_LENS[userIdx]; k++) {
        if (patternBuf[k] != PASSWORDS[userIdx][k])
            return false;
    }
    return true;
}

void SavePassword(uint8_t userIdx) { 
    for (uint8_t k=0; k<patternIdx; k++) PASSWORDS[userIdx][k] = patternBuf[k]; 
    PASS_LENS[userIdx] = patternIdx; 
    
    // If creating new user, apply the config
    if (userIdx == numUsers) { 
        USER_ACTIVE[numUsers] = cfgActive;
        PERMISSIONS[numUsers] = cfgPerm; 
        ACCESS_TYPE[numUsers] = cfgAccType;
        ACCESS_COUNT[numUsers] = cfgAccCount;
        numUsers++; 
    }
    // If editing exists, params were already saved in CONFIG state
    NVM_WriteAll(); 
}

// Blocking Delay
void delay(unsigned int delay_count) {
    T1CON = 0x8030; // Timer 1 ON, Prescale 1:256
    TMR1 = 0;
    while(TMR1 < delay_count);
    T1CON = 0;
}

// Helper to disable a user if access expired
void DeactivateUser(uint8_t uIdx) {
    USER_ACTIVE[uIdx] = 0; 
    NVM_WriteAll();
}

// --- Main Application ---

int main(void) {
    INIT_CLOCK(); CTMUInit(); RGBMapColorPins(); RGBTurnOnLED(); ResetDevice(); RTCC_Init();
    bool dataLoaded = NVM_ReadAll();

    if (!dataLoaded || numUsers == 0) {
        numUsers = 0; currentUser = 0; targetUserIdx = 0; 
        PERMISSIONS[0] = 1; ACCESS_TYPE[0] = ACC_PERMANENT; USER_ACTIVE[0] = 1;
        sysLanguage = 0; 
        current_state = STATE_LANGUAGE_SELECT; 
    } else {
        currentUser = 0; 
        current_state = STATE_SET_DATE; 
        cursorIndex = 0;                
    }
    
    SetRGBs(0, 0, 255); 
    bool needsRedraw = true; uint8_t state_last_loop = 255; 

    while(1) {
        ReadCTMU(); 
        if (current_state != state_last_loop) {
            needsRedraw = true; state_last_loop = current_state;
        }
        int8_t rawInput = GetStableInput();
        int8_t touch = -1;
        if (rawInput == lastSeenButton && rawInput != -1) {
            stableCount++;
            if (stableCount >= DEBOUNCE_THRESH) {
                touch = rawInput;
                stableCount = DEBOUNCE_THRESH;
            }
        } else {
            stableCount = 0;
            lastSeenButton = rawInput;
        }

        // --- STATE MACHINE ---
        
        if (current_state == STATE_LANGUAGE_SELECT) {
            if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE);
                UI_DrawString(20, 10, (char*)GetStr(S_LANG_SELECT)); 
                GFX_DrawLine(0, 20, 127, 20);
                UI_DrawString(20, 30, (sysLanguage==0) ? "> English" : "  English");
                UI_DrawString(20, 45, (sysLanguage==1) ? "> Deutsch" : "  Deutsch");
                needsRedraw = false;
            }
            if (touch != -1) {
                if (touch == 0) sysLanguage = 0; else if (touch == 2) sysLanguage = 1; 
                else if (touch == 4) { 
                    NVM_WriteAll(); 
                    // If reset/new, go to Welcome. Otherwise, go back to where we came from.
                    if (numUsers == 0) current_state = STATE_WELCOME; 
                    else current_state = returnState;
                    menuIndex = 0;
                }
                needsRedraw = true; while(buttons[touch]) ReadCTMU(); delay(5000);
            }
        }
        else if (current_state == STATE_WELCOME) {
             if (needsRedraw) { SetColor(BLACK); ClearDevice(); SetColor(WHITE); UI_DrawString(40, 25, (char*)GetStr(S_WELCOME)); UI_DrawString(10, 40, (char*)GetStr(S_PRESS_CENTER)); needsRedraw = false; }
             if(buttons[4]) { delay(5000); current_state = STATE_SET_DATE; cursorIndex = 0; while(buttons[4]) ReadCTMU(); }
        }
        else if (current_state == STATE_SET_DATE) {
             if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE); UI_DrawString(10, 10, (char*)GetStr(S_SET_DATE)); UI_DrawString(10, 30, "20"); UI_PrintNum(22, 30, editY, true); UI_DrawString(38, 30, "/"); UI_PrintNum(48, 30, editM, true); UI_DrawString(64, 30, "/"); UI_PrintNum(74, 30, editD, true);
                int cursX = (cursorIndex == 0) ? 22 : (cursorIndex == 1) ? 48 : 74; GFX_DrawLine(cursX, 39, cursX+10, 39); needsRedraw = false;
            }
            if (touch != -1) {
                if (touch == 1) { if(cursorIndex < 2) cursorIndex++; } else if (touch == 3) { if(cursorIndex > 0) cursorIndex--; }
                else if (touch == 0) { if(cursorIndex == 0 && editY < 99) editY++; if(cursorIndex == 1 && editM < 12) editM++; if(cursorIndex == 2 && editD < 31) editD++; } 
                else if (touch == 2) { if(cursorIndex == 0 && editY > 20) editY--; if(cursorIndex == 1 && editM > 1) editM--; if(cursorIndex == 2 && editD > 1) editD--; } 
                else if (touch == 4) { current_state = STATE_SET_TIME; cursorIndex = 0; while(buttons[4]) ReadCTMU(); }
                needsRedraw = true; delay(5000); 
            } else delay(1000); 
        }
        else if (current_state == STATE_SET_TIME) {
             if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE); UI_DrawString(10, 10, (char*)GetStr(S_SET_TIME)); UI_PrintNum(30, 30, editH, true); UI_DrawString(46, 30, ":"); UI_PrintNum(56, 30, editMin, true);
                int cursX = (cursorIndex == 0) ? 30 : 56; GFX_DrawLine(cursX, 39, cursX+10, 39); needsRedraw = false;
            }
            if (touch != -1) {
                if (touch == 1 || touch == 3) { cursorIndex = !cursorIndex; } else if (touch == 0) { if(cursorIndex == 0 && editH < 23) editH++; if(cursorIndex == 1 && editMin < 59) editMin++; } 
                else if (touch == 2) { if(cursorIndex == 0 && editH > 0) editH--; if(cursorIndex == 1 && editMin > 0) editMin--; } 
                else if (touch == 4) { 
                    RTCC_Set(editY, editM, editD, editH, editMin); 
                    
                    if (numUsers > 0) {
                        current_state = STATE_DOOR_OPEN_MENU;
                        menuIndex = 0;
                    } else {
                        current_state = STATE_TUTORIAL; 
                        targetUserIdx = 0; 
                    }
                    
                    while(buttons[4]) ReadCTMU(); 
                }
                needsRedraw = true; delay(5000);
            } else delay(1000);
        }
        else if (current_state == STATE_TUTORIAL) {
             if (needsRedraw) { SetColor(BLACK); ClearDevice(); SetColor(WHITE); UI_DrawString(5, 5, (char*)GetStr(S_TUTORIAL_TITLE)); GFX_DrawLine(0, 15, 127, 15); UI_DrawString(5, 25, (char*)GetStr(S_TUT_1)); UI_DrawString(5, 35, (char*)GetStr(S_TUT_2)); UI_DrawString(5, 45, (char*)GetStr(S_TUT_3)); UI_DrawString(5, 55, (char*)GetStr(S_PRESS_CENTER)); needsRedraw = false; }
            if(buttons[4]) { delay(5000); UI_ResetGrid(); current_state = STATE_SET_PATTERN; SetRGBs(100, 0, 100); while(buttons[4]) ReadCTMU(); }
        }

        // --- MAIN MENU ---
        else if (current_state == STATE_MENU) {
            int count; const uint8_t* items;
            if (currentUser == 0) { count = MENU_COUNT_ADMIN; items = menuItemsAdmin; } 
            else { if (PERMISSIONS[currentUser]) { count = MENU_COUNT_USER_FULL; items = menuItemsUserFull; } else { count = MENU_COUNT_USER_RESTRICTED; items = menuItemsUserRestricted; } }

            if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE); 
                UI_DrawString(35, 2, (char*)((currentUser==0)?GetStr(S_MENU_ADMIN):GetStr(S_MENU_USER))); 
                
                // Show Remaining Accesses (Bottom Right)
                if (currentUser != 0) {
                     char buf[12];
                     if (ACCESS_TYPE[currentUser] == ACC_ONETIME) {
                         sprintf(buf, "%s 1", (char*)GetStr(S_REMAINING));
                         UI_DrawString(70, 55, buf);
                     } else if (ACCESS_TYPE[currentUser] == ACC_MULTI) {
                         sprintf(buf, "%s %d", (char*)GetStr(S_REMAINING), ACCESS_COUNT[currentUser]);
                         UI_DrawString(70, 55, buf);
                     }
                }
                
                GFX_DrawLine(0, 9, 127, 9);
                for(int i=0; i<count; i++) { int yPos = 12 + (i * 9); if(i == menuIndex) UI_DrawString(2, yPos, ">"); UI_DrawString(10, yPos, (char*)GetStr(items[i])); }
                SetRGBs(0, 0, 255); needsRedraw = false;
            }
            if (touch != -1) {
                if (touch == 0) { if(menuIndex > 0) menuIndex--; else menuIndex = count - 1; } 
                else if (touch == 2) { if(menuIndex < count - 1) menuIndex++; else menuIndex = 0; } 
                else if (touch == 4) { 
                    uint8_t action = items[menuIndex];
                    if (action == S_M_CHANGE_PASS) { UI_ResetGrid(); current_state = STATE_SET_PATTERN; targetUserIdx = (currentUser==0)?0:currentUser; SetRGBs(100, 0, 100); }
                    else if (action == S_M_CREATE_USER) { 
                        if (numUsers >= MAX_USERS) current_state = STATE_ERROR_MSG; 
                        else { 
                            // Default for new user
                            cfgActive = 1;
                            cfgPerm = 0; cfgAccType = ACC_ONETIME; cfgAccCount = 5; 
                            cfgIsNewUser = true; targetUserIdx = numUsers;
                            current_state = STATE_USER_CONFIG; cursorIndex = 0; 
                        } 
                    }
                    else if (action == S_M_ADVANCED) { current_state = STATE_ADVANCED_MENU; menuIndex = 0; }
                    else if (action == S_M_LANG) { 
                        returnState = STATE_MENU; // <--- Tell it to come back to the Admin/User menu
                        current_state = STATE_LANGUAGE_SELECT; 
                    }
                    else if (action == S_M_EXIT) { current_state = STATE_DOOR_OPEN_MENU; menuIndex = 0; }
                    else if (action == S_M_LOGIN_SESSIONS) { current_state = STATE_USER_LOGS; userLogScroll = 0; }
                }
                needsRedraw = true; while(buttons[touch]) ReadCTMU(); delay(5000);
            }
        }
        
        // --- ADVANCED MENU ---
        else if (current_state == STATE_ADVANCED_MENU) {
            // Build Dynamic Menu for Advanced options
            int idx = 0;
            
            // Permissions
            sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_M_PERMS)); 
            dynamicMenuMap[idx] = 1; 
            idx++;
            
            // Admin Logs
            sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_M_LOGS)); 
            dynamicMenuMap[idx] = 2; 
            idx++;
            
            // Login User 1 (Only if created)
            if (numUsers > 1) {
                sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_M_LOGIN_U1)); 
                dynamicMenuMap[idx] = 3; 
                idx++;
            }
            
            // Login User 2 (Only if created)
            if (numUsers > 2) {
                sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_M_LOGIN_U2)); 
                dynamicMenuMap[idx] = 4; 
                idx++;
            }
            
            // Back
            sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_BACK)); 
            dynamicMenuMap[idx] = 99; 
            idx++;
            
            int count = idx;
            
            if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE); 
                UI_DrawString(30, 2, (char*)GetStr(S_M_ADVANCED)); GFX_DrawLine(0, 9, 127, 9);
                for(int i=0; i<count; i++) { 
                    int yPos = 12 + (i * 9); 
                    if(i == menuIndex) UI_DrawString(2, yPos, ">"); 
                    UI_DrawString(10, yPos, dynamicMenuLabels[i]); 
                }
                needsRedraw = false;
            }
            if (touch != -1) {
                if (touch == 0) { if(menuIndex > 0) menuIndex--; else menuIndex = count - 1; } 
                else if (touch == 2) { if(menuIndex < count - 1) menuIndex++; else menuIndex = 0; } 
                else if (touch == 4) {
                    uint8_t action = dynamicMenuMap[menuIndex];
                    if (action == 1) { current_state = STATE_PERMISSIONS; cursorIndex = 0; }
                    else if (action == 2) { current_state = STATE_ADMIN_LOGS; logScroll = 0; }
                    else if (action == 3) { 
                        // Login to User 1
                        currentUser = 1; 
                        current_state = STATE_MENU; 
                        menuIndex = 0; 
                        Log_Add(1, LOG_TYPE_SETTINGS, LOG_STATUS_SUCCESS); // Log successful login
                    }
                    else if (action == 4) { 
                        // Login to User 2
                        currentUser = 2; 
                        current_state = STATE_MENU; 
                        menuIndex = 0; 
                        Log_Add(2, LOG_TYPE_SETTINGS, LOG_STATUS_SUCCESS); // Log successful login
                    }
                    else if (action == 99) { current_state = STATE_MENU; menuIndex = 0; }
                }
                needsRedraw = true; while(buttons[touch]) ReadCTMU(); delay(5000);
            }
        }

        // --- PERMISSIONS LIST ---
        else if (current_state == STATE_PERMISSIONS) {
             if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE); UI_DrawString(30, 5, (char*)GetStr(S_M_PERMS)); GFX_DrawLine(0, 15, 127, 15);
                if (numUsers <= 1) UI_DrawString(10, 30, (char*)GetStr(S_MSG_NO_USERS));
                else {
                    for(int i=1; i<numUsers; i++) { 
                        int y = 25 + ((i-1)*15); char buf[15]; sprintf(buf, "User %d", i); UI_DrawString(20, y, buf);
                        // Show Active Status
                        UI_DrawString(80, y, USER_ACTIVE[i] ? "[x]" : "[ ]");
                        if (cursorIndex == (i-1)) UI_DrawString(10, y, ">");
                    }
                }
                UI_DrawString(5, 55, (char*)GetStr(S_BACK)); needsRedraw = false;
            }
            if (touch != -1) {
                int maxCursor = (numUsers > 1) ? (numUsers - 2) : 0;
                if (touch == 3) { current_state = STATE_ADVANCED_MENU; menuIndex = 0; } 
                else if (numUsers > 1) {
                    if (touch == 0) { if(cursorIndex > 0) cursorIndex--; } 
                    else if (touch == 2) { if(cursorIndex < maxCursor) cursorIndex++; } 
                    else if (touch == 4) { 
                        targetUserIdx = cursorIndex + 1;
                        cfgActive = USER_ACTIVE[targetUserIdx];
                        cfgPerm = PERMISSIONS[targetUserIdx];
                        cfgAccType = ACCESS_TYPE[targetUserIdx];
                        cfgAccCount = ACCESS_COUNT[targetUserIdx];
                        if (cfgAccCount < 2) cfgAccCount = 2; // enforce min
                        cfgIsNewUser = false;
                        current_state = STATE_USER_CONFIG; 
                        cursorIndex = 0;
                    }
                }
                needsRedraw = true; while(buttons[touch]) ReadCTMU(); delay(5000);
            }
        }

        // --- USER CONFIGURATION ---
        else if (current_state == STATE_USER_CONFIG) {
            if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE);
                UI_DrawString(30, 2, (char*)GetStr(S_CONF_TITLE));
                
                // Row 0: Active
                UI_DrawString(10, 12, (char*)GetStr(S_LBL_ACTIVE));
                if (cursorIndex==0) UI_DrawString(2, 12, ">");
                UI_DrawString(50, 12, cfgActive ? "[x]" : "[ ]");

                // Row 1: Chg PW
                UI_DrawString(10, 22, (char*)GetStr(S_LBL_CHG_PW));
                if (cursorIndex==1) UI_DrawString(2, 22, ">");
                UI_DrawString(50, 22, cfgPerm ? "[x]" : "[ ]");

                // Rows 2 & 3: Only if Active
                if (cfgActive) {
                    // Row 2: Access Type
                    UI_DrawString(10, 32, (char*)GetStr(S_ACC_TYPE));
                    if (cursorIndex==2) UI_DrawString(2, 32, ">");
                    if (cfgAccType == ACC_PERMANENT) UI_DrawString(50, 32, (char*)GetStr(S_ACC_PERM));
                    else if (cfgAccType == ACC_ONETIME) UI_DrawString(50, 32, (char*)GetStr(S_ACC_ONCE));
                    else UI_DrawString(50, 32, (char*)GetStr(S_ACC_MULTI));

                    // Row 3: Count
                    if (cfgAccType == ACC_MULTI) {
                        UI_DrawString(10, 42, (char*)GetStr(S_LBL_COUNT));
                        if (cursorIndex==3) UI_DrawString(2, 42, ">");
                        UI_PrintNum(50, 42, cfgAccCount, false);
                    }
                }

                // Row 4: Action
                int yAct = 55;
                if (cursorIndex==4) UI_DrawString(2, yAct, ">");
                UI_DrawString(10, yAct, cfgIsNewUser ? (char*)GetStr(S_NEXT) : (char*)GetStr(S_SAVE));

                needsRedraw = false;
            }

            if (touch != -1) {
                // Nav Up
                if (touch == 0) { 
                    if (cursorIndex > 0) cursorIndex--;
                    // If moving up from 4
                    if (cursorIndex == 3) {
                        if (!cfgActive) cursorIndex = 1; // Skip both if inactive
                        else if (cfgAccType != ACC_MULTI) cursorIndex = 2; // Skip count
                    }
                    else if (cursorIndex == 2 && !cfgActive) cursorIndex = 1; // Safety fallback
                }
                // Nav Down
                else if (touch == 2) { 
                    if (cursorIndex < 4) cursorIndex++;
                    // If moving down from 1
                    if (cursorIndex == 2) {
                        if (!cfgActive) cursorIndex = 4; // Skip to action
                    }
                    else if (cursorIndex == 3 && cfgAccType != ACC_MULTI) cursorIndex = 4; // Skip count
                }
                // Toggle/Change
                else if (touch == 1 || touch == 3) {
                    if (cursorIndex == 0) { 
                        cfgActive = !cfgActive; 
                        // Default to 1-Time on Enable
                        if (cfgActive) cfgAccType = ACC_ONETIME; 
                    }
                    else if (cursorIndex == 1) cfgPerm = !cfgPerm; 
                    else if (cursorIndex == 2 && cfgActive) {
                        if (touch==3) { if(cfgAccType < 2) cfgAccType++; else cfgAccType=0; }
                        else { if(cfgAccType > 0) cfgAccType--; else cfgAccType=2; }
                    }
                    else if (cursorIndex == 3 && cfgActive && cfgAccType == ACC_MULTI) { 
                        // Min value 2
                        if (touch==1 && cfgAccCount < 250) cfgAccCount++;
                        else if (touch==3 && cfgAccCount > 2) cfgAccCount--;
                    }
                }
                // Select
                else if (touch == 4) {
                    if (cursorIndex == 4) { 
                        if (cfgIsNewUser) {
                             current_state = STATE_TUTORIAL; 
                        } else {
                             USER_ACTIVE[targetUserIdx] = cfgActive;
                             PERMISSIONS[targetUserIdx] = cfgPerm;
                             ACCESS_TYPE[targetUserIdx] = cfgAccType;
                             ACCESS_COUNT[targetUserIdx] = cfgAccCount;
                             NVM_WriteAll();
                             current_state = STATE_PERMISSIONS; 
                             cursorIndex = targetUserIdx - 1;
                        }
                    }
                }
                needsRedraw = true; while(buttons[touch]) ReadCTMU(); delay(5000);
            }
        }
        
        else if (current_state == STATE_ADMIN_LOGS) {
            if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE);
                UI_DrawString(30, 5, (char*)GetStr(S_LOGS_TITLE)); GFX_DrawLine(0, 15, 127, 15);
                if (logCount == 0) UI_DrawString(10, 30, (char*)GetStr(S_LOGS_NONE));
                else {
                    for(int i=0; i<3; i++) { 
                        int idx = logScroll + i;
                        if (idx >= logCount) break;
                        int y = 25 + (i*10);
                        LogEntry l = ADMIN_LOGS[idx];
                        char buf[25]; char uStr[3] = "Ad";
                        if (l.userIdx == 1) sprintf(uStr, "G1"); if (l.userIdx == 2) sprintf(uStr, "G2");
                        char tStr[3] = "St"; if (l.type == LOG_TYPE_DOOR) sprintf(tStr, "Dr");
                        char sStr[3] = "XX"; if (l.status == LOG_STATUS_SUCCESS) sprintf(sStr, "OK");
                        sprintf(buf, "%s %02d/%02d %02d:%02d %s %s", uStr, l.mon, l.day, l.hour, l.min, tStr, sStr);
                        UI_DrawString(2, y, buf);
                    }
                }
                UI_DrawString(5, 55, (char*)GetStr(S_BACK)); needsRedraw = false;
            }
            if (touch != -1) {
                if (touch == 3) { current_state = STATE_ADVANCED_MENU; menuIndex = 0; } 
                else if (touch == 2) { if (logScroll < logCount - 1) logScroll++; } 
                else if (touch == 0) { if (logScroll > 0) logScroll--; } 
                needsRedraw = true; while(buttons[touch]) ReadCTMU(); delay(5000);
            }
        }
        else if (current_state == STATE_USER_LOGS) {
            if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE);
                UI_DrawString(20, 5, (char*)GetStr(S_M_LOGIN_SESSIONS)); GFX_DrawLine(0, 15, 127, 15);
                int totalUserLogs = 0;
                for(int k=0; k<logCount; k++) { if (ADMIN_LOGS[k].userIdx == currentUser) totalUserLogs++; }
                if (totalUserLogs == 0) UI_DrawString(10, 30, (char*)GetStr(S_LOGS_NONE));
                else {
                    int foundCount = 0; int displayed = 0;
                    for(int k=0; k<logCount; k++) {
                        if (ADMIN_LOGS[k].userIdx == currentUser) {
                            if (foundCount >= userLogScroll && displayed < 3) {
                                int y = 25 + (displayed*10);
                                LogEntry l = ADMIN_LOGS[k];
                                char buf[25];
                                char tStr[3] = "St"; if (l.type == LOG_TYPE_DOOR) sprintf(tStr, "Dr");
                                char sStr[3] = "XX"; if (l.status == LOG_STATUS_SUCCESS) sprintf(sStr, "OK");
                                sprintf(buf, "%02d/%02d %02d:%02d %s %s", l.mon, l.day, l.hour, l.min, tStr, sStr);
                                UI_DrawString(2, y, buf);
                                displayed++;
                            }
                            foundCount++;
                        }
                    }
                }
                UI_DrawString(5, 55, (char*)GetStr(S_BACK)); needsRedraw = false;
            }
            if (touch != -1) {
                int totalUserLogs = 0;
                for(int k=0; k<logCount; k++) { if (ADMIN_LOGS[k].userIdx == currentUser) totalUserLogs++; }
                if (touch == 3) { current_state = STATE_MENU; menuIndex = 0; } 
                else if (touch == 2) { if (totalUserLogs > 0 && userLogScroll < totalUserLogs - 1) userLogScroll++; } 
                else if (touch == 0) { if (userLogScroll > 0) userLogScroll--; } 
                needsRedraw = true; while(buttons[touch]) ReadCTMU(); delay(5000);
            }
        }
        else if (current_state == STATE_DOOR_OPEN_MENU) {
            int idx = 0;
            if (numUsers > 1 && PASS_LENS[1] > 0 && USER_ACTIVE[1]) { sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_OPEN_AS_G1)); dynamicMenuMap[idx] = 1; idx++; }
            if (numUsers > 2 && PASS_LENS[2] > 0 && USER_ACTIVE[2]) { sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_OPEN_AS_G2)); dynamicMenuMap[idx] = 2; idx++; }
            sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_OPEN_AS_ADMIN)); dynamicMenuMap[idx] = 0; idx++;
            sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_SETTINGS)); dynamicMenuMap[idx] = -1; idx++;
            int count = idx;
            if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE); UI_DrawString(30, 5, (char*)GetStr(S_DOOR_MENU)); GFX_DrawLine(0, 15, 127, 15);
                for(int i=0; i<count; i++) { int yPos = 20 + (i * 9); if(i == menuIndex) UI_DrawString(2, yPos, ">"); UI_DrawString(10, yPos, dynamicMenuLabels[i]); }
                SetRGBs(0, 0, 255); needsRedraw = false;
            }
            if (touch != -1) {
                if (touch == 0) { if(menuIndex > 0) menuIndex--; else menuIndex = count - 1; }
                else if (touch == 2) { if(menuIndex < count - 1) menuIndex++; else menuIndex = 0; }
                else if (touch == 4) { 
                    int action = dynamicMenuMap[menuIndex];
                    if (action == -1) { current_state = STATE_LOGIN_SETTINGS; menuIndex = 0; } 
                    else { targetUserIdx = action; UI_ResetGrid(); current_state = STATE_VERIFY_DOOR; }
                }
                needsRedraw = true; while(buttons[touch]) ReadCTMU(); delay(5000);
            }
        }
        else if (current_state == STATE_LOGIN_SETTINGS) {
            int idx = 0;
            // User 1 (if active)
            if (numUsers > 1 && PASS_LENS[1] > 0 && USER_ACTIVE[1]) { 
                sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_LOGIN_AS_G1)); 
                dynamicMenuMap[idx] = 1; 
                idx++; 
            }
            // User 2 (if active)
            if (numUsers > 2 && PASS_LENS[2] > 0 && USER_ACTIVE[2]) { 
                sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_LOGIN_AS_G2)); 
                dynamicMenuMap[idx] = 2; 
                idx++; 
            }
            
            // Admin
            sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_LOGIN_AS_ADMIN)); 
            dynamicMenuMap[idx] = 0; 
            idx++;
            
            // Language Option (ID 50)
            sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_M_LANG)); 
            dynamicMenuMap[idx] = 50; 
            idx++;

            // Back Option
            sprintf(dynamicMenuLabels[idx], (char*)GetStr(S_BACK)); 
            dynamicMenuMap[idx] = -1; 
            idx++;
            
            int count = idx;
            
            if (needsRedraw) {
                SetColor(BLACK); ClearDevice(); SetColor(WHITE); 
                UI_DrawString(20, 5, (char*)GetStr(S_LOGIN_SETTINGS)); 
                GFX_DrawLine(0, 15, 127, 15);
                
                for(int i=0; i<count; i++) { 
                    int yPos = 20 + (i * 9); 
                    if(i == menuIndex) UI_DrawString(2, yPos, ">"); 
                    UI_DrawString(10, yPos, dynamicMenuLabels[i]); 
                }
                
                SetRGBs(0, 0, 255); // Reset to Blue
                needsRedraw = false;
            }
            
            if (touch != -1) {
                // Scroll Up
                if (touch == 0) { 
                    if(menuIndex > 0) menuIndex--; else menuIndex = count - 1; 
                }
                // Scroll Down
                else if (touch == 2) { 
                    if(menuIndex < count - 1) menuIndex++; else menuIndex = 0; 
                }
                // Select
                else if (touch == 4) { 
                    int action = dynamicMenuMap[menuIndex];
                    
                    if (action == -1) { 
                        // Back
                        current_state = STATE_DOOR_OPEN_MENU; 
                        menuIndex = 0; 
                    } 
                    else if (action == 50) {
                        // Language Select
                        returnState = STATE_LOGIN_SETTINGS;
                        current_state = STATE_LANGUAGE_SELECT;
                    }
                    else { 
                        // Login Selection (0, 1, or 2)
                        targetUserIdx = action; 
                        UI_ResetGrid(); 
                        current_state = STATE_VERIFY_LOGIN; 
                    }
                }
                needsRedraw = true; while(buttons[touch]) ReadCTMU(); delay(5000);
            }
        }
        else if (current_state == STATE_VERIFY_DOOR) {
             if (touch != -1) {
                idleTimer = 0;
                if (!visitedMask[touch] && patternIdx < PATTERN_MAX) { patternBuf[patternIdx] = touch; visitedMask[touch] = true; GFX_DrawNode(btnX[touch], btnY[touch], true); if (patternIdx > 0) { uint8_t prev = patternBuf[patternIdx - 1]; GFX_DrawLine(btnX[prev], btnY[prev], btnX[touch], btnY[touch]); } SetRGBs(255, 255, 0); patternIdx++; }
            } else {
                if (idleTimer == 1) SetRGBs(0, 0, 255);   
                if (patternIdx > 0) {
                    idleTimer++;
                    if (idleTimer > TOUCH_TIMEOUT) {
                        SetColor(BLACK); ClearDevice(); SetColor(WHITE);
                        if (CheckPassword(targetUserIdx)) { 
                            bool accessAllowed = true;
                            if (targetUserIdx != 0) { 
                                if (ACCESS_TYPE[targetUserIdx] == ACC_ONETIME) { 
                                    DeactivateUser(targetUserIdx); 
                                } else if (ACCESS_TYPE[targetUserIdx] == ACC_MULTI) {
                                    if (ACCESS_COUNT[targetUserIdx] > 0) {
                                        ACCESS_COUNT[targetUserIdx]--;
                                        if (ACCESS_COUNT[targetUserIdx] == 1) {
                                            ACCESS_TYPE[targetUserIdx] = ACC_ONETIME;
                                        } 
                                        else if (ACCESS_COUNT[targetUserIdx] == 0) {
                                            DeactivateUser(targetUserIdx);
                                        }
                                        else NVM_WriteAll(); 
                                    } else {
                                        accessAllowed = false; 
                                    }
                                }
                            }
                            if (accessAllowed) {
                                UI_DrawString(25, 25, (char*)GetStr(S_DOOR_UNLOCKED)); SetRGBs(0, 255, 0); Log_Add(targetUserIdx, LOG_TYPE_DOOR, LOG_STATUS_SUCCESS); delay(40000); 
                            } else {
                                UI_DrawString(15, 25, (char*)GetStr(S_ACCESS_DENIED)); SetRGBs(255, 0, 0); Log_Add(targetUserIdx, LOG_TYPE_DOOR, LOG_STATUS_FAIL); delay(40000);
                            }
                        } 
                        else { UI_DrawString(15, 25, (char*)GetStr(S_INCORRECT_PASS)); SetRGBs(255, 0, 0); Log_Add(targetUserIdx, LOG_TYPE_DOOR, LOG_STATUS_FAIL); delay(40000); }
                        current_state = STATE_DOOR_OPEN_MENU; UI_ResetGrid(); idleTimer = 0;
                    }
                }
            }
        }
        else if (current_state == STATE_VERIFY_LOGIN) {
             if (touch != -1) {
                idleTimer = 0;
                if (!visitedMask[touch] && patternIdx < PATTERN_MAX) { patternBuf[patternIdx] = touch; visitedMask[touch] = true; GFX_DrawNode(btnX[touch], btnY[touch], true); if (patternIdx > 0) { uint8_t prev = patternBuf[patternIdx - 1]; GFX_DrawLine(btnX[prev], btnY[prev], btnX[touch], btnY[touch]); } SetRGBs(255, 255, 0); patternIdx++; }
            } else {
                if (idleTimer == 1) SetRGBs(0, 0, 255);   
                if (patternIdx > 0) {
                    idleTimer++;
                    if (idleTimer > TOUCH_TIMEOUT) {
                        bool passOk = CheckPassword(targetUserIdx);
                        if (passOk) { 
                            currentUser = targetUserIdx; menuIndex = 0; current_state = STATE_MENU; Log_Add(targetUserIdx, LOG_TYPE_SETTINGS, LOG_STATUS_SUCCESS); 
                        } 
                        else { 
                            SetColor(BLACK); ClearDevice(); SetColor(WHITE); 
                            UI_DrawString(15, 25, (char*)GetStr(S_INCORRECT_PASS)); 
                            SetRGBs(255, 0, 0); Log_Add(targetUserIdx, LOG_TYPE_SETTINGS, LOG_STATUS_FAIL); delay(40000); current_state = STATE_LOGIN_SETTINGS; 
                        }
                        UI_ResetGrid(); idleTimer = 0;
                    }
                }
            }
        }
        else if (current_state == STATE_SET_PATTERN) {
            if (touch != -1) {
                idleTimer = 0;
                if (!visitedMask[touch] && patternIdx < PATTERN_MAX) { patternBuf[patternIdx] = touch; visitedMask[touch] = true; GFX_DrawNode(btnX[touch], btnY[touch], true); if (patternIdx > 0) { uint8_t prev = patternBuf[patternIdx - 1]; GFX_DrawLine(btnX[prev], btnY[prev], btnX[touch], btnY[touch]); } SetRGBs(255, 255, 0); patternIdx++; }
            } else {
                if (idleTimer == 1) SetRGBs(100, 0, 100); 
                if (patternIdx > 0) {
                    idleTimer++;
                    if (idleTimer > TOUCH_TIMEOUT) {
                        SavePassword(targetUserIdx); 
                        if (targetUserIdx > 0 && targetUserIdx == (numUsers - 1)) currentUser = targetUserIdx;
                        SetColor(BLACK); ClearDevice(); SetColor(WHITE); UI_DrawString(20, 25, (char*)GetStr(S_PASS_SAVED)); SetRGBs(0, 255, 0); delay(20000);
                        current_state = STATE_MENU; menuIndex = 0; UI_ResetGrid(); idleTimer = 0;
                    }
                }
            }
        }
        else if (current_state == STATE_ERROR_MSG) {
             if (needsRedraw) { SetColor(BLACK); ClearDevice(); SetColor(WHITE); UI_DrawString(5, 20, (char*)GetStr(S_MSG_USER_LIMIT_1)); UI_DrawString(5, 30, (char*)GetStr(S_MSG_USER_LIMIT_2)); UI_DrawString(5, 40, (char*)GetStr(S_MSG_USER_LIMIT_3)); SetRGBs(255, 0, 0); needsRedraw = false; }
             if (touch != -1) { current_state = STATE_MENU; while(buttons[touch]) ReadCTMU(); delay(5000); }
        }
    }
    return 0;
}