#include "doomgeneric.h"

#include "display.h"
#include "model_init.h"
#include "edgetx.h"
#include "stdio.h"

#include "board.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "hal/adc_driver.h"
#include "hal/usb_driver.h"
#include "hal/watchdog_driver.h"
#include "hal/rotary_encoder.h"
#include "os/sleep.h"
#include "os/time.h"
#include "pulses/pulses.h"
#include "serial.h"

#ifdef SIMU
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef SIMU
const uint8_t keyboardMap[] = {KEY_ESCAPE,   KEY_USE,       KEY_ENTER,
                               KEY_UPARROW,  KEY_DOWNARROW, KEY_RIGHTARROW,
                               KEY_LEFTARROW};
#else
/*  Index → EnumKeys    → TX15 button → DOOM action
 *  0       KEY_MENU      (n/a)
 *  1       KEY_EXIT      RTN          → KEY_UPARROW   (move forward)
 *  2       KEY_ENTER     ROLL PUSH    → KEY_USE       (open doors)
 *  3       KEY_PAGEUP    PAGE<        → KEY_LEFTARROW (turn left)
 *  4       KEY_PAGEDN    PAGE>        → KEY_RIGHTARROW(turn right)
 *  5-10    (unused on TX15)
 *  11      KEY_MODEL     MDL          → KEY_FIRE      (fire)
 *  12      KEY_TELE      TELE         → KEY_DOWNARROW (move backward)
 *  13      KEY_SYS       SYS          → KEY_ESCAPE    (menu)
 *  14-15   (unused)
 */
const uint8_t keyboardMap[16] = {
    0,              // 0  KEY_MENU   — not on TX15
    KEY_UPARROW,    // 1  KEY_EXIT   — RTN
    KEY_USE,        // 2  KEY_ENTER  — ROLL push
    KEY_LEFTARROW,  // 3  KEY_PAGEUP — PAGE<
    KEY_RIGHTARROW, // 4  KEY_PAGEDN — PAGE>
    0, 0, 0, 0,     // 5-8  unused
    0, 0,            // 9-10 unused
    KEY_FIRE,       // 11 KEY_MODEL  — MDL
    KEY_DOWNARROW,  // 12 KEY_TELE   — TELE
    KEY_ESCAPE,     // 13 KEY_SYS    — SYS
    0, 0             // 14-15 unused
};
#endif

rotenc_t oldRotencValue;

void dg_Create() {
  pwrOn();
  DG_Init();
  if (!sdMounted())
    sdInit();
  generalDefault();
  g_eeGeneral.inactivityTimer = 0;
  setModelDefaults();
  logsInit();

#if defined(DEBUG) && defined(STM32) && !defined(SIMU)
  initSerialPorts();
  if (usbPlugged()) {
    setSelectedUsbMode(USB_SERIAL_MODE);
    serialInit(SP_VCP, serialGetMode(SP_VCP));
    usbStart();
  }
#endif
  // loadRadioSettings();
  resetBacklightTimeout();
  WDG_ENABLE(WDG_DURATION);
  pulsesStart();
  oldRotencValue = rotaryEncoderGetValue();
}

// Gimbal analog values for movement control
static int16_t stickLeftV = 0;   // Left stick vertical: forward/back
static int16_t stickLeftH = 0;   // Left stick horizontal: strafe left/right
static int16_t stickRightH = 0;  // Right stick horizontal: turn left/right

// Export analog values for joystick event generation in i_input.c
// These are in the range -32767 to 32767 as expected by DOOM
int AD_RV = 0;  // Right vertical (not used for turning, but exported for compatibility)
int AD_RH = 0;  // Right horizontal (turning)

// Deadzone threshold (6.5% of full range)
#define STICK_DEADZONE (int16_t)(RESX * 0.065)

// Threshold for triggering movement (20% of full range)
#define STICK_MOVEMENT_THRESHOLD (int16_t)(RESX * 0.20)

