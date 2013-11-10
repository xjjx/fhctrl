#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include <jack/jack.h>

#include <cdk.h>

#define CH 10
#define CC 3

typedef struct {
	pid_t pid;
	bool ready;
	char cmd[256];
} app_t;

typedef struct {
        char *src;
        char *dst;
} connection_t;

int child ( char* cmd ) {
	execlp ( "sh", "sh", "-c", cmd, NULL );
	return 1;
}

void refresh_applist ( CDKSCROLL* applist, app_t* app, int num ) {
	setCDKScrollItems ( applist, NULL, 0, 0 );
	int i;
	for ( i = 0; i < num; i++ ) {
		char chuj[256];
		snprintf ( chuj, sizeof chuj, "%s (PID: %d): %s", app[i].cmd, app[i].pid, (app[i].ready) ? "READY":"WORKING" );
		addCDKScrollItem ( applist, chuj );
	}
	drawCDKScroll ( applist, TRUE );
}

void app_wait ( app_t* app ) {
	int Stat;
	pid_t wpid = waitpid ( app->pid, &Stat, WNOHANG );
	if (wpid == 0) {
//		printf ( "Still wait for PID:%d\n", pid[i] );
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

bool check_con ( jack_client_t* client, connection_t* con ) {
	int port_name_size = jack_port_name_size();
	char portName[port_name_size];
/*
	for (n=argc-2; n < argc; n++) {
		if (! use_uuid || ! uuid2name(client, portName, argv[n], sizeof portName ) ) {
			snprintf( portName, sizeof(portName), "%s", argv[n] );
		}
*/

	snprintf( portName, sizeof portName, "%s", con->src );
	jack_port_t* sport = jack_port_by_name (client, portName);
	if ( ! sport ) return false;

	int port_flags = jack_port_flags (sport);
	if ( ! ( port_flags & JackPortIsOutput) ) return false;

	// Update uuid2name mapping if needed
//	if (use_uuid) uuid2name(client, portName, argv[n], sizeof(portName));
//	}
	
	snprintf( portName, sizeof portName, "%s", con->dst );
	jack_port_t* dport = jack_port_by_name (client, portName);
	if ( ! dport ) return false;

	port_flags = jack_port_flags (dport);
	if ( ! ( port_flags & JackPortIsInput) ) return false;

	const char** conn = jack_port_get_all_connections ( client, sport );
	if (! conn) return false;
	
	int i;
	bool ret = false;
	for (i=0; conn[i]; i++) {
		if ( !strcmp ( conn[i], con->dst ) ) {
			ret = true;
			break;
		}
	}
	jack_free (conn);
	return ret;
}

int main (int argc, char *argv[]) {
	/* init jack */
	jack_client_t* client = jack_client_open ( "CHUJ" , JackNullOption, NULL);
	if ( client == 0 ) {
		fprintf(stderr, "JACK server not running?\n");
		return 1;
	}
	jack_activate ( client );

	/* Initialize the Cdk screen.  */
	WINDOW *screen = initscr();
	CDKSCREEN* cdkscreen = initCDKScreen (screen);

	/* Disable cursor */
	curs_set(0);

	/* Start CDK Colors */
	initCDKColor();

	app_t app[CH];
	connection_t con[CC];

	/* Create APP list */
	CDKSCROLL *applist = newCDKScroll (
		cdkscreen, LEFT, TOP, RIGHT, 15, 0,
		"</U/63>APPLICATIONS list:<!05>", 0, 0, FALSE, A_NORMAL, TRUE, FALSE
	);

	/* Create connections list */
	CDKSCROLL *conlist = newCDKScroll (
		cdkscreen, LEFT, 16, RIGHT, 15, 0,
		"</U/63>CONNECTIONS list:<!05>", 0, 0, FALSE, A_NORMAL, TRUE, FALSE
	);

/*
	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	jack_on_shutdown ( client, jack_shutdown, 0 );
*/

	/* Init APP */
	int i;
	for ( i = 0; i < CH; i++ ) {
		snprintf ( app[i].cmd, sizeof app[i].cmd, "sleep %d", i + 3 );
		app[i].pid = 0;
		app[i].ready = true;
	}

	/* Init connections */
	for ( i = 0; i < CC; i++ ) {
		con[i].src = "system:capture_1";
		con[i].dst = "system:playback_1";
	}

	while ( true ) {
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
			
			app_wait ( &app[i] );
		}
		refresh_applist ( applist, app, CH );

		setCDKScrollItems ( conlist, NULL, 0, 0 );
		for ( i = 0; i < CC; i++ ) {
			bool connected = check_con ( client, &con[i] );
			char chuj[256];
			snprintf ( chuj, sizeof chuj, "%s -> %s (%s)", con[i].src, con[i].dst, (connected)?"CONNECTED":"WAIT" );
			addCDKScrollItem ( conlist, chuj );
		}
		drawCDKScroll ( conlist, TRUE );
		sleep ( 1 );
	}

	destroyCDKScroll ( applist );
	destroyCDKScroll ( conlist );
	destroyCDKScreen ( cdkscreen );
	endCDK();

        jack_deactivate ( client );
        jack_client_close ( client );

	return 0;
}
