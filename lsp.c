#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

//#include <config.h>
#define VERSION "XJ"

#include <jack/jack.h>
#include <jack/session.h>

char * my_name;

void
show_version (void)
{
	fprintf (stderr, "%s: JACK Audio Connection Kit version " VERSION "\n",
		my_name);
}

void
printf_name2uuid (jack_client_t* client, const char* pname)
{
	char *port_component = strchr( pname, ':' );
	size_t csize = port_component - pname + 1;
	char client_component[csize];
	snprintf(client_component, csize, pname);

	const char *uuid = jack_get_uuid_for_client_name(client, client_component);
	if (uuid && strcmp(uuid, "-1") != 0) {
		printf("%s%s\n", uuid, port_component );
	} else {
		printf("%s\n",pname);
	}
}

void
show_usage (void)
{
	show_version ();
	fprintf (stderr, "\nUsage: %s [options] [filter string]\n", my_name);
	fprintf (stderr, "List active Jack ports, and optionally display extra information.\n");
	fprintf (stderr, "Optionally filter ports which match ALL strings provided after any options.\n\n");
	fprintf (stderr, "Display options:\n");
	fprintf (stderr, "        -s, --server <name>   Connect to the jack server named <name>\n");
	fprintf (stderr, "        -A, --aliases         List aliases for each port\n");
	fprintf (stderr, "        -c, --connections     List connections to/from each port\n");
	fprintf (stderr, "        -l, --latency         Display per-port latency in frames at each port\n");
	fprintf (stderr, "        -L, --latency         Display total latency in frames at each port\n");
	fprintf (stderr, "        -p, --properties      Display port properties. Output may include:\n"
			 "                              input|output, can-monitor, physical, terminal\n\n");
	fprintf (stderr, "        -t, --type            Display port type\n");
	fprintf (stderr, "        -u, --uuid            Display uuid instead of client name (if available)\n");
	fprintf (stderr, "        -h, --help            Display this help message\n");
	fprintf (stderr, "        --version             Output version information and exit\n\n");
	fprintf (stderr, "For more information see http://jackaudio.org/\n");
}

