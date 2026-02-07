# Pattern Lock Security System for PIC24F

## About The Project 
This project demonstrates a smartphone-style Pattern Lock Security System implemented on the Microchip MPLAB Starter Kit for PIC24F. It utilizes the board's integrated capacitive touch pads to record and validate unlock patterns, providing visual feedback via an OLED display and RGB LEDs.
Capacitive touch sensors are inherently sensitive to **environmental noise** (humidity, skin moisture, electrical interference).  
To ensure reliable input, this project implements a robust input qualification system tailored for a pattern-lock interface.

![pattern-lock](/images/1.jpeg)

## Features
### 1. Pattern-Based Authentication

Instead of a numeric keypad, the system uses a graphical pattern lock similar to Android smartphones.

* **Touch Input:** Users input passwords by touching 5 distinct nodes (Up, Down, Left, Right, Center) on a touch screen.
* **Visual Feedback:** As the user touches nodes, lines are drawn on the screen connecting them.
* **Validation:** The system compares the drawn path against stored patterns in Flash memory.
* **Security:** It supports a maximum pattern length of 5 nodes.

### 2. User Roles & Management

The system supports up to **3 distinct users** with hierarchical privileges:

* **Administrator (User 0):**
    * Has full access to all menus.
    * Can create new users.
    * Can edit permissions and access rules for other users.
    * Can view all system logs.


* **Standard/Guest Users (User 1 & 2):**
    * Have restricted menu access.
    * Can primarily unlock the door.
    * Can view their own login history.
    * Can change their own password *only if* the Admin has granted that permission.



### 3. Advanced Access Control Logic

This is the core "Smart Lock" feature. The Admin can assign specific access types to each user:

* **Permanent Access:** The user can unlock the door indefinitely.
* **One-Time Access:** The user can unlock the door exactly once. After a successful unlock, the account is automatically deactivated.
* **Multi-Time Access:** The user is granted a specific number of unlocks (configurable between 2 and 250). The system decrements the counter upon every successful entry. When the count reaches zero, access is revoked.

### 4. Event Logging & Auditing

The system maintains a persistent log history stored in Flash memory to track usage.

* **Log Details:** Each entry records the **User ID**, **Event Type** (Door Unlock vs. Settings Login), **Status** (Success vs. Fail), and a **Timestamp**.
* **Admin View:** The Admin can view a scrollable list of all recent activities (e.g., "Guest 1 12/01 14:30 Door OK").
* **User View:** Users can view a "Login Sessions" screen that filters the logs to show only their own activity.
* **Capacity:** It stores the last 15 events, shifting old logs out as new ones arrive.

![Auditing](/images/2.jpeg)

### 5. System Configuration

* **Real-Time Clock (RTCC):** The system tracks date and time, which is used for timestamping logs. There is a dedicated UI for setting the Date and Time on boot.
* **Multi-Language Support:** The system can toggle between **English** and **German**, using the string definitions found in `en.po` and `de.po`.
* **Persistent Storage (NVM):** All critical data (passwords, user settings, logs, language choice) is saved to the microcontroller's Flash memory. This ensures settings are retained even if power is lost.

### 6. User Interface (UI) & UX

* **State Machine Architecture:** The application uses a robust state machine to handle navigation (Boot  Welcome  Menu  Config  Logs, etc.).
* **Dynamic Menus:** The menus change dynamically. For example, the "Door Open" menu only shows "Open as Guest 1" if Guest 1 is currently active and has a password set.
* **Tutorial Mode:** When a new user is created or the system is in inital launch, a tutorial guides them through the process of drawing a pattern.
* **RGB LED Status:**
    * **Blue:** Idle / Menus.
    * **Yellow:** Drawing patterns (touching buttons).
    * **Green:** Success (Door Unlocked / Login OK).
    * **Red:** Failure (Access Denied / Wrong Pattern).
    * **Purple:** Recording new password.



### 7. Hardware Handling

* **Debouncing:** The code implements software debouncing to prevent false touches or noise from registering as input.
* **Idle Timeout:** If a user starts drawing a pattern but stops, the system resets after a timeout.
* **Screen Drawing:** Custom graphics routines are implemented to draw strings, numbers, and lines using Bresenham's line algorithm on the 128x64 display.


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
`main.c` – Application logic (pattern lock state machine, graphics, noise filtering)

`TouchSense.c` – Handles low-level CTMU initialization, calibration, and reading of the 5 capacitive touch pads.

`SH1101A.c` – Driver for the OLED display, managing PMP communication and screen buffer updates.

`RGBLeds.c` – Controls the RGB LED color mixing using Output Compare (PWM) timers.

`PIC24FStarter.h` - Configuration bits and hardware definitions for the specific starter kit board.

`en.po` - Localization file containing string definitions for English

`de.po` - Localization file containing string definitions for Deutsch

`po2c.py` - Takes human-readable localization files(e.g. `en.po`) as input and outputs `languages.c` and `languages.h`