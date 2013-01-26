/*
   FSTHost Control by XJ / Pawel Piatek /

   This is part of FSTHost sources

   Based on jack-midi-dump by Carl Hetherington
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/session.h>
#include <jack/ringbuffer.h>

#include "sysex.h"
#include "fhctrl.h"

#include "ftdilcd.h"

// Config file support
bool dump_state(char const* config_file, struct Song **song_first, struct FSTPlug **fst);
bool load_state(const char* config_file, struct Song **song_first, struct FSTPlug **fst);

/* Out private variables */
static char const* client_name = "FHControl";
static jack_client_t* jack_client;
static jack_port_t* inport;
static jack_port_t* outport;
static jack_nframes_t jack_buffer_size;
static jack_nframes_t jack_sample_rate;
static jack_session_event_t* session_event;
static jack_ringbuffer_t* log_collector;
static short CtrlCh = 15; /* Our default MIDI control channel */
static short SongCount = 0;
static short SongSend = -1;
static bool ident_request = false;

static const SysExIdentRqst sysex_ident_request = SYSEX_IDENT_REQUEST;
static SysExDumpRequestV1 sysex_dump_request = SYSEX_DUMP_REQUEST;
static SysExDumpV1 sysex_dump = SYSEX_DUMP;
static SysExIdOffer sysex_offer = SYSEX_OFFER;

/* Public variables */
struct FSTPlug* fst[128] = {NULL};
struct Song* song_first = NULL;
struct CDKGUI gui;

struct LCDScreen lcd_screen;

/* Functions */
struct FSTState* state_new() {
	struct FSTState* fs = calloc(1,sizeof(struct FSTState));
	fs->state = FST_NA; // Initial state is Inactive
	return fs;
}

void fst_new(uint8_t uuid) {
	struct Song* s;
	struct FSTPlug* f = malloc(sizeof(struct FSTPlug));
	snprintf(f->name, sizeof f->name, "Device%d", uuid);
	f->id = uuid;
	f->state = state_new();
	f->change = true;

	// Add states to songs
	for(s = song_first; s; s = s->next) {
		s->fst_state[uuid] = state_new();
	}

	// Fill our global array
	fst[uuid] = f;
}

uint8_t fst_uniqe_id(uint8_t last) {
	short i;
	for(i=last+1; i < 128; i++) {
		if (fst[i] == NULL) return i;
	}
	return 0; // 0 mean error
}

struct FSTPlug* fst_get(uint8_t uuid) {
	if (fst[uuid] == NULL)
		fst_new(uuid);

	return fst[uuid];	
}

struct Song* song_new() {
	short i;
	struct Song** snptr;
	struct Song* s = calloc(1, sizeof(struct Song));

	//nLOG("Creating new song");
	// Add state for already known plugins
	for(i=0; i < 128; i++) {
		if (fst[i] == NULL) continue;

		s->fst_state[i] = state_new();
		*s->fst_state[i] = *fst[i]->state;
	}

	snprintf(s->name, sizeof s->name, "Song %d", SongCount);

	// Bind to song list
	if (song_first) {
		snptr = &song_first;
		while(*snptr) { snptr = &(*snptr)->next; }
		*snptr = s;
	} else {
		song_first = s;
	}

	SongCount++;

	return s;
}

struct Song* song_get(short SongNumber) {
	if (SongNumber >= SongCount)
		return NULL;

	struct Song* song;
	int s = 0;
	for(song=song_first; s < SongNumber; s++) {
		song = song->next;
		if (song == NULL) return NULL;
	}

	return song;
}

void song_send(short SongNumber) {
	SongSend = SongNumber;
}

void song_update(short SongNumber) {
	struct Song* song = song_get(SongNumber);
	if(song == NULL) {
		nLOG("SongUpdate: no such song");
		return;
	}

	short i;
	for(i=0; i < 128; i++) {
		// Do not update state for inactive FSTPlugs
		if (fst[i] && fst[i]->state->state == FST_NA)
			*song->fst_state[i] = *fst[i]->state;
	}
}

