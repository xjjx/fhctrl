/*
   FSTHost Control by Xj 

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
#include "log.h"

#include "ftdilcd.h"

#define CTRL_CHANNEL 15
#define CLIENT_NAME "FHControl"

// Config file support
bool dump_state(char const* config_file, Song** songs, FSTPlug** fst);
bool load_state(const char* config_file, Song** songs, FSTPlug** fst);

typedef struct _FJACK {
	jack_client_t*		client;
	jack_port_t*		in;
	jack_port_t*		out;
	jack_port_t*		fin;
	jack_port_t*		fout;
	jack_nframes_t		buffer_size;
	jack_nframes_t		sample_rate;
	jack_session_event_t*	session_event;
	const char*		session_uuid;
	bool			need_ses_reply;
	jack_ringbuffer_t*	log_collector;
	jack_ringbuffer_t*	buffer_midi_out;
	void*			user;
} FJACK;

/* Declarations */
static void collect_rt_logs(FJACK* fjack, char *fmt, ...);
void idle_cb( void* arg );
short song_count( Song** songs );

/* Functions */
void jack_log(const char *msg) { LOG((char*) msg); }
void jack_log_silent(const char *msg) { return; }

FSTState* state_new() {
	FSTState* fs = calloc(1,sizeof(FSTState));
	// Default state is Inactive
	fs->state = FST_NA;
	// Default program name is .. like below ;-)
	strcpy(fs->program_name, "<<<- UNKNOWN PRESET- >>>");
	// Rest is initialized to 0 ( by calloc )
	return fs;
}

void fst_new ( FSTPlug** fst, Song** songs, uint8_t uuid ) {
	FSTPlug* f = malloc(sizeof(FSTPlug));
	snprintf(f->name, sizeof f->name, "Device%d", uuid);
	f->id = uuid;
	f->type = FST_TYPE_PLUGIN; // default type
	f->state = state_new();
	f->change = true;

	// Add states to songs
	Song* s;
	for(s = *songs; s; s = s->next)
		s->fst_state[uuid] = state_new();

	// Fill our global array
	fst[uuid] = f;
}

uint8_t fst_uniqe_id ( FSTPlug** fst, uint8_t start ) {
	uint8_t i = start;
	for( ; i < 128; i++) if (!fst[i]) return i;
	return 0; // 0 mean error
}

FSTPlug* fst_get ( FSTPlug** fst, Song** songs, uint8_t uuid ) {
	if (fst[uuid] == NULL) fst_new( fst, songs, uuid);
	return fst[uuid];
}

FSTPlug* fst_next ( FSTPlug** fst, FSTPlug* prev ) {
	FSTPlug* fp;
	uint8_t i = (prev == NULL) ? 0 : prev->id + 1;
	while ( i < 128 ) {
		fp = fst[i];
		if (fp) return fp;
		i++;
	}
	return NULL;
}

Song* song_new(Song** songs, FSTPlug** fst) {
	uint8_t i;
	Song** snptr;
	Song* s = calloc(1, sizeof(Song));

	//LOG("Creating new song");
	// Add state for already known plugins
	for(i=0; i < 128; i++) {
		if (fst[i] == NULL) continue;

		s->fst_state[i] = state_new();
		*s->fst_state[i] = *fst[i]->state;
	}

	snprintf(s->name, sizeof s->name, "Song %d", song_count(songs) );

	// Bind to song list
	if (*songs) {
		snptr = songs;
		while(*snptr) { snptr = &(*snptr)->next; }
		*snptr = s;
	} else {
		/* Set first song */
		*songs = s;
	}

	return s;
}

Song* song_get(Song** songs, short SongNumber) {
	if (SongNumber >= song_count(songs)) return NULL;

	Song* song;
	int s = 0;
	for(song=*songs; song && s < SongNumber; song = song->next, s++);

	return song;
}

short song_count( Song** songs ) {
	Song* s;
	int count = 0;
	for(s = *songs; s; s = s->next, count++);
	return count;
}

void song_update(FSTPlug** fst, Song* song) {
	if(song == NULL) {
		LOG("SongUpdate: no such song");
		return;
	}

	uint8_t i;
	for(i=0; i < 128; i++) {
		// Do not update state for inactive FSTPlugs
		if (fst[i] && fst[i]->state->state != FST_NA)
			*song->fst_state[i] = *fst[i]->state;
	}
}

