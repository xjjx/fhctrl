#include <stdio.h>

#include "fhctrl.h"
#include "basics.h"

#include "ftdilcd.h"

void init_lcd( struct LCDScreen* lcd_screen ) {
	lcd_screen->available = lcd_init();
	if (! lcd_screen->available) return;

	char lcdline[16];
	snprintf(lcdline, 16, "%s", lcd_screen->app_name);
	lcd_text(0,0,lcdline);
	snprintf(lcdline, 16, "says HELLO");
	lcd_text(0,1,lcdline);
	lcd_screen->unit = NULL;
}

void update_lcd( struct LCDScreen* lcd_screen ) {
	if (! lcd_screen->available) return;

	Unit* fp = lcd_screen->unit;
	if(!fp) return;

	char line[24];
	snprintf(line, 24, "D%02d P%02d C%02d V%02d",
		fp->id,
		fp->state->program,
		fp->state->channel,
		fp->state->volume
	);
	lcd_text(0,0,line);			// Line 1
	lcd_text(0,1,fp->state->program_name);	// Line 2
	lcd_text(0,2,fp->name);			// Line 3
}

void lcd_set_current_unit ( struct LCDScreen* lcd_screen, Unit* fp ) {
	if ( ! lcd_screen->available ) return;
	lcd_screen->unit = fp;
}
