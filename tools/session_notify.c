/*
 *  session_notify.c -- ultra minimal session manager
 *
 *  Copyright (C) 2010 Torben Hohn.
 *  Copyright (C) 2013 Pawel Piatek.
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
#include <jack/session.h>

#include <libconfig.h>

#define CONNECT_APP "fhctrl_connect -w 60"
#define SKIP_MIDI 1

typedef struct {
	const char *name;
	const char *uuid;
} uuid_map_t;

typedef struct {
	char *src;
	char *dst;
} connection_t;

enum NodeType { APP = 0, CON = 1 };

jack_client_t *client;

void usage (char *program_name) {
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
	size_t size = port_component - name + 1;
	char client_component[size];
	snprintf(client_component, size, "%s", name );

	for ( node=*uuid_map; node; node=jack_slist_next(node) ) {
		uuid_map_t *map = node->data;
		if ( ! strcmp( map->name, client_component ) ) {
			snprintf( buf, buf_size, "%s%s", map->uuid, port_component );
			return;
		}
	}
	snprintf(buf, buf_size, "%s", name);
}

connection_t* new_connection(const char *src, const char *dst) {
	connection_t* new = malloc( sizeof(connection_t) );
	new->src = strdup( src );
	new->dst = strdup( dst );
	return new;
}

void free_connection(connection_t *c) {
	free(c->src);
	free(c->dst);
	free(c);
}

void store_connection(JSList** list, const char* src, const char* dst) {
	/* Check is unique */
	connection_t *c;
	JSList* l;
	for( l=*list; l; l=jack_slist_next(l) ) {
		c = l->data;
		if ( !strcmp(c->src, src) && !strcmp(c->dst, dst) ) {
//			printf("# Skip duplicate %s -> %s\n", src, dst);
			return;
		}
	}

	/* Append new connection */
	c = new_connection( src, dst );
	*list = jack_slist_append( *list, c );
}

void conf_add_app ( config_t* cfg, int num, const char* name, const char* command, int flags ) {
	char node_name[32];
	snprintf ( node_name, sizeof node_name, "app%d", num );
	config_setting_t* app = config_setting_add ( cfg->root, node_name, CONFIG_TYPE_GROUP );

	/* Node type */
	config_setting_t* type = config_setting_add ( app, "type", CONFIG_TYPE_INT );
	config_setting_set_int ( type, APP );

	/* Directory */
	config_setting_t* dir = config_setting_add ( app, "dir", CONFIG_TYPE_STRING );
	config_setting_set_string ( dir, name );

	/* Name */
	config_setting_t* cname = config_setting_add ( app, "name", CONFIG_TYPE_STRING );
	config_setting_set_string ( cname, name );

	/* Command */
	config_setting_t* cmd = config_setting_add ( app, "cmd", CONFIG_TYPE_STRING );
	if ( flags & JackSessionNeedTerminal ) {
		/* ncurses aplications */
		char scmd[256];
		snprintf ( scmd, sizeof scmd, "$XTERM %s", command );
	} else {
		/* other aplications */
		config_setting_set_string ( cmd, command );
	}
}

void conf_add_con ( config_t* cfg, int num, const char* src, const char* dst ) {
	char node_name[32];
	snprintf ( node_name, sizeof node_name, "con%d", num );
	config_setting_t* con = config_setting_add ( cfg->root, node_name, CONFIG_TYPE_GROUP );

	/* Node type */
	config_setting_t* type = config_setting_add ( con, "type", CONFIG_TYPE_INT );
	config_setting_set_int ( type, CON );

	/* Source/Output port */
	config_setting_t* csrc = config_setting_add ( con, "src", CONFIG_TYPE_STRING );
	config_setting_set_string ( csrc, src );

	/* Destination/Input port */
	config_setting_t* cdst = config_setting_add ( con, "dst", CONFIG_TYPE_STRING );
	config_setting_set_string ( cdst, dst );
}

