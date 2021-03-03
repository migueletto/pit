// This code was loosely based on:
// https://github.com/johnrickman/LiquidCrystal_I2C

#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include "i2c.h"
#include "lcdc.h"
#include "debug.h"

// commands
#define LCD_CLEARDISPLAY   0x01
#define LCD_RETURNHOME     0x02
#define LCD_ENTRYMODESET   0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT    0x10
#define LCD_FUNCTIONSET    0x20
#define LCD_SETCGRAMADDR   0x40
#define LCD_SETDDRAMADDR   0x80

// flags for entry mode
#define LCD_ENTRYLEFT           0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01

// flags for display control
#define LCD_DISPLAYON   0x04
#define LCD_CURSORON    0x02
#define LCD_BLINKON     0x01

// flags for display/cursor shift
#define LCD_DISPLAYMOVE 0x08
#define LCD_MOVERIGHT   0x04

// flags for function set
#define LCD_8BITMODE    0x10
#define LCD_2LINE       0x08

// flags for backlight control
#define LCD_BACKLIGHT   0x08

#define En 0x04  // Enable bit
#define Rw 0x02  // Read/Write bit
#define Rs 0x01  // Register select bit

static void lcdc_delayus(int us) {
  usleep(us);
}

static void lcdc_delay(int ms) {
  lcdc_delayus(ms * 1000);
}

static void lcdc_expanderWrite(i2c_provider_t *i2cp, i2c_t *i2c, unsigned char data, int bl) {
  unsigned char b = data;
  if (bl) b |= LCD_BACKLIGHT;
  debug(DEBUG_TRACE, "LCD", "write  0x%02X", b);
  i2cp->write(i2c, &b, 1);
}

static void lcdc_pulseEnable(i2c_provider_t *i2cp, i2c_t *i2c, unsigned char data, int bl) {
  debug(DEBUG_TRACE, "LCD", "pulsE  0x%02X", data);
  lcdc_expanderWrite(i2cp, i2c, data | En, bl);   // En high
  lcdc_delayus(1);        // enable pulse must be >450ns
  debug(DEBUG_TRACE, "LCD", "pulse  0x%02X", data);
  lcdc_expanderWrite(i2cp, i2c, data & ~En, bl);  // En low
  lcdc_delayus(50);       // commands need > 37us to settle
}

static void lcdc_write4bits(i2c_provider_t *i2cp, i2c_t *i2c, unsigned char value, int bl) {
  debug(DEBUG_TRACE, "LCD", "nibble 0x%02X", value);
  lcdc_expanderWrite(i2cp, i2c, value, bl);
  lcdc_pulseEnable(i2cp, i2c, value, bl);
}

// write either command or data
static void lcdc_send(i2c_provider_t *i2cp, i2c_t *i2c, unsigned char value, unsigned char mode, int bl) {
  unsigned char highnib = value & 0xf0;
  unsigned char lownib  = (value << 4) & 0xf0;
  lcdc_write4bits(i2cp, i2c, highnib | mode, bl);
  lcdc_write4bits(i2cp, i2c, lownib  | mode, bl); 
}

static void lcdc_data(i2c_provider_t *i2cp, i2c_t *i2c, unsigned char value, int bl) {
  debug(DEBUG_TRACE, "LCD", "data   0x%02X", value);
  lcdc_send(i2cp, i2c, value, Rs, bl);
}

static void lcdc_command(i2c_provider_t *i2cp, i2c_t *i2c, unsigned char value, int bl) {
  debug(DEBUG_TRACE, "LCD", "cmd    0x%02X", value);
  lcdc_send(i2cp, i2c, value, 0, bl);
}

// high level commands, for the user