int
main (int argc, char *argv[])
{
	jack_client_t *client;
	jack_status_t status;
	jack_options_t options = JackNoStartServer;
	const char **ports, **connections;
	unsigned int i, j, k;
	int skip_port;
	int show_aliases = 0;
	int show_con = 0;
	int show_port_latency = 0;
	int show_total_latency = 0;
	int show_properties = 0;
	int show_type = 0;
	int show_uuid = 0;
	int c;
	int option_index;
	char* aliases[2];
	char *server_name = NULL;

	
	struct option long_options[] = {
	    { "server", 1, 0, 's' },
		{ "aliases", 0, 0, 'A' },
		{ "connections", 0, 0, 'c' },
		{ "port-latency", 0, 0, 'l' },
		{ "total-latency", 0, 0, 'L' },
		{ "properties", 0, 0, 'p' },
		{ "type", 0, 0, 't' },
		{ "uuid", 0, 0, 'u' },
		{ "help", 0, 0, 'h' },
		{ "version", 0, 0, 'v' },
		{ 0, 0, 0, 0 }
	};

	my_name = strrchr(argv[0], '/');
	if (my_name == 0) {
		my_name = argv[0];
	} else {
		my_name ++;
	}

	while ((c = getopt_long (argc, argv, "s:AclLphvtu", long_options, &option_index)) >= 0) {
		switch (c) {
		case 's':
            server_name = (char *) malloc (sizeof (char) * strlen(optarg));
            strcpy (server_name, optarg);
            options |= JackServerName;
            break;
		case 'A':
			aliases[0] = (char *) malloc (jack_port_name_size());
			aliases[1] = (char *) malloc (jack_port_name_size());
			show_aliases = 1;
			break;
		case 'c':
			show_con = 1;
			break;
		case 'l':
			show_port_latency = 1;
			break;
		case 'L':
			show_total_latency = 1;
			break;
		case 'p':
			show_properties = 1;
			break;
		case 't':
			show_type = 1;
			break;
		case 'u':
			show_uuid = 1;
			break;
		case 'h':
			show_usage ();
			return 1;
			break;
		case 'v':
			show_version ();
			return 1;
			break;
		default:
			show_usage ();
			return 1;
			break;
		}
	}

	/* Open a client connection to the JACK server.  Starting a
	 * new server only to list its ports seems pointless, so we
	 * specify JackNoStartServer. */
	client = jack_client_open ("lsp", options, &status, server_name);
	if (client == NULL) {
		if (status & JackServerFailed) {
			fprintf (stderr, "JACK server not running\n");
		} else {
			fprintf (stderr, "jack_client_open() failed, "
				 "status = 0x%2.0x\n", status);
		}
		return 1;
	}

	ports = jack_get_ports (client, NULL, NULL, 0);

	for (i = 0; ports && ports[i]; ++i) {
		// skip over any that don't match ALL of the strings presented at command line
		skip_port = 0;
		for(k=optind; k < argc; k++){
			if(strstr(ports[i], argv[k]) == NULL ){
				skip_port = 1;
			}
		}
		if(skip_port) continue;

		if (show_uuid) {
			printf_name2uuid(client, ports[i]);
		} else {
			printf ("%s\n", ports[i]);
		}

		jack_port_t *port = jack_port_by_name (client, ports[i]);

		if (show_aliases) {
			int cnt;
			int i;

			cnt = jack_port_get_aliases (port, aliases);
			for (i = 0; i < cnt; ++i) {
				printf ("   %s\n", aliases[i]);
			}
		}
				
		if (show_con) {
			if ((connections = jack_port_get_all_connections (client, jack_port_by_name(client, ports[i]))) != 0) {
				for (j = 0; connections[j]; j++) {
					printf("   ");
					if (show_uuid) {
						printf_name2uuid(client, connections[j]);
					} else {
						printf("%s\n", connections[j]);
					}

				}
				jack_free (connections);
			} 
		}
		if (show_port_latency) {
			if (port) {
				jack_latency_range_t range;
				printf ("	port latency = %" PRIu32 " frames\n",
					jack_port_get_latency (port));

				jack_port_get_latency_range (port, JackPlaybackLatency, &range);
				printf ("	port playback latency = [ %" PRIu32 " %" PRIu32 " ] frames\n",
					range.min, range.max);

				jack_port_get_latency_range (port, JackCaptureLatency, &range);
				printf ("	port capture latency = [ %" PRIu32 " %" PRIu32 " ] frames\n",
					range.min, range.max);
			}
		}
		if (show_total_latency) {
			if (port) {
				printf ("	total latency = %" PRIu32 " frames\n",
					jack_port_get_total_latency (client, port));
			}
		}
		if (show_properties) {
			if (port) {
				int flags = jack_port_flags (port);
				printf ("	properties: ");
				if (flags & JackPortIsInput) {
					fputs ("input,", stdout);
				}
				if (flags & JackPortIsOutput) {
					fputs ("output,", stdout);
				}
				if (flags & JackPortCanMonitor) {
					fputs ("can-monitor,", stdout);
				}
				if (flags & JackPortIsPhysical) {
					fputs ("physical,", stdout);
				}
				if (flags & JackPortIsTerminal) {
					fputs ("terminal,", stdout);
				}
				putc ('\n', stdout);
			}
		}
		if (show_type) {
			if (port) {
				putc ('\t', stdout);
				fputs (jack_port_type (port), stdout);
				putc ('\n', stdout);
			}
		}
	}
	printf("NAME SIZE %d\n", jack_port_name_size());

	if (ports)
		jack_free (ports);

	jack_client_close (client);
	exit (0);
}
