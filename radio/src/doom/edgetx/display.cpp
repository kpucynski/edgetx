
#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomgeneric.h"
#include "edgetx.h"
#include "hal/watchdog_driver.h"

// Access the LCD frame buffers directly
extern pixel_t LCD_FIRST_FRAME_BUFFER[];

uint16_t* LCDFrameBuffer;

uint16_t display_get_width(void) {
  return LCD_W;
}

uint16_t display_get_height(void) {
  return LCD_H;
}

void DG_Init() {
  memset(LCD_FIRST_FRAME_BUFFER, 0, LCD_W * LCD_H * sizeof(pixel_t));
  lcdSetInitalFrameBuffer(LCD_FIRST_FRAME_BUFFER);
  lcdInit();
  backlightInit();
  BACKLIGHT_ENABLE();

  // Blue screen = DOOM is loading (confirms LCD pipeline works)
  uint16_t* fb = (uint16_t*)LCD_FIRST_FRAME_BUFFER;
  for (int i = 0; i < LCD_W * LCD_H; i++)
    fb[i] = 0x001F;
  SCB_CleanDCache();
}

void DG_StartFrame() {
  WDG_RESET();
  LCDFrameBuffer = (uint16_t*)LCD_FIRST_FRAME_BUFFER;
}

void DG_EndFrame() {
  SCB_CleanDCache();
}

void DG_ShowError(const char* message) {
  // Red screen = error (most likely missing DOOM1.WAD on SD card)
  uint16_t* fb = (uint16_t*)LCD_FIRST_FRAME_BUFFER;
  for (int i = 0; i < LCD_W * LCD_H; i++)
    fb[i] = 0xF800;
  SCB_CleanDCache();

  LED_ERROR_BEGIN();
  int n = 30;

  while (--n > 0) {
    WDG_RESET();
    DG_SleepMs(1000);
  }

#ifndef SIMU
  boardOff();
#else
  exit(1);
#endif
}
