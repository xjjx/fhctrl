#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "fjack.h"
#include "sysex.h"
#include "log.h"

#define MAX_UNITS 128

/* externs - for now */
extern int process (jack_nframes_t frames, void* arg);
extern int graph_order_callback_handler ( void *arg );
extern void registration_handler (jack_port_id_t port_id, int reg, void *arg);

static void jack_log(const char *msg) { LOG((char*) msg); }
static void jack_log_silent(const char *msg) { return; }

static void
session_callback_handler (jack_session_event_t *event, void* arg) {
	FJACK* fjack = (FJACK*) arg;
	fjack->session_event = event;
	fjack->need_ses_reply = true;
}

static bool
queue_midi_out (jack_ringbuffer_t* rbuf, jack_midi_data_t* data, size_t size ) {
	if ( jack_ringbuffer_write_space(rbuf) < size + sizeof(size) ) {
		LOG ("No space in MIDI OUT buffer");
		return false;
	}

	// Size of message
	jack_ringbuffer_write(rbuf, (char*) &size, sizeof size);
	// Message itself
	jack_ringbuffer_write(rbuf, (char*) data, size);
	return true;
}

bool fjack_send ( FJACK* fjack, void* data, size_t size, const char* What, int8_t id ) {
	bool ret = queue_midi_out ( fjack->buffer_midi_out, (jack_midi_data_t*) data, size );

	if ( ret ) {
		return true;
	} else {
		LOG ("%s - Cannot send for ID:%d", What, id);
		return false;
	}
}

void fjack_send_ident_request ( FJACK* fjack ) {
	LOG("Send ident request");
	SysExIdentRqst sysex_ident_request = SYSEX_IDENT_REQUEST;
	fjack_send ( fjack,
		     (jack_midi_data_t*) &sysex_ident_request,
		     sizeof(SysExIdentRqst),
		     "SendIdentRequest",
		     -1
	);
}

void fjack_send_reload ( FJACK* fjack, uint8_t uuid ) {
	LOG("Send reload for %d", uuid);
	SysExReload sr = SYSEX_RELOAD;
	sr.uuid = uuid;
	fjack_send ( fjack,
		     (jack_midi_data_t*) &sr,
		     sizeof(SysExReload),
		     "SendReload",
		    -1
	);
}

void fjack_send_dump_request ( FJACK* fjack , short id ) {
	SysExDumpRequestV1 sysex_dump_request = SYSEX_DUMP_REQUEST;
	sysex_dump_request.uuid = id;
	LOG("Sent dump request for ID:%d", id);
	fjack_send ( fjack,
		     (jack_midi_data_t*) &sysex_dump_request,
		     sizeof(SysExDumpRequestV1),
		     "SendDumpRequest",
		     id
	);
}

void fjack_send_offer ( FJACK* fjack, SysExIdentReply* reply, uint8_t uuid ) {
	SysExIdOffer sysex_offer = SYSEX_OFFER;

	sysex_offer.uuid = uuid;
	memcpy(sysex_offer.rnid, reply->version, sizeof(sysex_offer.rnid));

	collect_rt_logs( fjack, "Send Offer %d for %X %X %X %X", sysex_offer.uuid,
		sysex_offer.rnid[0], sysex_offer.rnid[1], sysex_offer.rnid[2], sysex_offer.rnid[3]
	);

	fjack_send ( fjack,
		     (jack_midi_data_t*) &sysex_offer,
		     sizeof(SysExIdOffer),
		     "SendOffer",
		     sysex_offer.uuid
	);
}

void connect_to_physical ( FJACK* fjack ) {
	const char **jports;
	jports = jack_get_ports(fjack->client, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput|JackPortIsPhysical);
	if (! jports) return;
	
	const char *pname = jack_port_name( fjack->in );
	int i;
	for (i=0; jports[i]; i++) {
		if ( jack_port_connected_to ( fjack->in, jports[i]) ) continue;
		jack_connect(fjack->client, jports[i], pname);
		LOG("%s -> %s\n", pname, jports[i]);
	}
	jack_free(jports);
}

void get_rt_logs( FJACK* fjack ) {
	char info[50];
	uint8_t len;
	jack_ringbuffer_t* rbuf = fjack->log_collector;
	while (jack_ringbuffer_read_space(rbuf)) {
		jack_ringbuffer_read( rbuf, (char*) &len, sizeof len);
		jack_ringbuffer_read( rbuf, (char*) &info, len);
		LOG(info);
	}
}

void collect_rt_logs(FJACK* fjack, char *fmt, ...) {
	char info[50];
	va_list args;

	va_start(args, fmt);
	vsnprintf(info, sizeof(info), fmt, args);
	va_end(args);

	uint8_t len = strlen(info) + 1;

	jack_ringbuffer_t* rbuf = fjack->log_collector;
	if (jack_ringbuffer_write_space(rbuf) < len + sizeof len) {
		LOG("No space in log collector");
	} else {
		// Size of message
		jack_ringbuffer_write(rbuf, (char*) &len, sizeof len);
		// Message itself
		jack_ringbuffer_write(rbuf, (char*) &info, len);
	}
}

void fjack_init ( FJACK* fjack, const char* client_name, void* user_ptr ) {
	fjack->user = user_ptr;

	// Init log collector
	fjack->log_collector = jack_ringbuffer_create(MAX_UNITS * 50 * sizeof(char));
	jack_ringbuffer_mlock( fjack->log_collector );

	// Init MIDI Out buffer
	fjack->buffer_midi_out = jack_ringbuffer_create(MAX_UNITS * SYSEX_MAX_SIZE);
	jack_ringbuffer_mlock( fjack->buffer_midi_out );

	// Init Jack
	if ( fjack->session_uuid ) {
		printf ( "Session UID: %s\n", fjack->session_uuid );
		fjack->client = jack_client_open (client_name, JackSessionID, NULL, fjack->session_uuid);
	} else {
		fjack->client = jack_client_open (client_name, JackNullOption, NULL);
	}

	if ( ! fjack->client ) {
		fprintf (stderr, "Could not create JACK client.\n");
		exit (EXIT_FAILURE);
	}

	fjack->buffer_size = jack_get_buffer_size(fjack->client);
	fjack->sample_rate = jack_get_sample_rate(fjack->client);

	fjack->in = jack_port_register (fjack->client, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	fjack->out = jack_port_register (fjack->client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	jack_set_process_callback(fjack->client, process, fjack);
	jack_set_session_callback(fjack->client, session_callback_handler, fjack);
	jack_set_port_registration_callback(fjack->client, registration_handler, fjack);
	jack_set_graph_order_callback(fjack->client, graph_order_callback_handler, fjack);
//	jack_set_port_connect_callback(j->client, connect_callback_handler, fjack);
	if ( jack_activate (fjack->client) != 0 ) {
		fprintf (stderr, "Could not activate client.\n");
		exit (EXIT_FAILURE);
	}
	jack_set_info_function(jack_log_silent);
	jack_set_error_function(jack_log);
}