// Send SysEx Ident request if found some N/A plugs
static void
detect_na() {
	short i;

	for(i=0; i < 128; i++) {
		if(fst[i] && fst[i]->state->state == FST_NA) {
			ident_request = true;
			return;
		}
	}
}

void send_ident_request() {
	short i;

	// Reset states to non-active
	for(i=0; i < 128; i++) if (fst[i]) fst[i]->state->state = FST_NA;

	ident_request = true;
	nLOG("Sent ident request");
}

void init_lcd() {
	lcd_screen.available = lcd_init();
	if (! lcd_screen.available) return;

	char lcdline[16];
	snprintf(lcdline, 16, "%s", client_name);
	lcd_text(0,0,lcdline);
	snprintf(lcdline, 16, "says HELLO");
	lcd_text(0,1,lcdline);
	lcd_screen.fst = NULL;
}

void update_lcd() {
	if (! lcd_screen.available)
		return;

	struct FSTPlug* fp = lcd_screen.fst;
	if(!fp) return;

	char line[24];
	snprintf(line, 24, "D%02d P%02d C%02d V%02d",
		fp->id,
		fp->state->program,
		fp->state->channel,
		fp->state->volume
	);
	lcd_text(0,0,line);			// Line 1
	lcd_text(0,1,fp->state->program_name);	// Line 2
	lcd_text(0,2,fp->name);			// Line 3
}

static void connect_to_physical() {
	int i;
	const char **jports;

        jports = jack_get_ports(jack_client, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput|JackPortIsPhysical);
        if (jports == NULL)
		return;

	const char *pname = jack_port_name(inport);
        for (i=0; jports[i] != NULL; i++) {
		if (jack_port_connected_to(inport, jports[i]))
			continue;

                jack_connect(jack_client, jports[i], pname);
                nLOG("%s -> %s\n", pname, jports[i]);
        }
        jack_free(jports);
}

void session_reply() {
	nLOG("session callback");

	char *restore_cmd = malloc(256);
	char filename[FILENAME_MAX];

	// Save state, set error if fail
	snprintf(filename, sizeof(filename), "%sstate.cfg", session_event->session_dir);
	if (! dump_state(filename, &song_first, fst) )
		session_event->flags |= JackSessionSaveError;

	session_event->flags |= JackSessionNeedTerminal;

	snprintf(restore_cmd, 256, "fhctrl \"${SESSION_DIR}state.cfg\" %s", session_event->client_uuid);
	session_event->command_line = restore_cmd;

	jack_session_reply(jack_client, session_event);

//	if (event->type == JackSessionSaveAndQuit)

	jack_session_event_free(session_event);
	gui.need_ses_reply = false;
}

static void session_callback_handler(jack_session_event_t *event, void* arg) {
	session_event = event;
	gui.need_ses_reply = true;
}

static int graph_order_callback_handler( void *arg ) {
	jack_port_t* outport = arg;

	connect_to_physical();

	if ( jack_port_connected(outport) ) detect_na();

	return 0;
}

int cpu_load() {
	return (int) jack_cpu_load(jack_client);
}

void get_rt_logs() {
	char info[50];
	uint8_t len;
	while (jack_ringbuffer_read_space(log_collector)) {
		jack_ringbuffer_peek(log_collector, (char*) &len, sizeof len);
		jack_ringbuffer_read_advance(log_collector, sizeof len);

		jack_ringbuffer_peek(log_collector, (char*) &info, len);
		jack_ringbuffer_read_advance(log_collector, len);

		nLOG(info);
	}
}

