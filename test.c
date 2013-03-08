#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

//#include <config.h>
#define VERSION "XJ"

#include <jack/jack.h>

char * my_name;

int
main (int argc, char *argv[])
{
	jack_client_t *client;
	jack_status_t status;
	jack_port_t* inport;
	jack_port_t* outport;
	jack_options_t options = JackNoStartServer;
	const char** ports;
	int i;

	my_name = strrchr(argv[0], '/');
	if (my_name == 0) {
		my_name = argv[0];
	} else {
		my_name ++;
	}

	client = jack_client_open ("TEST", options, &status, NULL);
	if (client == NULL) {
		if (status & JackServerFailed) {
			fprintf (stderr, "JACK server not running\n");
		} else {
			fprintf (stderr, "jack_client_open() failed, "
				 "status = 0x%2.0x\n", status);
		}
		return 1;
	}
	
	inport = jack_port_register (client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
	outport = jack_port_register (client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
/*
        if ( jack_activate (client) != 0 ) {
                fprintf (stderr, "Could not activate client.\n");
                exit (EXIT_FAILURE);
        }
*/
	i = jack_connect(client, jack_port_name(outport), jack_port_name(inport));
	printf("JackConnect status: %d\n", i);

        ports = jack_get_ports (client, NULL, NULL, 0);
        for (i = 0; ports && ports[i]; ++i) {
		printf("%s\n", ports[i]);
	}
//	sleep(10);

/*
        printf("Jack Deactivate\n");
        jack_deactivate(client);
*/

	jack_client_close (client);
	exit (0);
}
