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

#define LEFT_MARGIN	0	/* Where the plugin boxes start */
#define RIGHT_MARGIN	80	/* Where the infoboxes start */
#define TOP_MARGIN	6	/* How much space is reserved for the logo */
#define SONGWIN_WIDTH	42	/* Song window width */
#define SONGWIN_HEIGHT	24	/* Song window height */
#define CHUJ		38	/* TODO: what it is ;-) */
#define LABEL_LENGHT CHUJ+11	/* TODO: what it is ;-) */

struct labelbox {
	CDKLABEL *label;
	uint8_t fstid;
};

CDKSCROLL *song_list;
bool quit = false;
extern void send_ident_request(); // From fhctrl.c
extern void song_send(short SongNumber);
struct Song* song_new();
extern void song_update(short SongNumber);
extern void update_config();
extern int cpu_load();
extern struct FSTState* state_new();
extern struct FSTPlug* fst_next(struct FSTPlug* prev);
extern void fst_send(struct FSTPlug* fp);
extern void send_dump_request(short id);

int get_status_color ( struct FSTPlug* fp ) {
	switch ( fp->state->state ) {
		case FST_STATE_BYPASS: return 58;
		case FST_STATE_ACTIVE:
			switch ( fp->type ) {
				case FST_TYPE_DEVICE: return 60;
				case FST_TYPE_PLUGIN: return 59;
			}
			break;
		case FST_NA: return 0;
	}

	return 0;
}

static void show_log (CDKSCREEN *cdkscreen) {
	const char *filename = "/tmp/fhctrl.log";

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

static int get_value_dialog (CDKSCREEN *cdkscreen, char *title, char *label, char **values, int default_value, int count) {
	CDKITEMLIST *valuelist;
	char ttitle[20];
	char tlabel[20];
	int ret = 0, choice;

	snprintf(ttitle, sizeof ttitle, "<C>%s:", title);
	snprintf(tlabel, sizeof tlabel, "</U/05>%s:", label);

	valuelist = newCDKItemlist ( cdkscreen, CENTER, CENTER, ttitle, tlabel, values, count, default_value, TRUE, FALSE);
	if (!valuelist) return 0;

	/* Activate the widget. */
	choice = activateCDKItemlist (valuelist, NULL);

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
		ret = choice + 1;
	}
	destroyCDKItemlist(valuelist);

	return ret;
}

static int edit_selector(CDKSCREEN *cdkscreen, struct FSTPlug **fst) {
	unsigned short i, count, plug;
	struct FSTPlug *fp;
	struct FSTState *fs;
	char *values[128];
	unsigned short valfstmap[128];

	/* New temporary state */
	fs = state_new();

	/* Allocate memory for strings */
	for (i=0; i < 128; i++) values[i] = alloca( 40 );

	/* Select Plugin */
	for (i=0, count=0; i < 128; i++) {
		if (! (fp = fst[i]) ) continue;
		valfstmap[count] = i;
		snprintf(values[count++], 40, "<C>%s", fp->name);
	}
	plug = get_value_dialog (cdkscreen, "Select device", "Device", values, 0, count);
	if (!plug) return 0;
	fp = fst[ valfstmap[--plug] ];

	/* Select State */
	snprintf(values[0], 40, "<C>Bypass");
	snprintf(values[1], 40, "<C>Active");
	fs->state = get_value_dialog (cdkscreen, "Select state", "State", values, fp->state->state, 2);
	if (!fs->state) return 0;
	--fs->state;
	
	/* Select Channel */
	for (i=0, count=0; i < 16; i++) snprintf(values[count++], 40, "<C>Channel %d", i);
	fs->channel = get_value_dialog (cdkscreen, "Select channel", "Channel", values, fp->state->channel, count);
	if (!fs->channel) return 0;
	--fs->channel;

	/* Select Preset */
	for (i=0, count=0; i < 128; i++) snprintf(values[count++], 40, "<C>Preset %d", i);
	fs->program = get_value_dialog (cdkscreen, "Select program", "Preset", values, fp->state->program, count);
	if (!fs->program) return 0;
	--fs->program;

	/* Select Volume */
	if ( fp->type == FST_TYPE_PLUGIN ) {
		for (i=0, count=0; i < 128; i++) snprintf(values[count++], 40, "<C>%d", i);
		fs->volume = get_value_dialog (cdkscreen, "Select volume", "Volume", values, fp->state->volume, count);
		if (!fs->volume) return 0;
		--fs->volume;
	}

	*fp->state = *fs;

	fst_send(fp);
	send_dump_request(fp->id);

	return 1;
}