bool queue_midi_out(jack_ringbuffer_t* rbuf, jack_midi_data_t* data, size_t size, const char* What, int8_t id) {
	if (jack_ringbuffer_write_space(rbuf) < size + sizeof(size)) {
		LOG("%s - No space in MIDI OUT buffer (ID:%d)", What, id);
		return false;
	} else {
		// Size of message
		jack_ringbuffer_write(rbuf, (char*) &size, sizeof size);
		// Message itself
		jack_ringbuffer_write(rbuf, (char*) data, size);
		return true;
	}
}

void send_ident_request( FHCTRL* fhctrl ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	// Reset states to non-active
	// NOTE: non-sysex devices doesn't handle IdentRequest
	uint8_t i;
	FSTPlug** fst = fhctrl->fst;
	for(i=0; i < 128; i++) {
		if (fst[i] && fst[i]->type != FST_TYPE_DEVICE)
			fst[i]->state->state = FST_NA;
	}

	LOG("Send ident request");
	SysExIdentRqst sysex_ident_request = SYSEX_IDENT_REQUEST;
	queue_midi_out(
		fjack->buffer_midi_out,
		(jack_midi_data_t*) &sysex_ident_request,
		sizeof(SysExIdentRqst),
		"SendIdentRequest",
		-1
	);
}

// Send SysEx Ident request if found some N/A units
// NOTE: don't trigger for non-sysex units
static void detect_na( FHCTRL* fhctrl ) {
	uint8_t i;
	for(i=0; i < 128; i++) {
		FSTPlug* fp = fhctrl->fst[i];
		if(fp && fp->type != FST_TYPE_DEVICE && fp->state->state == FST_NA) {
			send_ident_request( fhctrl );
			return;
		}
	}
}

void send_dump_request( FHCTRL* fhctrl , short id ) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	SysExDumpRequestV1 sysex_dump_request = SYSEX_DUMP_REQUEST;
	sysex_dump_request.uuid = id;
	LOG("Sent dump request for ID:%d", id);
	queue_midi_out(
		fjack->buffer_midi_out,
		(jack_midi_data_t*) &sysex_dump_request,
		sizeof(SysExDumpRequestV1),
		"SendDumpRequest",
		id
	);
}

void inline send_offer(FHCTRL* fhctrl, SysExIdentReply* r) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	SysExIdOffer sysex_offer = SYSEX_OFFER;

	/* No more free id ? :-( */
	if ( (sysex_offer.uuid = fst_uniqe_id(fhctrl->fst, fhctrl->offered_last + 1)) == 0 ) return;
	fhctrl->offered_last_choke = 10;

	memcpy(sysex_offer.rnid, r->version, sizeof(sysex_offer.rnid));
	collect_rt_logs( fjack, "Send Offer %d for %X %X %X %X", sysex_offer.uuid,
		sysex_offer.rnid[0],
		sysex_offer.rnid[1],
		sysex_offer.rnid[2],
		sysex_offer.rnid[3]
	);
	fhctrl->offered_last = sysex_offer.uuid;
	queue_midi_out(
		fjack->buffer_midi_out,
		(jack_midi_data_t*) &sysex_offer,
		sizeof(SysExIdOffer),
		"SendOffer",
		sysex_offer.uuid
	);
}

void fst_send(FHCTRL* fhctrl, FSTPlug* fp) {
	FJACK* fjack = (FJACK*) fhctrl->user;
	FSTState* fs = fp->state;

	SysExDumpV1 sysex_dump = SYSEX_DUMP;
	sysex_dump.uuid = fp->id;
	sysex_dump.program = fs->program;
	sysex_dump.channel = fs->channel;
	sysex_dump.volume = fs->volume;
	sysex_dump.state = fs->state;
		
	/* NOTE: FSTHost ignore incoming strings anyway */
	memcpy(sysex_dump.program_name, fs->program_name, sizeof(sysex_dump.program_name));
	memcpy(sysex_dump.plugin_name, fp->name, sizeof(sysex_dump.plugin_name));

	queue_midi_out(
		fjack->buffer_midi_out,
		(jack_midi_data_t*) &sysex_dump,
		sizeof(SysExDumpV1),
		"SendSong",
		fp->id
	);
}

