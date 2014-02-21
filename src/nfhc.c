// nfhc.c
//
// Shamefully done by blj <blindluke@gmail.com>
//             and by  xj <xj@wp.pl>
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <stdio.h>
#include <unistd.h>
#include <cdk.h>
#include "fhctrl.h"
#include "log.h"

#define LOGO_HEIGHT	5	/* How much space is reserved for the logo */
#define SONGWIN_WIDTH	42	/* Song window width */
#define SONGWIN_HEIGHT	24	/* Song window height */
#define BOX_WIDTH	38	/* Label Box width */
#define BOX_HEIGHT	4	/* Label Box height */
#define LABEL_LENGHT BOX_WIDTH + 11 /* TODO: what it is ;-) */

struct box {
	struct box* next;
	CDKLABEL *label;
	uint8_t unitid;
};

struct light {
	CDKLABEL* label;
	bool state;
	bool* gui_in;
};

CDKSCREEN* cdkscreen;

int get_status_color ( Unit* fp ) {
	switch ( fp->state->state ) {
		case UNIT_STATE_BYPASS: return 58;
		case UNIT_STATE_ACTIVE:
			switch ( fp->type ) {
				case UNIT_TYPE_DEVICE: return 60;
				case UNIT_TYPE_PLUGIN: return 59;
			}
			break;
		case UNIT_NA: return 0;
	}

	return 0;
}

void nLOG ( char *msg, void *user_data ) {
	CDKSWINDOW *sw = (CDKSWINDOW*) user_data;
	if ( ! sw ) return;

	addCDKSwindow ( sw, msg, BOTTOM );
}

static void show_log () {
	const char *filename = get_logpath();

	CDKVIEWER *viewer = newCDKViewer (
		cdkscreen,
		22, /* xpos */
		2, /* ypos */
		30, /* height */
		100, /* width */
		NULL, /* CDK_CONST char **buttonList */
		0, /* buttonCount */
		A_REVERSE, /* buttonHighlight */
		TRUE, /* box */
		FALSE /*shadow */
	);
	/* Set up the viewer title, and the contents to the widget. */
	char vTitle[256];
	snprintf (vTitle, sizeof(vTitle), "<C></B/21>Filename:<!21></22>%20s<!22!B>", filename);

	char vFile[256];
	snprintf (vFile, sizeof ( vFile ), "<F=%s>", filename);

	char* FF[1] = { vFile };
	setCDKViewer (viewer, vTitle, FF, 1, A_REVERSE, TRUE, TRUE, TRUE);

	drawCDKViewer ( viewer, TRUE );
	activateCDKViewer ( viewer, NULL );

	destroyCDKViewer ( viewer);
}

void refresh_song_list ( CDKSCROLL* sl, Song** songs ) {
	/* Clear List */
	setCDKScrollItems ( sl, NULL, 0, FALSE );

	/* Fill list again */
	Song* song;
	for ( song = *songs; song; song = song->next )
		addCDKScrollItem ( sl, song->name );
}

static void change_song_name ( CDKSCROLL* sl, Song** songs, Song *song ) {
	if (! song) return;

	const char *title = "Change Song name";
	char ttitle[20];
	snprintf(ttitle, sizeof ttitle, "<C>%s:", title);

	CDKENTRY *entry = newCDKEntry ( cdkscreen, CENTER, CENTER, ttitle, "",
		A_REVERSE, ' ', vMIXED, sizeof(song->name), 1, sizeof(song->name) - 1, TRUE, FALSE );

	setCDKEntryValue ( entry, song->name );
	drawCDKEntry ( entry, TRUE );
	char* value = activateCDKEntry ( entry, NULL );
	if ( value && strcmp(song->name, value) ) {
		strncpy( song->name, value, sizeof (song->name) );
		refresh_song_list ( sl, songs ); 
	}

	destroyCDKEntry ( entry );
}