void collect_rt_logs(char *fmt, ...) {
	char info[50];
	va_list args;
	
	va_start(args, fmt);
	vsnprintf(info, sizeof(info), fmt, args);
	uint8_t len = strlen(info) + 1;

	if (jack_ringbuffer_write_space(log_collector) < len + sizeof len) {
		nLOG("No space in log collector");
	} else {
		// Size of message
		jack_ringbuffer_write(log_collector, (char*) &len, sizeof len);
		// Message itself
		jack_ringbuffer_write(log_collector, (char*) &info, len);
	}

	va_end(args);
}

int process (jack_nframes_t frames, void* arg) {
	void* inbuf;
	void* outbuf;
	short soi = 0; /* Sysex Offer ID */
	void *sov[128];
	jack_nframes_t count;
	jack_nframes_t i;
	unsigned short s;
	jack_midi_event_t event;
	struct FSTPlug* fp;

	inbuf = jack_port_get_buffer (inport, frames);
	assert (inbuf);

	outbuf = jack_port_get_buffer(outport, frames);
	assert (outbuf);
	jack_midi_clear_buffer(outbuf);

	count = jack_midi_get_event_count (inbuf);
	for (i = 0; i < count; ++i) {
		if (jack_midi_event_get (&event, inbuf, i) != 0)
			break;

//		collect_rt_logs("MIDI: %X", event.buffer[0]);

		// My Midi control channel handling
		if ( (event.buffer[0] & 0x0F) == CtrlCh) {
			gui.ctrl_midi_in = true;

			if( (event.buffer[0] & 0xF0) == 0xC0 )
				SongSend = event.buffer[1];
			continue;
		}
		gui.midi_in = true;

		// Ident Reply
		if ( event.size < 5 || event.buffer[0] != SYSEX_BEGIN )
			goto further;

		switch (event.buffer[1]) {
		case SYSEX_NON_REALTIME:
			// event.buffer[2] is target_id - in our case always 7F
			if ( event.buffer[3] != SYSEX_GENERAL_INFORMATION ||
				event.buffer[4] != SYSEX_IDENTITY_REPLY
			) goto further;

			SysExIdentReply* r = (SysExIdentReply*) event.buffer;
			collect_rt_logs("Got SysEx ID Reply ID %X : %X", r->id, r->model[1]);
			if (r->model[1] == 0) {
				sov[soi++] = r->version;
			} else {
				fp = fst_get(r->model[1]);

				// If this is FSTPlug then dump it state
				if (r->id == SYSEX_MYID) {
					// Note: we refresh GUI when dump back to us
					fp->dump_request = true;
				} else {
					fp->change = true;
				}
			}
			// don't forward this message
			continue;
		case SYSEX_MYID:
			if (event.size < sizeof(SysExDumpV1))
				continue;

			SysExDumpV1* d = (SysExDumpV1*) event.buffer;
			if (d->type != SYSEX_TYPE_DUMP)
				goto further;

			collect_rt_logs("Got SysEx Dump %X : %s : %s", d->uuid, d->plugin_name, d->program_name);
			fp = fst_get(d->uuid);
			fp->state->state = d->state;
			fp->state->program = d->program;
			fp->state->channel = d->channel;
			fp->state->volume = d->volume;
			strcpy(fp->state->program_name, (char*) d->program_name);
			strcpy(fp->name, (char*) d->plugin_name);

			if (lcd_screen.available)
				lcd_screen.fst = fp;

			fp->change = true;

			// don't forward this message
			continue;
		}

further:
		// Forward messages
		jack_midi_event_write(outbuf, event.time, event.buffer, event.size);
	}

	// Send Identity Request
	if (ident_request) {
		ident_request = false;
		jack_midi_event_write(outbuf, jack_buffer_size - 1, 
			(jack_midi_data_t*) &sysex_ident_request, sizeof(SysExIdentRqst));
	}

	// Send Sysex Offer
	for (s=0; soi > 0; soi--) {
		memcpy(sysex_offer.rnid, sov[soi-1], sizeof(sysex_offer.rnid));
		if ((sysex_offer.uuid = fst_uniqe_id(s)) == 0) break;
		s = sysex_offer.uuid;
		jack_midi_event_write(outbuf, jack_buffer_size - 1, (jack_midi_data_t*) &sysex_offer, sizeof(SysExIdOffer));
	}

	// Send Dump Request
	for (s=0; s < 128; s++) {
		fp = fst[s];
		if (!fp) continue;
		if (!fp->dump_request) continue;
		fp->dump_request = false;

		sysex_dump_request.uuid = fp->id;
		jack_midi_event_write(outbuf, jack_buffer_size - 1, (jack_midi_data_t*) &sysex_dump_request, sizeof(SysExDumpRequestV1));
	}

	// Send Song
	if (SongSend < 0) return 0;

	struct Song* song = song_get(SongSend);
	SongSend = -1;
	if (song == NULL) return 0;
	
	enum State curState;
	collect_rt_logs("Send Song \"%s\" SysEx", song->name);
	// Dump states via SysEx - for all FST
	for (s=0; s < 128; s++) {
		if (!fst[s]) continue;
		fp = fst[s];

		curState = fp->state->state;
		*fp->state = *song->fst_state[s];
		// If plug is NA then keep it state and skip sending to plug
		if(curState == FST_NA) {
			fp->state->state = FST_NA;
			fp->change = true; // Update display
			continue;
		// If plug is NA in Song then preserve it's current state
		} else if(fp->state->state == FST_NA) {
			fp->state->state = curState;
		}
		fp->change = true; // Update display

		sysex_dump.uuid = fp->id;
		sysex_dump.program = fp->state->program;
		sysex_dump.channel = fp->state->channel;
		sysex_dump.volume = fp->state->volume;
		sysex_dump.state = fp->state->state;
		memcpy(sysex_dump.program_name, fp->state->program_name, sizeof(sysex_dump.program_name));
		memcpy(sysex_dump.plugin_name, fp->name, sizeof(sysex_dump.plugin_name));

		jack_midi_event_write(outbuf, jack_buffer_size - 1, (jack_midi_data_t*) &sysex_dump, sizeof(SysExDumpV1));
	}

	return 0;
}

