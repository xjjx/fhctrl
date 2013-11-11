#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include <jack/jack.h>
#include <jack/jslist.h>
#include <jack/session.h>

#include <libconfig.h>

#include <cdk.h>

#define SESFILE "session.cfg"
#define APPNAME "nXjSM"

static volatile bool quit = false;
void signal_handler (int sig) { quit = true; }
void jack_shutdown (void *arg) { quit = true; }

typedef struct {
	pid_t pid;
	bool ready;
	char* dir;
	char* cmd;
} app_t;

typedef struct {
        char *src;
        char *dst;
} connection_t;

/* Proper dir require trailing slash */
char* proper_dir ( const char* dir ) {
	int len = strlen ( dir );
	if ( dir[len - 1] == '/' ) {
		return strdup ( dir );
	} else {
		char* d = malloc ( len + 2 );
		snprintf ( d, len + 2, "%s/", dir );
		return d;
	}
}

app_t* new_app ( const char* dir, const char* cmd ) {
//	printf ( "APP: DIR: %s | CMD: %s\n", dir, cmd );
	app_t* new = malloc ( sizeof(app_t) );
	new->dir = proper_dir ( dir );
	new->cmd = strdup ( cmd );
	new->pid = 0;
	new->ready = true;
	return new;
}

void free_app ( app_t* a ) {
	free ( a->dir );
	free ( a->cmd );
	free ( a );
}

connection_t* new_con ( const char *src, const char *dst ) {
//	printf ( "CON: %s -> %s\n", src, dst );
        connection_t* new = malloc( sizeof(connection_t) );
        new->src = strdup ( src );
        new->dst = strdup ( dst );
        return new;
}

void free_con ( connection_t *c ) {
        free(c->src);
        free(c->dst);
        free(c);
}

int run_app ( app_t* app ) {
	setenv ( "SESSION_DIR", app->dir, 1 );
	execlp ( "sh", "sh", "-c", app->cmd, NULL );
	return 1;
}