static int get_value_dialog ( char *title, char *label, char **values, int default_value, int count) {
	CDKITEMLIST *valuelist;
	char ttitle[20];
	char tlabel[20];

	snprintf(ttitle, sizeof ttitle, "<C>%s:", title);
	snprintf(tlabel, sizeof tlabel, "</U/05>%s:", label);

	valuelist = newCDKItemlist ( cdkscreen, CENTER, CENTER, ttitle, tlabel, values, count, default_value, TRUE, FALSE);
	if (!valuelist) return 0;

	/* Activate the widget. */
	int choice = activateCDKItemlist (valuelist, NULL);

	/* Check how they exited from the widget. */
	if (valuelist->exitType == vNORMAL) {
/*
		char *mesg[9];
		mesg[0] = "<C></U>Current selection:";
		mesg[1] = values[choice];
		mesg[2] = "";
		mesg[3] = "<C>Press any key to continue.";
		popupLabel (ScreenOf (valuelist), mesg, 4);
*/
	}
	destroyCDKItemlist(valuelist);

	return ++choice;
}

static int edit_selector ( FHCTRL* fhctrl ) {
	unsigned short i, count, plug;
	Unit** unit = fhctrl->unit;
	Unit* fp;
	UnitState* fs;
	char *values[128];
	unsigned short valunitmap[128];

	/* New temporary state */
	fs = state_new();

	/* Allocate memory for strings */
	for (i=0; i < 128; i++) values[i] = alloca( 40 );

	/* Select Plugin */
	for (i=0, count=0; i < 128; i++) {
		if (! (fp = unit[i]) ) continue;
		valunitmap[count] = i;
		snprintf(values[count], 40, "<C>%s", fp->name);
		count++;
	}
	if (count == 0) return 0;
	plug = get_value_dialog ("Select device", "Device", values, 0, count);
	if (!plug) return 0;
	fp = unit[ valunitmap[--plug] ];

	/* Select State */
	snprintf(values[0], 40, "<C>Bypass");
	snprintf(values[1], 40, "<C>Active");
	fs->state = get_value_dialog ("Select state", "State", values, fp->state->state, 2);
	if (!fs->state) return 0;
	--fs->state;
	
	/* Select Channel */
	for (i=0, count=0; i < 16; i++) snprintf(values[count++], 40, "<C>Channel %d", i);
	fs->channel = get_value_dialog ("Select channel", "Channel", values, fp->state->channel, count);
	if (!fs->channel) return 0;
	--fs->channel;

	/* Select Preset */
	for (i=0, count=0; i < 128; i++) snprintf(values[count++], 40, "<C>Preset %d", i);
	fs->program = get_value_dialog ("Select program", "Preset", values, fp->state->program, count);
	if (!fs->program) return 0;
	--fs->program;

	/* Select Volume */
	if ( fp->type == UNIT_TYPE_PLUGIN ) {
		for (i=0, count=0; i < MAX_UNITS; i++) snprintf(values[count++], 40, "<C>%d", i);
		fs->volume = get_value_dialog ("Select volume", "Volume", values, fp->state->volume, count);
		if (!fs->volume) return 0;
		--fs->volume;
	}

	*fp->state = *fs;

	fhctrl_unit_send ( fhctrl, fp, "GUI" );
	send_dump_request ( fhctrl, fp->id );

	return 1;
}

void box_update ( struct box *box, Unit *fp) {
	char chtxt[3];
	if ( fp->state->channel == 17 ) {
		strcpy(chtxt, "--");
	} else {
		snprintf(chtxt, sizeof chtxt, "%02d", fp->state->channel);
	}

	char text[2][BOX_WIDTH + 11];
	snprintf ( text[0], sizeof text[0], "</U/%d>%03d %-23s | %s | %02d<!05>",
		get_status_color(fp), 
		fp->id,
		fp->name,
		chtxt,
		fp->state->volume
	);
	snprintf ( text[1], sizeof text[1], "#%02d - %-24s", fp->state->program, fp->state->program_name );

	char* ptext[2] = { text[0], text[1] };
	setCDKLabelMessage ( box->label, ptext, 2 );
	drawCDKLabel ( box->label, TRUE );
}