void update_selector(struct labelbox *selector, struct FSTPlug *fp) {
	char text[2][LABEL_LENGHT];
	char* ptext[2] = { text[0], text[1] };
	char chtxt[3];

	if (fp->state->channel == 17) {
		strcpy(chtxt, "--");
	} else {
		snprintf(chtxt, sizeof chtxt, "%02d", fp->state->channel);
	}

	snprintf(text[0], LABEL_LENGHT,"</U/%d>%03d %-23s | %s | %02d<!05>",
		get_status_color(fp), 
		fp->id,
		fp->name,
		chtxt,
		fp->state->volume
	);
	snprintf(text[1], LABEL_LENGHT,"#%02d - %-24s", fp->state->program, fp->state->program_name);
	setCDKLabelMessage(selector->label, ptext, 2);
	drawCDKLabel(selector->label, TRUE);
}

void handle_light (CDKLABEL *label, bool* gui_in, bool* state) {
	if (*state != *gui_in) {
		*state = *gui_in;
		setCDKLabelBackgroundColor(label, (*state) ? "</B/24>" : "</B/64>");
		drawCDKLabel(label, TRUE);
	}
	*gui_in = false;
}

void nfhc (struct CDKGUI *gui) {
	short i, j;
	int lm = 0, tm = 0;
	bool midi_in_state = false;
	bool ctrl_midi_in_state = false;
	bool sysex_midi_in_state = false;
	CDKSCREEN *cdkscreen;
	CDKLABEL *top_logo;
	CDKLABEL *midi_light;
	CDKLABEL *ctrl_light;
	CDKLABEL *sysex_light;
	CDKSLIDER *cpu_usage;
	WINDOW *screen;
	char *mesg[9];
	struct labelbox selector[16];
	struct FSTPlug **fst = gui->fst;
	struct Song *song = *gui->song_first;

	/* Initialize the Cdk screen.  */
	screen = initscr();
	cdkscreen = initCDKScreen (screen);

	/* Disable cursor */
	curs_set(0);

	/* Start CDK Colors */
	initCDKColor();

	/* top_logo label setup */
	mesg[0] ="</56> ______   __  __     ______     ______   ______     __";
	mesg[1] ="</56>/\\  ___\\ /\\ \\_\\ \\   /\\  ___\\   /\\__  _\\ /\\  == \\   /\\ \\ ";
	mesg[2] ="</56>\\ \\  __\\ \\ \\  __ \\  \\ \\ \\____  \\/_/\\ \\/ \\ \\  __<   \\ \\ \\____";
	mesg[3] ="</56> \\ \\_\\    \\ \\_\\ \\_\\  \\ \\_____\\    \\ \\_\\  \\ \\_\\ \\_\\  \\ \\_____\\";
	mesg[4] ="</56>  \\/_/     \\/_/\\/_/   \\/_____/     \\/_/   \\/_/ /_/   \\/_____/ proudly done by Xj";
	top_logo = newCDKLabel (cdkscreen, LEFT_MARGIN+10, TOP, mesg, 5, FALSE, FALSE);
	drawCDKLabel(top_logo, TRUE);

	/* Create Song List */
	song_list = newCDKScroll (
		cdkscreen, RIGHT_MARGIN, TOP_MARGIN, RIGHT, SONGWIN_HEIGHT, SONGWIN_WIDTH,
		"</U/63>Select song:<!05>", 0, 0, FALSE, A_NORMAL, TRUE, FALSE
	);

	while ( song ) {
		addCDKScrollItem(song_list, song->name);
		song = song->next;
	}

//	bindCDKObject(vSCROLL, song_list, 'q', kurwa_jebana, NULL);
	drawCDKScroll(song_list, TRUE);

	cpu_usage = newCDKSlider (
		cdkscreen, RIGHT_MARGIN, TOP_MARGIN+25, "DSP LOAD [%]", "",
		A_REVERSE|' ', SONGWIN_WIDTH-4, 0, 0, 100, 1, 10, TRUE, FALSE
	);
	drawCDKSlider(cpu_usage, FALSE);

	mesg[0] = "MIDI IN";
	midi_light = newCDKLabel (cdkscreen, RIGHT_MARGIN, TOP_MARGIN+29, mesg, 1, TRUE, FALSE);
	setCDKLabelBackgroundColor(midi_light, "</B/64>");
	drawCDKLabel(midi_light, TRUE);

	mesg[0] = "SYSEX IN";
 	sysex_light = newCDKLabel (cdkscreen, RIGHT_MARGIN + 10, TOP_MARGIN+29, mesg, 1, TRUE, FALSE);
	setCDKLabelBackgroundColor(sysex_light, "</B/64>");
	drawCDKLabel(sysex_light, TRUE);

	mesg[0] = "CTRL IN";
	ctrl_light = newCDKLabel (cdkscreen, RIGHT_MARGIN + 21, TOP_MARGIN+29, mesg, 1, TRUE, FALSE);
	setCDKLabelBackgroundColor(ctrl_light, "</B/64>");
	drawCDKLabel(ctrl_light, TRUE);

	/* SELECTOR init - same shit for all boxes */
	{
		char text[2][LABEL_LENGHT];
		char* ptext[2] = { text[0], text[1] };

		for (i=0; i < 2; i++) cleanChar(text[i], CHUJ, '-');
		for (i  = 0; i < 16; i++, tm += 4) {
			if (i == 8) { lm = CHUJ+2; tm = 0; }

			selector[i].label = newCDKLabel (cdkscreen, LEFT_MARGIN+lm, TOP_MARGIN+(tm), ptext, 2, TRUE, FALSE);
			drawCDKLabel (selector[i].label, TRUE);
		}
	}

	noecho();
	filter();
	wtimeout(top_logo->win, 300);
//	cbreak();
	while(! quit) {
		struct FSTPlug *fp = NULL;
		// For our boxes
		for (i = 0; i < 16; i++) {
			fp = fst_next ( fp );
			if (!fp) break;
			if (selector[i].fstid == fp->id && !fp->change) continue;
			
			fp->change = false;
			gui->lcd_need_update = true;
			update_selector(&selector[i], fp);
		}

		if (gui->idle_cb) gui->idle_cb();

		setCDKSliderValue(cpu_usage, cpu_load());
		drawCDKSlider(cpu_usage, FALSE);

		handle_light ( midi_light, &gui->midi_in, &midi_in_state );
		handle_light ( ctrl_light, &gui->ctrl_midi_in, &ctrl_midi_in_state );
		handle_light ( sysex_light, &gui->sysex_midi_in, &sysex_midi_in_state );

		// Redraw
		drawCDKLabel(top_logo, TRUE);
		drawCDKScroll(song_list, TRUE);
		for (i = 0; i < 16; i++) drawCDKLabel(selector[i].label, TRUE);
//		refreshCDKScreen(cdkscreen);

		j = wgetch(top_logo->win);
		switch(j) {
			case 'q': quit=true; break;
		 	case 's': // Set Song
				setCDKScrollHighlight(song_list, A_REVERSE);
				tm = activateCDKScroll(song_list, NULL);
				song_send(tm);
				setCDKScrollHighlight(song_list, A_NORMAL);
				break;
			case 'u': // Update Song
				setCDKScrollHighlight(song_list, A_REVERSE);
				tm = activateCDKScroll(song_list, NULL);
				song_update(tm);
				setCDKScrollHighlight(song_list, A_NORMAL);
				break;
			case 'i': send_ident_request(); break;
			case 'n':
				song = song_new();
				addCDKScrollItem(song_list, song->name);
				break;
			case 'w': update_config(); break; // Update config file
			case 'e': edit_selector (cdkscreen, fst); break;
			case 'l': show_log(cdkscreen); break;
		}
	}

	/* Clean up */
	destroyCDKLabel(top_logo);
	destroyCDKScroll(song_list);
	destroyCDKSlider(cpu_usage);
	for (i = 0; i < 16; i++) destroyCDKLabel(selector[i].label);
	destroyCDKScreen(cdkscreen);
	endCDK();
}