int main (int argc, char* argv[]) {
	char const* config_file = NULL;
	char *uuid = NULL;

	if (argv[1]) config_file = argv[1];
	if (argv[2]) uuid = argv[2];

	/* Try change terminal size */
	printf("\033[8;43;132t\n");
	sleep(1); // Time for resize terminal

	// Init log collector
	log_collector = jack_ringbuffer_create(127 * 50 * sizeof(char));
	jack_ringbuffer_mlock(log_collector);

	// Init LCD
	init_lcd();

	// Try read file
	if (config_file) load_state(config_file, &song_first, fst);

	// Init Jack
	jack_client = jack_client_open (client_name, JackSessionID, NULL, uuid);
	if (jack_client == NULL) {
		fprintf (stderr, "Could not create JACK client.\n");
		exit (EXIT_FAILURE);
	}
	jack_buffer_size = jack_get_buffer_size(jack_client);
	jack_sample_rate = jack_get_sample_rate(jack_client);

	inport = jack_port_register (jack_client, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	outport = jack_port_register (jack_client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	jack_set_process_callback (jack_client, process, 0);
	jack_set_session_callback(jack_client, session_callback_handler, NULL);
	jack_set_graph_order_callback(jack_client, graph_order_callback_handler, outport);

	if ( jack_activate (jack_client) != 0 ) {
		fprintf (stderr, "Could not activate client.\n");
		exit (EXIT_FAILURE);
	}

	// ncurses GUI loop
	gui.song_first = &song_first;
	gui.fst = fst;
	gui.need_ses_reply = false;
	gui.midi_in = false;
	gui.ctrl_midi_in = false;
	nfhc(&gui);

	// Close LCD
	if (lcd_screen.available) lcd_close();

	// Save config
	if (config_file) dump_state(config_file, &song_first, fst);

	return 0;
}
