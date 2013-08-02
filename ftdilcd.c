#include <stdio.h>
#include <string.h>
#include <ftdi.h>

#include "ftdilcd.h"
#include "log.h"


/*
 * bitbang I/O pin mappings 
 * 
 * #define PIN_TXD 0x01
 * #define PIN_RXD 0x02
 * #define PIN_RTS 0x04
 * #define PIN_CTS 0x08
 * #define PIN_DTR 0x10
 * #define PIN_DSR 0x20
 * #define PIN_DCD 0x40
 * #define PIN_RI  0x80
 */

/* From Jacek K.
TXD(D0) (pin11) D4 D0 (0x01)
RXD(D1) (pin12) D5 D1 (0x02)
RTS(D2) (pin13) D6 D2 (0x04)
CTS(D3) (pin14) D7 D3 (0x08)
DTR(D4) (pin6)  EN D4 (0x10)
DSR(D5) (pin5)  RW D5 (0x20)
DCD(D6) (pin4)  RS D6 (0x40)
*/

#define FTDI_BAUDRATE 921600
#define FTDI_VENDOR 0x0403
#define FTDI_PRODUCT 0x6001
#define LCD_EN 0x10
#define LCD_RW 0x20
#define LCD_RS 0x40
#define LCD_WIDTH  16
#define LCD_HEIGHT 4

#define LCD_CMD_CLEAR        0x01
#define LCD_CMD_RETURN_HOME  0x02
#define LCD_CMD_CURSOR_RIGHT 0x06
#define LCD_CMD_DISPON_NOCUR 0x0C
#define LCD_CMD_DISPON_SOLID 0x0E
#define LCD_CMD_DISPON_BLINK 0x0F
#define LCD_CMD_4BIT_1LINE   0x20
#define LCD_CMD_4BIT_2LINE   0x28

#define LCD_LINE1 0x80
#define LCD_LINE2 LCD_LINE1+64
#define LCD_LINE3 LCD_LINE1+LCD_WIDTH
#define LCD_LINE4 LCD_LINE2+LCD_WIDTH

struct ftdi_context* ftdic;

enum LCD_MODE {
  LCD_CMD,
  LCD_DATA
};

static void lcd_write (enum LCD_MODE lcd_mode, char data) {
   unsigned char buf[4];
   unsigned char portControl = 0;
   int f;

  if (lcd_mode == LCD_DATA) portControl |= LCD_RS;

   buf[0] = ((data >> 4) & 0x0F) | portControl | LCD_EN;
   buf[1] = ((data >> 4) & 0x0F) | portControl;
   buf[2] = (data & 0x0F) | portControl | LCD_EN;
   buf[3] = (data & 0x0F) | portControl;

   f = ftdi_write_data(ftdic, buf, 4);

   if (f < 0) {
      LOG("unable to open ftdi device: %d (%s)", f, ftdi_get_error_string(ftdic));
      return;
   }
}

static void lcd_cmd(char cmd) {
   lcd_write(LCD_CMD, cmd);
}

static bool lcd_pos(short x, short y) {
   int ca; // Cursor Address

   if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
      return false;

   switch(y) {
      case 0: ca = LCD_LINE1; break;
      case 1: ca = LCD_LINE2; break;
      case 2: ca = LCD_LINE3; break;
      case 3: ca = LCD_LINE4; break;
      default: return false;
   }
   ca += x;

   lcd_cmd(ca);

   return true;
}

void lcd_text(short x, short y, char* txt) {
   int i;
   char* blank = " ";

   // Set Cursor at line
   if (! lcd_pos(0, y))
      return;

   for(i=0; i<LCD_WIDTH; i++) {
      if (i >= x && i < strlen(txt)) {
         lcd_write(LCD_DATA, txt[i]);
      } else {
         lcd_write(LCD_DATA, *blank);
      }
   }

}


bool lcd_init() {
    int f;
    ftdic = ftdi_new();
    
    /* Initialize context for subsequent function calls */
    ftdi_init(ftdic);

    ftdi_set_interface(ftdic, INTERFACE_A);
    f = ftdi_usb_open(ftdic, FTDI_VENDOR, FTDI_PRODUCT);
    if (f < 0) {
        LOG("FTDI: Can't open device %d", f);
	free(ftdic);
        return false;
    }

    f = ftdi_set_baudrate(ftdic, FTDI_BAUDRATE);
    if (f < 0) {
        LOG("FTDI: Can't set baudrate to %d", FTDI_BAUDRATE);
	free(ftdic);
        return false;
    }

    ftdi_set_bitmode(ftdic, 0xFF, BITMODE_BITBANG);

    lcd_cmd(LCD_CMD_CLEAR);
    lcd_cmd(LCD_CMD_4BIT_2LINE);
    lcd_cmd(LCD_CMD_CURSOR_RIGHT);
    lcd_cmd(LCD_CMD_DISPON_NOCUR);

    return true;
}

void lcd_close() {
   if (ftdic) {
	ftdi_deinit(ftdic);
	ftdi_free(ftdic);
   }
}

