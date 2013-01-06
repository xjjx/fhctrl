/*
    Copyright (C) 2002 Jeremy Hall
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

//#include <config.h>
#define VERSION "XJ"

#include <jack/jack.h>
#include <jack/session.h>

#define TRUE 1
#define FALSE 0

void show_version (char *my_name) {
	fprintf (stderr, "%s: JACK Audio Connection Kit version " VERSION "\n", my_name);
}

void show_usage (char *my_name) {
	show_version (my_name);
	fprintf (stderr, "\nusage: %s [options] port1 port2\n", my_name);
	fprintf (stderr, "Connects two JACK ports together.\n\n");
	fprintf (stderr, "        -s, --server <name>   Connect to the jack server named <name>\n");
	fprintf (stderr, "        -u,                   Use session uuid instead of names\n");
	fprintf (stderr, "        -w <timeout>,         Wait for ports, max <timeout>\n");
	fprintf (stderr, "        -v, --version         Output version information and exit\n");
	fprintf (stderr, "        -h, --help            Display this help message\n\n");
	fprintf (stderr, "For more information see http://jackaudio.org/\n");
}

int uuid2name(jack_client_t *client, char* outbuf, char* arg, size_t outbuf_size) {
	char *tmpname = strdup( arg );
	char *portname = strchr( tmpname, ':' );
	portname[0] = '\0';
	portname++;

	char *clientname = jack_get_client_name_by_uuid( client, tmpname );
	if ( ! clientname )
		return 0;

	snprintf( outbuf, outbuf_size, "%s:%s", clientname, portname );
	jack_free( clientname );
	free(tmpname);
	return 1;
}

int main (int argc, char *argv[]) {
	jack_client_t *client;
	jack_status_t status;
	char *server_name = NULL;
	short c;
	int option_index;
	jack_options_t options = JackNoStartServer;
	char *my_name = strrchr(argv[0], '/');
	jack_port_t *port = 0;
	jack_port_t *src_port = 0;
	jack_port_t *dst_port = 0;
	int port_flags;
	char portName[300];
	short use_uuid=0;
	short timeout=0;
	short connecting, disconnecting;
	short rc = 1;

	struct option long_options[] = {
	    { "server", 1, 0, 's' },
	    { "help", 0, 0, 'h' },
	    { "version", 0, 0, 'v' },
	    { "uuid", 0, 0, 'u' },
	    { "wait", 1, 0, 'w' },
	    { 0, 0, 0, 0 }
	};

	while ((c = getopt_long (argc, argv, "s:hvuw:", long_options, &option_index)) >= 0) {
		switch (c) {
		case 's':
			server_name = (char *) malloc (sizeof (char) * strlen(optarg));
			strcpy (server_name, optarg);
			options |= JackServerName;
			break;
		case 'u':
			use_uuid = 1;
			break;
		case 'w':
			timeout = strtol(optarg, NULL , 10);
			break;
		case 'h':
			show_usage (my_name);
			return 1;
		case 'v':
			show_version (my_name);
			return 1;
		default:
			show_usage (my_name);
			return 1;
		}
	}

	connecting = disconnecting = FALSE;
	if (my_name == 0) {
		my_name = argv[0];
	} else {
		my_name ++;
	}

	if (strstr(my_name, "disconnect")) {
		disconnecting = 1;
	} else if (strstr(my_name, "connect")) {
		connecting = 1;
	} else {
		fprintf(stderr, "ERROR! client should be called jack_connect or jack_disconnect. client is called %s\n", my_name);
		return 1;
	}
	
	if (argc < 3) {
		show_usage(my_name);
		return 1;
	}

	/* try to become a client of the JACK server */

	if ((client = jack_client_open (my_name, options, &status, server_name)) == 0) {
		fprintf (stderr, "jack server not running?\n");
		return 1;
	}

	/* find the two ports */

	short n, w=0;
	for (n=argc-2; n < argc; n++) {
		if (!use_uuid || !uuid2name(client, portName, argv[n], sizeof(portName))) {
			snprintf( portName, sizeof(portName), "%s", argv[n] );
		}

		while ((port = jack_port_by_name(client, portName)) == 0) {
			if (timeout-- < 1) {
				fprintf (stderr, "\rERROR %s not a valid port\n", portName);
				goto exit;
			}

			w = 1;
			fprintf(stderr, "\rWait for %s .. (%d)   ", portName, timeout + 1);
			sleep(1);
			// Update uuid2name mapping if needed
			if (use_uuid) uuid2name(client, portName, argv[n], sizeof(portName));
		}
		if(w) fprintf(stderr, "\33[2K\r");

		port_flags = jack_port_flags (port);
		if (port_flags & JackPortIsInput) {
			dst_port = port;
		} else if (port_flags & JackPortIsOutput) {
			src_port = port;
		} else {
			fprintf (stderr, "ERROR %s invalid port type\n", portName);
			goto exit;
		}
	}

	if (!src_port || !dst_port) {
		fprintf (stderr, "arguments must include 1 input port and 1 output port\n");
		goto exit;
	}

	/* connect the ports. Note: you can't do this before
	   the client is activated (this may change in the future).
	*/

	if (connecting) {
		if (jack_connect(client, jack_port_name(src_port), jack_port_name(dst_port))) {
			goto exit;
		}
	}

	if (disconnecting) {
		if (jack_disconnect(client, jack_port_name(src_port), jack_port_name(dst_port))) {
			goto exit;
		}
	}

	/* everything was ok, so setting exitcode to 0 */
	rc = 0;

exit:
	jack_client_close (client);
	exit (rc);
}