void button_update_loop() {
  // Read analog inputs for gimbal sticks
  getADC();
  evalInputs(e_perout_mode_notrainer);
  
  // Read stick positions:
  // Left stick vertical (ADC_MAIN_LV = index 1) for forward/back
  // Left stick horizontal (ADC_MAIN_LH = index 0) for strafing
  // Right stick horizontal (ADC_MAIN_RH = index 3) for turning
  int16_t lv_raw = calibratedAnalogs[ADC_MAIN_LV];
  int16_t lh_raw = calibratedAnalogs[ADC_MAIN_LH];
  int16_t rh_raw = calibratedAnalogs[ADC_MAIN_RH];
  
  // Apply deadzone
  stickLeftV = (lv_raw > STICK_DEADZONE || lv_raw < -STICK_DEADZONE) ? lv_raw : 0;
  stickLeftH = (lh_raw > STICK_DEADZONE || lh_raw < -STICK_DEADZONE) ? lh_raw : 0;
  stickRightH = (rh_raw > STICK_DEADZONE || rh_raw < -STICK_DEADZONE) ? rh_raw : 0;

  // Convert to DOOM joystick range (-32767 to 32767)
  // Note: RESX = 1024, so we multiply by ~32 to get full range
  AD_RV = 0;  // Not using right vertical for now
  AD_RH = -(stickRightH * 32767) / RESX;  // Right horizontal for turning (inverted)

  if (pwrPressed()) {
    boardOff();
  }
}

void DG_SleepMs(uint32_t ms) {
  WDG_RESET();
  sleep_ms(ms);
}

uint32_t DG_GetTicksMs() {
  WDG_RESET();
  return time_get_ms();
}

int DG_GetKey(int* pressed, unsigned char* key) {
  extern boolean menuactive;
  static uint32_t oldKeys = 0;
  static uint8_t analogKeys = 0;  // Bit flags for analog-triggered keys
  
  auto keys = readKeys();

  // Handle physical button presses
  for (auto i = 0; i < (int)sizeof(keyboardMap); i++) {
    if (!keyboardMap[i]) continue;  // skip unmapped keys
    uint32_t k = 1 << i;
    if ((keys & k) && !(oldKeys & k)) {
      *key = keyboardMap[i];
      // In menus, ROLL (KEY_USE) should work as ENTER for selection
      if (menuactive && *key == KEY_USE) {
        *key = KEY_ENTER;
      }
      *pressed = 1;
      oldKeys |= k;
      return 1;
    }
    if (!(keys & k) && (oldKeys & k)) {
      *key = keyboardMap[i];
      // In menus, ROLL (KEY_USE) should work as ENTER for selection
      if (menuactive && *key == KEY_USE) {
        *key = KEY_ENTER;
      }
      *pressed = 0;
      oldKeys = oldKeys & (~k);
      return 1;
    }
  }

  // Handle analog stick inputs (only when not in menu)
  if (!menuactive) {
    uint8_t newAnalogKeys = 0;
    
    // Left stick vertical: forward/back
    if (stickLeftV > STICK_MOVEMENT_THRESHOLD) {
      newAnalogKeys |= 0x01;  // Forward
    } else if (stickLeftV < -STICK_MOVEMENT_THRESHOLD) {
      newAnalogKeys |= 0x02;  // Backward
    }
    
    // Left stick horizontal: strafe left/right
    if (stickLeftH < -STICK_MOVEMENT_THRESHOLD) {
      newAnalogKeys |= 0x04;  // Strafe left
    } else if (stickLeftH > STICK_MOVEMENT_THRESHOLD) {
      newAnalogKeys |= 0x08;  // Strafe right
    }
    
    // Detect changes in analog stick state
    uint8_t changed = analogKeys ^ newAnalogKeys;
    
    if (changed & 0x01) {  // Forward
      *key = KEY_UPARROW;
      *pressed = (newAnalogKeys & 0x01) ? 1 : 0;
      analogKeys = (analogKeys & ~0x01) | (newAnalogKeys & 0x01);
      return 1;
    }
    if (changed & 0x02) {  // Backward
      *key = KEY_DOWNARROW;
      *pressed = (newAnalogKeys & 0x02) ? 1 : 0;
      analogKeys = (analogKeys & ~0x02) | (newAnalogKeys & 0x02);
      return 1;
    }
    if (changed & 0x04) {  // Strafe left
      *key = KEY_STRAFE_L;
      *pressed = (newAnalogKeys & 0x04) ? 1 : 0;
      analogKeys = (analogKeys & ~0x04) | (newAnalogKeys & 0x04);
      return 1;
    }
    if (changed & 0x08) {  // Strafe right
      *key = KEY_STRAFE_R;
      *pressed = (newAnalogKeys & 0x08) ? 1 : 0;
      analogKeys = (analogKeys & ~0x08) | (newAnalogKeys & 0x08);
      return 1;
    }
  }

  return 0;
}