void box_cleanup ( struct box** first ) {
	struct box* box = *first;
	int i = 0;
	while ( box ) {
		struct box* next = box->next;
		destroyCDKLabel ( box->label );
		free ( box );
		box = next;
		i++;
	}
	*first = NULL;
//	LOG ( "DESTROY %d", i );
}

void handle_slider ( CDKSLIDER *slider, int new_value ) {
	if ( new_value == getCDKSliderValue( slider ) ) return;
	setCDKSliderValue(slider, new_value);
	drawCDKSlider(slider, FALSE);
}

void handle_light ( struct light* light ) {
	bool* in = light->gui_in;
	if ( light->state != *in ) {
		light->state = *in;
		setCDKLabelBackgroundColor(light->label, (light->state) ? "</B/24>" : "</B/64>");
		drawCDKLabel(light->label, FALSE);
	}
	*in = false;
}

void init_light ( struct light* light, const char* name, bool* gui_in, int y, int x ) {
	char* mesg[1] = { (char*) name };
	light->label = newCDKLabel ( cdkscreen, y, x, mesg, 1, FALSE, FALSE );
	setCDKLabelBackgroundColor ( light->label, "</B/64>" );
	drawCDKLabel ( light->label, TRUE );
	light->state = false;
	light->gui_in = gui_in;
}

struct box* box_new ( struct box** first, int x, int y ) {
	struct box* new = malloc ( sizeof ( struct box ) );

	char text[2][BOX_WIDTH + 1];
	cleanChar(text[0], BOX_WIDTH, '-');
	cleanChar(text[1], BOX_WIDTH, '-');

	char* ptext[2] = { text[0], text[1] };
	new->label = newCDKLabel ( cdkscreen, x, y, ptext, 2, TRUE, FALSE );
	drawCDKLabel ( new->label, TRUE );

	/* If this is not first then bind to list */
	if ( *first ) {
		struct box* b;
		for ( b = *first; b->next; b = b->next );
		b->next = new;
	} else {
		*first = new;
	}

	new->next = NULL;
	new->unitid = 0;

	return new;
}

/* Return left margin */
short box_init ( struct box** first ) {
	int rows, cols;
	getmaxyx ( cdkscreen->window, rows, cols );
	cols -= SONGWIN_WIDTH + 1 + BOX_WIDTH;
	rows -= 5 + BOX_HEIGHT;
	short lm, tm;
	for ( lm = 0; lm < cols; lm += BOX_WIDTH + 1 )
		for ( tm = LOGO_HEIGHT; tm < rows; tm += BOX_HEIGHT )
			box_new ( first, lm, tm );
	return lm;
}

/* Return true if something changed */
static bool box_bind_to_unit ( struct box* box_first, Unit** unit ) {
	bool ret = false;
	Unit *fp;
	struct box* box;
	for ( fp = unit_next(unit,NULL), box = box_first;
	      fp && box;
	      fp = unit_next(unit,fp), box = box->next
	) {
		if ( fp->change || box->unitid != fp->id ) {
			fp->change = false;
			box_update ( box, fp );
			ret = true;
		}
	}
	return ret;
}

CDKLABEL* init_logo ( int x, int y ) {
	char* mesg[5];
	mesg[0] ="</56> ______   __  __     ______     ______   ______     __";
	mesg[1] ="</56>/\\  ___\\ /\\ \\_\\ \\   /\\  ___\\   /\\__  _\\ /\\  == \\   /\\ \\ ";
	mesg[2] ="</56>\\ \\  __\\ \\ \\  __ \\  \\ \\ \\____  \\/_/\\ \\/ \\ \\  __<   \\ \\ \\____";
	mesg[3] ="</56> \\ \\_\\    \\ \\_\\ \\_\\  \\ \\_____\\    \\ \\_\\  \\ \\_\\ \\_\\  \\ \\_____\\ proudly";
	mesg[4] ="</56>  \\/_/     \\/_/\\/_/   \\/_____/     \\/_/   \\/_/ /_/   \\/_____/ done by Xj / Blj";
	CDKLABEL* logo = newCDKLabel (cdkscreen, x, y, mesg, 5, FALSE, FALSE);
	return logo;
}

