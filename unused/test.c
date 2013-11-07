#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

//#include <config.h>
#define VERSION "XJ"

#include <jack/jack.h>
#include <jack/midiport.h>

char * my_name;
jack_port_t* outport;
jack_nframes_t jack_buffer_size;
jack_nframes_t jack_sample_rate;
uint8_t cc = 0;
uint8_t value = 0;
uint8_t start = 0;
uint8_t end = 0;

int process (jack_nframes_t frames, void* arg) {
	void *outbuf = jack_port_get_buffer(outport, frames);
	jack_midi_clear_buffer(outbuf);

	if ( ! start ) return 0;

	jack_midi_data_t data[3] = { 0xB1, cc, value };
	size_t size = sizeof(data);

	if ( jack_midi_event_write(outbuf, jack_buffer_size - 1, (const jack_midi_data_t*) &data, size) )
		printf("Write dropped\n");

	value += 60;
	if ( value >= 127 ) {
		value = 0;
		if (++cc >= 127) end = 1;
	}
	start = 0;

	return 0;
}

int main (int argc, char *argv[]) {
	jack_client_t *client;
	jack_status_t status;
	jack_options_t options = JackNoStartServer;
	//const char** ports;

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

        jack_buffer_size = jack_get_buffer_size(client);
        jack_sample_rate = jack_get_sample_rate(client);
	
	outport = jack_port_register (client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	jack_set_process_callback(client, process, 0);


	printf("Activate ... ");
	if ( jack_activate (client) != 0 ) {
                fprintf (stderr, "Could not activate client.\n");
                exit (EXIT_FAILURE);
        }
	printf("OK\n");
	
	printf("Wait 10s for start\n");
	sleep(15);

	printf("Started\n");
	while ( !end ) {
		start = 1;
		printf("CC: %d | VALUE: %d\n", cc, value);
		sleep(1);
	}

        printf("Jack Deactivate\n");
        jack_deactivate(client);

	jack_port_unregister(client, outport);

	jack_client_close (client);
	exit (0);
}
