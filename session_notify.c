/*
 *  session_notify.c -- ultra minimal session manager
 *
 *  Copyright (C) 2010 Torben Hohn.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <libgen.h>
#include <jack/jack.h>
#include <jack/jslist.h>
#include <jack/transport.h>
#include <jack/session.h>

typedef struct UNMAP {
	const char *name;
	const char *uuid;
} uuid_map_t;

jack_client_t *client;
JSList *uuid_map = NULL;

void usage(char *program_name) {
	fprintf(stderr, "usage: %s quit|save [path]\n", program_name);
	exit(9);
}

void jack_shutdown(void *arg) {
	fprintf(stderr, "JACK shut down, exiting ...\n");
	exit(1);
}

void signal_handler(int sig) {
	jack_client_close(client);
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

void map_uuid_name ( const char *uuid, const char *name ) {
	uuid_map_t *mapping = malloc( sizeof(uuid_map_t) );
	mapping->uuid = uuid;
	mapping->name = name;
	uuid_map = jack_slist_append( uuid_map, mapping );
}

void replace_name_by_uuid ( char* buf, size_t size_buf, const char* port_name ) {
	JSList *node;
	char *port_component = strchr( port_name, ':' );
	char *client_component = strndup( port_name, port_component - port_name );

	for ( node=uuid_map; node; node=jack_slist_next(node) ) {
		uuid_map_t *mapping = node->data;
		if ( ! strcmp( mapping->name, client_component ) ) {
			snprintf( buf, size_buf, "%s%s", mapping->uuid, port_component );
			return;
		}
	}
	free(client_component);
	strncpy(buf, port_name, size_buf);

	return;
}

int main(int argc, char *argv[]) {
	/* Parse arguments */
	char *package = basename(argv[0]); /* Program Name */
	if (argc != 3) usage(package);

	jack_session_event_type_t notify_type;
	if ( ! strcmp( argv[1], "quit" ) ) {
		notify_type = JackSessionSaveAndQuit;
	} else if ( ! strcmp( argv[1], "save" ) ) {
		notify_type = JackSessionSave;
	} else {
		usage(package);
	}

	/* Prase save path (Jack don't like trailing slash */
	char* save_path = alloca(strlen(argv[2]) + 1);
	strcpy(save_path, argv[2]);
	char *last = save_path + strlen(save_path) - 1;
	if (*last != '/') { *(++last) = '/'; *(++last) = '\0'; }

	/* become a JACK client */
	if ((client = jack_client_open(package, JackNullOption, NULL)) == 0) {
		fprintf(stderr, "JACK server not running?\n");
		exit(1);
	}

	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	jack_on_shutdown(client, jack_shutdown, 0);

	jack_activate(client);

	int k,i,j;
	int exit_code = 0;
	jack_session_command_t *retval =
		jack_session_notify( client, NULL, notify_type, save_path );
	for(i=0; retval[i].uuid; i++ ) {
		if ( retval[i].flags & JackSessionSaveError) {
			printf("# %s FAIL\n", retval[i].client_name);
			exit_code = 1;
			continue;
		}

		printf( "# UUID: %s | NAME : %s\n", retval[i].uuid, retval[i].client_name );
		printf( "export SESSION_DIR=\"%s%s/\"\n", save_path, retval[i].client_name );
		if ( retval[i].flags & JackSessionNeedTerminal ) {
			/* ncurses aplications */
			printf( "$XTERM %s &\n", retval[i].command );
		} else {
			/* Other apps aplications */
			printf( "%s &\n", retval[i].command );
		}
		map_uuid_name( retval[i].uuid, retval[i].client_name );
	}

	char srcport[300];
	char dstport[300];
	for(k=0; retval[k].uuid; k++ ) {
		if ( retval[k].flags & JackSessionSaveError ) continue;

		char* port_regexp = alloca( jack_client_name_size() + 3 );
//		char* client_name = jack_get_client_name_by_uuid( client, retval[k].uuid );
		snprintf( port_regexp, sizeof(port_regexp), "%s:.*", retval[k].client_name );
//		jack_free(client_name);
		const char **ports = jack_get_ports( client, port_regexp, NULL, 0 );
		if( !ports ) continue;

		for (i = 0; ports[i]; ++i) {
			const char **connections =
				jack_port_get_connections( jack_port_by_name(client, ports[i]) );
			if (! connections) continue;

			for (j=0; connections[j]; j++) {
				replace_name_by_uuid( srcport, sizeof(srcport), ports[i] );
				replace_name_by_uuid( dstport, sizeof(dstport), connections[j] );

				printf( "fhctrl_connect -w 10 -u \"%s\" \"%s\"\n", srcport, dstport );
			}
			jack_free (connections);
		}
		jack_free(ports);

	}
	jack_session_commands_free(retval);

	jack_client_close(client);

	return exit_code;
}
