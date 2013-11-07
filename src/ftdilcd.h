#ifndef __ftdilcd_h__
#define __ftdilcd_h__

#include <stdbool.h>

// LCD via FTDI support
bool lcd_init();
void lcd_text(short x, short y, char* txt);
void lcd_close();

#endif /* __ftdilcd_h__ */