void lcdc_init(i2c_provider_t *i2cp, i2c_t *i2c, int cols, int rows) {
  unsigned char displayfunction;

  debug(DEBUG_TRACE, "LCD", "init");
  // SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION!
  // according to datasheet, we need at least 40ms after power rises above 2.7V
  // before sending commands. Arduino can turn on way befer 4.5V so we'll wait 50
  lcdc_delay(50); 
  
  // Now we pull both RS and R/W low to begin commands
  lcdc_expanderWrite(i2cp, i2c, 0, 0); // reset expander
  lcdc_delay(1000);

  // put the LCD into 4 bit mode
  // this is according to the hitachi HD44780 datasheet
  // figure 24, pg 46

  // we start in 8bit mode, try to set 4 bit mode
  lcdc_write4bits(i2cp, i2c, 0x03 << 4, 0);
  lcdc_delayus(4500); // wait min 4.1ms
   
  // second try
  lcdc_write4bits(i2cp, i2c, 0x03 << 4, 0);
  lcdc_delayus(4500); // wait min 4.1ms
   
  // third go!
  lcdc_write4bits(i2cp, i2c, 0x03 << 4, 0); 
  lcdc_delayus(150);
   
  // finally, set to 4-bit interface
  lcdc_write4bits(i2cp, i2c, 0x02 << 4, 0); 

  // set # lines
  displayfunction = (rows > 1) ? LCD_2LINE : 0;
  lcdc_command(i2cp, i2c, LCD_FUNCTIONSET | displayfunction, 0);  

  lcdc_displaycontrol(i2cp, i2c, 1, 0, 0, 0);
  lcdc_entrymode(i2cp, i2c, 1, 0, 0);
  lcdc_clear(i2cp, i2c, 0);
  lcdc_home(i2cp, i2c, 0);
}

void lcdc_finish(i2c_provider_t *i2cp, i2c_t *i2c) {
  debug(DEBUG_TRACE, "LCD", "finish");
  lcdc_clear(i2cp, i2c, 0);
  lcdc_home(i2cp, i2c, 0);
  lcdc_displaycontrol(i2cp, i2c, 0, 0, 0, 0);
}

void lcdc_clear(i2c_provider_t *i2cp, i2c_t *i2c, int bl) {
  debug(DEBUG_TRACE, "LCD", "clear");
  lcdc_command(i2cp, i2c, LCD_CLEARDISPLAY, bl);
  lcdc_delayus(2000);
}

void lcdc_home(i2c_provider_t *i2cp, i2c_t *i2c, int bl) {
  debug(DEBUG_TRACE, "LCD", "home");
  lcdc_command(i2cp, i2c, LCD_RETURNHOME, bl);
  lcdc_delayus(2000);
}

void lcdc_setcursor(i2c_provider_t *i2cp, i2c_t *i2c, int col, int row, int bl) {
  debug(DEBUG_TRACE, "LCD", "setcursor %d,%d", col, row);
  int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
  lcdc_command(i2cp, i2c, LCD_SETDDRAMADDR | (col + row_offsets[row % 4]), bl);
}

void lcdc_displaycontrol(i2c_provider_t *i2cp, i2c_t *i2c, int on, int cursor, int blink, int bl) {
  unsigned char displaycontrol = 0;

  debug(DEBUG_TRACE, "LCD", "displaycontrol on=%d cursor=%d blink=%d", on, cursor, blink);
  if (on) displaycontrol |= LCD_DISPLAYON;
  if (cursor) displaycontrol |= LCD_CURSORON;
  if (blink) displaycontrol |= LCD_BLINKON;

  lcdc_command(i2cp, i2c, LCD_DISPLAYCONTROL | displaycontrol, bl);
}

void lcdc_scrollleft(i2c_provider_t *i2cp, i2c_t *i2c, int bl) {
  lcdc_command(i2cp, i2c, LCD_CURSORSHIFT | LCD_DISPLAYMOVE, bl);
}

void lcdc_scrollright(i2c_provider_t *i2cp, i2c_t *i2c, int bl) {
  lcdc_command(i2cp, i2c, LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT, bl);
}

void lcdc_entrymode(i2c_provider_t *i2cp, i2c_t *i2c, int left, int increment, int bl) {
  unsigned char entrymode = 0;

  debug(DEBUG_TRACE, "LCD", "entrymode left=%d increment=%d", left, increment);
  if (left) entrymode |= LCD_ENTRYLEFT;
  if (increment) entrymode |= LCD_ENTRYSHIFTINCREMENT;

  lcdc_command(i2cp, i2c, LCD_ENTRYMODESET | entrymode, bl);
}

void lcdc_createchar(i2c_provider_t *i2cp, i2c_t *i2c, int location, unsigned char charmap[], int bl) {
  int i;

  location &= 0x7; // we only have 8 locations 0-7
  lcdc_command(i2cp, i2c, LCD_SETCGRAMADDR | (location << 3), bl);
  for (i = 0; i < 8; i++) {
    lcdc_data(i2cp, i2c, charmap[i], bl);
  }
}

void lcdc_write(i2c_provider_t *i2cp, i2c_t *i2c, char *s, int n, int bl) {
  int i;

  for (i = 0; i < n; i++) {
    lcdc_data(i2cp, i2c, s[i], bl);
  }
}
