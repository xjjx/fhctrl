//      xj-interface.c
//      
//      Shamefully done by blj <blindluke@gmail.com>
//      
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//      

#include <stdio.h>
#include <unistd.h>
#include <cdk/cdk.h>
#include "fhctrl.h"

#define LEFT_MARGIN     0   /* Where the plugin boxes start */
#define RIGHT_MARGIN    80  /* Where the infoboxes start */
#define TOP_MARGIN      7   /* How much space is reserved for the logo */
#define LOGWIN_WIDTH    40
#define CHUJ 38
#define LABEL_LENGHT CHUJ+11

struct labelbox {
    CDKLABEL    *label;
    uint8_t fstid;
};

CDKSWINDOW *logwin;
CDKSCROLL *song_list;
bool quit = false;
extern void send_ident_request();
extern void update_lcd();
int state_color[3] = { 58, 59, 57 };

static int get_selector_1(CDKSCREEN *cdkscreen) {
    char    *title  = "<C>Set a new value:";
    char    *label  = "</U/05>Values:";
    CDKITEMLIST *valuelist = 0;
    char    *values[5], *mesg[9];
    int     choice;

    /* Create the choice list. */
    /* *INDENT-EQLS* */
    values[0]      = "<C>value 1";
    values[1]      = "<C>value 2";
    values[2]      = "<C>value 3";
    values[3]      = "<C>value 4";
    values[4]      = "<C>value 5";

    /* Create the itemlist widget. */
    valuelist = newCDKItemlist (
        cdkscreen, CENTER, CENTER,
        title, label, values, 5, 
        0, /* index of the default value */
        TRUE, FALSE
    );

    /* Is the widget null? if so, pass fail code to parent */
    if (valuelist == 0) {
        return 0;
    }

    /* Activate the widget. */
    choice = activateCDKItemlist (valuelist, 0);

    /* Check how they exited from the widget. */
    if (valuelist->exitType == vESCAPE_HIT) {
        mesg[0] = "<C>You hit ESC. No value selected.";
        mesg[1] = "";
        mesg[2] = "<C>Press any key to continue.";
        popupLabel (ScreenOf (valuelist), mesg, 3);
    }
    else if (valuelist->exitType == vNORMAL) {
        mesg[0] = "<C></U>Current selection:";
        mesg[1] = values[choice];
        mesg[2] = "";
        mesg[3] = "<C>Press any key to continue.";
        popupLabel (ScreenOf (valuelist), mesg, 4);
    }

    return (choice + 1);
}

void nLOG(char *fmt, ...) {
   char info[LOGWIN_WIDTH];
   va_list args;

   va_start(args, fmt);
   vsnprintf(info, sizeof(info), fmt, args);
   addCDKSwindow(logwin, info, 0);
}

void nfhc(struct Song *song_first, struct FSTPlug **fst) {
    short i, f, j;
    bool lcd_update = false;
    CDKSCREEN       *cdkscreen;
    CDKLABEL        *top_logo;
    WINDOW          *screen;
    char            *mesg[9];
    char            *text[2];
    struct labelbox selector[16];
    struct FSTPlug *fp;
    struct Song *song;

    /* Initialize the Cdk screen.  */
    screen = initscr();
    cdkscreen = initCDKScreen (screen);

    /* Disable cursor */
    curs_set(0);

    /* Start CDK Colors */
    initCDKColor();

    /* top_logo label setup */
    mesg[0] = "</56> ______  ______   ______  __  __   ______   ______   ______  ";
    mesg[1] = "</56>/\\  ___\\/\\  ___\\ /\\__  _\\/\\ \\_\\ \\ /\\  __ \\ /\\  ___\\ /\\__  _\\ ";
    mesg[2] = "</56>\\ \\  __\\\\ \\___  \\\\/_/\\ \\/\\ \\  __ \\\\ \\ \\/\\ \\\\ \\___  \\\\/_/\\ \\/ ";
    mesg[3] = "</56> \\ \\_\\   \\/\\_____\\  \\ \\_\\ \\ \\_\\ \\_\\\\ \\_____\\\\/\\_____\\  \\ \\_\\ ";
    mesg[4] = "</56>  \\/_/    \\/_____/   \\/_/  \\/_/\\/_/ \\/_____/ \\/_____/   \\/_/      proudly done by xj, 2012";
    top_logo = newCDKLabel (cdkscreen, LEFT_MARGIN+10, TOP, mesg, 5, FALSE, FALSE);
    drawCDKLabel(top_logo, TRUE);

    logwin = newCDKSwindow (cdkscreen, RIGHT_MARGIN, TOP_MARGIN, 8, LOGWIN_WIDTH, "</U/63>LOG", 10, TRUE, FALSE);
    drawCDKSwindow(logwin, TRUE);

    /* Create Song List */
    song_list = newCDKScroll ( cdkscreen, RIGHT_MARGIN, TOP_MARGIN+10, RIGHT, 8, 
          LOGWIN_WIDTH, "</U/63>Select song preset:<!05>", 0, 0, FALSE, A_REVERSE, TRUE, FALSE);

    song = song_first;
    while(song) {
       addCDKScrollItem(song_list, song->name);
       song = song->next;
    }
    drawCDKScroll(song_list, TRUE);

    /* SELECTOR init - same shit for all boxes */
    for(i=0; i<2; i++) text[i] = malloc( sizeof(char) * LABEL_LENGHT );

    int lm = 0, tm = 0;
    for (i  = 0; i < 16; i++, tm += 4) {
        if (i == 8) {
           lm = CHUJ+2;
           tm = 0;
	}

	for(j=0; j < 2; j++) cleanChar(text[j], CHUJ, '-');

        selector[i].label = newCDKLabel (cdkscreen, LEFT_MARGIN+lm, TOP_MARGIN+(tm), text, 2, TRUE, FALSE);
        drawCDKLabel (selector[i].label, TRUE);
    }

    int k;
    timeout(300);
    cbreak();
    noecho();
    while(! quit) {
       // For our boxes
       for (i = f = 0; i < 16; i++) {
          // Get next FST
          while (f < 128) {
             fp = fst[f++];
             if (!fp) continue;
             if (selector[i].fstid == fp->id && !fp->change)
                break;

             fp->change = false;
             lcd_update = true;

             snprintf(text[0], LABEL_LENGHT,"</U/%d>%03d %-23s | %02d | %02d<!05>",
                state_color[fp->state->state], 
                fp->id,
                fp->name,
                fp->state->channel,
                fp->state->volume
             );
             snprintf(text[1], LABEL_LENGHT,"#%02d - %-24s", fp->state->program, fp->state->program_name);

             setCDKLabelMessage(selector[i].label, text, 2);

	     break;
          }
       }
       if (lcd_update) {
          lcd_update = false;
          update_lcd();
       }
       //usleep(300000);
       k = getch();
       if(k == 'q') quit = true;
       refreshCDKScreen(cdkscreen);
    }

//    get_selector_1 (cdkscreen);
    
    /* Clean up */
    destroyCDKScreen(cdkscreen);
    destroyCDKLabel(top_logo);
    destroyCDKSwindow(logwin);
    destroyCDKScroll(song_list);
    for (i = 0; i < 16; i++) destroyCDKLabel(selector[i].label);

    endCDK();
}

