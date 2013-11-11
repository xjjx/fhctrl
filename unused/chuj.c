#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include <jack/jack.h>
#include <jack/session.h>

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

static volatile bool quit = false;
void signal_handler (int sig) { quit = true; }

void jack_shutdown (void *arg) { quit = true; }

int run_cmd ( char* cmd ) {
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

int uuid2name ( jack_client_t *client, const char* arg, char* outbuf, size_t outbuf_size ) {
	char *port_part = strchr( arg, ':' );
	size_t size = port_part - arg + 1;
	char uuid[size];
	snprintf ( uuid, size, "%s", arg );

	char *clientname = jack_get_client_name_by_uuid ( client, uuid );
	if ( ! clientname ) return 0;

	if ( outbuf_size < strlen(clientname) + strlen(port_part) + 1)
		return 0;

	snprintf ( outbuf, outbuf_size, "%s%s", clientname, port_part );
	jack_free (clientname);

	return 1;
}

jack_port_t* get_port ( jack_client_t* client, char* name, int flags ) {
	int port_name_size = jack_port_name_size();
	char portName[port_name_size];

	if ( ! uuid2name ( client, name, portName, sizeof portName ) )
		strncpy ( portName, name, sizeof(portName) - 1 );

	jack_port_t* port = jack_port_by_name ( client, portName );
	if ( ! port ) return NULL;

	int port_flags = jack_port_flags (port);
	if ( ! ( port_flags & flags) ) return NULL;

	return port;
}

const char* get_port_name ( jack_client_t* client, char* name, int flags ) {
	jack_port_t* port = get_port ( client, name, flags );
	if ( ! port ) return NULL; 
	return jack_port_name ( port );
}

bool check_con ( connection_t* con, jack_client_t* client ) {
	/* Get src port */
	jack_port_t* sport = get_port ( client, con->src, JackPortIsOutput );
	if ( ! sport ) return false;

	/* Get dst port */
	jack_port_t* dport = get_port ( client, con->dst, JackPortIsInput );
	if ( ! dport ) return false;
	const char* dport_name = jack_port_name ( dport );

	/* Check connections */
	const char** conn = jack_port_get_all_connections ( client, sport );
	if (! conn) return false;
	
	int i;
	for ( i=0; conn[i]; i++ ) {
		if ( !strcmp ( conn[i], dport_name ) ) {
			jack_free (conn);
			return true;
		}
	}
	jack_free (conn);
	return false;
}

void refresh_conlist ( CDKSCROLL* conlist, connection_t* con, int num, jack_client_t* client ) {
	setCDKScrollItems ( conlist, NULL, 0, 0 );
	int i;
	for ( i = 0; i < num; i++ ) {
		bool connected = check_con ( &con[i], client );
		char chuj[256];
		snprintf ( chuj, sizeof chuj, "%s -> %s (%s)", con[i].src, con[i].dst, (connected)?"CONNECTED":"WAIT" );
		addCDKScrollItem ( conlist, chuj );
	}
	drawCDKScroll ( conlist, TRUE );
}

int graph_cb_handler ( void *arg ) {
	bool* graph_changed = (bool*) arg;
	*graph_changed = true;

        return 0;
}

void restore_connections ( connection_t* con, int num, jack_client_t* client ) {
	int i;
	for ( i = 0; i < num; i++ ) {
		bool connected = check_con ( &con[i], client );
		if ( connected ) continue;

		/* Can not pass name directly because of uuid mapping */
		const char* src = get_port_name ( client, con->src, JackPortIsOutput );
		if ( ! src ) continue;

		const char* dst = get_port_name ( client, con->dst, JackPortIsInput );
		if ( ! dst ) continue;

		jack_connect (client, src, dst);
	}
}

int main (int argc, char *argv[]) {
	bool graph_changed = true;

	/* init jack */
	jack_client_t* client = jack_client_open ( "CHUJ" , JackNullOption, NULL);
	if ( client == 0 ) {
		fprintf(stderr, "JACK server not running?\n");
		return 1;
	}
        jack_set_graph_order_callback (client, graph_cb_handler, &graph_changed);
	jack_on_shutdown ( client, jack_shutdown, 0 );
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

	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);


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

	while ( ! quit ) {
		for ( i = 0; i < CH; i++ ) {
			if ( app[i].ready ) {
				pid_t p = fork();
				if ( p == 0 ) { // Child
					return run_cmd ( app[i].cmd );
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
		if ( graph_changed ) {
			restore_connections ( con, CC, client );
			refresh_conlist ( conlist, con, CC, client );
		}

		sleep ( 1 );
	}

	/* Cleanup */
	destroyCDKScroll ( applist );
	destroyCDKScroll ( conlist );
	destroyCDKScreen ( cdkscreen );
	endCDK();

        jack_deactivate ( client );
        jack_client_close ( client );

	puts ( "This was clean .." );

	return 0;
}
