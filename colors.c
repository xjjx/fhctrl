#include <cdk/cdk.h>


int main() {
	CDKSWINDOW *colours;
	CDKSCREEN *cdkscreen;
	WINDOW *screen;


	screen = initscr();
	cdkscreen = initCDKScreen (screen);

	/* Start CDK Colors */
	initCDKColor();

	colours = newCDKSwindow (cdkscreen, 0, 0, 33, 64, "</U/63>LOG", 33, FALSE, FALSE);

	short i;
	short j;
	char txt[128];
	char tmp[32];
	for (i = 1; i < 64; i += 8) {
		*txt = '\0';
		for(j=0; j < 8; j++) {
			sprintf(tmp, "</B/%02d>   %02d   <!%02d>", i+j, i+j, i+j);
			strcat(txt, tmp);
		}
		addCDKSwindow(colours, txt, 0);
	}

	drawCDKSwindow(colours, FALSE);

	sleep(30);

	destroyCDKSwindow(colours);
	destroyCDKScreen(cdkscreen);
	endCDK();
}