void refresh_applist ( CDKSCROLL* applist, JSList* list ) {
	JSList* l;
	char chuj[256];

	setCDKScrollItems ( applist, NULL, 0, 0 );
	for ( l = list; l; l = jack_slist_next(l) ) {
		app_t* app = l->data;

		snprintf ( chuj, sizeof chuj, "%s (PID: %d): %s", app->cmd, app->pid, (app->ready) ? "READY":"WORKING" );
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

void refresh_conlist ( CDKSCROLL* conlist, JSList* list, jack_client_t* client ) {
	JSList* l;
	char chuj[256];

	setCDKScrollItems ( conlist, NULL, 0, 0 );
	for ( l = list; l; l = jack_slist_next(l) ) {
		connection_t* con = l->data;

		bool connected = check_con ( con, client );
		snprintf ( chuj, sizeof chuj, "%s -> %s (%s)", con->src, con->dst, (connected)?"CONNECTED":"WAIT" );
		addCDKScrollItem ( conlist, chuj );
	}
	drawCDKScroll ( conlist, TRUE );
}

int graph_cb_handler ( void *arg ) {
	bool* graph_changed = (bool*) arg;
	*graph_changed = true;
        return 0;
}

void restore_connections ( JSList* list, jack_client_t* client ) {
	JSList* l;
	for ( l = list; l; l = jack_slist_next(l) ) {
		connection_t* con = l->data;

		bool connected = check_con ( con, client );
		if ( connected ) continue;

		/* Can not pass name directly because of uuid mapping */
		const char* src = get_port_name ( client, con->src, JackPortIsOutput );
		if ( ! src ) continue;

		const char* dst = get_port_name ( client, con->dst, JackPortIsInput );
		if ( ! dst ) continue;

		jack_connect (client, src, dst);
	}
}

enum NodeType { APP = 0, CON = 1 };
bool read_config ( const char* config_file, JSList** app_list, JSList** con_list ) {
	bool ret = false;
	config_t cfg;
	config_init(&cfg);

	if ( !config_read_file(&cfg, config_file) )
		goto read_config_return;

	config_setting_t* root = config_root_setting ( &cfg );
	if ( ! root ) goto read_config_return; // WTF ?

	int i;
	for ( i=0; i < config_setting_length (root); i++ ) {
		config_setting_t* node = config_setting_get_elem ( root, i );

		int type;
		if ( config_setting_lookup_int ( node, "type", &type ) != CONFIG_TRUE )
			continue;

		const char *dir , *cmd, *src, *dst;
		switch ( type ) {
		case APP:
			config_setting_lookup_string ( node, "dir", &dir );
			config_setting_lookup_string ( node, "cmd", &cmd );

			/* Append new app */
			app_t* a = new_app ( dir, cmd );
			*app_list = jack_slist_append ( *app_list, a );
			break;
		case CON:
			config_setting_lookup_string ( node, "src", &src );
			config_setting_lookup_string ( node, "dst", &dst );

			/* Append new connection */
			connection_t* c = new_con ( src, dst );
			*con_list = jack_slist_append ( *con_list, c );
			break;
		}
	}
	ret = true;

read_config_return:
	config_destroy(&cfg);
	return ret;
}

int main ( int argc, char *argv[] ) {
	if ( argc < 2 ) {
		fprintf ( stderr, "Usage: %s <session_dir>\n", argv[0] );
		return 1;
	}

	const char* session_dir = argv[1];
	if ( chdir ( session_dir ) != 0 ) {
		perror ( "Error:" );
		return 1;
	}

	JSList* app_list = NULL;
	JSList* con_list = NULL;
	if ( ! read_config ( SESFILE, &app_list, &con_list ) ) {
		fprintf ( stderr, "Read config error\n" );
		return 1;
	}

	/* init jack */
	jack_client_t* client = jack_client_open ( APPNAME, JackNullOption, NULL);
	if ( client == 0 ) {
		fprintf ( stderr, "JACK server not running?\n" );
		return 1;
	}

	bool graph_changed = true;
        jack_set_graph_order_callback (client, graph_cb_handler, &graph_changed);
	jack_on_shutdown ( client, jack_shutdown, 0 );

	jack_activate ( client );

	/* Initialize the Cdk screen.  */
	WINDOW *screen = initscr();
	CDKSCREEN* cdkscreen = initCDKScreen (screen);

	int rows, cols;
	getmaxyx ( cdkscreen->window, rows, cols );
	cols = cols; // fix warning

	/* Disable cursor */
	curs_set(0);

	/* Start CDK Colors */
	initCDKColor();

	/* Create APP list */
	CDKSCROLL *applist = newCDKScroll (
		cdkscreen, LEFT, TOP, RIGHT, rows/2, 0,
		"</U/63>APPLICATIONS list:<!05>", 0, 0, FALSE, A_NORMAL, TRUE, FALSE
	);

	/* Create connections list */
	CDKSCROLL *conlist = newCDKScroll (
		cdkscreen, LEFT, rows/2, RIGHT, rows/2 + 1, 0,
		"</U/63>CONNECTIONS list:<!05>", 0, 0, FALSE, A_NORMAL, TRUE, FALSE
	);

	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	while ( ! quit ) {
		JSList* l;
		for ( l = app_list; l; l = jack_slist_next(l) ) {
			app_t* app = l->data;

			if ( app->ready ) {
				pid_t p = fork();
				if ( p == 0 ) { // Child
					return run_app ( app );
				} else if ( p < 0 ) { // FORK FAIL // Parent
					app->pid = 0;
				} else { // p > 0 // Parent
					app->pid = p;
				}
				app->ready = false;
			}
			app_wait ( app );
		}
		refresh_applist ( applist, app_list );
		if ( graph_changed ) {
			restore_connections ( con_list, client );
			refresh_conlist ( conlist, con_list, client );
		}

		sleep ( 1 );
	}

	/* Cleanup */
	JSList* l;
	for ( l = app_list; l; l = jack_slist_next(l) ) {
		app_t *a = l->data;
		free_app ( a );
	}

	for ( l = con_list; l; l = jack_slist_next(l) ) {
		connection_t *c = l->data;
		free_con ( c );
	}

	destroyCDKScroll ( applist );
	destroyCDKScroll ( conlist );
	destroyCDKScreen ( cdkscreen );
	endCDK();

        jack_deactivate ( client );
        jack_client_close ( client );

	puts ( "This was clean .." );

	return 0;
}
