//		nfhc.c
//		
//		Shamefully done by blj <blindluke@gmail.com>
//		
//		This program is distributed in the hope that it will be useful,
//		but WITHOUT ANY WARRANTY; without even the implied warranty of
//		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//		

#include <stdio.h>
#include <unistd.h>
#include <cdk.h>
#include "fhctrl.h"

#define LEFT_MARGIN	0	/* Where the plugin boxes start */
#define RIGHT_MARGIN	80	/* Where the infoboxes start */
#define TOP_MARGIN	6	/* How much space is reserved for the logo */
#define LOGWIN_WIDTH	42
#define CHUJ		38
#define LABEL_LENGHT CHUJ+11

struct labelbox {
	CDKLABEL *label;
	uint8_t fstid;
};

CDKSWINDOW *logwin = NULL;
CDKSCROLL *song_list;
bool quit = false;
extern void send_ident_request(); // From fhctrl.c
extern void song_send(short SongNumber);
struct Song* song_new();
extern void song_update(short SongNumber);
extern void update_config();
extern int cpu_load();
extern struct FSTState* state_new();
extern void fst_send(struct FSTPlug* fp);
extern void send_dump_request(short id);
int state_color[3] = { 58, 59, 0 };

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
	fs->state =	 get_value_dialog (cdkscreen, "Select state", "State", values, fp->state->state, 2);
	if (!fs->state) return 0;
	--fs->state;

	/* Select Channel */
	for (i=0, count=0; i < 15; i++) snprintf(values[count++], 40, "<C>Channel %d", i);
	fs->channel = get_value_dialog (cdkscreen, "Select channel", "Channel", values, fp->state->channel, count);
	if (!fs->channel) return 0;
	--fs->channel;

	/* Select Preset */
	for (i=0, count=0; i < 128; i++) snprintf(values[count++], 40, "<C>Preset %d", i);
	fs->program = get_value_dialog (cdkscreen, "Select program", "Preset", values, fp->state->program, count);
	if (!fs->program) return 0;
	--fs->program;

	/* Select Volume */
	for (i=0, count=0; i < 128; i++) snprintf(values[count++], 40, "<C>%d", i);
	fs->volume = get_value_dialog (cdkscreen, "Select volume", "Volume", values, fp->state->volume, count);
	if (!fs->volume) return 0;
	--fs->volume;

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
		state_color[fp->state->state], 
		fp->id,
		fp->name,
		chtxt,
		fp->state->volume
	);
	snprintf(text[1], LABEL_LENGHT,"#%02d - %-24s", fp->state->program, fp->state->program_name);
	setCDKLabelMessage(selector->label, ptext, 2);
	drawCDKLabel(selector->label, TRUE);
}

void nLOG(char *fmt, ...) {
	char info[LOGWIN_WIDTH-2];
	va_list args;

	va_start(args, fmt);

	vsnprintf(info, sizeof(info), fmt, args);
	if (logwin != NULL) {
		addCDKSwindow(logwin, info, 0);
	} else {
		printf("%s\n", info);
	}

	va_end(args);
}

