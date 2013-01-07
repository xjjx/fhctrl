#include <cdk.h>
#include <string.h>

int main (int argc, char* argv[]) {
	CDKSCREEN *cdkscreen;
	WINDOW *screen;

	screen = initscr();
	cdkscreen = initCDKScreen (screen);

	/* Start CDK Colors */
	initCDKColor();

	CDKSWINDOW *colours =
		newCDKSwindow (cdkscreen, 0, 0, 33, 64, "", 33, FALSE, FALSE);

	short i;
	short j;
	char txt[164];
	char tmp[32];
	for (i = 1; i < 64; i += 8) {
		*txt = '\0';
		for(j=0; j < 8; j++) {
			snprintf(tmp, sizeof(tmp), "</B/%02d>   %02d   <!%02d>", i+j, i+j, i+j);
			strncat(txt, tmp, sizeof(txt)-strlen(txt)-1);
		}
		addCDKSwindow(colours, txt, 0);
	}
	drawCDKSwindow(colours, FALSE);

	sleep(5);

	destroyCDKSwindow(colours);
	destroyCDKScreen(cdkscreen);
	endCDK();

	return 0;
}
