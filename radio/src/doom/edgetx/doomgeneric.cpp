#include "doomgeneric.h"

#include "display.h"
#include "model_init.h"
#include "edgetx.h"
#include "stdio.h"

#include "board.h"
#include "doomkeys.h"
#include "doomtype.h"
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
 *  2       KEY_ENTER     ROLL PUSH    → KEY_ENTER     (fire / select)
 *  3       KEY_PAGEUP    PAGE<        → KEY_LEFTARROW (turn left)
 *  4       KEY_PAGEDN    PAGE>        → KEY_RIGHTARROW(turn right)
 *  5-10    (unused on TX15)
 *  11      KEY_MODEL     MDL          → KEY_USE       (open doors)
 *  12      KEY_TELE      TELE         → KEY_DOWNARROW (move backward)
 *  13      KEY_SYS       SYS          → KEY_ESCAPE    (menu)
 *  14-15   (unused)
 */
const uint8_t keyboardMap[16] = {
    0,              // 0  KEY_MENU   — not on TX15
    KEY_UPARROW,    // 1  KEY_EXIT   — RTN
    KEY_ENTER,      // 2  KEY_ENTER  — ROLL push
    KEY_LEFTARROW,  // 3  KEY_PAGEUP — PAGE<
    KEY_RIGHTARROW, // 4  KEY_PAGEDN — PAGE>
    0, 0, 0, 0,     // 5-8  unused
    0, 0,            // 9-10 unused
    KEY_USE,        // 11 KEY_MODEL  — MDL
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

int AD_RV = 0;
int AD_RH = 0;

#define deadzone (32767 * 0.065)

int map_values(int x, int in_min, int in_max, int out_min, int out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void button_update_loop() {
  // getADC();
  // evalInputs(e_perout_mode_notrainer);
  // AD_RV = map_values(calibratedAnalogs[2], -RESX, RESX, -32767, 32767);
  // AD_RH = map_values(calibratedAnalogs[3], -RESX, RESX, 32767, -32767);

  // if (AD_RV < deadzone && AD_RV > -deadzone) {
  //   AD_RV = 0;
  // }

  // if (AD_RH < deadzone && AD_RH > -deadzone) {
  //   AD_RH = 0;
  // }

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
  auto keys = readKeys();

  for (auto i = 0; i < (int)sizeof(keyboardMap); i++) {
    if (!keyboardMap[i]) continue;  // skip unmapped keys
    uint32_t k = 1 << i;
    if ((keys & k) && !(oldKeys & k)) {
      *key = keyboardMap[i];
      if (!menuactive && *key == KEY_ENTER) {
        *key = KEY_FIRE;
      }
      *pressed = 1;
      oldKeys |= k;
      return 1;
    }
    if (!(keys & k) && (oldKeys & k)) {
      *key = keyboardMap[i];
      if (!menuactive && *key == KEY_ENTER) {
        *key = KEY_FIRE;
      }
      *pressed = 0;
      oldKeys = oldKeys & (~k);

      return 1;
    }
  }

  return 0;
}