void nfhc (struct CDKGUI *gui) {
	short i, j;
	int lm = 0, tm = 0;
	bool midi_in_state = false;
	bool ctrl_midi_in_state = false;
	CDKSCREEN	*cdkscreen;
	CDKLABEL	*top_logo;
	CDKLABEL	*midi_light;
	CDKLABEL	*ctrl_light;
	CDKSLIDER	*cpu_usage;
	WINDOW		*screen;
	char		*mesg[9];
	struct labelbox selector[16];
	struct FSTPlug **fst = gui->fst;
	struct FSTPlug *fp;
	struct Song *song = *gui->song_first;

	/* Initialize the Cdk screen.  */
	screen = initscr();
	cdkscreen = initCDKScreen (screen);

	/* Disable cursor */
	curs_set(0);

	/* Start CDK Colors */
	initCDKColor();

	/* top_logo label setup */
	mesg[0] = "</56> ______	  __  __	 ______		______	 ______		__";
	mesg[1] = "</56>/\\	 ___\\ /\\ \\_\\ \\	  /\\  ___\\   /\\__  _\\ /\\  == \\   /\\ \\";
	mesg[2] = "</56>\\ \\  __\\ \\ \\  __ \\  \\ \\ \\____	\\/_/\\ \\/ \\ \\  __<	 \\ \\ \\____";
	mesg[3] = "</56> \\ \\_\\	 \\ \\_\\ \\_\\	 \\ \\_____\\	 \\ \\_\\  \\ \\_\\ \\_\\  \\ \\_____\\";
	mesg[4] = "</56>  \\/_/		\\/_/\\/_/	 \\/_____/	   \\/_/   \\/_/ /_/   \\/_____/ proudly done by xj, 2012";
	top_logo = newCDKLabel (cdkscreen, LEFT_MARGIN+10, TOP, mesg, 5, FALSE, FALSE);
	drawCDKLabel(top_logo, TRUE);

	logwin = newCDKSwindow (cdkscreen, RIGHT_MARGIN, TOP_MARGIN, 15, LOGWIN_WIDTH, "</U/63>LOG", 17, TRUE, FALSE);
	drawCDKSwindow(logwin, TRUE);

	/* Create Song List */
	song_list = newCDKScroll ( cdkscreen, RIGHT_MARGIN, TOP_MARGIN+17, RIGHT, 8, 
		LOGWIN_WIDTH-1, "</U/63>Select song preset:<!05>", 0, 0, FALSE, A_NORMAL, TRUE, FALSE);

	while(song) {
		addCDKScrollItem(song_list, song->name);
		song = song->next;
	}
//	bindCDKObject(vSCROLL, song_list, 'q', kurwa_jebana, NULL);
	drawCDKScroll(song_list, TRUE);

	cpu_usage = newCDKSlider ( cdkscreen, RIGHT_MARGIN, TOP_MARGIN+25, "DSP LOAD [%]", "", A_REVERSE|' ', 
				LOGWIN_WIDTH-4, 0, 0, 100, 1, 10, TRUE, FALSE);
	drawCDKSlider(cpu_usage, FALSE);

	mesg[0] = "MIDI IN";
	midi_light = newCDKLabel (cdkscreen, RIGHT_MARGIN, TOP_MARGIN+29, mesg, 1, TRUE, FALSE);
	setCDKLabelBackgroundColor(midi_light, "</B/64>");
	drawCDKLabel(midi_light, TRUE);

	mesg[0] = "CTRL IN";
	ctrl_light = newCDKLabel (cdkscreen, RIGHT_MARGIN + 10, TOP_MARGIN+29, mesg, 1, TRUE, FALSE);
	setCDKLabelBackgroundColor(ctrl_light, "</B/64>");
	drawCDKLabel(ctrl_light, TRUE);

	/* SELECTOR init - same shit for all boxes */
	{
		char text[2][LABEL_LENGHT];
		char* ptext[2] = { text[0], text[1] };

		for(i=0; i < 2; i++) cleanChar(text[i], CHUJ, '-');
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
	// For our boxes
		for (i = j = 0; i < 16; i++) {
			// Get next FST
			while (j < 128) {
				fp = fst[j++];
				if (!fp) continue;
				if (selector[i].fstid == fp->id && !fp->change) break;

				fp->change = false;
				gui->lcd_need_update = true;
				update_selector(&selector[i], fp);

				break;
			}
		}

		if (gui->idle_cb) gui->idle_cb();

		setCDKSliderValue(cpu_usage, cpu_load());
		drawCDKSlider(cpu_usage, FALSE);

		if (midi_in_state != gui->midi_in) {
			midi_in_state = gui->midi_in;
			setCDKLabelBackgroundColor(midi_light, (midi_in_state) ? "</B/24>" : "</B/64>");
			drawCDKLabel(midi_light, TRUE);
		}
		gui->midi_in = false;

		if (ctrl_midi_in_state != gui->ctrl_midi_in) {
			ctrl_midi_in_state = gui->ctrl_midi_in;
			setCDKLabelBackgroundColor(ctrl_light, (ctrl_midi_in_state) ? "</B/24>" : "</B/64>");
			drawCDKLabel(ctrl_light, TRUE);
		}
		gui->ctrl_midi_in = false;

		// Redraw
		drawCDKLabel(top_logo, TRUE);
		drawCDKSwindow(logwin, TRUE);
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
		}
	}

	/* Clean up */
	destroyCDKLabel(top_logo);
	destroyCDKSwindow(logwin);
	logwin = NULL;
	destroyCDKScroll(song_list);
	destroyCDKSlider(cpu_usage);
	for (i = 0; i < 16; i++) destroyCDKLabel(selector[i].label);
	destroyCDKScreen(cdkscreen);
	endCDK();
}