CDKLABEL* init_foot ( int x, int y ) {
	char* mesg[2];
	mesg[0] ="</32> q - quit, s - set song, n - new song, u - update song, g - change song name, i - send ident request, w - write config";
	mesg[1] = "</32> e - edit, l - show full log, r - resize/refresh";
	CDKLABEL* foot = newCDKLabel (cdkscreen, LEFT, BOTTOM, mesg, 2, FALSE, FALSE);
	drawCDKLabel(foot, TRUE);
	return foot;
}

CDKSWINDOW* init_log_win ( int x, int y ) {
	CDKSWINDOW *sw = newCDKSwindow ( cdkscreen, x, y, 7, SONGWIN_WIDTH + 1, NULL, 10, TRUE, FALSE);
	set_logcallback ( nLOG, sw );
//	set_logcallback ( NULL, NULL );
	return sw;
}

void move_log_win ( CDKSWINDOW** sw, int x, int y ) {
	set_logcallback ( NULL, NULL );

	CDKSWINDOW* newsw = newCDKSwindow ( cdkscreen, x, y, 7, SONGWIN_WIDTH + 1, NULL, 10, TRUE, FALSE);
	drawCDKSwindow ( newsw, TRUE );

	int lines = 0;
	chtype** info = getCDKSwindowContents ( *sw, &lines );
	if ( lines > 0 ) {
		char* info2[lines];
		short i;
		for ( i = 0; i < lines; i++ )
			info2[i] = chtype2Char ( info[i] );
		setCDKSwindowContents ( newsw, info2, lines );
		freeCharList ( info2, lines );
	}

	set_logcallback ( nLOG, newsw );

//	destroyCDKSwindow ( *sw );

	*sw = newsw;
}

