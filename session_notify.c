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

#define CONNECT_APP "fhctr_connect"

typedef struct UNMAP {
	const char *name;
	const char *uuid;
} uuid_map_t;

struct connection {
	char *src;
	char *dst;
};

jack_client_t *client;

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

void map_uuid_name ( JSList** uuid_map, const char *uuid, const char *name ) {
	uuid_map_t *mapping = malloc( sizeof(uuid_map_t) );
	mapping->uuid = uuid;
	mapping->name = name;
	*uuid_map = jack_slist_append( *uuid_map, mapping );
}

void name2uuid ( JSList** uuid_map, char* buf, const char* name, size_t buf_size ) {
	JSList *node;
	char *port_component = strchr( name, ':' );
	size_t size = port_component - name;
	char client_component[size];

	strncpy( client_component, name, size );
	client_component[size] = '\0'; /* strncpy doesn't handle this */

	for ( node=*uuid_map; node; node=jack_slist_next(node) ) {
		uuid_map_t *map = node->data;
		if ( strcmp( map->name, client_component ) == 0 ) {
			snprintf( buf, buf_size, "%s%s", map->uuid, port_component );
			return;
		}
	}
	strncpy(buf, name, buf_size);
	buf[buf_size] = '\0'; /* strncpy doesn't handle this */
}

void store_connection(JSList** list, const char* src, const char* dst) {
	struct connection *c;
	/* Check is unique */
	JSList* l;
	for( l=*list; l; l=jack_slist_next(l) ) {
		c = l->data;
		if ( strcmp(c->src, src) == 0 && strcmp(c->dst, dst) == 0 ) {
//			printf("# Skip duplicate %s -> %s\n", src, dst);
			return;
		}
	}
	c = malloc( sizeof(struct connection) );
	c->src = strdup( src );
	c->dst = strdup( dst );
	*list = jack_slist_append(*list, c);
}

int main(int argc, char *argv[]) {
	JSList *uuid_map = NULL;
	JSList *connections_list = NULL;

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

	unsigned short i, j, k;
	unsigned short exit_code = 0;
	jack_session_command_t *retval =
		jack_session_notify( client, NULL, notify_type, save_path );
	for(i=0; retval[i].uuid; i++ ) {
		printf( "# UUID: %s | NAME : %s\n", retval[i].uuid, retval[i].client_name );
		if ( retval[i].flags & JackSessionSaveError) {
			printf("# %s FAIL\n", retval[i].client_name);
			exit_code = 1;
			continue;
		}
		printf( "export SESSION_DIR=\"%s%s/\"\n", save_path, retval[i].client_name );
		if ( retval[i].flags & JackSessionNeedTerminal ) {
			/* ncurses aplications */
			printf( "$XTERM %s &\n", retval[i].command );
		} else {
			/* Other aplications */
			printf( "%s &\n", retval[i].command );
		}
		printf("\n");

		/* Mapping uuids */
		map_uuid_name( &uuid_map, retval[i].uuid, retval[i].client_name );

		char* port_regexp = alloca( jack_client_name_size() + 3 );
		snprintf( port_regexp, sizeof(port_regexp), "^%s:.*", retval[i].client_name );

		const char **ports = jack_get_ports( client, port_regexp, NULL, 0 );
		if( !ports ) continue;

		for (j = 0; ports[j]; ++j) {
			jack_port_t* jack_port = jack_port_by_name( client, ports[j] );
			int flags = jack_port_flags( jack_port );

			const char **conn = jack_port_get_connections( jack_port );
			if (! conn) continue;
			for (k=0; conn[k]; k++) {
				if ( flags & JackPortIsInput ) {
					store_connection(&connections_list, conn[k], ports[j]);
				} else { // assume JackPortIsOutput
					store_connection(&connections_list, ports[j], conn[k]);
				}
			}
			jack_free (conn);
		}
		jack_free(ports);
	}

	JSList* l;
	char src[300];
	char dst[300];
	for( l=connections_list; l; l=jack_slist_next(l) ) {
		struct connection *c = l->data;
		name2uuid( &uuid_map, src, c->src, sizeof(src) );
		name2uuid( &uuid_map, dst, c->dst, sizeof(dst) );
		printf( "%s -w 10 -u \"%s\" \"%s\"\n", CONNECT_APP, src, dst );
		free(c->src);
		free(c->dst);
	}
	jack_session_commands_free(retval);
	jack_slist_free(uuid_map);
	jack_slist_free(connections_list);
	jack_client_close(client);

	return exit_code;
}