int main(int argc, char *argv[]) {
	JSList *uuid_map = NULL;
	JSList *connections_list = NULL;
	unsigned short exit_code = 0;

	/* Parse arguments */
	char *package = basename(argv[0]); /* Program Name */
	if (argc != 3) usage(package);

	char* mode = argv[1];
	char* path = argv[2];

	jack_session_event_type_t notify_type;
	if ( ! strcmp( mode, "quit" ) ) {
		notify_type = JackSessionSaveAndQuit;
	} else if ( ! strcmp( mode, "save" ) ) {
		notify_type = JackSessionSave;
	} else {
		usage(package);
	}

	/* Parse save path (Jack need trailing slash) */
	size_t plen = strlen(path);
	char save_path[plen + 2];
	strncpy(save_path, path, plen + 2);
	if ( save_path[plen - 1] != '/' ) save_path[plen] = '/';

	/* become a JACK client */
	if ((client = jack_client_open(package, JackNullOption, NULL)) == 0) {
		fprintf(stderr, "JACK server not running?\n");
		exit(1);
	}
	jack_on_shutdown(client, jack_shutdown, 0);

	signal(SIGQUIT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	/* Init config */
	config_t cfg;
	config_init ( &cfg );

	jack_activate(client);

	jack_session_command_t *retval = jack_session_notify( client, NULL, notify_type, save_path );

	printf ( "# Applications:\n" );
	unsigned short i;
	for (i=0; retval[i].uuid; i++ ) {
		/* Do not save invalid clients */
		printf( "# UUID: %-5s NAME: %-30s STATE: ", retval[i].uuid, retval[i].client_name );
		if ( retval[i].flags & JackSessionSaveError) {
			printf("FAIL !!\n");
			exit_code = 1;
			continue;
		}
		printf( "OK\n" );

		/* Add app node to config */
		conf_add_app ( &cfg, i, retval[i].client_name, retval[i].command, retval[i].flags );

		/* Mapping uuids */
		map_uuid_name( &uuid_map, retval[i].uuid, retval[i].client_name );

		/* Store connections */
		int regexp_size = jack_client_name_size() + 4;
		char port_regexp[regexp_size];
		snprintf ( port_regexp, sizeof port_regexp, "^%s:.*", retval[i].client_name );

		const char **ports = jack_get_ports( client, port_regexp, NULL, 0 );
		if ( !ports ) continue;

		unsigned short j;
		for (j = 0; ports[j]; ++j) {
			jack_port_t* jack_port = jack_port_by_name( client, ports[j] );
			int port_flags = jack_port_flags ( jack_port );
			const char* ptype = jack_port_type ( jack_port );

#ifdef SKIP_MIDI
			if ( !strcmp(ptype, JACK_DEFAULT_MIDI_TYPE) ) continue;
#endif

			const char **conn = jack_port_get_all_connections( client, jack_port );
			if ( !conn ) continue;

			unsigned short k;
			for (k=0; conn[k]; k++) {
				if ( port_flags & JackPortIsInput ) {
					store_connection(&connections_list, conn[k], ports[j]);
				} else if ( port_flags & JackPortIsOutput ) {
					store_connection(&connections_list, ports[j], conn[k]);
				} // otherwise skip port
			}
			jack_free (conn);
		}
		jack_free (ports);
	}
	jack_session_commands_free(retval);
	jack_client_close(client);

	JSList* l;
	int name_size = jack_port_name_size();
	char src[name_size];
	char dst[name_size];
	printf ("# Connections: %d\n", jack_slist_length(connections_list));
	for( l=connections_list, i=0; l; l=jack_slist_next(l), i++ ) {
		connection_t *c = l->data;
		name2uuid( &uuid_map, src, c->src, sizeof src );
		name2uuid( &uuid_map, dst, c->dst, sizeof dst );
		free_connection(c);

		printf ("# %-34s -> %s\n", src, dst);
		conf_add_con ( &cfg, i, (const char*) src, (const char*) dst );
	}
	jack_slist_free ( uuid_map );
	jack_slist_free ( connections_list );

	/* Write config */
	const char* config_file = "/tmp/chuj.cfg";
	int ret = config_write_file ( &cfg, config_file );
	config_destroy(&cfg);

	if (ret == CONFIG_TRUE) {
		printf ( "Save to %s OK\n", config_file );
	} else {
		printf ( "Save to %s Fail\n", config_file );
		exit_code = 2;
	}

	return exit_code;
}