void nfhc ( FHCTRL* fhctrl ) {
	/* Initialize the Cdk screen.  */
	WINDOW *screen = initscr();
	cdkscreen = initCDKScreen (screen);

	/* Disable cursor */
	curs_set(0);

	/* Start CDK Colors */
	initCDKColor();

	/* top_logo label setup */
	CDKLABEL* top_logo = init_logo ( LEFT, TOP );

	/* BOX init - same shit for all boxes */
	struct box* box_first = NULL;
	short left_margin = box_init ( &box_first );

	/* Lights */
	struct light midi_light, sysex_light, ctrl_light;
	init_light ( &midi_light, "MIDI IN", &fhctrl->gui.midi_in, left_margin, TOP );
	init_light ( &sysex_light, "SYSEX IN", &fhctrl->gui.sysex_midi_in, left_margin + 10, TOP );
	init_light ( &ctrl_light, "CTRL IN", &fhctrl->gui.ctrl_midi_in, left_margin + 21, TOP );

	/* CPU usage */
	CDKSLIDER *cpu_usage = newCDKSlider (
		cdkscreen, left_margin - 1, 1, "DSP LOAD [%]", "",
		A_REVERSE|' ', SONGWIN_WIDTH, 0, 0, 100, 1, 10, TRUE, FALSE
	);
	
	/* Song List */
	CDKSCROLL *song_list = newCDKScroll (
		cdkscreen, left_margin, LOGO_HEIGHT, RIGHT, SONGWIN_HEIGHT, SONGWIN_WIDTH,
		"</U/63>Select song:<!05>", 0, 0, FALSE, A_NORMAL, TRUE, FALSE
	);
	refresh_song_list ( song_list, fhctrl->songs );

	/* Log window */
	CDKSWINDOW *short_log = init_log_win ( left_margin, LOGO_HEIGHT+SONGWIN_HEIGHT );

	/* Foot */
	CDKLABEL* foot = init_foot ( LEFT, BOTTOM );

	/* Configure keyboard ( bind to top_logo ) */
	noecho();
	filter();
	wtimeout(top_logo->win, 300);

	/* Main loop */
	bool need_redraw = true;
	while ( true ) {
		if ( box_bind_to_unit ( box_first, fhctrl->unit ) )
			fhctrl->gui.lcd_need_update = true;

		fhctrl_idle ( fhctrl );

		handle_light ( &midi_light );
		handle_light ( &ctrl_light );
		handle_light ( &sysex_light );

		handle_slider ( cpu_usage, cpu_load ( fhctrl ) );

		// Redraw
		if (need_redraw) {
			need_redraw = false;
			drawCDKLabel(top_logo, TRUE);
			drawCDKScroll(song_list, TRUE);
			drawCDKSwindow ( short_log, TRUE );
			struct box* box;
			for (box=box_first; box; box=box->next )
				drawCDKLabel(box->label, TRUE);
			drawCDKSlider(cpu_usage, FALSE);
			drawCDKLabel(foot, TRUE);
//			refreshCDKScreen(cdkscreen);
		}

		int c = wgetch(top_logo->win);
		switch (c) {
			short r;
			case 'q': goto cleanup;
		 	case 's': // Set Song
				setCDKScrollHighlight(song_list, A_REVERSE);
				r = activateCDKScroll(song_list, NULL);
				fhctrl_song_send (fhctrl, r);
				setCDKScrollHighlight(song_list, A_NORMAL);
				break;
			case 'u': // Update Song
				setCDKScrollHighlight(song_list, A_REVERSE);
				r = activateCDKScroll(song_list, NULL);
				song_update( song_get( fhctrl->songs, r), fhctrl->unit );
				setCDKScrollHighlight (song_list, A_NORMAL);
				break;
			case 'i': send_ident_request( fhctrl ); break;
			case 'n': // New song
				need_redraw = true;
				Song *song = song_new (fhctrl->songs, fhctrl->unit);
				change_song_name ( song_list, fhctrl->songs, song );
				break;
			case 'g': // Change song name
				need_redraw = true;
				r = getCDKScrollCurrent ( song_list );
				change_song_name ( song_list, fhctrl->songs, song_get ( fhctrl->songs, r ) );
				break;
			case 'w': update_config( fhctrl ); break; // Update config file
			case 'e': // Edit unit
				edit_selector (fhctrl);
				need_redraw = true;
				break;
			case 'l': // Show log
				show_log();
				need_redraw = true;
				break;
			case 'r': ;
//			case KEY_RESIZE:
				int rows, cols, lm, tm;
				getmaxyx ( cdkscreen->window, rows, cols );
				cols -= SONGWIN_WIDTH + BOX_WIDTH + 1;
				rows -= LOGO_HEIGHT + BOX_HEIGHT;
				struct box** pbox = &box_first;
				for ( lm = 0; lm < cols; lm += BOX_WIDTH + 1 ) {
					for ( tm = LOGO_HEIGHT; tm < rows; tm += BOX_HEIGHT ) {
						struct box* box = *pbox;
						if ( box ) {
							moveCDKLabel ( box->label, lm, tm, FALSE, FALSE );
						} else {
							box = box_new ( &box_first, lm, tm );
						}
						pbox = &(box->next);
					}
				}
				if ( *pbox ) box_cleanup ( pbox );

				moveCDKScroll ( song_list, lm, LOGO_HEIGHT, FALSE, FALSE );
				// FIXME: this have bug:
				//moveCDKSwindow ( short_log, lm, LOGO_HEIGHT+SONGWIN_HEIGHT, FALSE, FALSE);
				move_log_win ( &short_log, lm, LOGO_HEIGHT+SONGWIN_HEIGHT );
				moveCDKLabel ( foot, LEFT, BOTTOM, FALSE, FALSE );
				need_redraw = true;
				break;
		}
	}

cleanup:
	destroyCDKLabel(top_logo);
	destroyCDKScroll(song_list);
	destroyCDKSlider(cpu_usage);
	set_logcallback ( NULL, NULL );
	destroyCDKSwindow(short_log);
	box_cleanup ( &box_first );
	destroyCDKScreen(cdkscreen);
	endCDK();
}