void song_send(FHCTRL* fhctrl, short SongNumber) {
	FJACK* fjack = (FJACK*) fhctrl->user;

	short i;
	FSTPlug* fp;
	Song* song = song_get(fhctrl->songs, SongNumber);
	if (!song) return;
	
	enum State curState;
	collect_rt_logs( fjack, "SendSong \"%s\"", song->name);
	// Dump states via SysEx - for all FST
	for (i=0; i < 128; i++) {
		if (! (fp = fhctrl->fst[i]) ) continue;

		curState = fp->state->state;
		*fp->state = *song->fst_state[i];
		
		// If unit is NA in Song then preserve it's current state and skip sending
		if (fp->state->state == FST_NA) {
			fp->state->state = curState;
			continue;
		}

		// Send state to unit
		switch ( fp->type ) {
		case FST_TYPE_DEVICE: ;
			// For devices just send ProgramChange
			jack_midi_data_t pc[2];
			pc[0] = ( fp->state->channel - 1 ) & 0x0F;
			pc[0] |= 0xC0;
			pc[1] = fp->state->program & 0x0F;
			queue_midi_out( fjack->buffer_midi_out, pc, sizeof (pc), "SendSong", fp->id);
			break;
		case FST_TYPE_PLUGIN:
			// If unit is NA then keep it state and skip sending
			if (curState == FST_NA) {
				fp->state->state = FST_NA;
			} else {
				fst_send(fhctrl, fp);
			}
			break;
		}
		fp->change = true; // Update display
	}
}

void init_lcd( struct LCDScreen* lcd_screen ) {
	lcd_screen->available = lcd_init();
	if (! lcd_screen->available) return;

	char lcdline[16];
	snprintf(lcdline, 16, "%s", CLIENT_NAME);
	lcd_text(0,0,lcdline);
	snprintf(lcdline, 16, "says HELLO");
	lcd_text(0,1,lcdline);
	lcd_screen->fst = NULL;
}

