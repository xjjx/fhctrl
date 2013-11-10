#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include <cdk.h>

#define CH 10

struct APP {
	pid_t pid;
	bool ready;
	char cmd[256];
};

int child ( char* cmd ) {
	execlp ( "sh", "sh", "-c", cmd, NULL );
	return 1;
}

void refresh_applist ( CDKSCROLL* applist, struct APP* app, int num ) {
	setCDKScrollItems ( applist, NULL, 0, 0 );
	int i;
	for ( i = 0; i < num; i++ ) {
		char chuj[256];
		snprintf ( chuj, sizeof chuj, "%s (PID: %d): %s\n", app[i].cmd, app[i].pid, (app[i].ready) ? "READY":"WORKING" );
		addCDKScrollItem ( applist, chuj );
	}
	drawCDKScroll ( applist, TRUE );
}

void app_wait ( struct APP* app, bool* wait ) {
	int Stat;
	pid_t wpid = waitpid ( app->pid, &Stat, WNOHANG );
	if (wpid == 0) {
//		printf ( "Still wait for PID:%d\n", pid[i] );
		*wait = true;
	} else {
		if (WIFEXITED(Stat)) {
//			printf("Child %d exited, status=%d\n", pid[i], WEXITSTATUS(Stat));
//			deleteCDKScrollItem ( applist, i );
		} else if (WIFSIGNALED(Stat)) {
//			printf("Child %d was terminated with a status of: %d \n", pid[i], WTERMSIG(Stat));
//			deleteCDKScrollItem ( applist, i );
		}
		app->pid = 0;
		app->ready = true;
	}
}

int main (int argc, char *argv[]) {
	/* Initialize the Cdk screen.  */
	WINDOW *screen = initscr();
	CDKSCREEN* cdkscreen = initCDKScreen (screen);

	/* Disable cursor */
	curs_set(0);

	/* Start CDK Colors */
	initCDKColor();

	struct APP app[CH];

	/* Create APP list */
	CDKSCROLL *applist = newCDKScroll (
		cdkscreen, LEFT, TOP, RIGHT, 15, 0,
		"</U/63>APP list:<!05>", 0, 0, FALSE, A_NORMAL, TRUE, FALSE
	);

	/* Init APP */
	int i;
	for ( i = 0; i < CH; i++ ) {
		snprintf ( app[i].cmd, sizeof app[i].cmd, "sleep %d", i + 3 );
		app[i].pid = 0;
		app[i].ready = true;
	}

	bool wait = true;
	while ( wait ) {
		wait = false;
		for ( i = 0; i < CH; i++ ) {
			if ( app[i].ready ) {
				pid_t p = fork();
				if ( p == 0 ) { // Child
					return child ( app[i].cmd );
				} else if ( p < 0 ) { // FORK FAIL // Parent
					app[i].pid = 0;
				} else { // p > 0 // Parent
					app[i].pid = p;
				}
				app[i].ready = false;
			}
			
			app_wait ( &app[i], &wait );
		}
		refresh_applist ( applist, app, CH );
		sleep ( 1 );
	}

	destroyCDKScroll ( applist );
	destroyCDKScreen ( cdkscreen );
	endCDK();
	return 0;
}