void update_lcd( struct LCDScreen* lcd_screen ) {
	if (! lcd_screen->available) return;

	FSTPlug* fp = lcd_screen->fst;
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

static void session_reply( FJACK* fjack ) {
	LOG("session callback");

	FHCTRL* fhctrl = (FHCTRL*) fjack->user;

	char *restore_cmd = malloc(256);
	char filename[FILENAME_MAX];
	jack_session_event_t* sev = fjack->session_event;

	// Save state, set error if fail
	snprintf(filename, sizeof(filename), "%sstate.cfg", sev->session_dir);
	if (! dump_state(filename, fhctrl->songs, fhctrl->fst) )
		sev->flags |= JackSessionSaveError;

	sev->flags |= JackSessionNeedTerminal;

	snprintf(restore_cmd, 256, "fhctrl \"${SESSION_DIR}state.cfg\" %s", sev->client_uuid);
	sev->command_line = restore_cmd;

	jack_session_reply( fjack->client, sev );

//	if (event->type == JackSessionSaveAndQuit)

	jack_session_event_free(sev);
}

static void connect_to_physical( FJACK* j ) {
	const char **jports;
	jports = jack_get_ports(j->client, NULL, JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput|JackPortIsPhysical);
	if (! jports) return;
	
	const char *pname = jack_port_name( j->fin );
	int i;
	for (i=0; jports[i] != NULL; i++) {
		if (jack_port_connected_to(j->fin, jports[i])) continue;
		jack_connect(j->client, jports[i], pname);
		LOG("%s -> %s\n", pname, jports[i]);
	}
	jack_free(jports);
}

void session_callback_handler(jack_session_event_t *event, void* arg) {
	FJACK* fjack = (FJACK*) arg;
	fjack->session_event = event;
	fjack->need_ses_reply = true;
}

int graph_order_callback_handler( void *arg ) {
	FJACK* fjack =(FJACK*) arg;
	FHCTRL* fhctrl = (FHCTRL*) fjack->user;
	// If our outport isn't connected to anyware - it's no sense to try send anything
	if ( jack_port_connected(fjack->out) ) fhctrl->graph_order_changed = 10;

	return 0;
}

void registration_handler(jack_port_id_t port_id, int reg, void *arg) {
	FJACK* j =(FJACK*) arg;
	FHCTRL* fhctrl = (FHCTRL*) j->user;
	if (reg != 0) fhctrl->try_connect_to_physical = true;
}

int cpu_load( FHCTRL* fhctrl ) {
	FJACK* fjack = (FJACK*) fhctrl->user;
	return (int) jack_cpu_load(fjack->client);
}

static void get_rt_logs( FJACK* fjack ) {
	char info[50];
	uint8_t len;
	jack_ringbuffer_t* rbuf = fjack->log_collector;
	while (jack_ringbuffer_read_space(rbuf)) {
		jack_ringbuffer_peek( rbuf, (char*) &len, sizeof len);
		jack_ringbuffer_read_advance( rbuf, sizeof len);

		jack_ringbuffer_peek( rbuf, (char*) &info, len);
		jack_ringbuffer_read_advance( rbuf, len);

		LOG(info);
	}
}

static void collect_rt_logs(FJACK* fjack, char *fmt, ...) {
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

// Midi control channel handling - true if handled
inline bool ctrl_channel_handling ( FHCTRL* fhctrl, jack_midi_data_t data[] ) {
	if ( (data[0] & 0x0F) != CTRL_CHANNEL ) return false;

	// Don't shine for realitime messages
	if ( data[0] < 0xF0 ) fhctrl->gui.ctrl_midi_in = true;

	if ( (data[0] & 0xF0) == 0xC0 ) song_send( fhctrl, data[1] );
	return true;
}

int process (jack_nframes_t frames, void* arg) {
	FJACK* j = (FJACK*) arg;
	FHCTRL* fhctrl = (FHCTRL*) j->user;

	jack_nframes_t i, count;
	jack_midi_event_t event;
	FSTPlug* fp;

	void* inbuf = jack_port_get_buffer (j->in, frames);
	assert (inbuf);

	void* outbuf = jack_port_get_buffer(j->out, frames);
	assert (outbuf);
	jack_midi_clear_buffer(outbuf);

	count = jack_midi_get_event_count (inbuf);
	for (i = 0; i < count; ++i) {
		if (jack_midi_event_get (&event, inbuf, i) != 0) break;

//		collect_rt_logs(j, "MIDI: %X", event.buffer[0]);

		if ( ctrl_channel_handling ( fhctrl, event.buffer ) ) continue;

		if ( event.size < 5 || event.buffer[0] != SYSEX_BEGIN ) continue;
		fhctrl->gui.sysex_midi_in = true;

		switch (event.buffer[1]) {
		case SYSEX_NON_REALTIME:
			// event.buffer[2] is target_id - in our case always 7F
			if ( event.buffer[3] != SYSEX_GENERAL_INFORMATION ||
				event.buffer[4] != SYSEX_IDENTITY_REPLY
			) continue;

			SysExIdentReply* r = (SysExIdentReply*) event.buffer;
			collect_rt_logs(j, "Got SysEx ID Reply ID %X : %X", r->id, r->model[0]);
			// If this is FSTPlug then try to deal with him ;-)
			if (r->id == SYSEX_MYID) {
				if (r->model[0] == 0) {
					send_offer(fhctrl, r);
				} else {
					// Note: we refresh GUI when dump back to us
					fp = fst_get( fhctrl->fst, fhctrl->songs, r->model[0] );
					fp->type = FST_TYPE_PLUGIN; // We just know this ;-)
					send_dump_request(fhctrl, fp->id);
				}
			} /* else { Regular device - not supported yet } */
			break;
		case SYSEX_MYID:
			if (event.size < sizeof(SysExDumpV1)) continue;

			SysExDumpV1* d = (SysExDumpV1*) event.buffer;
			if (d->type != SYSEX_TYPE_DUMP) continue;

			collect_rt_logs(j, "Got SysEx Dump %X : %s : %s", d->uuid, d->plugin_name, d->program_name);
			/* Dump from address zero is fail - try fix this by drop and send ident req */
			if (d->uuid == 0) {
				send_ident_request( fhctrl );
				continue;
			}

			fp = fst_get( fhctrl->fst, fhctrl->songs, d->uuid );
			fp->type = FST_TYPE_PLUGIN; // We just know this ;-)
			fp->state->state = d->state;
			fp->state->program = d->program;
			fp->state->channel = d->channel;
			fp->state->volume = d->volume;
			strcpy(fp->state->program_name, (char*) d->program_name);
			strcpy(fp->name, (char*) d->plugin_name);

			if (fhctrl->lcd_screen.available)
				fhctrl->lcd_screen.fst = fp;

			fp->change = true;
			break;
		}
	}

	/* Send our queued messages */
	while (jack_ringbuffer_read_space(j->buffer_midi_out)) {
		size_t size;
		jack_ringbuffer_peek(j->buffer_midi_out, (char*) &size, sizeof size);
		if ( size > jack_midi_max_event_size(outbuf) ) {
			collect_rt_logs(j, "No more space in midi buffer");
			break;
		}
		jack_ringbuffer_read_advance(j->buffer_midi_out, sizeof size);
		
		jack_midi_data_t tmpbuf[size];
		jack_ringbuffer_peek(j->buffer_midi_out, (char*) &tmpbuf, size);
		jack_ringbuffer_read_advance(j->buffer_midi_out, size);

		if (jack_midi_event_write(outbuf, j->buffer_size - 1, (const jack_midi_data_t *) &tmpbuf, size) )
			collect_rt_logs(j, "SendOur - Write dropped");
	}

	/* Forward port handling */
	inbuf = jack_port_get_buffer (j->fin, frames);
	assert (inbuf);

	outbuf = jack_port_get_buffer(j->fout, frames);
	assert (outbuf);
	jack_midi_clear_buffer(outbuf);

	count = jack_midi_get_event_count (inbuf);
	for (i = 0; i < count; ++i) {
		if (jack_midi_event_get (&event, inbuf, i) != 0) break;

		if ( ctrl_channel_handling ( fhctrl, event.buffer ) ) continue;

		/* Filer out unneded messages - mostly active sensing */
		if ( event.buffer[0] > 0xEF || event.buffer[0] < 0x80 ) continue;

		fhctrl->gui.midi_in = true;
		if ( jack_midi_event_write(outbuf, event.time, event.buffer, event.size) ) {
			collect_rt_logs(j, "Forward - Write dropped (buffer size: %d)", jack_midi_max_event_size(outbuf));
			break;
		}
	}

	return 0;
}

void update_config(FHCTRL* fhctrl) {
	if (fhctrl->config_file) dump_state(fhctrl->config_file, fhctrl->songs, fhctrl->fst);
}

// Will be called from nfhc
void idle_cb( void* arg ) {
	FHCTRL* fhctrl = (FHCTRL*) arg;
	FJACK* fjack = (FJACK*) fhctrl->user;

	/* Update LCD */
	if (fhctrl->gui.lcd_need_update) {
		fhctrl->gui.lcd_need_update = false;
		update_lcd( &fhctrl->lcd_screen );
	}

	/* If Jack need our answer */
	if (fjack->need_ses_reply) {
		fjack->need_ses_reply = false;
		session_reply( fjack );
	}

	/* Try connect to physical ports */
	if (fhctrl->try_connect_to_physical) {
		fhctrl->try_connect_to_physical = false;
		connect_to_physical( fjack );
	}

	/* discollect RT logs */
	get_rt_logs( fjack );

	/* Clear last offered and detect N/A (with some choke) */
	if (fhctrl->offered_last_choke == 0) {
		if (fhctrl->graph_order_changed > 0)
			if (--fhctrl->graph_order_changed == 0) detect_na( fhctrl );
		if (fhctrl->offered_last > 0) fhctrl->offered_last = 0;
	} else fhctrl->offered_last_choke--;
}

void fjack_init( FJACK* fjack ) {
	// Init log collector
	fjack->log_collector = jack_ringbuffer_create(127 * 50 * sizeof(char));
	jack_ringbuffer_mlock( fjack->log_collector );

	// Init MIDI Out buffer
	fjack->buffer_midi_out = jack_ringbuffer_create(127 * SYSEX_MAX_SIZE);
	jack_ringbuffer_mlock(fjack->buffer_midi_out);

	// Init Jack
	fjack->client = jack_client_open (CLIENT_NAME, JackSessionID, NULL, fjack->session_uuid);
	if ( ! fjack->client ) {
		fprintf (stderr, "Could not create JACK client.\n");
		exit (EXIT_FAILURE);
	}
	fjack->buffer_size = jack_get_buffer_size(fjack->client);
	fjack->sample_rate = jack_get_sample_rate(fjack->client);

	fjack->in = jack_port_register (fjack->client, "input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	fjack->fin = jack_port_register (fjack->client, "forward_input", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	fjack->out = jack_port_register (fjack->client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	fjack->fout = jack_port_register (fjack->client, "forward_output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

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

void fhctrl_init( FHCTRL* fhctrl ) {
	fhctrl->songs = &fhctrl->song_first;

	/* try on start */
	fhctrl->try_connect_to_physical = true;

	/* Try detect on start */
	fhctrl->graph_order_changed = 1;

	// Set pointer to idle callback
	fhctrl->idle_cb = idle_cb;
}

int main (int argc, char* argv[]) {
	FHCTRL fhctrl = (FHCTRL) {0};
	FJACK fjack = (FJACK) {0};
	fjack.user = (void*) &fhctrl;
	fhctrl.user = (void*) &fjack;
	fhctrl_init ( &fhctrl );
	fjack_init ( &fjack );

	if (argv[1]) fhctrl.config_file = argv[1];
	if (argv[2]) fjack.session_uuid = argv[2];

	clear_log();

	/* Try change terminal size */
	printf("\033[8;43;132t\n");
//	sleep(1); // Time for resize terminal

	// Init LCD
	init_lcd( &fhctrl.lcd_screen );

	// Try read file
	if (fhctrl.config_file) load_state(fhctrl.config_file, fhctrl.songs, fhctrl.fst);

	// ncurses GUI loop
	nfhc(&fhctrl);

	jack_deactivate ( fjack.client );
	jack_client_close ( fjack.client );

	// Close LCD
	if (fhctrl.lcd_screen.available) lcd_close();

	return 0;
}
